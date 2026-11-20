// Node glue: a worker_threads worker that runs one virtio device.
// A disk is backed by a file: backend = { path }.
import { closeSync, fsyncSync, openSync, readSync, writeSync } from "node:fs";
import { parentPort } from "node:worker_threads";
import { runDevice } from "./device.js";

parentPort.once("message", (init) => {
  const fd = openSync(init.backend.path, "r+");
  runDevice(init, {
    read: (view, position) => readSync(fd, view, 0, view.length, position),
    write: (view, position) => writeSync(fd, view, 0, view.length, position),
    flush: () => fsyncSync(fd),
    close: () => closeSync(fd),
  });
});
