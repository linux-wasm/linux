# Linux on WebAssembly — port notes

Status: **boots into user space; processes and console input work.** The
kernel boots in a browser (headless Chromium 147) and under node 22, loads
a WebAssembly `/init` from its built-in initramfs and runs it in user mode.
User programs can make system calls, create a child with `clone`, `execve`
another program, wait for it, and read what is typed on the console.

The programs doing this are libc-free tests (`tools/wasm/tests/init/`).
There is no libc and no shell yet, and signal handlers do not run.

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
| `tools/wasm/scripts/serve.py` | Dev server with COOP/COEP headers |
| `tools/wasm/scripts/sync-upstream.sh` | Merges (or rebases onto) a newer upstream tag |
| `tools/wasm/host/` | JavaScript host: `index.html`, `host.js`, `worker.js` (browser), `node-run.mjs`, `node-worker.mjs` (node), shared `machine.js`, `thread.js`, `input.js` |
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
| wabt | 1.0.36 |
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
this arch additionally enables `PRINTK`, `BUG`, `BLK_DEV_INITRD`, `TTY` and
`BINFMT_WASM` (`arch/wasm/configs/tiny.config`).

For a kernel without the test init:
`kmake.sh tinyconfig && kmake.sh -j"$(nproc)"` (it then ends in
"No working init found").

`CONTAINER=podman` switches the container runtime. Building on the host
directly also works if clang >= 19 *and* `wasm-ld` are installed
(`make O=build/wasm ARCH=wasm LLVM=1 tinyconfig`); this machine has
clang 20.1.8 but no lld package, hence the container.

## How to run

Browser:

```sh
tools/wasm/scripts/serve.py       # http://127.0.0.1:8080/
```

The server sends `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp` and serves
`build/wasm/vmlinux.wasm` as `/vmlinux.wasm`. Page parameters:
`?mem=128` (MiB), `?cmdline=...`, `?kernel=url`.

Headless (same machine/thread code, kernel log on stdout):

```sh
node tools/wasm/host/node-run.mjs --timeout 20 [--mem 128] [--cmdline "..."]
```

Exit code 0/2 on halt (2 = panic), 3 on a trap, 124 on timeout.

## Patch series (`wasm-port` on top of `v7.3.4`)

Generic code, one commit each (4 files, additions only):

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
  `uname`, `mmap2`, `munmap`, `clone`, `execve`, `waitid`, `exit`,
  `reboot`, and an out-of-range number (`-ENOSYS`).
- Processes: `clone(CLONE_VM | CLONE_VFORK)` with an entry function, the
  child `execve`s a second program with arguments and environment, the
  parent sees its exit status with `waitid`.
- Console input: bytes typed in the page (or passed to `node-run.mjs
  --input`) reach a `read()` on `/dev/console`, with tty echo and
  backspace editing.
- The boot test passed 6 of 6 times under node; the same sequence,
  including typing with key events, was checked in headless Chromium.

## What is not exercised or known to be off

- **Only about a dozen system calls have ever been called from user
  space** (listed above). The rest of the table links but is untested,
  including everything with 64-bit arguments, file I/O, pipes and signals.
- Threads (`clone` without `CLONE_VFORK`), several processes running at
  once, and `execve` in a process that was not created by `clone` from the
  test have not been tried.
- **Boot stalls.** There are gaps of 0.3-1.3 s in the log (before the
  "Memory:" line and before "clocksource: Switched") that vary between
  runs and hosts. Not investigated; most likely something waiting for timer
  ticks, which are only polled in idle, `cpu_relax()` and syscall entry.
- A user program that loops without making system calls freezes the whole
  machine: nothing can interrupt running wasm code.
- Only Chromium and node were tried. Firefox and Safari are untested.
  (The public page https://linux-wasm.github.io/distro/ was checked with
  headless Chromium 147 only.)
- The "Memory:" line reports 0K kernel code/data/bss (empty range symbols).
- Kernel version shows as `7.3.4+` because the tree has commits on top of
  the tag.
- The initramfs is built into the kernel image; the host cannot supply one.

## What is stubbed (every `FIXME(wasm)` in the tree)

