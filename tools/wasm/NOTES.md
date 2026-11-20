# Linux on WebAssembly — port notes

Status: **boots into user space; processes and console input work.** The
kernel boots in a browser (headless Chromium 147) and under node 22, loads
a WebAssembly `/init` from its built-in initramfs and runs it in user mode.
User programs can make system calls, create a child with `clone`, `execve`
another program, wait for it, and read what is typed on the console.

The programs doing this are libc-free tests (`tools/wasm/tests/init/`).
With musl (linux-wasm/musl) and unpatched BusyBox, linux-wasm/distro boots
this kernel to an interactive ash shell with fork(), Ctrl-C and job control; the
kernel's own tests do not depend on either.

Everything shown in the page's `<pre>` is real output of the kernel or of
user programs, except lines starting with `[host]`, which the JavaScript
host adds itself and styles differently.

## Layout

This repository is the kernel tree. The port adds:

| Path | What |
|---|---|
| `arch/wasm/` | The architecture |
| `tools/wasm/docker/Dockerfile` | Build environment (stock Debian 13 LLVM 19) |
| `tools/wasm/scripts/build-env.sh` | Builds the container image `linux-wasm-build:llvm19` |
| `tools/wasm/scripts/kmake.sh` | Runs `make ARCH=wasm LLVM=...` in the container, output in `build/wasm/` |
| `tools/wasm/scripts/build.sh` | The standard build: tinyconfig + test init as initramfs + kernel |
| `tools/wasm/scripts/build-test-init.sh` | Compiles `tools/wasm/tests/init/{init,hello}.c` to wasm user programs |
| `tools/wasm/scripts/test-boot.sh` | Headless boot test (node), compares against `tools/wasm/tests/boot-expect.txt` |
| `tools/wasm/scripts/sync-upstream.sh` | Merges (or rebases onto) a newer upstream tag |
| `tools/wasm/host/` | JavaScript host: `machine.js`, `thread.js`, `input.js`, `irq.js`, the virtio devices `virtio.js`, `virtio-blk.js`, `virtio-net.js`, `device.js`, and the network behind the network card, `usernet.js` and `wisp.js` (independent of browser or node) and `node-run.mjs`, `node-worker.mjs`, `node-device-worker.mjs` (headless runner). The browser page and dev server are in linux-wasm/distro |
| `.github/workflows/wasm.yml` | CI: build + boot test |
| `README.md` | Front page; the upstream `README` is untouched |
| `build/wasm/` | Kernel build output (`vmlinux.wasm`, `vmlinux.map`, `.config`); ignored by git via the `.gitignore` kbuild writes into it |

Git: branch `wasm-port`; remote `origin` is
https://github.com/linux-wasm/linux, remote `upstream` is kernel.org stable.
User space lives in other repositories of the linux-wasm organisation
(`musl`, `distro`), never in this tree.

## Exact versions

| Component | Version |
|---|---|
| Linux base | `v7.3.4` (`7595a62d45ee1a2297eafa5f2ef5d0a45142d84d`), stable, released 2026-11-10 |
| Container base | `debian:trixie-slim` (Debian 13) |
| clang | Debian clang 19.1.7 (`clang-19 1:19.1.7-3+b1`) |
| wasm-ld | Debian LLD 19.1.7 (`lld-19 1:19.1.7-3+b1`) — **stock, unpatched** |
| llvm binutils | `llvm-19 1:19.1.7-3+b1` |
| compiler-rt (user space only) | `libclang-rt-19-dev-wasm32`, same LLVM 19.1.7 |
| wabt | 1.0.36 |
| Binaryen (user space only) | 133, upstream release binary, checked by SHA-256 |
| GNU make | 4.4.1 |
| node (headless tests, later) | v20.19.2 |

Reproducibility caveat: the image is pinned by Debian release, not by snapshot
date or digest, so a rebuild picks up Debian point-release updates of these
packages. FIXME(wasm): pin to snapshot.debian.org if bit-for-bit matters.

## How to build

```sh
tools/wasm/scripts/build-env.sh     # once: build the container image
tools/wasm/scripts/build.sh         # tinyconfig + test init + kernel -> build/wasm/vmlinux.wasm
tools/wasm/scripts/test-boot.sh     # boots it under node, checks the log
```

