// Runs one kernel thread: instantiates the kernel module on the shared
// memory and calls its entry point. Environment independent (a browser Worker
// or node worker_threads); the glue passes every message of the machine to
// runThread, with `post` to talk back.
//
// There are two messages, because starting a host thread takes far longer
// than anything else about creating a kernel task, and the machine
// therefore keeps a few threads ready: "prepare" instantiates the kernel,
// and the later "start" says which task the thread is to run.
//
// This file implements the import side of arch/wasm/include/asm/host.h.

import { InputQueue } from "./input.js";
import { virtioImports } from "./virtio.js";

const THREAD_EXIT = Symbol("thread_exit");
const HALT = Symbol("halt");
const USER_UNWIND = Symbol("user_unwind");

const ENOEXEC = 8;
const ENOSYS = 38;
// enum wasm_user_entry
const USER_CLONE = 1;
const USER_FORK = 2;
// enum wasm_signal
const SIGNAL_RETURNED = 0;
const SIGNAL_THREW = 1;
const SIGNAL_NOT_CALLABLE = 2;
// Function addresses of a program start here: 0 must trap as a null
// pointer, and 0 and 1 are SIG_DFL and SIG_IGN, not handlers.
const TABLE_BASE = 2;

let start = null;

export function runThread(msg, post) {
  if (msg.type === "prepare") start = prepareThread(msg, post);
  else start(msg);
}

