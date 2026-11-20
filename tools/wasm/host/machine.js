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
  #pendingConsole = new Map();
  #input = new InputQueue();
  #decoder = new TextDecoder();
  #userPrograms = new Map(); // task -> program for a thread yet to be spawned
  #raiseIrq = null;
  #devices = [];
  #deviceWorkers = [];
  halted = false;

  // opts: { module, memoryMiB, cmdline, createWorker(),
  //         disks, createDeviceWorker(),
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

  #spawn(entry) {
    const id = this.#nextId++;
    const worker = this.#opts.createWorker();
    this.#workers.set(id, worker);
    worker.onmessage = (msg) => this.#onMessage(id, msg);
    worker.onerror = (err) =>
      this.#event({ type: "worker-error", id, message: String(err?.message ?? err) });
    worker.postMessage({
      module: this.#opts.module,
      memory: this.#memory,
      seq: this.#seq,
      input: this.#input.buffer,
      cmdline: this.#opts.cmdline,
      devices: this.#devices.map((d) => d.device),
      userProgram: this.#userPrograms.get(entry.task),
      entry,
    });
    this.#userPrograms.delete(entry.task);
    this.#event({ type: "thread-start", id, entry });
  }

  #onMessage(id, msg) {
    if (this.halted) return;
    switch (msg.type) {
      case "console":
        this.#console(msg);
        break;
      case "spawn":
        this.#spawn({ kind: "thread", task: msg.task, sp: msg.sp });
        break;
      case "exit":
        this.#reap(id);
        this.#event({ type: "thread-exit", id });
        break;
      case "halt":
        this.#flushConsole();
        this.halted = true;
        for (const worker of this.#workers.values()) worker.terminate();
        for (const worker of this.#deviceWorkers) worker.terminate();
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
        this.#flushConsole();
        this.#reap(id);
        this.#event({ ...msg, id });
        break;
    }
  }

  #reap(id) {
    this.#workers.get(id)?.terminate();
    this.#workers.delete(id);
  }

  // Console messages come from different workers and may overtake each
  // other; `seq` gives their true order.
  #console(msg) {
    this.#pendingConsole.set(msg.seq, msg.bytes);
    while (this.#pendingConsole.has(this.#nextSeq)) {
      this.#emitConsole(this.#pendingConsole.get(this.#nextSeq));
      this.#pendingConsole.delete(this.#nextSeq++);
    }
  }

  #emitConsole(bytes) {
    if (this.#opts.onConsoleBytes) this.#opts.onConsoleBytes(bytes);
    else this.#opts.onConsole(this.#decoder.decode(bytes, { stream: true }));
  }

  // Emit whatever is buffered even if an earlier message never arrived.
  #flushConsole() {
    for (const seq of [...this.#pendingConsole.keys()].sort((a, b) => a - b))
      this.#emitConsole(this.#pendingConsole.get(seq));
    this.#pendingConsole.clear();
    this.#nextSeq = Atomics.load(this.#seq, 0);
  }

  #event(event) {
    this.#opts.onEvent?.(event);
  }
}
