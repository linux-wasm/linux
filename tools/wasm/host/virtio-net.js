// A virtio network device. Frames the kernel sends go to
// net.receive(frame); frames for the kernel come from net.onframe(frame).
// `net` is anything with that shape; here it is the user-mode network of
// usernet.js.

import { VIRTIO_F_VERSION_1, VIRTIO_ID_NET } from "./virtio.js";

const F_MAC = 5;
const HEADER = 12; // struct virtio_net_hdr_v1; all zero: no offloads
const RX = 0;
const TX = 1;

// The description of a network card with this MAC address (six bytes);
// the caller adds `control` (see virtio.js).
export function netDevice(mac) {
  const config = new Uint8Array(8); // struct virtio_net_config: mac, status
  config.set(mac);
  return {
    id: VIRTIO_ID_NET,
    featuresLo: 1 << F_MAC,
    featuresHi: 1 << (VIRTIO_F_VERSION_1 - 32),
    config,
  };
}

// Returns { notify(queues), reset() } for device.js: notify is called
// when the driver has rung the doorbell, with queues(q) giving
// { queue, num, desc, avail, used } of queue q or null; it returns true if
// an interrupt is due. `interrupt()` raises one outside of notify.
export function netModel(memory, net, interrupt) {
  const bytes = (addr, len) => new Uint8Array(memory.buffer, addr, len);
  // Frames for the kernel that found no receive buffer. The link to the
  // kernel never drops a frame, which is what lets usernet.js do without
  // retransmission.
  let backlog = [];
  let lastQueues = null;

  function deliver(queues) {
    const rx = queues?.(RX);
    if (!rx) return false;
    let used = false;
    while (rx && backlog.length) {
      const next = rx.queue.pop(memory, rx.num, rx.desc, rx.avail);
      if (!next) break;
      const frame = backlog.shift();
      // Header and frame, laid out over the chain's buffers.
      const packet = new Uint8Array(HEADER + frame.length);
      packet.set(frame, HEADER);
      let offset = 0;
      for (const { addr, len } of next.chain) {
        const part = packet.subarray(offset, offset + len);
        bytes(addr, part.length).set(part);
        offset += part.length;
        if (offset === packet.length) break;
      }
      // A frame longer than the buffer is cut short; the driver's
      // buffers hold a full Ethernet frame.
      if (rx.queue.push(memory, rx.num, rx.avail, rx.used, next.head, offset))
        used = true;
    }
    return used;
  }

  net.onframe = (frame) => {
    backlog.push(frame);
    if (deliver(lastQueues)) interrupt();
  };

  return {
    notify(queues) {
      // (Not ready: queues() gives null for every queue.)
      lastQueues = queues(RX) ? queues : null;
      let used = deliver(lastQueues);
      const tx = queues(TX);
      if (tx) {
        const sent = tx.queue.process(memory, tx.num, tx.desc, tx.avail, tx.used, (chain) => {
          const total = chain.reduce((n, d) => n + d.len, 0);
          const packet = new Uint8Array(total);
          let offset = 0;
          for (const { addr, len } of chain) {
            packet.set(bytes(addr, len), offset);
            offset += len;
          }
          if (total > HEADER) net.receive(packet.subarray(HEADER));
          return 0;
        });
        if (sent) used = true;
      }
      return used;
    },
    reset() {
      backlog = [];
      lastQueues = null;
    },
  };
}
