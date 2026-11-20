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
        curl \
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

# Binaryen (wasm-opt), for user space: programs are run through its
# Asyncify pass so that fork() can capture and restore their call stack
# (see arch/wasm/kernel/fork.c). The upstream release, because Asyncify
# only handles code with exception handling (setjmp/longjmp) in versions
# newer than Debian's package (120).
ARG BINARYEN_VERSION=133
ARG BINARYEN_SHA256_x86_64=2dc9c7813f5375db93d96ead4b78222fcc3e2677bbb832297af4797782a37489
ARG BINARYEN_SHA256_aarch64=89c07ea56faf38d0fbecf36ca8ec0721756716185f265b568e133d427f299bf8
RUN arch="$(uname -m)" \
    && eval "sum=\$BINARYEN_SHA256_$arch" \
    && curl -fsSL -o /tmp/binaryen.tar.gz \
        "https://github.com/WebAssembly/binaryen/releases/download/version_${BINARYEN_VERSION}/binaryen-version_${BINARYEN_VERSION}-${arch}-linux.tar.gz" \
    && echo "$sum  /tmp/binaryen.tar.gz" | sha256sum -c - \
    && tar -xzf /tmp/binaryen.tar.gz -C /opt \
    && ln -s "/opt/binaryen-version_${BINARYEN_VERSION}/bin/wasm-opt" /usr/local/bin/wasm-opt \
    && rm /tmp/binaryen.tar.gz

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
echo "binaryen: $(wasm-opt --version)"\n\
echo "make:     $(make --version | head -1)"\n\
echo "hostcc:   $(gcc --version | head -1)"\n\
echo "node:     $(node --version)"\n' > /usr/local/bin/toolchain-versions \
    && chmod +x /usr/local/bin/toolchain-versions

WORKDIR /work
