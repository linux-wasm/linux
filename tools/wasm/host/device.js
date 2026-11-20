// Runs one virtio device on a host thread of its own (not a kernel
// thread): waits for the doorbell, serves the queues in kernel memory and
// raises the device's interrupt. Environment independent; the glue opens
// whatever backs the device (a file, ...) and passes it in.
//
// init: { module, memory, index, device } as posted by machine.js.
// For a block device `backend` is a disk, see virtio-blk.js.

import { irqRaiser } from "./irq.js";
import { blkHandler } from "./virtio-blk.js";
import * as V from "./virtio.js";

export async function runDevice(init, backend) {
  const { module, memory, index, device } = init;
  const control = new Int32Array(device.control);
  const raise = await irqRaiser(module, memory);

  let handle;
  switch (device.id) {
    case V.VIRTIO_ID_BLOCK:
      handle = blkHandler(memory, backend);
      break;
    default:
      throw new Error(`no implementation for virtio device id ${device.id}`);
  }

  let queues = [];
  for (;;) {
    const doorbell = Atomics.load(control, V.DOORBELL);

    const reset = Atomics.load(control, V.RESET_SEQ);
    if (reset !== Atomics.load(control, V.RESET_ACK)) {
      queues = [];
      Atomics.store(control, V.RESET_ACK, reset);
      Atomics.notify(control, V.RESET_ACK);
    }

    if (Atomics.load(control, V.STATUS) & V.STATUS_DRIVER_OK) {
      let interrupt = false;
      for (let q = 0; q < V.MAX_QUEUES; q++) {
        const at = V.QUEUES + q * V.QUEUE_WORDS;
        const num = Atomics.load(control, at + V.QUEUE_NUM);
        if (!num) continue;
        queues[q] ??= new V.VirtQueue();
        if (
          queues[q].process(
            memory,
            num,
            Atomics.load(control, at + V.QUEUE_DESC) >>> 0,
            Atomics.load(control, at + V.QUEUE_AVAIL) >>> 0,
            Atomics.load(control, at + V.QUEUE_USED) >>> 0,
            handle,
          )
        )
          interrupt = true;
      }
      if (interrupt) raise(V.IRQ_VIRTIO0 + index);
    }

    Atomics.wait(control, V.DOORBELL, doorbell);
  }
}
