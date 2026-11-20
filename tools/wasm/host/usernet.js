// A user-mode network: the other end of the kernel's network card.
//
// The kernel runs its own TCP/IP and believes it is on a small Ethernet
// (10.0.2.0/24, the numbers QEMU's user networking made customary):
//
//   10.0.2.15  the kernel (whatever address it uses is accepted)
//   10.0.2.2   the gateway. Connections to it go to the machine the proxy
//              runs on (127.0.0.1 there), if the proxy allows that.
//   10.0.2.3   a name server: DNS queries to it go to the server named in
//              the options.
//
// There is no network behind it. This code answers ARP for every address,
// answers pings to the gateway, and is the remote end of every UDP
// exchange and TCP connection the kernel starts; the payload travels
// through a Wisp proxy (wisp.js), which opens the real sockets. Nothing
// can connect to the kernel from outside.
//
// TCP here is only what a peer on a link that neither loses nor reorders
// needs (virtio-net.js queues instead of dropping): sequence numbers,
// windows, and the opening and closing handshakes. No retransmission, no
// congestion control.
//
// FIXME(wasm): a connection is accepted before the proxy has reached the
// destination, so a refused connection shows as a reset after connect()
// succeeded. Wisp cannot close one direction: when the kernel's side
// sends FIN before anything came back, the connection is kept until the
// remote closes or IDLE_MS pass; after an answer it is closed both ways.
// IPv4 only, no fragments, no ICMP other than echo to the gateway.

import { REASON_NETWORK, TCP, UDP, Wisp } from "./wisp.js";

const GATEWAY_MAC = Uint8Array.of(0x52, 0x55, 0x0a, 0x00, 0x02, 0x02);
const GATEWAY_IP = 0x0a000202;
const DNS_IP = 0x0a000203;
const MSS = 1460;
const WINDOW = 65535;
const IDLE_MS = 60000;

const FIN = 1;
const SYN = 2;
const RST = 4;
const PSH = 8;
const ACK = 16;

const dotted = (ip) =>
  `${ip >>> 24}.${(ip >>> 16) & 255}.${(ip >>> 8) & 255}.${ip & 255}`;

// Internet checksum over the given byte arrays, as if concatenated (each
// but the last must have an even length).
function checksum(...parts) {
  let sum = 0;
  for (const part of parts) {
    let i = 0;
    for (; i + 1 < part.length; i += 2) sum += (part[i] << 8) | part[i + 1];
    if (i < part.length) sum += part[i] << 8;
  }
  while (sum > 0xffff) sum = (sum & 0xffff) + (sum >>> 16);
  return ~sum & 0xffff;
}

export class UserNet {
  // Set by the network card: called with every frame for the kernel.
  onframe = () => {};

  #wisp;
  #dns;
  #guestMac = new Uint8Array(6);
  #ipId = 1;
  #tcp = new Map();
  #udp = new Map();

  // options: { proxy, dns, onstatus }
  //   proxy     URL of a Wisp server (ws:// or wss://); without one only
  //             the gateway answers
  //   dns       "host" or "host:port" of the name server behind 10.0.2.3
  //   onstatus  called with a line of text when the proxy connection
  //             changes
  constructor(options = {}) {
    this.#wisp = options.proxy ? new Wisp(options.proxy, options.onstatus) : null;
    const [host, port] = (options.dns || "1.1.1.1").split(":");
    this.#dns = { host, port: Number(port ?? 53) };
  }

