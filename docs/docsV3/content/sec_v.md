# The windowing OS — wserver and the toolkit
> backing stores · a client protocol · where the privilege boundary falls

[Sheet T](sec_t) specifies Neon and [sheet U](sec_u) the engine that draws; this sheet is the software that runs on them — the window server, the compositor, the client drawing protocol and the widget toolkit. **The first pass at it described the Amiga milestone as software rather than as a capability, and it was right about the shape of the thing** — but it was drawn against a machine with a 12 MHz CPU, 32 MB of unpartitioned video memory and a write-only aperture, none of which is this board. So every figure below is corrected, most of them in the same direction, and one of that pass's architectural claims does not hold at all.

## Five corrections to its premises — read these before trusting any number it quotes.

- V.1 — **The CPU is 8 MHz, not 12, and the aperture is slower than the first pass modelled it.** Its whole argument rested on command emission being the binding constraint, and that argument gets stronger here, not weaker.

| | Assumed then | This board |
|---|---|---|
| CPU | 12 MHz | **8 MHz** ([B.1](sec_b#b1)) |
| Aperture throughput | ~4 B/µs implied | **~1.3 B/µs** in practice, 1.6 at best ([T.2](sec_t#t2)) |
| A 12-byte command | 4.2 µs | **~9 µs** |
| Sheet T's 6-word `COPY_RECT` | — | **~18 µs** ([T.43](sec_t#t43)) |
| Commands per millisecond of CPU | ~240 | **~110, or ~55** |

- V.2 — **Neon's memory is 64 MB and it is partitioned by function**, not 32 MB of free space ([D14](sec_q#d14), [D38](sec_q#d38)). The capacity correction is welcome and changes nothing; the partition is a constraint the first pass's memory map violates outright, because everything in that map lands inside what [U.16](sec_u#u16) reserves for the front framebuffer. Redrawn below (→ [V.31](sec_v#v31)).
- V.3 — **The CPU can read VRAM** ([D32](sec_q#d32), [D37](sec_q#d37)); the "aperture is write-only" premise is two revisions old. **Every conclusion it draws from that premise survives anyway** — no software rasterisation into VRAM, no `GetPixel`, save-under as a blitter copy — because [T.16](sec_t#t16) keeps the software model write-only for the reason that outlasts the hardware: a read runs at aperture speed while the blitter beside it runs at a hundred times that. What does change is the failure mode. Writes are posted through a 32-entry FIFO and never stall; **reads stall PHI2**, so a client library that reads back a pixel does not merely run slowly, it adds to interrupt latency ([T.19](sec_t#t19)).
- V.4 — [[!blocking]] **The aperture is no longer the whole protection story, and a user-mode `wserver` cannot drive Neon at all.** The first pass mapped bank `$FE` into exactly one address space and called that the protection model. That half still works — [J.5](sec_j#j5) already provides the mapping ioctl and [L.10](sec_l#l10) makes `$FE` mappable. But [D36](sec_q#d36) moved Neon's control surface to `$FF:8000`, which is **unmappable to user space by construction**, and `LIST_BASE`, `CMD_CTRL.LIST_RUN`, `CMD_PORT`, `VRAM_PAGE`, `DISPLAY_BASE` and the cursor position are all in it. **Command emission is therefore a privileged act on this machine**, and where the user/kernel split falls is decided by that fact rather than by taste (→ [V.15](sec_v#v15), [Q62](sec_q#q62)).
- V.5 — **The vblank interrupt exists, and the blocking open item N-6 is closed.** [D34](sec_q#d34) routes Neon's three interrupt sources — vblank, raster compare and the list's `SIGNAL` — into Helium's PIC as one line. The compositor is interrupt-driven and does not poll. ((Until the PIC's gateware exists, [T.47](sec_t#t47)'s rule applies: software polls `STATUS.VBLANK` and every pattern here works either way.))

## What this sheet settles, and it is more than it disturbs.

- V.6 — **It is the software half of [Q60](sec_q#q60), and it argues the same way [U.31](sec_u#u31) did from the hardware side.** [U.31](sec_u#u31) showed backing stores with damage-limited recomposition are affordable; this sheet shows what the OS looks like once you have them, and the finding is that **the system gets smaller, not larger**. `layers.library` — cliprect arithmetic, `SIMPLE_REFRESH`, `SMART_REFRESH`, `SUPER_BITMAP` — exists to answer one question, *what is behind this window and who repaints it*, and per-window backing stores delete the question. That is a real argument for Model C and it did not exist when [T.57](sec_t#t57) recommended the flat framebuffer.
- V.7 — **Three consequences worth stating in the affirmative**, because they are what the milestone actually buys. **Clients never receive refresh events** — no `REFRESHWINDOW`, no `BeginRefresh`/`EndRefresh`, and the single largest source of bugs in Amiga application code disappears. **A hung client still displays**, because its backing store is intact and the compositor keeps drawing it, where a hung `SIMPLE_REFRESH` app left a blank hole in the Workbench. And **live window drag is the default rather than an XOR outline**, because a recomposite costs one frame's worth of blitter time and dragging at 60 Hz costs nothing beyond it.
- V.8 — **The event set collapses with it.** Ten events — mouse down/up/move, key, resized, activate, deactivate, close, menupick, timer — against Intuition's IDCMP, most of whose extra traffic exists to service the refresh model or to move gadget handling into the server. **Gadgets are entirely client-side here**, so `EV_MOUSEDOWN` with window-relative coordinates is enough. Resize is the one event a client is obliged to act on, and it is obliged because the server has reallocated and cleared the backing store.
- V.9 — **Menus are windows.** Borderless, always-front, composited like anything else, closed and recomposited. No save-under path, no special case — which is worth recording next to [T.58](sec_t#t58), where save-under was specified as a bounded special case for the flat model. **Under backing stores it is not needed at all**, and the scratch pool [T.58](sec_t#t58) reserves for it can be spent elsewhere.

## The bit-depth fork reaches the toolkit, and the first pass assumed 1 bpp throughout.

- V.10 — Every visual and every budget in it assumes Mode 1 — 1024 × 600 × 1 bpp, 75 KB, dithered patterns, Susan Kare and Workbench 1.3. [Q59](sec_q#q59) may replace that mode with 8 bpp, which [U.10](sec_u#u10) argues for on **logic** rather than on colour. The arithmetic is not close:

| Per composite pass | Mode 1, 1 bpp | 1024 × 600 × 8 bpp |
|---|---|---|
| Clear the back buffer | 76.8 KB W · **0.51 ms** | 614 KB W · **4.1 ms** |
| One full-screen surface copy | 153.6 KB · **1.02 ms** | 1.23 MB · **8.2 ms** |
| Typical desktop, overdraw 1.8× | ~276 KB · **1.8 ms** | ~2.2 MB · **~15 ms** |
| Full recomposites in the 15.5 ms left after scanout | **~15** | **~1.4–1.9** |
| 400 × 300 backing store | 15 KB | 120 KB |
| A hundred of them | 1.5 MB | 12 MB |

- V.11 — **At 1 bpp "recomposite, don't repair" is a luxury; at 8 bpp it is a bare fit, and that changes the advice rather than the model.** The first pass said not to implement dirty-rect union in v1 and to measure first. That is correct at 1 bpp, where the whole screen can be rebuilt fifteen times a frame. At 8 bpp a typical desktop at 1.8× overdraw consumes **essentially the whole frame — ~15 ms of the 15.5 available** — and [U.15](sec_u#u15)'s more conservative 111 MB/s puts it over; the heavy case of 3× overdraw does not fit under either figure. So damage-limited recomposition stops being an optimisation held in reserve and becomes the routine path from the first day. [U.31](sec_u#u31)'s inversion still holds and is what makes it safe — full recomposition stays correct and always available, so a bug in damage accounting costs frames rather than pixels — but the fast path has to exist at 8 bpp, and the first pass did not budget for writing it.
- V.12 — **The client-rendering budget the first pass advertised mostly evaporates too.** It left ~12 ms of the frame for client drawing; at 8 bpp, after one composite pass, there is closer to 5–8 ms. This is not fatal — client drawing is blitter work into backing stores, and a window repaint is a handful of wide blits — but a design that assumes three quarters of the frame is free will be surprised.
- V.13 — **The 1-bit visual language is not portable across this decision, and it is the expensive part to redo.** Gray-as-pattern, selection-as-invert, depth-as-1px-frame-plus-shadow: every icon, every widget state and every dither pattern is drawn against a two-colour screen. **Redrawing them for 256 colours is weeks of art, not an afternoon of code.** Open item N-4 said decide before drawing a single icon; that item is not separate from [Q59](sec_q#q59), it *is* [Q59](sec_q#q59) seen from the toolkit, and it is the strongest reason to answer it early.
  NOTE: N-4 proposes 2 bpp as a middle path — four colours, the Workbench look, 150 KB. **On this device it is the worst of the three.** [U.10](sec_u#u10)'s shifter argument is not linear: at 8 bpp on a 16-bit bus the shifter degenerates to a byte swap, at 2 bpp it is an eight-position shifter and the ~400 LUT largely come back. 2 bpp pays most of the logic cost of 1 bpp and buys four colours instead of 256.
- V.14 — **The toolkit needs XOR, and that is a third argument in [Q58](sec_q#q58) arriving from software.** Selection highlight, pressed states, the text caret, the focus rectangle and any rubber-band outline are all invert operations. Sheet T's operation set has none ([T.34](sec_t#t34)); sheet U's minterm LUT has `$3C` and 255 others ([U.22](sec_u#u22)). Without a raster op the toolkit's §8.3 has to be rewritten around explicit two-pass masked drawing — every selection becomes a fill plus a keyed copy from an inverted duplicate of the content, at twice the traffic and twice the commands. **The two earlier arguments for [U.4](sec_u#u4)'s hybrid were about logic cost and setup cost; this one is about whether the widget set can be drawn at all.**

## Where the window server lives — the one place the first pass's architecture has to change.

- V.15 — **What `wserver` needs to touch, and which side of the wall each thing is on.** This is the whole of the problem in one table.

| It needs | Where it lives | Reachable from user space |
|---|---|---|
| Backing stores, framebuffers, atlases | Neon SDRAM, through the `$FE` window | **Yes** — the mapping ioctl of [J.5](sec_j#j5) |
| The palette | `$FE:8000` ([T.49](sec_t#t49)) | **Yes**, same mapping |
| `CMD_PORT` — FIFO command emission | `$FF:8050` | **No** |
| `LIST_BASE` · `CMD_CTRL.LIST_RUN` | `$FF:8056`, `$FF:8055` | **No** |
| `DISPLAY_BASE` · `SWAP_BUFFERS` | `$FF:8018` and the command stream | **No** |
| `VRAM_PAGE` — which 32 KB the aperture shows | `$FF:8020` | **No** |
| `CURSOR_X` · `CURSOR_Y` | `$FF:8060` | **No** |
| Vblank interrupt | Helium's PIC | **No** |

- V.16 — **So the split falls on command emission, and the recommendation is that the compositor is kernel code and the window server is not.** The glossary already draws this line without having been asked to — *"Compositor: the kernel code that assembles the visible framebuffer from the backing stores, driving the blitter. Distinct from the window manager, which decides what should be where"* ([sheet Z](sec_z)) — and the register map is what makes it true rather than tidy. **The compositor is small**: a vblank handler, a z-ordered list of surfaces, a top-level command list it rebuilds when geometry changes, and a swap. **The window server is not small** — hit-testing, focus, drag, menus, decoration policy, client validation — and none of it needs a privileged instruction.
- V.17 — **`wserver` in user space then builds command lists in VRAM through its aperture mapping and asks the kernel to run them**, which costs two syscalls a frame — start the list, swap the buffers — against a `COP` entry that is tens of cycles ([J.3](sec_j#j3)). That is free. It also keeps [D23](sec_q#d23)'s rule intact: **no new syscalls**, a `/dev/neon` node and an `ioctl`, with the privilege check coming from the node's permissions exactly as `/dev/power` does.
- V.18 — [[open]] **`VRAM_PAGE` is the wrinkle in that arrangement and it is open item N-2 answered with a twist the first pass did not anticipate.** The aperture shows 32 KB at a time ([T.20](sec_t#t20)) and the page register is privileged, so **a user-space server pays a syscall every time its writes cross a 32 KB boundary**. Reprogramming costs one byte store and no stall, so the cost is the syscall, not the hardware. Two mitigations, and they are cheap: keep the list arena, the string cache and the decoration assets **inside as few pages as possible**, and let the ioctl take *page and offset* so a page change and a burst of list writes are one call. If measurement shows the boundary crossings dominating, the compositor takes the list building with it into the kernel; that is a move of one module, not a redesign.
- V.19 — **Client validation is unaffected by any of this and remains the security property that matters.** The shared page is client-writable and must be treated as hostile ([V.20](sec_v#v20)); `wserver` bounds-checks every op against the window's own surface and rejects any surface handle the client does not own. **A user-space server that is compromised can corrupt what is on screen and nothing else** — it cannot aim the command processor at system memory, because it cannot write `LIST_BASE`. Under the first pass's model, where the server owned the registers, a compromised server owned the machine: the command processor reads and writes all of VRAM with no MMU in front of it. **[D36](sec_q#d36) turned that from a rule into a wall, and this is the first design that stands behind it.**

## The client protocol — and it needs no new kernel mechanism.

- V.20 — **The shared command buffer is `mshare`, which [J.5](sec_j#j5)'s table already has.** A 4 KB page mapped writable in the client and readable in the server; the client's `gcl` library appends compact ops and flushes once. A widget repaint is 5–20 ops in one syscall, a full window repaint 50–200. **The alternative rejected here is worth recording** — a syscall per primitive costs ~200 cycles of kernel entry against a primitive the blitter finishes in microseconds, and it would put the drawing API on the wrong side of the ratio the whole machine is arranged around ([T.2](sec_t#t2)).
- V.21 — **The op set maps one-to-one onto Neon primitives, and it is worth saying plainly what that forbids.** `rect_fill` · `frame` · `line` · `blit` · `blit_masked` · `tile` · `text` · `clip`, and no arcs, no curves, no polygons, no floods. **Clients can only produce imagery Neon's primitive set can produce**, because a client cannot write SDRAM and cannot rasterise into it.
  NOTE: The escape hatch — `upload(src_sysram, surf, x, y, w, h)` — is worse than the first pass assumed and should be named as such. The client rasterises on an 8 MHz 65816 with no multiplier, the result crosses into the server, and the server pushes it through the aperture at **~1.3 MB/s**: a 200 × 100 patch at 8 bpp is 20 KB and **~15 ms**, a whole frame, most of it CPU. It is correct to provide it and correct to discourage it; an application whose visual identity depends on it has chosen the wrong machine.
- V.22 — **Decoration is composed into the window's own backing store, and this is quietly the right answer to [U.18](sec_u#u18).** The server draws the frame, title bar and gadgets into the surface above the content area and clips the client out of that band. So the per-frame composite of a window is **one wide blit**, not a wide blit plus four narrow ones — and [U.18](sec_u#u18) is explicit that a 16 × 400 blit pays 400 line-boundary costs against 8 words of payload each. Window furniture is exactly the narrow-rectangle case, and this arrangement pays for it once per resize instead of once per frame.

## Text — where the compositor above already answers most of its worst problem.

- V.23 — **The per-glyph arithmetic first, corrected to this board.** A text-filled screen at the 8 × 16 cell is 128 × 37 ≈ 4,700 glyphs. That was costed at ~20 ms of emission at 12 MHz; here it is **44 ms at 12 bytes per command and ~88 ms at sheet T's 6-word `COPY_EXPAND`** — three to five frames to emit commands for one screen of text, against **~0.23 ms** of blitter traffic to draw it at 1 bpp. The mismatch is not 20:1, it is closer to **200:1**, and the conclusion — naive per-glyph blitting is unusable — is understated rather than wrong.
- V.24 — **But the emission happens once, not once a frame, and per-window command sublists are what make that true.** They live in SDRAM and are `CALL`ed, which is [T.39](sec_t#t39)'s Layer 3. Two kinds of list follow and they are worth naming separately, because the first pass ran them together: a **content list**, which draws into a backing store and is re-executed only when that window's content changes, and a **composite list**, one or two commands per window, executed every frame. **Text lives in the content list.** A window whose text has not changed costs zero commands per frame, no cache of any kind involved.
- V.25 — **What is left is repaint latency, and there the string cache earns its place.** When a window's content does change, the server rebuilds its content list, and that rebuild is emission through the aperture at 0.77 µs a byte. A 20-line text pane is ~1,200 glyphs — **~22 ms of emission**, more than a frame, for one repaint. Composed as cached runs it is 20 `COPY_RECT`s, **~0.4 ms**. So the cache is not what makes text possible per frame — the content list is — **it is what makes a repaint feel instant**, and hit rates in a desktop are high because menu labels, button labels, filenames and column headers all repeat. Proportional fonts fall out of it free, since it stores composed runs and not glyphs, and the caret and selection stay separate `rect_fill`s rather than being baked into a cached run.
  NOTE: Composing a run into the strip still places glyphs at arbitrary x, which is [Q49](sec_q#q49) exactly: whether `COPY_EXPAND` reads its 1-bpp source at a bit-granular offset decides whether the atlas can be packed tight or must be padded to 8 pixels. The cache does not remove that question, it concentrates it in one place.
- V.26 — **`DRAW_GLYPHS` — open item N-3 — is not a substitute for the cache; it is the complement of it, and costing them together is what shows why.** The proposal is one command taking `(font_base, glyph_w, glyph_h, string_ptr_in_sdram, count, dx, dy)`.

| Drawing an 80-character line | CPU emission | First time |
|---|---|---|
| Per glyph, `COPY_EXPAND` | 80 × 24 B ≈ **1.5 ms** | same |
| Cached string, one `COPY_RECT` | 24 B ≈ **18 µs** | ~1.5 ms to compose the strip |
| `DRAW_GLYPHS` | 80 B of text + the command ≈ **80 µs** | **same — there is no first time** |

- V.27 — **So the cache wins for stable labels and `DRAW_GLYPHS` wins for text that changes**, which is to say for the editor, the terminal and the file listing — the applications a desktop is *for*. A 128 × 32 console repaint is ~3.2 ms with `DRAW_GLYPHS` against ~76 ms per glyph, and a single line update is 100 µs. It also removes 256 KB of strip and all the LRU machinery. **The reason to hesitate is [Q61](sec_q#q61) and nothing else**: block RAM is at 32 of 32 before this feature is asked for, and a glyph sequencer wants a source buffer. Cost it in LUT and EBR against the 128-glyph font relief ([Q45](sec_q#q45)) before committing to either (→ [Q64](sec_q#q64)).

## Kernel additions — ports, signals and messages.

- V.28 — **Fixed 32-byte messages copied by the kernel, bulk payloads through `mshare`.** The model is Exec's and it is the right one: each port bound to one bit of the owning task's 32-bit signal mask, `task_wait(sigmask)` as **the single blocking primitive**. Two numbers matter on this CPU. A 32-byte copy through the kernel is ~100 cycles, **~12 µs at 8 MHz**, which is fine for input events and window notifications and is why the message is fixed-size rather than variable. And **motion coalescing is not optional** — at 12 µs a message, an uncoalesced mouse stream is a measurable tax on a machine that also has to composite; never deliver more than one `EV_MOUSEMOVE` per frame per client.
- V.29 — [[open]] **This is the first addition to [J.3](sec_j#j3)'s dispatch table since it was written, and it deserves the same scrutiny [D23](sec_q#d23) applied to power.** Five calls — `port_create`, `port_send`, `port_recv`, `task_wait`, `task_signal` — and they pass the test that `sys_poweroff()` failed: they are general primitives usable by anything, not one subsystem's verb wearing a syscall's clothes. **What must not happen is two blocking mechanisms.** [N.3](sec_n#n3) already has per-device blocked queues; if `task_wait` is added beside them, a process waiting on a device and a message has no single place to wait. **Fold the device queues onto signal bits** so there is one wait primitive, or do not add this one (→ [Q63](sec_q#q63)).
- V.30 — **Input arrives RP2040 → HID driver → kernel input queue → `wserver`**, and that path puts one requirement on [Q4](sec_q#q4), which still owes the HID event format: **an event must fit in 32 bytes and must carry enough state to be coalesced** — absolute position rather than deltas, button state as a bitmap rather than as edges. A format that forces the server to replay every intermediate motion to know where the pointer is cannot be coalesced at all, and that is discovered late and expensively.

## The memory map, redrawn against the bank partition.

- V.31 — The first pass's memory map is a flat allocation from `$000000` upward: two screen buffers, command lists, font atlases, string cache, assets, then backing stores. **Under [D38](sec_q#d38) all of it lands in bank 0, which is the front framebuffer**, and the invariant that every ACTIVATE is issued inside another bank's data phase collapses — worth a factor of two in delivered bandwidth ([U.16](sec_u#u16)). The map is not wrong so much as written before the partition existed. Redrawn, keeping the original sizes at 1 bpp:

| Bank | Contents | GUI use |
|---|---|---|
| 0 | Framebuffer, front | Scanout reads it |
| 1 | Framebuffer, back | The compositor writes it; they exchange at vblank |
| 2 | Backing stores, even-numbered windows | Composite source |
| 3 | Backing stores, odd windows · font atlas · **string cache strip** · icon and pattern assets · **command lists** · audio | Composite source, and everything the content lists read |

- V.32 — [[open]] **Putting the command lists in bank 3 gives the arbiter a fifth stream that [D38](sec_q#d38) never counted**, and it is the one place this sheet adds a hardware question rather than answering one. During a composite pass the command processor is fetching from SDRAM while the blitter reads a backing store and writes the back buffer. For an even-numbered window the fetch and the pixel read are in different banks and nothing is lost; **for an odd-numbered window they are both in bank 3** and each alternation costs a row activation. The traffic is small — a 20-window composite list is a few hundred bytes — but the stall is per crossing, not per byte. Either give the lists their own bank, or interleave window parity so that a window's backing store and the list describing it are never in the same bank, or measure and decide it is noise. **It is cheap to decide now and awkward to discover at G2** (→ [Q65](sec_q#q65)).
- V.33 — [[open]] **And the map cannot exceed 16 MB whatever the device holds, which is worth confirming rather than assuming.** `DISPLAY_BASE`, `DRAW_BASE`, `LIST_BASE` and sheet U's four channel pointers are all **24-bit** ([T.48](sec_t#t48), [U.19](sec_u#u19)), and [U.16](sec_u#u16)'s partition is written as four 4 MB ranges from `$000000` to `$FFFFFF` — a 16 MB space on a 64 MB part. Nothing in the GUI needs more: at 8 bpp a hundred backing stores is 12 MB and the two framebuffers are 1.2 MB. But it means **the partition is a 16 MB window into the device, not a division of it**, and whether the address registers widen to 26 bits or the upper 48 MB is simply unaddressed by the blitter should be stated in sheet T rather than left to be inferred here (→ [Q66](sec_q#q66)).

## Mode switching — open item N-7, and this board answers most of it.

- V.34 — **The video mode is global, so a colour application takes the whole display and the desktop is suspended** — there is no Amiga-style multi-Screen coexistence, and that is a consequence of the hardware rather than a simplification. What survives a switch and back is the useful part: **backing stores are in SDRAM and survive untouched**, so the desktop is restored by one full recomposite — 1 ms at 1 bpp, ~8 ms at 8 bpp, one frame either way. Three things do not look after themselves. **The palette is shared state** at `$FE:8000` and the returning application's is not the desktop's, so the compositor saves and restores it. **The framebuffers change size** between modes — 75 KB at 1 bpp against 600 KB at 8 — so banks 0 and 1 are reallocated rather than reused. And **the text console's block RAM may have been reclaimed** by the graphics mode ([T.12](sec_t#t12), [Q45](sec_q#q45)), which is why [J.4](sec_j#j4) already requires the console driver to hold its own scrollback and repaint from it — the GUI inherits that requirement rather than adding one.

## Staging — G0 to G8, and where they land on the board's own build.

| Stage | Goal | Needs from Neon | Board stage |
|---|---|---|---|
| **G0** | `neon` driver, aperture, command-list builder | N1 | E5 |
| **G1** | Compositor, N static surfaces, vblank swap, cursor tracking | N2 · N3 · the PIC | E8 |
| **G2** | `wserver`: window create/destroy/move/raise, live drag | N3 · **N4** | E8 |
| **G3** | Ports, signals, input routing, focus | kernel only | E7 · E8 |
| **G4** | Shared command buffer, validation, op set | `mshare` | E8 |
| **G5** | Font atlas, string cache, `text()` | N3 `COPY_EXPAND` | E8 |
| **G6** | `tk`: tree, layout, button, label, checkbox | — | E8 · + |
| **G7** | Menus, listbox, scrollbar, textfield | — | + |
| **G8** | Desktop shell — icons, drag-and-drop, launch | — | + |

- V.35 — **The compositor needs N4, not N5, and that is the scheduling finding.** A 20-window desktop composited from the FIFO is ~40 commands — **960 bytes, ~0.7 ms of emission, about 4 % of a frame** — so full command lists in SDRAM are not on the critical path for the Amiga milestone. [Sheet P](sec_p) already puts N3 and N4 at [E8](sec_p#e8) and N5 after it, and that ordering survives this sheet unchanged. **What N5 buys is client repaint, not compositing**: it is what turns a content list into something re-executed rather than re-emitted ([V.24](sec_v#v24)), and until it exists every window repaint is paid in full at 0.77 µs a byte.
- V.36 — **G0 to G2 are the stages that prove the architecture, and the gate is one measurement.** If a full-screen recomposite does not come in near 1.0 ms at 1 bpp — or near 8 ms at 8 bpp — then [V.6](sec_v#v6) through [V.11](sec_v#v11) need revisiting before anything above G2 is written. Instrument it deliberately at G2 rather than inferring it from frame rate: **the number wanted is the blitter's own time for the pass**, separable from emission and from whatever the client tasks are doing.

## What the first pass left open, and what became of it.

| Left open | Here |
|---|---|
| **N-1** Raster operations — does the blitter have XOR? | Sheet T's op set does not, sheet U's minterm LUT does. **This is [Q58](sec_q#q58)**, and the toolkit is now its third argument ([V.14](sec_v#v14)) |
| **N-2** Aperture addressing and the cost of paging | `VRAM_PAGE`, 32 KB window, base = value × 32768 ([T.20](sec_t#t20)). One byte store, no stall — but it is privileged, so the cost is a syscall ([V.18](sec_v#v18)) |
| **N-3** A `DRAW_GLYPHS` command | Complementary to the string cache rather than a replacement; blocked on EBR, not on merit ([V.26](sec_v#v26), [Q61](sec_q#q61)) |
| **N-4** A 2-bpp hires mode | Folded into [Q59](sec_q#q59). **2 bpp is the worst of the three** on this device ([V.13](sec_v#v13)) |
| **N-5** Blitter arbitration | **Closed by [D39](sec_q#d39)** — fixed priority, blitter lowest, preemption at burst boundaries, 223 ns worst case |
| **N-6** Interrupt line to the CPU | **Closed by [D34](sec_q#d34)** — vblank into Helium's PIC. Not blocking |
| **N-7** Mode switch protocol | Answered in [V.34](sec_v#v34); the palette and the framebuffer reallocation are the parts that need code |
| **N-8** Timing closure and EBR at 32/32 | [Q44](sec_q#q44) and [Q61](sec_q#q61), both already open and both already blocking |
| 12 MHz CPU · 32 MB · write-only aperture · unpartitioned VRAM | [V.1](sec_v#v1)–[V.3](sec_v#v3) |
| `wserver` owns the aperture and that is the protection model | Half true. Command emission is privileged; the compositor is kernel code ([V.4](sec_v#v4), [V.16](sec_v#v16)) |

## Verification — the G2 gate, and none of it needs a client.

- [ ] V.37 — **A full-screen recomposite is measured**, blitter time separated from emission time, against 1.0 ms at 1 bpp or ~8 ms at 8 bpp.
- [ ] V.38 — **A window drags at 60 Hz** with no tearing and no visible cost, over a non-uniform background.
- [ ] V.39 — **A window raised from the back of the z-order shows content that was never repainted** — the proof that backing stores are doing what they exist for.
- [ ] V.40 — **A task killed mid-frame leaves its window on screen**, intact and composited, until the server closes it ([V.7](sec_v#v7)).
- [ ] V.41 — **A client cannot draw outside its own window**, with a deliberately corrupted shared buffer as the test vector rather than a well-behaved one ([V.19](sec_v#v19)).

![Fig. 12 — The windowing stack. Pixels and command lists cross into VRAM through the mappable `$FE` aperture; starting a list crosses into `$FF:8000`, which no user task can reach — so the privilege boundary falls on command emission rather than on the framebuffer.](figures/fig-12-wserver.svg)
LEGEND: Trace legend: <span class="m">mint = pixel and command data</span> · <span class="g">gold = the privileged path and the wall it crosses</span> · dashed = what a repaint costs when nothing is cached.
