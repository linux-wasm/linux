// Runs one virtio device on a host thread of its own (not a kernel
// thread): waits for the doorbell, serves the queues in kernel memory and
// raises the device's interrupt. Environment independent; the glue opens
// whatever backs the device and passes it in.
//
// init: { module, memory, index, device, backend } as posted by
// machine.js. `backend` (the second argument, what the glue made of
// init.backend) is
//   for a block device: a disk, see virtio-blk.js;
//   for a network device: the options of the user-mode network, see
//   usernet.js, with `onstatus` added by the glue if it wants to know.

import { irqRaiser } from "./irq.js";
import { UserNet } from "./usernet.js";
import { blkHandler } from "./virtio-blk.js";
import { netModel } from "./virtio-net.js";
import * as V from "./virtio.js";

export async function runDevice(init, backend) {
  const { module, memory, index, device } = init;
  const control = new Int32Array(device.control);
  const raise = await irqRaiser(module, memory);
  const interrupt = () => raise(V.IRQ_VIRTIO0 + index);

  let queues = [];
  // What the driver said about queue q, or null if it has none.
  const queue = (q) => {
    const at = V.QUEUES + q * V.QUEUE_WORDS;
    const num = Atomics.load(control, at + V.QUEUE_NUM);
    if (!num) return null;
    return {
      queue: (queues[q] ??= new V.VirtQueue()),
      num,
      desc: Atomics.load(control, at + V.QUEUE_DESC) >>> 0,
      avail: Atomics.load(control, at + V.QUEUE_AVAIL) >>> 0,
      used: Atomics.load(control, at + V.QUEUE_USED) >>> 0,
    };
  };

  // A model has notify(queue) -> interrupt wanted, and reset(). A device
  // that only ever reacts to the driver can sleep in Atomics.wait; one
  // that has events of its own (frames arriving) must keep its event
  // loop running and waits asynchronously.
  let model;
  let asynchronous = false;
  switch (device.id) {
    case V.VIRTIO_ID_BLOCK: {
      const handle = blkHandler(memory, backend);
      model = {
        notify(queue) {
          const q = queue(0);
          return q ? q.queue.process(memory, q.num, q.desc, q.avail, q.used, handle) : false;
        },
        reset() {},
      };
      break;
    }
    case V.VIRTIO_ID_NET:
      model = netModel(memory, new UserNet(backend), interrupt);
      asynchronous = true;
      break;
    default:
      throw new Error(`no implementation for virtio device id ${device.id}`);
  }

  const none = () => null;
  for (;;) {
    const doorbell = Atomics.load(control, V.DOORBELL);

    const reset = Atomics.load(control, V.RESET_SEQ);
    if (reset !== Atomics.load(control, V.RESET_ACK)) {
      queues = [];
      model.reset();
      Atomics.store(control, V.RESET_ACK, reset);
      Atomics.notify(control, V.RESET_ACK);
    }

    const ready = Atomics.load(control, V.STATUS) & V.STATUS_DRIVER_OK;
    if (model.notify(ready ? queue : none)) interrupt();

    if (asynchronous) {
      const wait = Atomics.waitAsync(control, V.DOORBELL, doorbell);
      if (wait.async) await wait.value;
    } else {
      Atomics.wait(control, V.DOORBELL, doorbell);
    }
  }
}
