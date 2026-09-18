## 13. Vendor and proprietary toolchains

Anything that cannot be installed from a package repository — licensed compilers, vendor FPGA suites,
proprietary SDKs — should not be baked into a public image or committed to the repository. Two workable
patterns:

1. **Mount the installer.** Add a read-only bind mount of a host directory holding the installer and licence file to the project's compose fragment, and run the installer once from a shell inside the container (`dev shell <project>`), or from a `RUN` line in the Dockerfile that consumes the mount at build time. **Not from `postCreateCommand`:** lifecycle hooks never run here (see below), so an installer wired to one silently never executes.
2. **Private base image.** Build a private image once with the vendor tool inside, push it to a private registry, and have `devcontainer.json` use it as the `FROM`.

**`devcontainer.json` is a build manifest here, not a lifecycle manifest.** `dev build` calls `devcontainer
build`, which only produces an image; the container is then started by docker compose with the entrypoint from
section 18.4. Everything the Dev Containers CLI would normally run at *connect* time is therefore ignored:

| Ignored on the devbox | Where the equivalent lives |
|---|---|
| `postCreateCommand`, `postStartCommand`, `onCreateCommand`, `postAttachCommand` | a `RUN` line in the Dockerfile, or a command you run in `dev shell <project>` |
| `forwardPorts`, `portsAttributes` | the editor's own port forwarding, once connected through either entrypoint |
| `runArgs`, `mounts`, `containerEnv` | the project's `devenv.compose.yml` (section 18.5) |
| `remoteUser` | honoured — it is baked into the image at build time |

`features` and everything else that shapes the image work normally, which is the reason the Dev Containers CLI
is used as the builder at all. The same `devcontainer.json` keeps working unchanged on GitHub Codespaces,
where the ignored keys do take effect.

**Architecture first.** The devbox is ARM64. Before planning around a vendor tool, confirm that it ships an
aarch64 Linux build; many do not. Legacy Xilinx ISE, needed for Spartan-6 targets, is x86-64 only: it does not
run on this host (qemu-user emulation aside, which is impractically slow for synthesis), and the open-source
`yosys` / `nextpnr` flow does not support Spartan-6 either. Keep that work on an x86 machine.

**Sizing.** Some vendor suites are very large — ISE is roughly 15–20 GB installed. Check free disk before
committing to putting one of these inside a container image, and prefer pattern 2 so the cost is paid once
rather than per rebuild. Command-line flows run headless, so where the architecture fits, it is a disk
problem, not a GUI problem.

---
