// Client for a Wisp server: TCP and UDP connections multiplexed over one
// WebSocket (https://github.com/MercuryWorkshop/wisp-protocol, version 1).
// A browser page cannot open sockets; a Wisp server opens them for it.
//
// Packet: type u8, stream id u32 (little endian), payload.
//   CONNECT  0x01  stream type u8 (1 TCP, 2 UDP), port u16, host name
//   DATA     0x02  bytes (UDP: one datagram)
//   CONTINUE 0x03  u32: how many more DATA packets the server will take
//                  on this (TCP) stream; sent for stream 0 at the start
//                  as the allowance of every new stream
//   CLOSE    0x04  reason u8

const CONNECT = 1;
const DATA = 2;
const CONTINUE = 3;
const CLOSE = 4;
export const TCP = 1;
export const UDP = 2;
const REASON_VOLUNTARY = 0x02;
export const REASON_NETWORK = 0x03;

export class Wisp {
  #url;
  #socket = null;
  #ready = null; // promise for the initial CONTINUE
  #streams = new Map();
  #nextId = 1;
  #allowance = 0;

  // onstatus(text) is told about the connection to the server.
  constructor(url, onstatus = () => {}) {
    this.#url = url;
    this.onstatus = onstatus;
  }

  #connect() {
    if (this.#ready) return this.#ready;
    const socket = new WebSocket(this.#url);
    socket.binaryType = "arraybuffer";
    this.#socket = socket;
    this.#ready = new Promise((resolve, reject) => {
      const fail = (why) => {
        if (this.#socket !== socket) return;
        this.#socket = null;
        this.#ready = null;
        for (const stream of [...this.#streams.values()])
          stream.closed(REASON_NETWORK);
        this.onstatus(`proxy ${this.#url}: ${why}`);
        reject(new Error(why));
      };
      socket.onerror = () => fail("connection failed");
      socket.onclose = () => fail("connection closed");
      socket.onmessage = (event) => {
        const packet = new Uint8Array(event.data);
        if (packet.length < 5) return;
        const view = new DataView(packet.buffer, packet.byteOffset, packet.length);
        const id = view.getUint32(1, true);
        const stream = this.#streams.get(id);
        switch (packet[0]) {
          case CONTINUE:
            if (id === 0) {
              this.#allowance = view.getUint32(5, true);
              this.onstatus(`proxy ${this.#url}: connected`);
              resolve();
            } else stream?.allow(view.getUint32(5, true));
            break;
          case DATA:
            stream?.ondata?.(packet.subarray(5));
            break;
          case CLOSE:
            stream?.closed(packet[5] ?? REASON_NETWORK);
            break;
        }
      };
    });
    // Callers handle failure through their stream's onclose.
    this.#ready.catch(() => {});
    return this.#ready;
  }

  #send(type, id, payload) {
    const packet = new Uint8Array(5 + payload.length);
    packet[0] = type;
    new DataView(packet.buffer).setUint32(1, id, true);
    packet.set(payload, 5);
    this.#socket?.send(packet);
  }

  // Returns a stream: { send(bytes), close(), ondata(bytes),
  // onclose(reason), ondrain(), writable }. send() may be called at once;
  // what the server's flow control does not yet allow is kept back, and
  // `writable` says whether there is none of that (ondrain: again).
  open(type, host, port) {
    const id = this.#nextId++;
    const queue = [];
    let allowed = 0;
    let open = false;
    let done = false;
    const stream = {
      ondata: null,
      onclose: null,
      ondrain: null,
      get writable() {
        return queue.length === 0;
      },
      send: (bytes) => {
        if (done) return;
        queue.push(bytes);
        flush();
      },
      close: () => {
        if (done) return;
        done = true;
        this.#streams.delete(id);
        if (open) this.#send(CLOSE, id, Uint8Array.of(REASON_VOLUNTARY));
      },
      allow: (count) => {
        allowed = count;
        flush();
      },
      closed: (reason) => {
        if (done) return;
        done = true;
        this.#streams.delete(id);
        stream.onclose?.(reason);
      },
    };
    const flush = () => {
      if (!open) return;
      const had = queue.length > 0;
      while (queue.length && (type === UDP || allowed > 0)) {
        this.#send(DATA, id, queue.shift());
        allowed--;
      }
      if (had && !queue.length) stream.ondrain?.();
    };
    this.#streams.set(id, stream);
    this.#connect().then(
      () => {
        if (done) return;
        const name = new TextEncoder().encode(host);
        const payload = new Uint8Array(3 + name.length);
        payload[0] = type;
        new DataView(payload.buffer).setUint16(1, port, true);
        payload.set(name, 3);
        this.#send(CONNECT, id, payload);
        allowed = this.#allowance;
        open = true;
        flush();
      },
      () => stream.closed(REASON_NETWORK),
    );
    return stream;
  }
}
