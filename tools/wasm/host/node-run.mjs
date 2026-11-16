#!/usr/bin/env node
// Node glue: run the kernel headless with the same machine/thread code as
// the browser. Kernel output goes to stdout, host notes to stderr.
//
//   node tools/wasm/host/node-run.mjs [--kernel build/wasm/vmlinux.wasm]
//        [--mem 128] [--cmdline "..."] [--timeout seconds]
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import { Worker } from "node:worker_threads";
import { Machine } from "./machine.js";

const { values: args } = parseArgs({
  options: {
    kernel: {
      type: "string",
      default: fileURLToPath(
        new URL("../../../build/wasm/vmlinux.wasm", import.meta.url),
      ),
    },
    mem: { type: "string", default: "128" },
    cmdline: { type: "string", default: "" },
    timeout: { type: "string", default: "0" },
  },
});

const note = (text) => process.stderr.write(`[host] ${text}\n`);
const module = await WebAssembly.compile(await readFile(args.kernel));

const finish = (code) => {
  // Kernel threads are blocked in atomic waits; nothing else will end them.
  setTimeout(() => process.exit(code), 50);
};

const machine = new Machine({
  module,
  memoryMiB: Number(args.mem),
  cmdline: args.cmdline,
  createWorker() {
    const worker = new Worker(new URL("./node-worker.mjs", import.meta.url));
    const handle = {
      postMessage: (msg) => worker.postMessage(msg),
      terminate: () => worker.terminate(),
      onmessage: null,
      onerror: null,
    };
    worker.on("message", (msg) => handle.onmessage?.(msg));
    worker.on("error", (err) => handle.onerror?.(err));
    return handle;
  },
  onConsole: (text) => process.stdout.write(text),
  onEvent(event) {
    switch (event.type) {
      case "halt":
        note(`machine halted: ${event.reason}`);
        finish(event.reason === "panic" ? 2 : 0);
        break;
      case "returned":
        note(`thread ${event.id} returned from its entry point`);
        finish(3);
        break;
      case "trap":
        note(`thread ${event.id} trapped: ${event.message}\n${event.stack}`);
        finish(3);
        break;
      case "worker-error":
        note(`worker ${event.id} failed: ${event.message}`);
        finish(3);
        break;
    }
  },
});

if (Number(args.timeout) > 0)
  setTimeout(() => {
    note(`timeout after ${args.timeout}s, ${machine.threadCount} kernel threads alive`);
    process.exit(124);
  }, Number(args.timeout) * 1000);

machine.boot();
