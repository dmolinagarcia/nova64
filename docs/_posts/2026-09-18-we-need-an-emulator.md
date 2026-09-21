---
categories: general
---

## We need an emulator

At some point, probably way earlier than I think, we will need a noVa64 Emulator. There is going to be an unbelievable amount of work on the software side of the noVa64 and there is a very long time ahead before this work can be carried out on the noVa64 itself. Development will be done on a *regular PC*. So, why not run the new software there too?

But I don't want to depend on a single computer for that. I move, a lot. Different computers, different networks. I need a cloud-based environment. Let's build it!

### Why emulate a machine that doesn't exist

My lab is still packed away. No bench, no scope, no soldering iron within reach, and real noVa64 hardware is a long way off. Meanwhile the document keeps growing, and almost everything is still untested theory. The virtual memory scheme, the TLB, the abort path: they read well on paper, but paper never crashes.

An emulator does. It is the cheapest way I have to find out whether the design holds together, and it's the first thing in this project that can tell me I'm wrong without waiting for a board.

I have also based the versioning scheme on the emulator itself. For the second digit to advance, a feature has to clear review, run in the emulator, and then work on real hardware. Having an emulator will help me progress.

What it will be: a host emulator written in C, cycle-stepped, running natively and in the browser through WebAssembly. Text only in the short term. It doesn't need to reproduce every video mode to be useful, at least, not in the short term; it needs to be accurate enough that software written against it will run on the real machine.

### Why a browser-based environment

Two reasons.

The first is the way I work, the way I move around. Some of the networks I use are restrictive: proxies that inspect TLS, block `vscode.dev`, or break the WebSocket a remote terminal depends on. I needed something that works through all of them, from any browser, with nothing installed locally.

The second is the emulator itself. It will run in the browser, so the same box that builds it will also host it. Edit, build and try it out, all from the same tab.

Until last week I was using GitHub Codespaces. It served me well for the documentation, but its free allowance is a fixed number of core-hours a month, and a C toolchain plus WebAssembly builds would eat through that quickly. I have been hitting the monthly limit regularly for some time. So I decided to build my own.

### What I ended up with

An Oracle Cloud Always Free instance: an ARM64 Ampere machine with 2 cores and 12 GB of RAM, running one container per project, defined by the same `devcontainer.json` that Codespaces uses. The editor runs *inside* the container, and there are two independent ways to reach it:

- **Entrypoint A**, a VS Code tunnel through `vscode.dev`. Outbound connections only, nothing to open on the server.
- **Entrypoint B**, code-server behind Caddy on my own domain, for the networks that don't like `vscode.dev`.

A single `dev` command wraps the whole lifecycle: create a project, build it, update it, back it up. Even the setup needs nothing but a browser: it runs from OCI Cloud Shell.

It's not all roses. Oracle halved the free ARM allowance earlier this year, capacity is often exhausted when launching an instance, idle instances on free accounts can be reclaimed, and Entrypoint B needs a domain, which costs a bit of money. The runbook is upfront about all of it, and every step that could cost you money is flagged.

### The runbook

Everything is in a single sheet: [The development environment]({{ '/docsV3/' | relative_url }}#/sc_v7). It's self-contained: every script, template and configuration file is reproduced in full, and a short `awk` one-liner extracts them into a ready-to-run kit, so there's nothing to copy and paste by hand beyond a few short commands.

It is also the first sheet of the Emulation area to leave the AI part and move into the reviewed part of the document. I didn't just read this one. I ran it, broke it, and ran it again until it came up clean.

Don't expect emulator code in the repository just yet. There is a first prototype already running, but it was written faster than I can read it, and I'm not putting my name on code I don't understand. So next up is more of the same, only slower: detailing how the emulator should work before building it for real, and carrying on with the review of the AI-generated documentation, one sheet at a time.
