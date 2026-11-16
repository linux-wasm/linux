// Node glue: a worker_threads worker that runs one kernel thread.
import { parentPort } from "node:worker_threads";
import { runThread } from "./thread.js";

parentPort.once("message", (data) =>
  runThread(data, (msg) => parentPort.postMessage(msg)),
);
