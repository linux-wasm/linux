// Virtio devices: the part shared by the kernel threads (which implement
// the virtio_* imports of arch/wasm/include/asm/host.h) and the device
// threads (device.js, which serve the queues).
//
// A device is described by a plain object that survives postMessage:
//
//   { id, featuresLo, featuresHi, config: Uint8Array, control }
//
// `control` is a SharedArrayBuffer through which a kernel thread tells the
// device thread what the driver did. Kernel threads cannot send messages
// that anybody would wait for, and a device thread may be blocked in a
// file operation, so everything goes through this memory and
// Atomics.wait/notify:
//
//   DOORBELL    incremented (and notified) whenever there is something to
//               look at: new buffers in a queue, or a reset
//   STATUS      the virtio status byte
//   FEATURES_*  the features the driver accepted
//   RESET_SEQ   incremented by a reset; the device thread copies it to
//   RESET_ACK   when it has forgotten all queues, which the resetting
//               kernel thread waits for
//   QUEUES...   per queue: size, and the addresses of its three parts

export const IRQ_VIRTIO0 = 2; // WASM_IRQ_VIRTIO0 in the kernel's asm/irq.h

export const VIRTIO_ID_BLOCK = 2;
export const VIRTIO_F_VERSION_1 = 32; // bit number
export const STATUS_DRIVER_OK = 4;

export const DOORBELL = 0;
export const STATUS = 1;
export const FEATURES_LO = 2;
export const FEATURES_HI = 3;
export const RESET_SEQ = 4;
export const RESET_ACK = 5;
export const QUEUES = 8;
export const QUEUE_NUM = 0;
export const QUEUE_DESC = 1;
export const QUEUE_AVAIL = 2;
export const QUEUE_USED = 3;
export const QUEUE_WORDS = 4;
export const MAX_QUEUES = 8;

export function createControl() {
  return new SharedArrayBuffer(4 * (QUEUES + MAX_QUEUES * QUEUE_WORDS));
}

// The virtio_* imports for one kernel thread. `bytes(ptr, len)` returns a
// view of kernel memory.
export function virtioImports(devices, bytes) {
  const controls = devices.map((d) => new Int32Array(d.control));
  const ring = (n) => {
    Atomics.add(controls[n], DOORBELL, 1);
    Atomics.notify(controls[n], DOORBELL);
  };

  return {
    virtio_probe(n, info) {
      const device = devices[n >>> 0];
      if (!device) return -1;
      // struct wasm_virtio_info
      const view = new DataView(bytes(info, 20).buffer, info >>> 0, 20);
      view.setUint32(0, device.id, true);
      view.setUint32(4, 0, true);
      view.setUint32(8, device.featuresLo, true);
      view.setUint32(12, device.featuresHi, true);
      view.setUint32(16, device.config.length, true);
      return 0;
    },

    virtio_config_read(n, offset, ptr, len) {
      bytes(ptr, len).set(devices[n].config.subarray(offset, offset + len));
    },

    virtio_set_features(n, lo, hi) {
      Atomics.store(controls[n], FEATURES_LO, lo);
      Atomics.store(controls[n], FEATURES_HI, hi);
    },

    virtio_set_status(n, status) {
      const control = controls[n];
      Atomics.store(control, STATUS, status);
      if (status !== 0) return;
      // Reset: wait until the device thread is done with guest memory.
      const seq = Atomics.add(control, RESET_SEQ, 1) + 1;
      ring(n);
      for (;;) {
        const ack = Atomics.load(control, RESET_ACK);
        if (ack === seq) break;
        Atomics.wait(control, RESET_ACK, ack);
      }
    },

    virtio_queue(n, q, num, desc, avail, used) {
      if (q >= MAX_QUEUES) throw new Error(`virtio device ${n}: queue ${q}`);
      const at = QUEUES + q * QUEUE_WORDS;
      Atomics.store(controls[n], at + QUEUE_NUM, 0);
      Atomics.store(controls[n], at + QUEUE_DESC, desc);
      Atomics.store(controls[n], at + QUEUE_AVAIL, avail);
      Atomics.store(controls[n], at + QUEUE_USED, used);
      Atomics.store(controls[n], at + QUEUE_NUM, num);
    },

    virtio_notify(n) {
      ring(n);
    },
  };
}

// Device side of one split virtqueue.
const DESC_F_NEXT = 1;
const DESC_F_WRITE = 2;
const AVAIL_F_NO_INTERRUPT = 1;

export class VirtQueue {
  #lastAvail = 0;
  #usedIdx = 0;

  // Hand every buffer chain the driver has made available to
  // handle(chain), in order, and mark it used. A chain is an array of
  // { addr, len, write }; handle returns how many bytes it wrote into the
  // writable parts. Returns true if the driver wants an interrupt.
  process(memory, num, desc, avail, used, handle) {
    const view = new DataView(memory.buffer);
    let handled = false;
    while (this.#lastAvail !== view.getUint16(avail + 2, true)) {
      const head = view.getUint16(avail + 4 + 2 * (this.#lastAvail % num), true);
      const chain = [];
      for (let i = head, n = 0; ; n++) {
        if (i >= num || n >= num) throw new Error("virtqueue: bad descriptor chain");
        const at = desc + 16 * i;
        // Addresses are 64 bit, but memory is not.
        const flags = view.getUint16(at + 12, true);
        chain.push({
          addr: view.getUint32(at, true),
          len: view.getUint32(at + 8, true),
          write: (flags & DESC_F_WRITE) !== 0,
        });
        if (!(flags & DESC_F_NEXT)) break;
        i = view.getUint16(at + 14, true);
      }
      const written = handle(chain);
      const slot = used + 4 + 8 * (this.#usedIdx % num);
      view.setUint32(slot, head, true);
      view.setUint32(slot + 4, written, true);
      this.#usedIdx = (this.#usedIdx + 1) & 0xffff;
      view.setUint16(used + 2, this.#usedIdx, true);
      this.#lastAvail = (this.#lastAvail + 1) & 0xffff;
      handled = true;
    }
    return handled && !(view.getUint16(avail, true) & AVAIL_F_NO_INTERRUPT);
  }
}
