# Linux on WebAssembly

This is the Linux kernel source tree with one addition: `arch/wasm`, a port
of the kernel to WebAssembly (wasm32) that boots in a browser tab or under
node.

**Try it: https://linux-wasm.github.io/distro/** (built and deployed from
[linux-wasm/distro](https://github.com/linux-wasm/distro)).

**Status: early bring-up.** The kernel boots, loads a WebAssembly `/init`
from its initramfs and runs it in user mode. Programs can make system
calls, `clone` and `execve` other programs, and read console input. The
kernel's own tests are small programs without libc; linux-wasm/distro
boots it to a BusyBox shell. See
[tools/wasm/NOTES.md](tools/wasm/NOTES.md) for exactly what works, what is
stubbed and the known problems.

The upstream kernel README is the file [README](README).

## Quick start

Requirements: docker (or podman), node >= 20, python3.

```sh
tools/wasm/scripts/build-env.sh              # once: toolchain container (stock LLVM 19)
tools/wasm/scripts/build.sh                  # -> build/wasm/vmlinux.wasm
tools/wasm/scripts/test-boot.sh              # headless boot test under node
tools/wasm/scripts/serve.py                  # http://127.0.0.1:8080/
```

## Where things are

Everything specific to the port lives in three places; the rest of the tree
is upstream Linux.

| Path | Contents |
|---|---|
| `arch/wasm/` | The architecture: Kconfig, headers, boot, tasks, interrupts, timers, link glue (`arch/wasm/tools/`) |
| `tools/wasm/docker/` | Build container (pinned toolchain) |
| `tools/wasm/scripts/` | `build-env.sh`, `build.sh`, `kmake.sh` (kbuild in the container), `test-boot.sh`, `serve.py` (dev server with COOP/COEP), `sync-upstream.sh` |
| `tools/wasm/host/` | The JavaScript host: browser page and node runner. The only place that knows about browsers |
| `tools/wasm/tests/` | Test programs (no libc) and the expected boot log |
| `tools/wasm/NOTES.md` | Port notes: versions, design, stubs, blockers |
| `.github/workflows/wasm.yml` | CI: build + boot test |

The kernel/host contract is documented in
[arch/wasm/include/asm/host.h](arch/wasm/include/asm/host.h), the ABI of
user programs in
[arch/wasm/kernel/binfmt_wasm.c](arch/wasm/kernel/binfmt_wasm.c).

## Related repositories

| Repository | Role |
|---|---|
| [linux-wasm/linux](https://github.com/linux-wasm/linux) | This one: the kernel |
| [linux-wasm/musl](https://github.com/linux-wasm/musl) | C library for user space (port not started) |
| [linux-wasm/distro](https://github.com/linux-wasm/distro) | Builds kernel + user space into something bootable; pins a commit of this repository |

User space (musl, BusyBox, initramfs) is deliberately kept out of this tree.

## Branches and upstream

- `wasm-port` is the development branch and the target for pull requests.
  It is based on an upstream stable tag (currently `v7.3.4`).
- `master` is an untouched mirror of torvalds/linux from the time of the
  fork; nothing is developed there.
- Remote `upstream` is kernel.org stable. New upstream releases are brought
  in with `tools/wasm/scripts/sync-upstream.sh <tag>`, which merges the tag
  (or, with `--rebase`, replays the port on top of it as a clean series).

## Contributing

- Keep changes inside `arch/wasm/` and `tools/wasm/` whenever possible.
- If generic kernel code must change, put that change in its own commit
  and explain in the message why it cannot be done in the arch. The port
  currently carries four such commits; the list is in the notes.
- One logical change per commit, in English, with a
  [Conventional Commits](https://www.conventionalcommits.org/) subject:
  `feat(wasm): ...`, `fix(wasm): ...`, `build(tools/wasm): ...`,
  `docs(wasm): ...`. Use the touched area as scope (`wasm`, `tools/wasm`,
  or the generic subsystem such as `time`, `kbuild`).
- Nothing in `arch/wasm/` may depend on a browser. Anything the kernel needs
  from outside goes through `asm/host.h`.
- Mark every stub or shortcut with `FIXME(wasm)` and list it in
  `tools/wasm/NOTES.md`. Never fake output.
- `tools/wasm/scripts/test-boot.sh` must pass; update
  `tools/wasm/tests/boot-expect.txt` when the kernel boots further.

## License

GPL-2.0, like the kernel (see [COPYING](COPYING)). Parts of `arch/wasm` are
derived from [tombl/linux](https://github.com/tombl/linux); the approach
also draws on [joelseverin/linux-wasm](https://github.com/joelseverin/linux-wasm).