  // Where the proxy should connect for a destination the kernel names.
  #target(ip, port) {
    if (ip === DNS_IP && port === 53) return this.#dns;
    if (ip === GATEWAY_IP || ip === DNS_IP) return { host: "127.0.0.1", port };
    return { host: dotted(ip), port };
  }

  // A frame from the kernel.
  receive(frame) {
    if (frame.length < 14) return;
    this.#guestMac = frame.slice(6, 12);
    const type = (frame[12] << 8) | frame[13];
    if (type === 0x0806) this.#arp(frame.subarray(14));
    else if (type === 0x0800) this.#ipv4(frame.subarray(14));
  }

  #frame(type, payload) {
    const frame = new Uint8Array(14 + payload.length);
    frame.set(this.#guestMac, 0);
    frame.set(GATEWAY_MAC, 6);
    frame[12] = type >> 8;
    frame[13] = type & 255;
    frame.set(payload, 14);
    this.onframe(frame);
  }

  // Every address but the asker's own is "here": the gateway takes it.
  #arp(packet) {
    if (packet.length < 28 || packet[7] !== 1) return;
    const senderIp = packet.subarray(14, 18);
    const targetIp = packet.subarray(24, 28);
    if (senderIp.every((b, i) => b === targetIp[i])) return;
    const reply = new Uint8Array(28);
    reply.set(packet.subarray(0, 6));
    reply[7] = 2;
    reply.set(GATEWAY_MAC, 8);
    reply.set(targetIp, 14);
    reply.set(packet.subarray(8, 18), 18);
    this.#frame(0x0806, reply);
  }

  #ipv4(packet) {
    if (packet.length < 20 || packet[0] >> 4 !== 4) return;
    const headerLength = (packet[0] & 15) * 4;
    const view = new DataView(packet.buffer, packet.byteOffset, packet.length);
    const total = view.getUint16(2);
    // Fragments are not reassembled.
    if (view.getUint16(6) & 0x3fff || total > packet.length) return;
    const src = view.getUint32(12);
    const dst = view.getUint32(16);
    const payload = packet.subarray(headerLength, total);
    switch (packet[9]) {
      case 1:
        this.#icmp(src, dst, payload);
        break;
      case 6:
        this.#tcpSegment(src, dst, payload);
        break;
      case 17:
        this.#udpDatagram(src, dst, payload);
        break;
    }
  }

  // An IPv4 packet to the kernel.
  #send(protocol, src, dst, payload) {
    const packet = new Uint8Array(20 + payload.length);
    const view = new DataView(packet.buffer);
    packet[0] = 0x45;
    view.setUint16(2, packet.length);
    view.setUint16(4, this.#ipId++ & 0xffff);
    view.setUint16(6, 0x4000); // don't fragment
    packet[8] = 64;
    packet[9] = protocol;
    view.setUint32(12, src);
    view.setUint32(16, dst);
    view.setUint16(10, checksum(packet.subarray(0, 20)));
    packet.set(payload, 20);
    this.#frame(0x0800, packet);
  }

  #pseudoHeader(protocol, src, dst, length) {
    const pseudo = new Uint8Array(12);
    const view = new DataView(pseudo.buffer);
    view.setUint32(0, src);
    view.setUint32(4, dst);
    pseudo[9] = protocol;
    view.setUint16(10, length);
    return pseudo;
  }

  #icmp(src, dst, message) {
    if (message.length < 8 || message[0] !== 8) return;
    if (dst !== GATEWAY_IP && dst !== DNS_IP) return;
    const reply = message.slice();
    reply[0] = 0;
    reply[2] = reply[3] = 0;
    new DataView(reply.buffer).setUint16(2, checksum(reply));
    this.#send(1, dst, src, reply);
  }

  // UDP: one proxy stream per (source port, destination), closed after a
  // while without traffic.
  #udpDatagram(src, dst, datagram) {
    if (datagram.length < 8 || !this.#wisp) return;
    const view = new DataView(datagram.buffer, datagram.byteOffset, datagram.length);
    const sport = view.getUint16(0);
    const dport = view.getUint16(2);
    const key = `${sport}:${dst}:${dport}`;
    let flow = this.#udp.get(key);
    if (!flow) {
      const { host, port } = this.#target(dst, dport);
      const stream = this.#wisp.open(UDP, host, port);
      flow = { stream, timer: null };
      this.#udp.set(key, flow);
      const touch = () => {
        clearTimeout(flow.timer);
        flow.timer = setTimeout(() => stream.close() || this.#udp.delete(key), IDLE_MS);
      };
      flow.touch = touch;
      stream.ondata = (data) => {
        touch();
        const reply = new Uint8Array(8 + data.length);
        const out = new DataView(reply.buffer);
        out.setUint16(0, dport);
        out.setUint16(2, sport);
        out.setUint16(4, reply.length);
        reply.set(data, 8);
        out.setUint16(6, checksum(this.#pseudoHeader(17, dst, src, reply.length), reply) || 0xffff);
        this.#send(17, dst, src, reply);
      };
      stream.onclose = () => {
        clearTimeout(flow.timer);
        this.#udp.delete(key);
      };
    }
    flow.touch();
    flow.stream.send(datagram.slice(8, view.getUint16(4)));
  }

  // A TCP segment to the kernel on connection c (or described by c).
  #tcpSend(c, flags, payload = new Uint8Array(0), options = new Uint8Array(0)) {
    const segment = new Uint8Array(20 + options.length + payload.length);
    const view = new DataView(segment.buffer);
    view.setUint16(0, c.dport);
    view.setUint16(2, c.sport);
    view.setUint32(4, c.sndNxt);
    view.setUint32(8, flags & ACK ? c.rcvNxt : 0);
    segment[12] = ((20 + options.length) / 4) << 4;
    segment[13] = flags;
    view.setUint16(14, c.stream?.writable === false ? 0 : WINDOW);
    segment.set(options, 20);
    segment.set(payload, 20 + options.length);
    view.setUint16(16, checksum(this.#pseudoHeader(6, c.dst, c.src, segment.length), segment));
    this.#send(6, c.dst, c.src, segment);
  }

  #tcpSegment(src, dst, segment) {
    if (segment.length < 20) return;
    const view = new DataView(segment.buffer, segment.byteOffset, segment.length);
    const sport = view.getUint16(0);
    const dport = view.getUint16(2);
    const seq = view.getUint32(4);
    const ack = view.getUint32(8);
    const flags = segment[13];
    const payload = segment.subarray((segment[12] >> 4) * 4);
    const key = `${sport}:${dst}:${dport}`;
    let c = this.#tcp.get(key);

    if (flags & RST) {
      if (c) this.#tcpDrop(key, c);
      return;
    }

    if (flags & SYN && !(flags & ACK)) {
      if (c && c.irs !== seq) {
        this.#tcpDrop(key, c);
        c = null;
      }
      if (!c) c = this.#tcpOpen(key, src, dst, sport, dport, seq);
      if (c.failed) {
        // No proxy: refuse, as a closed port would.
        this.#tcpSend({ ...c, sndNxt: 0 }, RST | ACK);
        this.#tcpDrop(key, c);
        return;
      }
      // (Again, if the kernel repeats its SYN.)
      const synAck = { ...c, sndNxt: c.iss };
      this.#tcpSend(synAck, SYN | ACK, undefined, Uint8Array.of(2, 4, MSS >> 8, MSS & 255));
      return;
    }

    if (!c) {
      // Not a connection we know: refuse.
      const stray = { src, dst, sport, dport, sndNxt: ack, rcvNxt: 0 };
      this.#tcpSend(stray, RST);
      return;
    }

    if (flags & ACK) {
      // Acceptable if it acknowledges something sent and not yet acked.
      if (((ack - c.sndUna) | 0) >= 0 && ((c.sndNxt - ack) | 0) >= 0)
        c.sndUna = ack;
      c.window = view.getUint16(14);
    }

    let reply = false;
    if (payload.length) {
      if (seq === c.rcvNxt) {
        c.rcvNxt = (c.rcvNxt + payload.length) >>> 0;
        c.stream.send(payload.slice());
      }
      reply = true;
    }
    if (flags & FIN && ((seq + payload.length) >>> 0) === c.rcvNxt && !c.guestClosed) {
      c.rcvNxt = (c.rcvNxt + 1) >>> 0;
      c.guestClosed = true;
      reply = true;
      // See the FIXME at the top: no half-close through the proxy.
      if (c.answered) this.#tcpRemoteClosed(c);
      else c.timer = setTimeout(() => this.#tcpRemoteClosed(c), IDLE_MS);
    }

    if (!this.#tcpPump(c) && reply) this.#tcpSend(c, ACK);

    // Both directions closed and our FIN acknowledged.
    if (c.guestClosed && c.finSent && c.sndUna === c.sndNxt) this.#tcpDrop(key, c);
  }

  #tcpOpen(key, src, dst, sport, dport, seq) {
    const iss = (Math.random() * 2 ** 32) >>> 0;
    const c = {
      src, dst, sport, dport,
      irs: seq,
      rcvNxt: (seq + 1) >>> 0,
      iss,
      sndNxt: (iss + 1) >>> 0,
      sndUna: iss,
      window: 0,
      queue: [], // from the remote, not yet sent to the kernel
      stream: null,
      answered: false,
      remoteClosed: false,
      finSent: false,
      guestClosed: false,
      timer: null,
    };
    this.#tcp.set(key, c);
    if (!this.#wisp) {
      c.remoteClosed = c.failed = true;
      return c;
    }
    const { host, port } = this.#target(dst, dport);
    c.stream = this.#wisp.open(TCP, host, port);
    c.stream.ondata = (data) => {
      c.answered = true;
      c.queue.push(data);
      this.#tcpPump(c);
    };
    c.stream.onclose = (reason) => {
      // Closed before anything came back and not by the remote's own
      // choice: it never was a connection.
      if (!c.answered && reason >= REASON_NETWORK) c.failed = true;
      this.#tcpRemoteClosed(c);
    };
    // The window was closed while the proxy would not take more.
    c.stream.ondrain = () => this.#tcpSend(c, ACK);
    return c;
  }

  #tcpRemoteClosed(c) {
    if (c.remoteClosed) return;
    c.remoteClosed = true;
    c.stream?.close();
    this.#tcpPump(c);
  }

  // Send what the kernel's window allows, then FIN (or RST) once the
  // remote is done. Returns true if a segment was sent.
  #tcpPump(c) {
    let sent = false;
    if (c.failed) {
      if (this.#tcp.get(`${c.sport}:${c.dst}:${c.dport}`) === c) {
        this.#tcpSend(c, RST | ACK);
        this.#tcpDrop(`${c.sport}:${c.dst}:${c.dport}`, c);
      }
      return true;
    }
    while (c.queue.length) {
      const room = (c.sndUna + c.window - c.sndNxt) | 0;
      if (room <= 0) break;
      const head = c.queue[0];
      const length = Math.min(head.length, room, MSS);
      this.#tcpSend(c, ACK | PSH, head.subarray(0, length));
      c.sndNxt = (c.sndNxt + length) >>> 0;
      if (length === head.length) c.queue.shift();
      else c.queue[0] = head.subarray(length);
      sent = true;
    }
    if (!c.queue.length && c.remoteClosed && !c.finSent) {
      this.#tcpSend(c, FIN | ACK);
      c.sndNxt = (c.sndNxt + 1) >>> 0;
      c.finSent = true;
      sent = true;
    }
    return sent;
  }

  #tcpDrop(key, c) {
    clearTimeout(c.timer);
    c.stream?.close();
    this.#tcp.delete(key);
  }
}
