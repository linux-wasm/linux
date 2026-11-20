// Runs one kernel thread: instantiates the kernel module on the shared
// memory and calls its entry point. Environment independent (a browser Worker
// or node worker_threads); the glue passes in `post` to talk to the machine.
//
// This file implements the import side of arch/wasm/include/asm/host.h.

import { InputQueue } from "./input.js";
import { virtioImports } from "./virtio.js";

const THREAD_EXIT = Symbol("thread_exit");
const HALT = Symbol("halt");
const USER_UNWIND = Symbol("user_unwind");

const ENOEXEC = 8;
const USER_CLONE = 1; // enum wasm_user_entry
// enum wasm_signal
const SIGNAL_RETURNED = 0;
const SIGNAL_THREW = 1;
const SIGNAL_NOT_CALLABLE = 2;
// Function addresses of a program start here: 0 must trap as a null
// pointer, and 0 and 1 are SIG_DFL and SIG_IGN, not handlers.
const TABLE_BASE = 2;

export function runThread(init, post) {
  const { module, memory, seq, cmdline, entry } = init;
  const input = new InputQueue(init.input);

  // Pointers arrive as signed i32.
  const bytes = (ptr, len) =>
    new Uint8Array(memory.buffer, ptr >>> 0, len >>> 0);

  let instance; // the kernel, for this thread

  // The user program of this thread: { program, memoryBase, tableSize,
  // instance, table }. A thread created by clone() starts with the parent's
  // program and no instance; user_run creates it.
  let user = init.userProgram
    ? { ...init.userProgram, instance: null, table: null }
    : null;

  // An exception with which a signal handler ended, between user_signal
  // and user_rethrow.
  let thrown = null;

  // Returns { instance, table }.
  function instantiateUser(program, memoryBase, tableSize, sp) {
    const i32 = (value, mutable = false) =>
      new WebAssembly.Global({ value: "i32", mutable }, value);
    const table = new WebAssembly.Table({
      initial: (tableSize >>> 0) + TABLE_BASE,
      element: "anyfunc",
    });
    const userInstance = new WebAssembly.Instance(program, {
      env: {
        memory,
        __memory_base: i32(memoryBase),
        __table_base: i32(TABLE_BASE),
        __stack_pointer: i32(sp, true),
        __indirect_function_table: table,
      },
      linux: { syscall: instance.exports.wasm_syscall },
    });
    userInstance.exports.__wasm_apply_data_relocs?.();
    return { instance: userInstance, table };
  }

  const host = {
    ...virtioImports(init.devices ?? [], bytes),

    console_write(ptr, len) {
      // `seq` is a counter shared by all threads so the machine can
      // restore the global order of console output that arrives from
      // different workers. The bytes are decoded there, as one stream.
      post({ type: "console", seq: Atomics.add(seq, 0, 1), bytes: bytes(ptr, len).slice() });
    },

    console_size() {
      return input.size;
    },

    console_read(ptr, len) {
      return input.read(bytes(ptr, len));
    },

    clock_ns() {
      // timeOrigin + now() is the same clock in every worker.
      return BigInt(
        Math.round((performance.timeOrigin + performance.now()) * 1e6),
      );
    },

    clock_realtime_ns() {
      return BigInt(Date.now()) * 1000000n;
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

    // User mode (see arch/wasm/kernel/binfmt_wasm.c for the program ABI).

    user_exec(image, len, memoryBase, tableSize, sp) {
      try {
        const program = new WebAssembly.Module(bytes(image, len).slice());
        user = {
          program,
          memoryBase,
          tableSize,
          ...instantiateUser(program, memoryBase, tableSize, sp),
        };
        return 0;
      } catch (e) {
        post({ type: "user-exec-error", message: String(e) });
        return -ENOEXEC;
      }
    },

    user_clone(task) {
      const { program, memoryBase, tableSize } = user;
      post({ type: "user-clone", task, userProgram: { program, memoryBase, tableSize } });
    },

    user_run(arg, entryKind) {
      try {
        if (entryKind === USER_CLONE) {
          Object.assign(
            user,
            instantiateUser(user.program, user.memoryBase, user.tableSize, arg),
          );
          user.instance.exports.__clone_entry(arg);
        } else {
          user.instance.exports._start(arg);
        }
        return 0;
      } catch (e) {
        if (e === USER_UNWIND) return 1;
        throw e;
      }
    },

    user_unwind() {
      throw USER_UNWIND;
    },

    // Signal handlers (see arch/wasm/kernel/signal.c). The handler is a
    // function address of the program, i.e. an index into its table. JS
    // drops the arguments a one-parameter handler does not declare.
    user_signal(handler, sig, info, uc) {
      const index = handler >>> 0;
      const fn = index < user.table.length ? user.table.get(index) : null;
      if (!fn) return SIGNAL_NOT_CALLABLE;
      try {
        fn(sig, info, uc);
        return SIGNAL_RETURNED;
      } catch (e) {
        // longjmp() out of the handler. Anything else (a trap, or our own
        // unwinding for exec and thread exit) is not the kernel's to handle.
        if (!(e instanceof WebAssembly.Exception)) throw e;
        thrown = e;
        return SIGNAL_THREW;
      }
    },

    user_rethrow() {
      const e = thrown;
      thrown = null;
      throw e;
    },
  };

  try {
    instance = new WebAssembly.Instance(module, {
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
