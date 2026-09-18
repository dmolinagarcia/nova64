FROM mcr.microsoft.com/devcontainers/base:bookworm

# Replaces mcr.microsoft.com/devcontainers/universal:2-linux, which publishes no
# arm64 manifest and so cannot run on the Ampere A1 devbox, and which carries
# neither of the editor servers the self-hosted setup starts inside the
# container. base:bookworm is multi-arch, so this file builds both here and on
# GitHub Codespaces (x86-64).

# --- Project toolchain -------------------------------------------------------
# python3: docs server (web.sh), documented in docs/docsV3/README.md
# build-essential: C/Makefile
# cc65: ca65/ld65 for the emulator's boot images
# clang-18 + lld-18: the emulator's WebAssembly build (wasm32 target, wasm-ld)
# nodejs 22: headless harnesses and docs tooling
# apt.llvm.org and NodeSource publish amd64 and arm64 alike, so the same
# lines build on the ARM64 devbox and on x86-64.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
	ca-certificates curl gnupg \
	&& install -d -m 0755 /etc/apt/keyrings \
	&& curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
		| gpg --dearmor -o /etc/apt/keyrings/llvm.gpg \
	&& echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm-18 main" \
		> /etc/apt/sources.list.d/llvm.list \
	&& curl -fsSL https://deb.nodesource.com/gpgkey/nodesource-repo.gpg.key \
		| gpg --dearmor -o /etc/apt/keyrings/nodesource.gpg \
	&& echo "deb [signed-by=/etc/apt/keyrings/nodesource.gpg] https://deb.nodesource.com/node_22.x nodistro main" \
		> /etc/apt/sources.list.d/nodesource.list \
	&& apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
		build-essential git make cmake ninja-build python3 python3-pip \
		cc65 clang-18 lld-18 nodejs \
	&& ln -sf /usr/bin/clang-18 /usr/bin/clang \
	&& ln -sf /usr/bin/clang++-18 /usr/bin/clang++ \
	&& rm -rf /var/lib/apt/lists/*

# --- WASI sysroot and wasm32 builtins ----------------------------------------
# Both from the same wasi-sdk release (see nova64-emu-ongoing/README.md). The
# builtins must sit in clang's own resource directory, where it looks for them
# when linking for wasm32. Plain archives, so the host architecture is irrelevant.
ARG WASI_SDK=25
RUN cd /tmp \
	&& curl -fsSL -O "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK}/wasi-sysroot-${WASI_SDK}.0.tar.gz" \
	&& tar -xzf "wasi-sysroot-${WASI_SDK}.0.tar.gz" \
	&& mv "wasi-sysroot-${WASI_SDK}.0" /opt/wasi-sysroot \
	&& curl -fsSL -O "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK}/libclang_rt.builtins-wasm32-wasi-${WASI_SDK}.0.tar.gz" \
	&& tar -xzf "libclang_rt.builtins-wasm32-wasi-${WASI_SDK}.0.tar.gz" \
	&& mkdir -p "$(clang -print-resource-dir)/lib/wasi" \
	&& cp "libclang_rt.builtins-wasm32-wasi-${WASI_SDK}.0/libclang_rt.builtins-wasm32.a" \
		"$(clang -print-resource-dir)/lib/wasi/" \
	&& rm -rf /tmp/wasi-sysroot-* /tmp/libclang_rt.builtins-wasm32-wasi-*

# --- Editor servers (self-hosted requirement, not needed on Codespaces) ------
# The VS Code CLI Alpine build is statically linked and runs on glibc distros
# too. The architecture is detected at build time, so the same file builds on
# the ARM64 devbox and on x86-64.
RUN case "$(uname -m)" in \
		x86_64)  cli_os=cli-alpine-x64 ;; \
		aarch64) cli_os=cli-alpine-arm64 ;; \
		*) echo "unsupported architecture: $(uname -m)" >&2; exit 1 ;; \
	esac \
	&& curl -fsSL "https://code.visualstudio.com/sha/download?build=stable&os=${cli_os}" \
		-o /tmp/vscode-cli.tar.gz \
	&& tar -xf /tmp/vscode-cli.tar.gz -C /usr/local/bin \
	&& rm /tmp/vscode-cli.tar.gz \
	&& curl -fsSL https://code-server.dev/install.sh | sh

# --- Pre-create the volume mount points owned by the runtime user ------------
# A fresh named volume inherits ownership from the image directory it covers.
# Without this the volumes come up root-owned and both doors fail to start.
RUN mkdir -p /home/vscode/.vscode-cli \
		/home/vscode/.vscode-server \
		/home/vscode/.local/share/code-server \
		/home/vscode/.claude \
	&& chown -R vscode:vscode /home/vscode
