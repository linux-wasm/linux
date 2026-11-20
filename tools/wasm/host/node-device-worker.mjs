// Node glue: a worker_threads worker that runs one virtio device.
// A disk is backed by a file: backend = { path }. A network device's
// backend is the options of usernet.js; what it reports goes to stderr.
import { closeSync, fsyncSync, openSync, readSync, writeSync } from "node:fs";
import { parentPort } from "node:worker_threads";
import { runDevice } from "./device.js";

parentPort.once("message", (init) => {
  if (init.backend.path === undefined) {
    // The device waits with Atomics.waitAsync, which does not keep a
    // node worker alive by itself.
    setInterval(() => {}, 2 ** 30);
    runDevice(init, {
      ...init.backend,
      onstatus: (text) => parentPort.postMessage({ type: "device-note", text }),
    });
    return;
  }
  const fd = openSync(init.backend.path, "r+");
  runDevice(init, {
    read: (view, position) => readSync(fd, view, 0, view.length, position),
    write: (view, position) => writeSync(fd, view, 0, view.length, position),
    flush: () => fsyncSync(fd),
    close: () => closeSync(fd),
  });
});