`build.sh` takes about a minute on 16 cores and finishes with 0 warnings and
a passing `CHKSEC` post-link check. `vmlinux.wasm` is 2.2 MB. tinyconfig for
this arch additionally enables `PRINTK`, `BUG`, `BLK_DEV_INITRD`, `TTY`,
`FUTEX` and `BINFMT_WASM` (`arch/wasm/configs/tiny.config`).

For a kernel without the test init:
`kmake.sh tinyconfig && kmake.sh -j"$(nproc)"` (it then ends in
"No working init found").

`CONTAINER=podman` switches the container runtime. Building on the host
directly also works if clang >= 19 *and* `wasm-ld` are installed
(`make O=build/wasm ARCH=wasm LLVM=1 tinyconfig`); this machine has
clang 20.1.8 but no lld package, hence the container.

## How to run

Headless, kernel log on stdout:

```sh
node tools/wasm/host/node-run.mjs --timeout 20 [--mem 128] [--cmdline "..."]
                                  [--input "text" --input-after "marker"]...
```

Exit code 0/2 on halt (2 = panic), 3 on a trap, 124 on timeout.

In a browser: linux-wasm/distro (`scripts/serve.mjs`). This repository has
no web page and no web server; it only keeps the host code that both use.

## Patch series (`wasm-port` on top of `v7.3.4`)

Generic code, one commit each (6 files):

1. `build(kbuild): add clang target triple for ARCH=wasm` — `scripts/Makefile.clang`.
   Kbuild errors out for an arch without a `--target=` entry.
2. `build(modpost): let mk_elfconfig accept WebAssembly objects` —
   `scripts/mod/mk_elfconfig.c`. This host tool runs in `prepare0` for every
   arch and aborts on a non-ELF `empty.o`.
3. `feat(time): provide jiffies as an alias of jiffies_64 when linking with wasm-ld`
   — `kernel/time/timer.c`, under `#ifdef CONFIG_LD_IS_WASMLD`. Normally a
   linker-script assignment; an alias can only be emitted in the object that
   defines its target. Hunk taken from tombl/linux.

Commit subjects follow Conventional Commits, without co-author trailers.

4. `build(initramfs): assemble the built-in initramfs for wasm-ld` —
   `usr/initramfs_data.S`, a `CONFIG_LD_IS_WASMLD` variant. The file uses
   ELF section flags and unsized labels, which the wasm assembler rejects,
   and it is compiled for every arch with `BLK_DEV_INITRD`.

`arch/wasm` only:

- skeleton, headers, host interface and console, boot entry and memory
  init, tasks on host threads, interrupts and timers, `__multi3`, the link
  flow, tinyconfig fragment and boot fixes (13 commits, see `git log`)
- `feat(wasm): add system call entry`
- `feat(wasm): add a tty on top of the host console`
- `feat(wasm): add binfmt_wasm and enter user mode`
- `feat(wasm): add console input`
- `feat(wasm): support clone and execve from user programs`
- `feat(wasm): add vfork by lending the child the parent's thread`
- `feat(wasm): add fork()` (`mm/fork.c`, `wasm_fork()` in `kernel/process.c`)
- `feat(wasm): run signal handlers and restart system calls`
- virtio transport (`kernel/virtio.c`, `CONFIG_VIRTIO_WASM`), the console's
  window size and "\r\n" in the kernel log, `.notes` markers for sysfs,
  CPU devices (`GENERIC_CPU_DEVICES`)
- `feat(wasm/config): enable initramfs, tty and binfmt_wasm in tinyconfig`
- `fix(wasm): let the kernel know the machine can power off`

Only the tip of the series is known to build and boot; the first arch
commits add files that are not wired into the build until the link-flow
commit.

Outside `arch/wasm`, without touching existing upstream files:
`tools/wasm/` (tooling, host, tests, these notes), `README.md`,
`.github/workflows/wasm.yml`.

Updating to a new upstream tag: `tools/wasm/scripts/sync-upstream.sh v7.3.5`
merges the tag (keeps history, for the published branch);
`--rebase` replays the port on top of the tag as a clean series and keeps
the old tip as `<branch>-on-<oldtag>`. Neither mode has been exercised on a
real newer tag yet; only the "already based on" path has run.

## What works

Observed, not assumed:

- Build from scratch, 0 warnings; `wasm-validate --enable-threads` passes.
- Boot in headless Chromium 147 (local server and the public page) and
  under node 22; CI on GitHub runs the build and the boot test.