// Returns the function for the "start" message { entry, userProgram }.
function prepareThread(init, postUnordered) {
  const { module, memory, seq, cmdline } = init;
  // Messages of different threads can overtake each other on their way to
  // the machine. `seq` is a counter shared by all threads, from which the
  // machine restores the order in which things happened: console output,
  // and a program announced (user_clone) before its thread is spawned.
  const post = (msg) =>
    postUnordered({ ...msg, seq: Atomics.add(seq, 0, 1) });
  const input = new InputQueue(init.input);

  // Pointers arrive as signed i32.
  const bytes = (ptr, len) =>
    new Uint8Array(memory.buffer, ptr >>> 0, len >>> 0);

  let instance; // the kernel, for this thread

  // The user program of this thread: { program, memoryBase, tableSize,
  // instance, table, sp, entry, forking, forked }. A thread created by
  // clone() or fork() starts with the parent's program and no instance;
  // user_run creates it.
  //   sp       the instance's __stack_pointer global
  //   entry    { kind, arg }: how user_run entered the program, which is
  //            how it is entered again to rewind after a fork
  //   forking  a fork in progress, see linuxFork
  //   forked   for the child of a fork: { buffer, sp, entry } of the parent
  let user = null;

  // What user_fork passes on to the child, set around wasm_fork().
  let forkInfo = null;
  // Signal handlers running (they nest).
  let signalDepth = 0;

  // An exception with which a signal handler ended, between user_signal
  // and user_rethrow.
  let thrown = null;

  // The program's import linux.fork(buffer, size), see wasm_fork() in
  // arch/wasm/kernel/process.c. It is called twice per fork: the first
  // call starts unwinding the program's stack into the buffer (and its
  // return value is never seen); user_run does the fork and rewinds, which
  // ends in the second call, and that one returns the result.
  function linuxFork(buffer, size) {
    const asyncify = user.instance.exports;
    if (user.forking?.rewinding) {
      const { result } = user.forking;
      asyncify.asyncify_stop_rewind();
      user.forking = null;
      return result;
    }
    // Unwinding would have to pass through the kernel frames that called
    // the signal handler.
    if (!asyncify.asyncify_start_unwind || signalDepth > 0 || size >>> 0 < 16)
      return -ENOSYS;
    buffer >>>= 0;
    // Asyncify's header: where the saved stack starts and where it must end.
    const view = new DataView(memory.buffer, buffer, 8);
    view.setUint32(0, buffer + 8, true);
    view.setUint32(4, buffer + (size >>> 0), true);
    user.forking = { buffer };
    asyncify.asyncify_start_unwind(buffer);
    return 0;
  }

  // Returns { instance, table, sp }. Relocations are applied to the data
  // of a program that was just loaded, not to memory a fork copied.
  function instantiateUser(program, memoryBase, tableSize, sp, relocate = true) {
    const i32 = (value, mutable = false) =>
      new WebAssembly.Global({ value: "i32", mutable }, value);
    const table = new WebAssembly.Table({
      initial: (tableSize >>> 0) + TABLE_BASE,
      element: "anyfunc",
    });
    const stackPointer = i32(sp, true);
    const userInstance = new WebAssembly.Instance(program, {
      env: {
        memory,
        __memory_base: i32(memoryBase),
        __table_base: i32(TABLE_BASE),
        __stack_pointer: stackPointer,
        __indirect_function_table: table,
      },
      linux: { syscall: instance.exports.wasm_syscall, fork: linuxFork },
    });
    if (relocate) userInstance.exports.__wasm_apply_data_relocs?.();
    return { instance: userInstance, table, sp: stackPointer };
  }

  const host = {
    ...virtioImports(init.devices ?? [], bytes),

    console_write(ptr, len) {
      // The bytes are decoded by the machine, as one stream.
      post({ type: "console", bytes: bytes(ptr, len).slice() });
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

    user_fork(task) {
      const { program, memoryBase, tableSize } = user;
      post({
        type: "user-clone",
        task,
        userProgram: { program, memoryBase, tableSize, forked: forkInfo },
      });
    },

    user_run(arg, entryKind) {
      try {
        if (entryKind === USER_FORK) {
          const { buffer, sp, entry } = user.forked;
          Object.assign(
            user,
            instantiateUser(user.program, user.memoryBase, user.tableSize, sp, false),
          );
          user.entry = entry;
          user.forked = null;
          user.forking = { buffer, rewinding: true, result: 0 };
          user.instance.exports.asyncify_start_rewind(buffer);
        } else {
          if (entryKind === USER_CLONE)
            Object.assign(
              user,
              instantiateUser(user.program, user.memoryBase, user.tableSize, arg),
            );
          user.entry = { kind: entryKind, arg };
        }

        for (;;) {
          const program = user.instance.exports;
          if (user.entry.kind === USER_CLONE)
            program.__clone_entry(user.entry.arg);
          else program._start(user.entry.arg);

          // The entry function only returns when linuxFork has made the
          // program unwind. Its stack is in the buffer now: fork, then
          // run it again to rewind.
          if (!user.forking || user.forking.rewinding) return 0;
          program.asyncify_stop_unwind();
          const { buffer } = user.forking;
          forkInfo = { buffer, sp: user.sp.value, entry: user.entry };
          const result = instance.exports.wasm_fork();
          forkInfo = null;
          user.forking = { buffer, rewinding: true, result };
          program.asyncify_start_rewind(buffer);
        }
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
      signalDepth++;
      try {
        fn(sig, info, uc);
        return SIGNAL_RETURNED;
      } catch (e) {
        // longjmp() out of the handler. Anything else (a trap, or our own
        // unwinding for exec and thread exit) is not the kernel's to handle.
        if (!(e instanceof WebAssembly.Exception)) throw e;
        thrown = e;
        return SIGNAL_THREW;
      } finally {
        signalDepth--;
      }
    },

    user_rethrow() {
      const e = thrown;
      thrown = null;
      throw e;
    },
  };

  const report = (e) => {
    if (e === THREAD_EXIT) post({ type: "exit" });
    else if (e !== HALT)
      post({ type: "trap", message: String(e), stack: e?.stack ?? "" });
  };

  try {
    instance = new WebAssembly.Instance(module, {
      env: { memory },
      host,
    });
  } catch (e) {
    report(e);
  }

  return ({ entry, userProgram }) => {
    if (userProgram) user = { ...userProgram, instance: null, table: null };
    try {
      if (entry.kind === "boot") instance.exports._start();
      else instance.exports.wasm_thread_entry(entry.task, entry.sp);
      post({ type: "returned" });
    } catch (e) {
      report(e);
    }
  };
}
