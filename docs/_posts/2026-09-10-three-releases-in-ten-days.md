---
categories: general
---

## Three releases in ten days

Ten days ago I published [the synthesis document]({{ '/docsV3/' | relative_url }}) as v0.2 and said the next phase was to go through it sheet by sheet and decide what survives contact with reality. Since then there have been three more releases. So, quick tour.

### v0.2.1 — the EC boundary

Sheet D3 now spells out what actually crosses between the embedded controller and the rest of the machine. Writing it down killed a line I'd been repeating for weeks — that the SPI link is the *only* connection between the EC and noVa64. Turns out that's only true for data. Resets, chip selects and the power rails all cross too, and pretending otherwise was never going to survive schematic capture.

### v0.2.2 — the first sheet to survive review

**Vision and philosophy** left the AI area and moved into the human-reviewed part of the document. In diff terms it's two lines. In practice it's the first sheet I've read line by line and been willing to sign, and it also happens to be the smallest, easiest sheet in the whole thing. There's still a ton of work ahead!

### v0.2.3 — video, a third phase, and an emulator

This is the big update, with four major changes.

**Video split into three sheets.** Palette and line control is now introduced and gets its own: two banks, one write port, a global offset, and an atomic switch, which costs block RAM the original budget didn't have. This on its own should enable plasma or fire effects, explosions, player damage, smooth transitions (like day to night) so, pretty powerful for such a low cost in hardware. The blitter and compositor got another, and it opens by admitting it proposes a *different blitter* from the one the main Neon sheet describes — two machines, not two register maps. The panel interface is the third, and it drops the eDP bridge in favour of LVDS. It also contradicts an earlier decision about panel size, says so out loud, and leaves it hanging rather than quietly picking a winner. This content is still AI generated, but I am starting to get the hang of it, and I am sure most of it will survive.

**A third build phase.** There are three phases now instead of two. Phase 0 is a Spartan-6 board already sitting on my desk plus a cheap HAT with a CPU socket, a Pico, an SD card and VGA on it. It is not part of noVa64, it blocks nothing, and everything built on it is disposable by design. Its only job is to soak up calendar time that would otherwise be idle while the prototype carrier doesn't exist yet, and get me going with FPGA programming, as I have no experience at all.

**A whole new area: Emulation.** A host emulator in C, cycle-stepped, running natively and in a browser. The MMU, TLB, abort path and cache model are done and pass directed tests. The CPU core is structurally complete with a partial opcode subset. The compositor isn't there at all. The emulator won't go beyond text only in the short term. But it should help a lot with software design, if I manage to get it accurate enough.
Actual emulator code is not included in this version, but a lot of it is in these sheets.

**And some plumbing.** Syntax highlighting, collapsible code blocks, and the development environment runbook as a first sheet in the Emulation area.

### The obvious problem

Three versions in ten days, and exactly one sheet reviewed. The document is still growing faster than I can check it, which is precisely the thing I said I'd stop doing. I'm not going to pretend that's fixed. But the emulator shifts the odds a little: it's the first thing in this project that can tell me I'm wrong without waiting for a board. And to be honest, I am running out of ideas to throw at Claude to add more features to this madness.

### The versioning scheme

Which brings me to the version numbers, because they aren't decorative.

- The **third** digit moves for documentation: new sheets, rewrites, corrections, anything that doesn't change what the machine does.
- The **second** moves for implementation. Something that exists now and didn't exist before.
- The **first** moves only for milestones big enough that I'd want to phone someone about them.

v0.1.0, which is not even in the repository, is my first draft for noVa64 from 4 years ago. v0.2.0 is the first major AI documentation release. Since then, everything has been third-digit work. Three releases in ten days and we're still on 0.2.x, and that is the scheme doing its job. Nothing new has been implemented, so nothing above the third digit gets to move.

For the second digit to move, a feature has to run the full lap, in this order:

1. It clears human review and leaves the AI area for the part of the document I'm willing to sign.
2. It gets implemented in the emulator.
3. It gets built in real hardware and validated.

Only then does the second digit move — or the first, if the lap was a big enough deal.

The whole point is to get the document, the emulator and the prototypes travelling at the same speed.