- Kernel side: `start_kernel`, memblock/zone setup (128 MiB and 64 MiB
  tried), SLUB, IRQ init, clocksource `wasm-host`, timers, kernel threads,
  all initcall levels, built-in initramfs unpacked, `/dev/console` opened.
- Context switching between host threads, lazy thread creation, idle with
  timer deadline, timer interrupt delivery, command line from the host,
  panic and power-off reported to the host as halts.
- User mode: `/init` (a position-independent wasm module) is loaded by
  `binfmt_wasm`, instantiated by the host and entered; argc/argv/envp
  arrive correctly.
- System calls exercised from user space: `write`, `read`, `getpid`,
  `uname`, `mmap2`, `munmap`, `clone`, `wasm_vfork`, `execve`, `waitid`, `exit`,
  `kill`, `rt_sigaction`, `rt_sigprocmask`, `reboot`, and an out-of-range
  number (`-ENOSYS`).
- Processes: `clone(CLONE_VM | CLONE_VFORK)` with an entry function, the
  child `execve`s a second program with arguments and environment, the
  parent sees its exit status with `waitid`.
- Signal handlers: a handler installed with `rt_sigaction` runs before the
  `kill` that sent the signal returns, makes system calls of its own, and
  gets siginfo and a context; the signal is blocked meanwhile and the mask
  is restored afterwards; a blocked signal is delivered when unblocked.
  Covered from linux-wasm/distro only (`/bin/selftest`, Ctrl-C and Ctrl-Z
  in the shell): system call restart with and without `SA_RESTART`,
  restart after a stop (`sleep 2`, Ctrl-Z, `fg`), `longjmp` out of a handler, `SIGCHLD`,
  killing a child, the tty's interrupt and suspend characters.
- Console input: bytes typed in the page (or passed to `node-run.mjs
  --input`) reach a `read()` on `/dev/console`, with tty echo and
  backspace editing.
- Disks: virtio block devices served by the host (`node-run.mjs --disk`,
  a file each). Used from linux-wasm/distro with `BLOCK`, `VIRTIO_BLK`,
  erofs, ext4 (with journal) and overlayfs: a read-only erofs root with
  an ext4 layer on top, changes found again at the next boot; in Chromium
  with the ext4 disk in the origin private file system, including after
  the browser was killed without a shutdown.
- Also enabled and booted from distro, each only as far as noted: sysfs,
  devtmpfs, `/proc/sys`, Unix98 ptys (`/dev/pts/ptmx` exists; nothing
  has opened a pty), epoll/eventfd/signalfd/timerfd/inotify (linked, not
  called), `MULTIUSER` (only root has run anything).
- Window size: `node-run.mjs --size 100x30`, then `stty size` prints
  `30 100`; resizing the page's terminal changes what `stty size` prints.
- The boot test passes under node. Typing with key events, including
  Ctrl-C, was checked in headless Chromium 147 with the distro's page.

## What is not exercised or known to be off

- **Only about a dozen system calls have ever been called from user
  space** (listed above). The rest of the table links but is untested,
  including everything with 64-bit arguments, file I/O and pipes.
- The kernel's own tests do not cover threads or files. Those are covered
  from linux-wasm/distro (`/bin/selftest`, built with musl): pthreads with
  a mutex and join, files with 64-bit offsets, pipes, `nanosleep`, wall
  clock, signals. A shell with a stopped or background job is as far as
  several processes at once have been tried.
- Signals in a multi-threaded process (which thread a handler runs on,
  `pthread_kill`) and a handler that calls `vfork` or `execve` have not
  been tried. The kernel's own boot test has no case for a handler that
  leaves with an exception; that path is only tested from distro.
- **Boot stalls.** There are gaps of 0.3-1.3 s in the log (before the
  "Memory:" line and before "clocksource: Switched") that vary between
  runs and hosts. Not investigated; most likely something waiting for timer
  ticks, which are only polled in idle, `cpu_relax()` and syscall entry.
- A user program that loops without making system calls freezes the whole
  machine: nothing can interrupt running wasm code. It cannot be signalled
  either, not even with SIGKILL: signals are acted upon when a system call
  returns.
- Only Chromium and node were tried. Firefox and Safari are untested.
  (The public page https://linux-wasm.github.io/distro/ was checked with
  headless Chromium 147 only.)
