# Build environment for the Linux/Wasm port.
#
# Uses the *stock* Debian 13 (trixie) LLVM 19 packages: clang, wasm-ld (lld)
# and the llvm binutils. No patched LLVM is required (unlike the
# joelseverin/linux-wasm flow, which carries a wasm-ld linker-script patch).
#
# Build:  tools/wasm/scripts/build-env.sh
# Use:    tools/wasm/scripts/kmake.sh <make args>
FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        bc \
        binutils \
        bison \
        bzip2 \
        ca-certificates \
        clang-19 \
        cpio \
        e2fsprogs \
        erofs-utils \
        flex \
        gcc \
        git \
        libc6-dev \
        libclang-rt-19-dev-wasm32 \
        libelf-dev \
        libssl-dev \
        lld-19 \
        llvm-19 \
        make \
        nodejs \
        perl \
        python3 \
        rsync \
        wabt \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

# libclang-rt-19-dev-wasm32 is not needed by the kernel (it carries its own
# __multi3) but by user space: musl and programs link compiler-rt builtins.

# Kbuild is invoked with LLVM=/usr/lib/llvm-19/bin/ (see tools/wasm/scripts/kmake.sh),
# which resolves clang, llvm-ar, ... from that directory; arch/wasm/Makefile
# then selects wasm-ld from the same place.

# Print the exact toolchain versions (recorded in tools/wasm/NOTES.md).
RUN printf '#!/bin/sh\n\
set -e\n\
. /etc/os-release; echo "os:       $PRETTY_NAME"\n\
echo "clang:    $(clang-19 --version | head -1)"\n\
echo "wasm-ld:  $(wasm-ld-19 --version)"\n\
echo "llvm-ar:  $(llvm-ar-19 --version | sed -n 2p | xargs)"\n\
echo "wabt:     $(wasm-objdump --version)"\n\
echo "make:     $(make --version | head -1)"\n\
echo "hostcc:   $(gcc --version | head -1)"\n\
echo "node:     $(node --version)"\n' > /usr/local/bin/toolchain-versions \
    && chmod +x /usr/local/bin/toolchain-versions

WORKDIR /work