| Where | What |
|---|---|
| `asm/compiler.h`, `asm/linkage.h` | `__builtin_return_address()`, `_RET_IP_`, `_THIS_IP_` are always 0: nothing can name its caller |
| `asm/ptrace.h` | `user_mode()` is always 0 |
| `kernel/syscall.c` | signal handlers are never run (signals with a handler are dropped; default actions work), no `rt_sigreturn`, interrupted syscalls return `-EINTR` instead of restarting |
| `kernel/binfmt_wasm.c` | fixed 256 KiB user stack without a guard; no `brk` |
| `kernel/process.c` | `fork()` and a `vfork()` that returns twice are impossible: `clone` needs `CLONE_VM`, a stack and an entry function. No ptrace (`-EIO`), no stack traces |
| `kernel/irq.c` | timer is only polled in idle, `cpu_relax()` and syscall entry; long-running kernel or user code gets no ticks |
| `kernel/setup.c` | RAM size fixed at boot, no `memory.grow` |
| `kernel/sections.c` | text/rodata/init/data/bss ranges are empty; init data is never freed; "Memory:" line shows 0K |
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

Limits: only the tables tinyconfig references are bracketed. Enabling
features that add linker tables (e.g. `__start_notes`, per-CPU sections for
SMP, `__irqchip_of_table`, LSM tables, kallsyms) gives "undefined symbol" at
link time until they are added to the head/tail files.

## Host interface

Defined and documented in `arch/wasm/include/asm/host.h`.

Imports (module `host`): `console_write(ptr, len)`, `clock_ns() -> i64`,
`spawn_thread(task, sp)`, `thread_exit()`, `halt(reason)`,
`get_cmdline(ptr, len) -> len`, `console_read(ptr, len) -> n`,
`user_exec(image, len, memory_base, table_size, sp) -> errno`,
`user_clone(task)`, `user_run(arg, entry) -> unwound`, `user_unwind()`;
plus `env.memory` (shared). Interrupt 1 is console input.
Exports: `_start()`, `wasm_thread_entry(task, sp)`, `wasm_raise_irq(irq)`,
`wasm_syscall(nr, a0..a5)`.

`spawn_thread` means: start a new host thread with a fresh instance of the
same module on the same memory and call `wasm_thread_entry` with the two
opaque values. Timers need no host call: the idle task sleeps with
`memory.atomic.wait32` and a timeout, and an interrupt is an atomic OR +
`notify`.

JavaScript side (`tools/wasm/host/`), no kernel knowledge beyond the list above:

- `thread.js` implements the imports and runs one kernel thread; used
  unchanged by the browser Worker (`worker.js`) and node (`node-worker.mjs`).
- `machine.js` owns the shared memory and the threads. Kernel threads block
  in atomic waits and can never service messages, so console output and
  thread creation are relayed to a thread that does not block (the browser
  main thread). Console chunks carry a shared sequence number so output
  from different workers is shown in the order the kernel wrote it.
- `host.js`/`index.html` are the page; `node-run.mjs` the headless runner.
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
  `__clone(fn, stack, ...)` and enough for `posix_spawn` and threads, but
  `fork()` and a `vfork()` that returns twice cannot exist. Programs that
  call `vfork()` directly (BusyBox does) will need patching.
- `execve()` works: the old program's frames are thrown away by the host.
- `tools/wasm/scripts/build-test-init.sh` has the exact compiler and linker
  flags.

## Known blockers / risks

- Each kernel task needs its own wasm call stack, so even a uniprocessor
  kernel needs one host thread (Web Worker) per task and shared memory.
  "Single CPU" therefore still requires SharedArrayBuffer and COOP/COEP.
- BusyBox's shell and init use `vfork()`; with this ABI they need a libc
  `vfork` built on something like setjmp, or patches to use `posix_spawn`.
- Signal handlers cannot run yet, which a shell needs for Ctrl-C and
  job control.
- One Worker per kernel task is heavy (14 for an idle tinyconfig kernel)
  and browsers cap the number of workers; a busy user space will hit that.
- Without preemption or timer polling in running code, a kernel loop that
  never idles would hang the machine silently.
- Text addresses do not exist in wasm (functions are table indices), so
  `_stext`/`_etext`-style checks, kallsyms and stack traces will be stubs.