- The "Memory:" line reports 0K kernel code/data/bss (empty range symbols).
- Kernel version shows as `7.3.4+` because the tree has commits on top of
  the tag.
- An initramfs can only be built into the kernel image; the host cannot
  supply one (it can supply disks).

## What is stubbed (every `FIXME(wasm)` in the tree)

| Where | What |
|---|---|
| `asm/compiler.h`, `asm/linkage.h` | `__builtin_return_address()`, `_RET_IP_`, `_THIS_IP_` are always 0: nothing can name its caller |
| `asm/ptrace.h` | `user_mode()` is always 0 |
| `kernel/signal.c` | signals are only delivered when a system call returns; no alternate signal stack (`SA_ONSTACK` is ignored); `rt_sigreturn` is `-ENOSYS` because handlers return by returning; signals for a vfork child wait until it has left its parent's thread |
| `kernel/binfmt_wasm.c` | fixed 256 KiB user stack without a guard; no `brk` |
| `kernel/process.c` | `clone` without `CLONE_VM` is `-EINVAL` (fork is `wasm_fork()`, not a system call); with it, it needs a stack and an entry function. `fork()` in a signal handler or in a vfork child fails. No ptrace (`-EIO`), no stack traces |
| `mm/fork.c` | processes related by fork exchange the contents of the memory they have in common at every switch between them; `access_process_vm()` on a process that is not running sees the resident's contents; such memory cannot be partially unmapped; one CPU only |
| `kernel/vfork.c` | one level only (a vfork child cannot vfork); a loaded program image is leaked if the task dies before running it |
| `kernel/irq.c` | timer is only polled in idle, `cpu_relax()` and syscall entry; long-running kernel or user code gets no ticks |
| `kernel/setup.c` | RAM size fixed at boot, no `memory.grow` |
| `kernel/sections.c` | text/rodata/init/data/bss ranges are empty; init data is never freed; "Memory:" line shows 0K |
| `kernel/virtio.c` | no writes to a device's configuration space; no config-change interrupt; the host serves a request before the kernel does anything else (one CPU), there is no asynchronous I/O |
| `tools/wasm-objcopy.sh` | `.modinfo` stays in the image; `modules.builtin.modinfo` is empty |

Also not marked in code because it is build glue rather than a stub:

- `EXPORT_SYMBOL` is compiled out (`-D__DISABLE_EXPORTS`).
- `CONFIG_MODULES`, `CONFIG_KALLSYMS`, `CONFIG_SMP` and anything that needs
  more linker tables than tinyconfig (see next section) cannot be forbidden
  from arch Kconfig and will fail at link time or misbehave if enabled.
- `kernel/configs/tiny.config` asks for `KERNEL_XZ` and
  `LD_DEAD_CODE_DATA_ELIMINATION`; kconfig drops both.
- Atomics use the asm-generic uniprocessor versions (IRQ-off based). Correct
  only because exactly one kernel thread runs at a time; must be replaced
  for SMP.

## Prior art and what is reused

- **joelseverin/linux-wasm** (kernel 7.0, GPL-2.0): patched LLVM 18.1.2 so
  that wasm-ld understands GNU linker scripts, plus a Python wrapper that
  rewrites ELF-style tool command lines. One Web Worker per task, nommu,
  wasm binfmt, hvc console. Requires building LLVM from source.
- **tombl/linux** (kernel 7.1.5, GPL-2.0): stock LLVM 19. No linker script;
  section bounds are extracted from the linked image with wabt + perl,
  passed through the device tree and stored in pointer variables at boot,
  which needs `#ifdef CONFIG_WASM` in generic headers (about 60 generic
  files touched in total, incl. scheduler and mm). One worker per task,
  SMP, virtio, userspace.

This port follows tombl's toolchain choice (stock LLVM) and task model, but
handles sections differently to stay out of generic code. Actually reused
from tombl/linux so far (GPL-2.0, credited in the commit messages and file
headers): `arch/wasm/lib/multi3.c` verbatim, the `jiffies` alias hunk,
`asm/page.h` and `asm/pgtable.h` (derived), and the design of `irq.c` and
`time.c` (rewritten for one CPU). Nothing is taken from joelseverin's tree.

## Linking without linker scripts

Kbuild expects an ELF linker with a linker script; wasm-ld has neither. All
of the adaptation lives in `arch/wasm`:

