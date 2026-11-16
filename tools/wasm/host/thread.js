// Runs one kernel thread: instantiates the kernel module on the shared
// memory and calls its entry point. Environment independent (browser Worker
// or node worker_threads); the glue passes in `post` to talk to the machine.
//
// This file implements the import side of arch/wasm/include/asm/host.h.

const THREAD_EXIT = Symbol("thread_exit");
const HALT = Symbol("halt");

export function runThread({ module, memory, seq, cmdline, entry }, post) {
  // Pointers arrive as signed i32.
  const bytes = (ptr, len) =>
    new Uint8Array(memory.buffer, ptr >>> 0, len >>> 0);
  const decoder = new TextDecoder();

  const host = {
    console_write(ptr, len) {
      // TextDecoder refuses views of shared memory, hence the copy. `seq`
      // is a counter shared by all threads so the machine can restore the
      // global order of console output that arrives from different workers.
      const text = decoder.decode(bytes(ptr, len).slice(), { stream: true });
      post({ type: "console", seq: Atomics.add(seq, 0, 1), text });
    },

    clock_ns() {
      // timeOrigin + now() is the same clock in every worker.
      return BigInt(
        Math.round((performance.timeOrigin + performance.now()) * 1e6),
      );
    },

    spawn_thread(task, sp) {
      post({ type: "spawn", task, sp });
    },

    thread_exit() {
      throw THREAD_EXIT;
    },

    halt(reason) {
      post({ type: "halt", reason });
      throw HALT;
    },

    get_cmdline(ptr, len) {
      const encoded = new TextEncoder().encode(cmdline);
      bytes(ptr, Math.min(len >>> 0, encoded.length)).set(
        encoded.subarray(0, len >>> 0),
      );
      return encoded.length;
    },
  };

  try {
    const instance = new WebAssembly.Instance(module, {
      env: { memory },
      host,
    });
    if (entry.kind === "boot") instance.exports._start();
    else instance.exports.wasm_thread_entry(entry.task, entry.sp);
    post({ type: "returned" });
  } catch (e) {
    if (e === THREAD_EXIT) post({ type: "exit" });
    else if (e !== HALT)
      post({ type: "trap", message: String(e), stack: e?.stack ?? "" });
  }
}
