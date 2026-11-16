// One virtual machine: a shared linear memory plus one host thread per
// kernel task. Environment independent; the caller supplies `createWorker`.
//
// Kernel threads block in memory.atomic.wait32, so they can never service
// their own message queue. Everything they need from the outside (console
// output, creating further threads) is therefore done here, on a thread
// that does not block: in a browser this must be the main thread.

import { InputQueue } from "./input.js";

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
  #userPrograms = new Map(); // task -> program for a thread yet to be spawned
  #irqInstance = null;
  halted = false;

  // opts: { module, memoryMiB, cmdline, createWorker(),
  //         onConsole(text), onEvent({type, ...}) }
  // createWorker() returns { postMessage(msg), terminate(),
  //                          onmessage: (data) => void, onerror: (err) => void }
  constructor(opts) {
    this.#opts = opts;
    const pages = (opts.memoryMiB * 1024 * 1024) / WASM_PAGE;
    // Fixed size: the kernel treats all of it as RAM at boot.
    this.#memory = new WebAssembly.Memory({
      initial: pages,
      maximum: pages,
      shared: true,
    });
  }

  get threadCount() {
    return this.#workers.size;
  }

  boot() {
    this.#spawn({ kind: "boot" });
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

  // Raise a kernel interrupt. wasm_raise_irq only touches shared memory, so
  // it can be called on an instance of the kernel that is otherwise unused;
  // none of that instance's imports are ever called.
  async raiseIrq(irq) {
    if (this.halted) return;
    this.#irqInstance ??= WebAssembly.instantiate(this.#opts.module, {
      env: { memory: this.#memory },
      host: Object.fromEntries(
        WebAssembly.Module.imports(this.#opts.module)
          .filter((i) => i.module === "host")
          .map((i) => [i.name, () => { throw new Error(`host.${i.name} called on the irq instance`); }]),
      ),
    });
    (await this.#irqInstance).exports.wasm_raise_irq(irq);
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
    this.#pendingConsole.set(msg.seq, msg.text);
    while (this.#pendingConsole.has(this.#nextSeq)) {
      this.#opts.onConsole(this.#pendingConsole.get(this.#nextSeq));
      this.#pendingConsole.delete(this.#nextSeq++);
    }
  }

  // Emit whatever is buffered even if an earlier message never arrived.
  #flushConsole() {
    for (const seq of [...this.#pendingConsole.keys()].sort((a, b) => a - b))
      this.#opts.onConsole(this.#pendingConsole.get(seq));
    this.#pendingConsole.clear();
    this.#nextSeq = Atomics.load(this.#seq, 0);
  }

  #event(event) {
    this.#opts.onEvent?.(event);
  }
}