- `tools/wasm-ld.sh` wraps wasm-ld (kbuild's `LD`): drops `--script`, `-z`,
  `--build-id`, `--start/end-group`; for the final link adds the wasm flags
  and puts `sections_head.o` first and `sections_tail.o` last.
- `kernel/sections_head.c` / `sections_tail.c` define `__initcallN_start`,
  `__setup_start/end`, `__sched_class_highest/lowest`, `__start___ex_table`,
  ... as zero-sized objects inside the table sections (crtbegin/crtend
  style). `--no-merge-data-segments` keeps one segment per section, created
  in order of first appearance, so the head file fixes the layout.
- `__start_X`/`__stop_X` for C-identifier sections (`__param`) come from
  wasm-ld itself.
- `Makefile.postlink` runs `tools/check-sections.sh` on `vmlinux.map` after
  every link and fails the build if a listed segment is missing or two
  neighbours are not exactly adjacent. Tested: passes on the real link, fails
  on a map with one address or one segment altered.
- `tools/wasm-objcopy.sh` neutralises the ELF `.modinfo` steps; an empty
  `vmlinux.lds.S` and a generated `.vmlinux.export.c` satisfy rules that need
  the files to exist.

Limits: only the tables tinyconfig references are bracketed, plus
`.notes` (empty) for sysfs. Enabling
features that add linker tables (e.g. `__start_notes`, per-CPU sections for
SMP, `__irqchip_of_table`, LSM tables, kallsyms) gives "undefined symbol" at
link time until they are added to the head/tail files.

## Host interface

Defined and documented in `arch/wasm/include/asm/host.h`.

Imports (module `host`): `console_write(ptr, len)`, `clock_ns() -> i64`,
`clock_realtime_ns() -> i64`,
`spawn_thread(task, sp)`, `thread_exit()`, `halt(reason)`,
`get_cmdline(ptr, len) -> len`, `random(ptr, len)`, `console_read(ptr, len) -> n`,
`console_size() -> rows << 16 | columns`,
`virtio_probe`, `virtio_config_read`, `virtio_set_features`,
`virtio_set_status`, `virtio_queue`, `virtio_notify` (devices, below),
`user_exec(image, len, memory_base, table_size, sp) -> errno`,
`user_clone(task)`, `user_run(arg, entry) -> unwound`, `user_unwind()`,
`user_signal(handler, sig, info, uc) -> how it ended`, `user_rethrow()`;
plus `env.memory` (shared). Interrupt 1 is console input and a changed
terminal size; interrupt 2 + n is virtio device n.
Exports: `_start()`, `wasm_thread_entry(task, sp)`, `wasm_raise_irq(irq)`,
`wasm_syscall(nr, a0..a5)`.

`spawn_thread` means: start a new host thread with a fresh instance of the
same module on the same memory and call `wasm_thread_entry` with the two
opaque values. Timers need no host call: the idle task sleeps with
`memory.atomic.wait32` and a timeout, and an interrupt is an atomic OR +
`notify`.

JavaScript side (`tools/wasm/host/`), no kernel knowledge beyond the list above:

- `thread.js` implements the imports and runs one kernel thread; used
  unchanged by node (`node-worker.mjs`) and by the browser Worker in distro.
- `machine.js` owns the shared memory and the threads. Kernel threads block
  in atomic waits and can never service messages, so console output and
  thread creation are relayed to a thread that does not block (the browser
  main thread). Console chunks carry a shared sequence number so output
  from different workers is shown in the order the kernel wrote it.
- Devices are virtio devices with a transport of host calls
  (`kernel/virtio.c`): the kernel asks the host which devices exist, tells
  it where each virtqueue is, and kicks it. The queues are ordinary split
  rings in kernel memory. `device.js` serves them on a host thread of its
  own per device (it may block in file I/O, which a kernel thread's
  import must not rely on being able to do in a browser: synchronous file
  access exists only for the one worker that opened the file).
  `virtio.js` has the small shared block through which kernel threads and
  the device thread talk (doorbell, status, queue addresses, reset
  handshake); `virtio-blk.js` is the block device over a
  `{ read, write, flush }` disk that the glue supplies (node: a file;
  distro's page: memory or the origin private file system).
- Console output reaches the machine as bytes and is decoded there as one
  UTF-8 stream (`onConsole`), or handed on undecoded for a terminal
  emulator (`onConsoleBytes`). `machine.resize(columns, rows)` sets the
  size the kernel reports as the tty's window size.
- `node-run.mjs` is the headless runner; the page is in distro (`site/`).
- A thread that traps or returns from its entry point is reported as a
  `[host]` error line; that is always a kernel or host bug.

## User-space ABI

Defined by this port (documented at the top of
`arch/wasm/kernel/binfmt_wasm.c`); it is not the ABI of tombl's or
joelseverin's kernels, so their musl ports do not apply unchanged.

- A program is a position-independent wasm module with a `dylink.0`
  section (`clang -fPIC`, `wasm-ld -pie --experimental-pic --import-memory
  --shared-memory --max-memory=4294967296`). It runs inside the kernel's
  linear memory: no MMU, no isolation between programs or from the kernel.
- It imports `env.memory`, `env.__memory_base`, `env.__table_base`,
  `env.__indirect_function_table`, `env.__stack_pointer` as needed, and
  `linux.syscall` of type `(i32 nr, i32 x 6) -> i32`.
- System call numbers are the generic 32-bit ones; errors are `-errno`;
  a 64-bit argument takes two consecutive slots, low word first.
- It exports `_start(sp)`; `sp` points at `argc, argv[], NULL, envp[], NULL,
  auxv` like the initial stack of any Linux process.
- `clone()` never returns in the child. The child runs on a new instance
  of the module (same memory) and starts at the export
  `__clone_entry(stack)`, where `stack` is clone's stack argument.
  `CLONE_VM` and a stack are mandatory. This is the shape of musl's
  `__clone(fn, stack, ...)` and enough for `posix_spawn` and threads.
- `fork()` exists for programs that were run through Binaryen's Asyncify
  pass and import `linux.fork(buffer, size)`; see "fork()" below.
  `vfork()` is the system call `wasm_vfork`
  (244): it returns 0 and the calling thread continues as the child, on
  the parent's call stack. The child's successful `execve()` or its
  `exit()` returns the child's pid, and the thread is the parent again.
  musl wraps this in a `vfork()` macro built on setjmp/longjmp.
- `execve()` works: the old program's frames are thrown away by the host.
- Signal handlers are called by the kernel (through the host and the
  program's function table) when a system call of the program is about to
  return, nested on the program's own stack. A handler returns normally;
  there is no `sa_restorer` and no sigreturn. If it leaves with `longjmp`
  (a wasm exception), the host holds the exception until the kernel has
  left the system call and throws it again at the system call's call site.
  The `ucontext` is the asm-generic one; its machine context is the
  interrupted system call (number, arguments, result), and a changed
  `uc_sigmask` takes effect when the handler returns.
- The host places a program's functions at table index 2 and up, so no
  function has the address of `SIG_DFL` (0) or `SIG_IGN` (1).
- `tools/wasm/scripts/build-test-init.sh` has the exact compiler and linker
  flags.

## Networking

The kernel has its own TCP/IP (`CONFIG_NET`, `CONFIG_INET`) and a virtio
network card (`CONFIG_VIRTIO_NET`, `virtio-net.js`). What is on the other
end of the card is not a network but `usernet.js`, a user-mode network in
the host, in the manner of QEMU's: it answers ARP for every address and
pings to the gateway (10.0.2.2), and is the remote end of every TCP
connection and UDP exchange the kernel starts. The payload goes through
one WebSocket to a Wisp proxy (`wisp.js`; the server is
linux-wasm/netproxy), which opens the real sockets. The reason for the
detour is that a page can only open WebSockets.

- The link between kernel and `usernet.js` never loses or reorders a
  frame (`virtio-net.js` queues frames while the kernel has no receive
  buffer free), so its TCP has sequence numbers, windows and the
  handshakes, and no retransmission or congestion control.
- The network device's thread waits with `Atomics.waitAsync`, because it
  has events of its own (the WebSocket); a disk's thread still blocks.
- `node-run.mjs --net ws://host:port/ [--dns host[:port]]`, `--net none`
  for a card with nothing behind the gateway.
- Entropy: `asm/archrandom.h` takes random numbers from the host
  (`random` import, `crypto.getRandomValues`). Without it the first
  reader of `/dev/urandom` hung: the kernel's fallback measures timer
  jitter in a loop, and no timer interrupt arrives in a loop.

Observed (linux-wasm/distro's network test, node 22, and by hand in
Chromium): ping to the gateway; HTTP over TCP in both directions with a
300 kB body arriving intact; a name resolved over UDP; a refused port;
bash's `/dev/tcp`; HTTPS to a public site with BusyBox wget (which does
not verify certificates); the same against wisp-js 0.5.3 as the proxy.
Not tried: many connections at once, large uploads (the path that closes
the window), UDP other than DNS, unix sockets (`CONFIG_UNIX` is on).
Known gaps are in the FIXME at the top of `usernet.js`: a refused
connection is a reset after `connect()` succeeded, there is no half-close,
no IPv6, and nothing can connect from outside.

The build now has one compiler warning, in generic code:
`net/core/skbuff.c` prints a pointer difference with `%u`, and
`ptrdiff_t` is `long` on wasm32.

## fork()

Two things stand in the way of fork on this machine, and each has its own
answer.

**The call stack.** The child must continue inside the parent's wasm call
stack, which is the engine's and cannot be copied. So the program copies
it: user programs are post-processed with Binaryen's Asyncify
(`wasm-opt --asyncify --pass-arg=asyncify-imports@linux.fork`, done by
distro's `wasm-cc`), which makes every function from which the import
`linux.fork` can be reached able to save its locals and return ("unwind")
and to be re-entered and restore them ("rewind"). libc's `_Fork()` calls
`linux.fork(buffer, size)`; the host (`thread.js`) unwinds the program
into the buffer down to `user_run`, calls the kernel's export
`wasm_fork()`, and rewinds the parent. The child's host thread
instantiates the program again and rewinds it from the same buffer, which
it has because it has a copy of the memory. Cost: programs grow (BusyBox
by about 40%) and functions that make indirect calls check a flag after
each call.

**The memory.** The child needs a copy of the parent's memory at the same
addresses, with no MMU. Only one task runs at a time, so the two never
look at an address at the same moment: they share the nommu regions
(generic commit 5 below gives the child the parent's VMAs) and
`arch/wasm/mm/fork.c` keeps one copy of the contents per process,
exchanging region and copy in `switch_mm()`. An `exec` or exit ends the
sharing. Cost: a fork copies the process's private memory, and so does
every switch between two processes that still share it.

Generic commit 5: `feat(mm/nommu): let an architecture support fork()` —
`mm/nommu.c`, `mm/Kconfig`: `ARCH_HAS_NOMMU_FORK`, under which `dup_mmap()`
copies the VMAs (sharing regions) and calls `arch_nommu_dup_mmap()`,
`delete_vma()` calls `arch_nommu_delete_vma()`, and a region with more
than one user is not split or shrunk. It cannot live in the arch: the
region bookkeeping is private to `mm/nommu.c`.

Observed (linux-wasm/distro, node 22 and Chromium): `/bin/selftest`'s fork
tests (own memory in parent and child, both alive and talking through
pipes, fork below several frames, a grandchild, `longjmp` in the child to
a `setjmp` from before the fork, fork + exec); BusyBox ash as the shell
(built for an MMU, i.e. with fork): subshells, pipelines of shell code,
command substitution nested 8 deep, background jobs, job control; 700
processes in a row without memory going missing. Not tried: fork in a
process with several threads, programs with big memories (the cost
above), `MAP_SHARED` memory across a fork.

The host keeps four idle host threads ready (`spareThreads`), because
starting a Worker took about 100 ms and every new task waited for it; a
fork now takes about 15 ms in Chromium and 27 ms under node.

## Known blockers / risks

- Each kernel task needs its own wasm call stack, so even a uniprocessor
  kernel needs one host thread (Web Worker) per task and shared memory.
  "Single CPU" therefore still requires SharedArrayBuffer and COOP/COEP.
- Signals depend on the program making system calls. A real fix needs a
  way to interrupt running wasm code, which engines do not offer; the
  alternative is compiler-inserted polling in user programs.
- One Worker per kernel task is heavy (14 for an idle tinyconfig kernel)
  and browsers cap the number of workers; a busy user space will hit that.
- Without preemption or timer polling in running code, a kernel loop that
  never idles would hang the machine silently.
- Text addresses do not exist in wasm (functions are table indices), so
  `_stext`/`_etext`-style checks, kallsyms and stack traces will be stubs.
