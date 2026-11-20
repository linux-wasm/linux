// One virtual machine: a shared linear memory plus one host thread per
// kernel task. Environment independent; the caller supplies `createWorker`.
//
// Kernel threads block in memory.atomic.wait32, so they can never service
// their own message queue. Everything they need from the outside (console
// output, creating further threads) is therefore done here, on a thread
// that does not block: in a browser this must be the main thread.

import { InputQueue } from "./input.js";
import { irqRaiser } from "./irq.js";
import { blkDevice } from "./virtio-blk.js";
import { createControl } from "./virtio.js";

const WASM_PAGE = 65536;
const IRQ_CONSOLE = 1; // WASM_IRQ_CONSOLE in the kernel's asm/irq.h
export const HALT_REASONS = ["poweroff", "restart", "panic"];

export class Machine {
  #opts;
  #memory;
  #seq = new Int32Array(new SharedArrayBuffer(4));
  #workers = new Map();
  #nextId = 0;
  #nextSeq = 0;
  #pending = new Map();
  #input = new InputQueue();
  #decoder = new TextDecoder();
  #userPrograms = new Map(); // task -> program for a thread yet to be spawned
  #raiseIrq = null;
  #devices = [];
  #deviceWorkers = [];
  #spares = [];
  halted = false;

  // opts: { module, memoryMiB, cmdline, createWorker(),
  //         disks, createDeviceWorker(), spareThreads,
  //         onConsole(text) or onConsoleBytes(Uint8Array),
  //         onEvent({type, ...}) }
  // Console output is UTF-8 as far as the kernel is concerned; onConsole
  // gets it decoded, onConsoleBytes (for a terminal emulator) as it is.
  // createWorker() returns { postMessage(msg), terminate(),
  //                          onmessage: (data) => void, onerror: (err) => void }
  // disks: [{ bytes, readOnly, backend }], the virtio block devices
  // (/dev/vda, ...).
  // Each is served by a worker from createDeviceWorker() (same shape as
  // createWorker()), whose glue opens `backend` and calls runDevice().
  constructor(opts) {
    this.#opts = opts;
    const pages = (opts.memoryMiB * 1024 * 1024) / WASM_PAGE;
    // Fixed size: the kernel treats all of it as RAM at boot.
    this.#memory = new WebAssembly.Memory({
      initial: pages,
      maximum: pages,
      shared: true,
    });
    for (const disk of opts.disks ?? [])
      this.#devices.push({
        device: { ...blkDevice(disk.bytes, disk.readOnly), control: createControl() },
        backend: disk.backend,
      });
  }

  get threadCount() {
    return this.#workers.size;
  }

  boot() {
    this.#devices.forEach(({ device, backend }, index) => {
      const worker = this.#opts.createDeviceWorker();
      this.#deviceWorkers.push(worker);
      // Whatever the glue has to say, e.g. { type: "device-error", message }.
      worker.onmessage = (msg) => this.#event({ ...msg, device: index });
      worker.onerror = (err) =>
        this.#event({ type: "worker-error", id: `device ${index}`, message: String(err?.message ?? err) });
      worker.postMessage({
        module: this.#opts.module,
        memory: this.#memory,
        index,
        device,
        backend,
      });
    });
    this.#spawn({ kind: "boot" });
  }

  // Pull the plug: end all threads without telling the kernel. What it
  // has not written to a disk is lost.
  stop() {
    if (this.halted) return;
    this.halted = true;
    for (const worker of this.#workers.values()) worker.terminate();
    for (const worker of this.#deviceWorkers) worker.terminate();
    for (const worker of this.#spares) worker.terminate();
    this.#workers.clear();
  }

  // Console input (keyboard): a string or bytes. Returns the number of
  // bytes accepted; the rest is dropped if the kernel is not reading.
  input(data) {
    const bytes =
      typeof data === "string" ? new TextEncoder().encode(data) : data;
    const written = this.#input.write(bytes);
    this.raiseIrq(IRQ_CONSOLE);
    return written;
  }

  // The terminal showing the console has this size now (TIOCGWINSZ,
  // SIGWINCH in the kernel).
  resize(columns, rows) {
    this.#input.setSize(columns, rows);
    this.raiseIrq(IRQ_CONSOLE);
  }

  // Raise a kernel interrupt (see irq.js).
  async raiseIrq(irq) {
    if (this.halted) return;
    this.#raiseIrq ??= irqRaiser(this.#opts.module, this.#memory);
    (await this.#raiseIrq)(irq);
  }

  // A host thread with the kernel instantiated, waiting to be told which
  // task to run (see thread.js).
  #prepare() {
    const worker = this.#opts.createWorker();
    worker.postMessage({
      type: "prepare",
      module: this.#opts.module,
      memory: this.#memory,
      seq: this.#seq,
      input: this.#input.buffer,
      cmdline: this.#opts.cmdline,
      devices: this.#devices.map((d) => d.device),
    });
    return worker;
  }

  #spawn(entry) {
    const id = this.#nextId++;
    const worker = this.#spares.shift() ?? this.#prepare();
    this.#workers.set(id, worker);
    worker.onmessage = (msg) => this.#onMessage(id, msg);
    worker.onerror = (err) =>
      this.#event({ type: "worker-error", id, message: String(err?.message ?? err) });
    worker.postMessage({
      type: "start",
      userProgram: this.#userPrograms.get(entry.task),
      entry,
    });
    this.#userPrograms.delete(entry.task);
    this.#event({ type: "thread-start", id, entry });
    // The kernel waits for a new task's thread, so have the next ones
    // ready before it asks.
    while (this.#spares.length < (this.#opts.spareThreads ?? 4))
      this.#spares.push(this.#prepare());
  }

  // Messages of different workers may overtake each other; `seq` gives
  // their true order (see thread.js), and they are handled in it.
  #onMessage(id, msg) {
    this.#pending.set(msg.seq, { id, msg });
    while (!this.halted && this.#pending.has(this.#nextSeq)) {
      const next = this.#pending.get(this.#nextSeq);
      this.#pending.delete(this.#nextSeq++);
      this.#handle(next.id, next.msg);
    }
  }

  #handle(id, msg) {
    switch (msg.type) {
      case "console":
        this.#emitConsole(msg.bytes);
        break;
      case "spawn":
        this.#spawn({ kind: "thread", task: msg.task, sp: msg.sp });
        break;
      case "exit":
        this.#reap(id);
        this.#event({ type: "thread-exit", id });
        break;
      case "halt":
        this.halted = true;
        for (const worker of this.#workers.values()) worker.terminate();
        for (const worker of this.#deviceWorkers) worker.terminate();
        for (const worker of this.#spares) worker.terminate();
        this.#workers.clear();
        this.#event({ type: "halt", id, reason: HALT_REASONS[msg.reason] ?? msg.reason });
        break;
      case "user-clone":
        this.#userPrograms.set(msg.task, msg.userProgram);
        break;
      case "user-exec-error":
        this.#event({ ...msg, id });
        break;
      case "returned":
      case "trap":
        // The kernel never returns from an entry point; either is a bug.
        this.#reap(id);
        this.#event({ ...msg, id });
        break;
    }
  }

  #reap(id) {
    this.#workers.get(id)?.terminate();
    this.#workers.delete(id);
  }

  #emitConsole(bytes) {
    if (this.#opts.onConsoleBytes) this.#opts.onConsoleBytes(bytes);
    else this.#opts.onConsole(this.#decoder.decode(bytes, { stream: true }));
  }

  #event(event) {
    this.#opts.onEvent?.(event);
  }
}
