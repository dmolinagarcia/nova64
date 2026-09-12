FROM mcr.microsoft.com/devcontainers/base:bookworm

# Replaces mcr.microsoft.com/devcontainers/universal:2-linux, which publishes no
# arm64 manifest and so cannot run on the Ampere A1 devbox, and which carries
# neither of the editor servers the self-hosted setup starts inside the
# container. base:bookworm is multi-arch, so this file builds both here and on
# GitHub Codespaces (x86-64).

# --- Project toolchain -------------------------------------------------------
# python3: docs server (web.sh), documented in docs/docsV3/README.md
# build-essential: C/Makefile
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
	build-essential git make cmake ninja-build python3 python3-pip \
	&& rm -rf /var/lib/apt/lists/*

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
