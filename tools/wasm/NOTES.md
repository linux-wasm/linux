# Linux on WebAssembly — port notes

Status: **M3 and M4 done, M5 partly.** The kernel boots in a browser
(headless Chromium 147) and under node 22, prints its log into the page,
runs all initcalls and ends in the expected

    Kernel panic - not syncing: No working init found.

There is no user space, no initramfs and no input. Everything shown in the
page's `<pre>` is real kernel output, except lines starting with `[host]`,
which the JavaScript host adds itself and styles differently.

## Layout

This repository is the kernel tree. The port adds:

| Path | What |
|---|---|
| `arch/wasm/` | The architecture |
| `tools/wasm/docker/Dockerfile` | Build environment (stock Debian 13 LLVM 19) |
| `tools/wasm/scripts/build-env.sh` | Builds the container image `linux-wasm-build:llvm19` |
| `tools/wasm/scripts/kmake.sh` | Runs `make ARCH=wasm LLVM=...` in the container, output in `build/wasm/` |
| `tools/wasm/scripts/test-boot.sh` | Headless boot test (node), compares against `tools/wasm/tests/boot-expect.txt` |
| `tools/wasm/scripts/serve.py` | Dev server with COOP/COEP headers |
| `tools/wasm/scripts/sync-upstream.sh` | Merges (or rebases onto) a newer upstream tag |
| `tools/wasm/host/` | JavaScript host: `index.html`, `host.js`, `worker.js` (browser), `node-run.mjs`, `node-worker.mjs` (node), shared `machine.js`, `thread.js` |
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
tools/wasm/scripts/build-env.sh              # once: build the container image
tools/wasm/scripts/kmake.sh tinyconfig       # writes build/wasm/.config
tools/wasm/scripts/kmake.sh -j"$(nproc)"     # builds build/wasm/vmlinux.wasm (~1 min on 16 cores)
tools/wasm/scripts/test-boot.sh              # boots it under node, checks the log
```

Verified from an empty `build/wasm/`: 0 errors, 0 warnings, and the
`CHKSEC` post-link check passes. Output is `vmlinux` (the wasm module) and an
identical copy `vmlinux.wasm`, 1.9 MB with tinyconfig (which for this arch
includes `PRINTK`, `PRINTK_TIME` and `BUG`, see `arch/wasm/configs/tiny.config`).

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

Generic code, one commit each (18 added lines in 3 files, nothing removed):

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

`arch/wasm` only (52 files, ~1600 lines):

4. `feat(wasm): add architecture skeleton (Kconfig, Makefile)`
5. `feat(wasm): add asm and uapi headers`
6. `feat(wasm): define the host interface and add a console on top of it`
7. `feat(wasm): add boot entry, setup_arch and memory init`
8. `feat(wasm): add tasks and context switching on host threads`
9. `feat(wasm): add interrupt delivery, clocksource and clockevent`
10. `feat(wasm): add __multi3`
11. `build(wasm): build and link vmlinux with stock wasm-ld`
12. `feat(wasm/config): enable printk and BUG in tinyconfig`
13. `fix(wasm): define _sdata, _edata, __bss_start and __bss_stop`
14. `fix(wasm): refresh vmlinux.wasm on every build and move kernel data out of page 0`
15. `fix(wasm): start sched_clock at zero from the first call`
16. `feat(wasm): report a panic to the host`

(In git order the two generic commits 2 and 3 sit between 4 and 5.) Only the
tip of the series is known to build; commits 5-10 add files that are not
wired into the build until commit 11.

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
- Boot in headless Chromium 147 via `tools/wasm/scripts/serve.py`: page is
  cross-origin isolated, log appears in `<pre id="console">`, status ends as
  "halted (panic)", no page errors.
- Boot under node 22 (`tools/wasm/host/node-run.mjs`): 8 of 8 runs reached the same
  panic.
- Along the way: `start_kernel`, banner, memblock/zone setup (128 MiB and
  64 MiB tried), SLUB, IRQ init, clocksource switch to `wasm-host`, timers,
  kernel threads (14 host threads alive at the end), all initcall levels,
  `kernel_init` trying `/sbin/init` ... `/bin/sh`.
- Context switching between host threads, lazy thread creation, idle with
  timer deadline, timer interrupt delivery, console, command line from the
  host (`?cmdline=` shows up in "Kernel command line:"), panic reported to
  the host as a halt.

## What is not exercised or known to be off

- **Boot stalls.** There are gaps of 0.3-1.3 s in the log (before the
  "Memory:" line and before "clocksource: Switched") that vary between
  runs and hosts. Not investigated; most likely something waiting for timer
  ticks, which are only polled in idle and `cpu_relax()`.
- Only Chromium and node were tried. Firefox and Safari are untested.
  (The public page https://linux-wasm.github.io/distro/ was checked with
  headless Chromium 147 only.)
- `wasm_raise_irq` (host-raised interrupts) has never been called; the JS
  host does not use it yet. `halt` was only seen with reason "panic".
  Whether any kernel thread has exited through `thread_exit` is unknown.
- The "Memory:" line reports 0K kernel code/data/bss (empty range symbols).
- Kernel version shows as `7.3.4+` because the tree has commits on top of
  the tag.
- No user space: `binfmt`, syscalls, initramfs, tty are all absent
  (`BLK_DEV_INITRD`, `TTY`, `PROC_FS`, `SYSFS` are off in tinyconfig).

## What is stubbed (every `FIXME(wasm)` in the tree)

| Where | What |
|---|---|
| `asm/compiler.h`, `asm/linkage.h` | `__builtin_return_address()`, `_RET_IP_`, `_THIS_IP_` are always 0: nothing can name its caller |
| `asm/linkage.h` | `cond_syscall()` expands to nothing: no `sys_ni` fallbacks, because there is no syscall table |
| `asm/ptrace.h`, `asm/syscall.h` | no user mode, no syscall entry; `user_mode()` is 0 |
| `kernel/process.c` | no return to user space (a kernel thread that execs panics), no user fork/clone (`-ENOSYS`), no ptrace (`-EIO`), no stack traces |
| `kernel/irq.c` | timer is only polled in idle and `cpu_relax()`; long-running kernel code gets no ticks |
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
`get_cmdline(ptr, len) -> len`; plus `env.memory` (shared).
Exports: `_start()`, `wasm_thread_entry(task, sp)`, `wasm_raise_irq(irq)`.

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

## Known blockers / risks

- Each kernel task needs its own wasm call stack, so even a uniprocessor
  kernel needs one host thread (Web Worker) per task and shared memory.
  "Single CPU" therefore still requires SharedArrayBuffer and COOP/COEP.
- One Worker per kernel task is heavy (14 for an idle tinyconfig kernel)
  and browsers cap the number of workers; a busy user space will hit that.
- Without preemption or timer polling in running code, a kernel loop that
  never idles would hang the machine silently.
- Text addresses do not exist in wasm (functions are table indices), so
  `_stext`/`_etext`-style checks, kallsyms and stack traces will be stubs.
