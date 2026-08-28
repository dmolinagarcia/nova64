# noVa64 — Windowing OS Architecture Sketch

**Status:** design sketch, first pass. Nothing here is verified against synthesis or silicon.
**Target:** Amiga-OS-class desktop (overlapping windows, preemptive multitasking, widget toolkit) on W65C816S + Neon.
**Date:** 2026-08-15

---

## 1. Premises

These are the hardware facts that drive every decision below. If one of them is wrong, re-read this document.

| Fact | Consequence for the OS |
|---|---|
| GUI mode is **1024×600, 1 bpp**, 75 KB framebuffer | The entire visual language is 1-bit. Shading is dithered patterns. Aesthetic reference is Mac System 1–6 / Amiga OS 1.x. |
| Neon SDRAM is **32 MB at ~150 MB/s** | Off-screen surfaces are effectively free. Every window can own a full backing store. |
| Full-screen mono blit = 153.6 KB of traffic ≈ **1.0 ms** | Full-screen recomposite fits ~13× per frame. Damage tracking is an optimisation, not a requirement. |
| **CPU cannot read VRAM.** Aperture is write-only | No read-modify-write, no `GetPixel`, no software rasterisation into VRAM. All pixel work is blitter work. |
| **Blitter reads without restriction** | Backing-store→screen and screen→backing-store both work. Save-unders, live drag, and compositing are all available. |
| Neon is **not a bus master** | Neon never touches system RAM. Every byte of asset, font, and command list enters through the aperture or the RP2040 service port. |
| **Hardware cursor in all modes** | The pointer is not the OS's problem. No save/restore under the pointer, no cursor flicker, no damage from pointer motion. |
| Video mode is **global** | There is no Amiga-style multi-Screen coexistence. A colour app takes the whole display; the desktop is suspended. |
| MMU with per-process 16 MB spaces, ASIDs, page-level protection | The aperture can be mapped into exactly one address space. Use this. |

### 1.1 The binding constraint

It is not GPU bandwidth. It is **CPU command emission**.

A 16-bit store to the aperture costs ~3 cycles/byte on the 65816. A 12-byte blitter command therefore costs ~50 cycles including loop overhead. At 12 MHz that is 4.2 µs per command.

- 1.0 ms of blitter work = ~1 full-screen copy = **1 command**.
- 1.0 ms of CPU emission = **~240 commands**.

So the design rule is: *minimise the number of commands the CPU writes, not the number of pixels the blitter moves.* Every optimisation in this document follows from that sentence. Concretely, this is why §6.4 caches rendered strings rather than blitting per glyph — per-glyph emission for a text-filled screen costs ~4.5 ms of CPU against ~0.23 ms of blitter bandwidth, a 20:1 mismatch.

---

## 2. Departure from the Amiga model

Amiga OS layered as: **Exec** (tasks, ports, messages) → **graphics.library** (RastPorts, blitter) → **layers.library** (clipping, damage, cliprect save/restore) → **Intuition** (windows, gadgets, IDCMP) → **gadtools/BOOPSI** (widgets).

`layers.library` exists to solve one problem: *what is behind this window, and who repaints it when the window moves?* Its answers were `SIMPLE_REFRESH` (the app repaints, and you see the hole), `SMART_REFRESH` (obscured strips saved to scattered cliprect bitmaps), and `SUPER_BITMAP` (window owns a full off-screen bitmap, too expensive to use widely in 512 KB).

**On noVa64, `SUPER_BITMAP` is affordable for every window.** A 400×300 mono backing store is 15 KB. A hundred of them is 1.5 MB out of 32.

That collapses the stack:

| Amiga | noVa64 | Note |
|---|---|---|
| Exec | **Kernel** (already designed: COP syscalls, tasks, MMU) | Add ports/signals/messages. |
| graphics.library | **`neon` driver** | Command-list builder. Owns the aperture. |
| layers.library | **deleted** | Replaced by the compositor. |
| Intuition | **`wserver`** (window server) | Window objects, z-order, input routing, focus. |
| gadtools / BOOPSI | **`tk`** (toolkit) | Client-side library, not a server component. |

Three consequences worth stating plainly:

1. **Clients never receive refresh events.** There is no `REFRESHWINDOW`, no `BeginRefresh`/`EndRefresh`. A client paints its surface when its own state changes and never otherwise. This removes the single largest source of bugs in Amiga application code.
2. **A hung client still displays.** Its backing store is intact; the compositor keeps drawing it. On Amiga a hung `SIMPLE_REFRESH` app left a blank hole in the desktop.
3. **Live window drag is the default,** not an XOR outline. Recomposite is 1–3 ms; dragging at 60 Hz costs nothing extra.

---

## 3. Stack

```
  ┌─────────────────────────────────────────────┐
  │ shell / apps          (tasks, own 16MB VAS) │
  ├─────────────────────────────────────────────┤
  │ tk    — widgets, layout, fonts  [client lib]│
  │ gcl   — drawing ops → shared cmd buffer     │
  ├══════════════ syscall / message ════════════┤
  │ wserver — windows, z-order, input, focus    │  ← owns the aperture
  │ comp    — z-order → Neon command list       │
  │ neon    — command-list builder, FIFO, IRQ   │
  ├─────────────────────────────────────────────┤
  │ kernel — tasks, MMU, ports, signals, drivers│
  └─────────────────────────────────────────────┘
```

`wserver` is a privileged task. **The 64 KB graphics aperture at bank `$FE` is mapped only into `wserver`'s address space.** No client can touch Neon. This is the whole protection story for graphics, and it costs nothing because the MMU already does it (`VRAM_SEL`).

---

## 4. Surfaces and the compositor

### 4.1 Surface

A surface is a 1-bpp bitmap in Neon SDRAM.

```c
typedef struct {
    uint32_t base;      /* byte offset in Neon SDRAM       */
    uint16_t stride;    /* bytes per row, multiple of 2    */
    uint16_t w, h;      /* pixels                          */
} surface_t;
```

Every window owns one. The two screen buffers are surfaces. The font atlas is a surface. The string cache (§6.4) is a surface. Icons are surfaces.

Allocation is a simple first-fit allocator over Neon SDRAM, kept in system RAM by `wserver`. 32 MB against typical demand of 1–2 MB means fragmentation is not worth engineering against in v1.

### 4.2 Frame

```
  vblank IRQ
    │
    ├─ SWAP_BUFFERS (already queued last frame)
    ├─ if (z-order or geometry dirty) rebuild top-level command list
    ├─ else reuse it unchanged
    └─ start command list
         FILL_RECT   back buffer, desktop pattern
         CALL        window[n-1] sublist     ← back to front
         CALL        window[n-2] sublist
         ...
         CALL        window[0]  sublist      ← frontmost
         SWAP_BUFFERS
         SIGNAL
```

Painter's algorithm, back to front. No clip-rect lists, no region arithmetic. The blitter's built-in clipping handles screen edges; window-on-window occlusion is handled by simply drawing the front one afterwards.

Each window's sublist is a small pre-built block in SDRAM — typically two commands (frame decoration + content copy). It is rebuilt only when that window moves or resizes. The top-level list is `n` `CALL` entries, 8 bytes each: a 20-window desktop costs 160 bytes of CPU emission per rebuild, and zero when nothing moved.

### 4.3 Frame budget

Mono 1024×600, 150 MB/s, 15.5 ms usable.

| Operation | Traffic | Time |
|---|---|---|
| Clear back buffer | 76.8 KB W | 0.51 ms |
| One full-screen surface copy | 153.6 KB R+W | 1.02 ms |
| Typical desktop, 6 windows, overdraw 1.8× | ~276 KB | 1.8 ms |
| Heavy desktop, 15 windows, overdraw 3× | ~460 KB | 3.1 ms |
| **Left for client rendering** | | **~12 ms** |

Overdraw here means total composited area ÷ screen area, counting the desktop clear.

Do **not** implement dirty-rect union in v1. Measure first. If the heavy case turns out to matter, the optimisation is: track the union of moved/damaged rectangles, set the blitter clip to it, and run the same command list unchanged. It is a two-day change made later, not a design constraint carried from the start.

---

## 5. Kernel additions: ports, signals, messages

Amiga's Exec passed messages by pointer because there was one address space. You have per-process paging, so this needs a decision.

**Model:** fixed-size messages copied by the kernel; bulk payloads via shared pages.

```c
port_t  port_create(void);
int     port_send(port_t dst, const msg_t *m);   /* copies 32 bytes */
int     port_recv(port_t p, msg_t *out);         /* non-blocking    */
uint32_t task_wait(uint32_t sigmask);            /* blocks          */
void    task_signal(task_t t, uint32_t sigmask);
```

- `msg_t` is **32 bytes fixed**. Copy cost through the kernel is ~100 cycles. This covers every input event, every window notification, and every short request.
- Each port is bound to one signal bit in the owning task's 32-bit signal mask. `task_wait()` is the single blocking primitive — this is Exec's model and it is the right one.
- Anything larger than 32 bytes goes through a **shared memory object**: a page range mapped into two address spaces. Used for exactly one thing in the GUI stack, the per-client command buffer (§6.2).

Amiga's reply-port convention (send message, wait for it to come back) is worth keeping for synchronous requests, but most GUI traffic is one-way.

---

## 6. Client drawing protocol

### 6.1 The problem

Clients cannot touch Neon. So a drawing call has to cross an address-space boundary. Doing that per primitive — one syscall per `draw_line` — costs ~200+ cycles of kernel entry/exit against a primitive that costs the blitter ~2 µs. Unacceptable.

### 6.2 Shared command buffer

Each client gets a **4 KB page shared with `wserver`**, mapped writable in the client and readable in the server. The client's `gcl` library appends compact drawing ops to it and flushes once.

```c
gc_t *gc = win_begin(w);          /* returns cursor into shared buffer */
    gfx_rect_fill(gc, 0,0, w->cw, w->ch, PAT_WHITE);
    gfx_frame     (gc, 4,4, 100,20);
    gfx_text      (gc, 8,8, "Rename…");
win_end(gc);                      /* one syscall: flush */
```

`wserver` validates every op (clip to the window's own surface, bounds-check, reject any surface handle the client doesn't own) and translates it into blitter commands. Validation is what makes this safe despite the shared page — the client can scribble anything into the buffer, and the server assumes it will.

A typical widget repaint is 5–20 ops in one syscall. A full window repaint is 50–200 ops in one syscall.

### 6.3 Op set exposed to clients

Deliberately small, and every entry maps to something Neon does natively:

| Op | Neon primitive |
|---|---|
| `rect_fill(x,y,w,h,pat)` | `FILL_RECT` / `FILL_PATTERN` |
| `frame(x,y,w,h)` | 4× `HLINE`/`VLINE` |
| `line(x0,y0,x1,y1)` | `DRAW_LINE` |
| `blit(src,sx,sy,w,h,dx,dy)` | `COPY_RECT` |
| `blit_masked(...)` | `COPY_KEYED` |
| `tile(src,x,y,w,h)` | `COPY_TILED` |
| `text(x,y,str)` | see §6.4 |
| `clip(x,y,w,h)` | blitter clip registers |

No arcs, no curves, no polygons, no floods. If an app needs them it rasterises into its own surface... which it cannot do, because it cannot write SDRAM directly. **This is a real limitation and it should be named:** clients can only produce imagery Neon's primitive set can produce. An app that wants arbitrary rasterisation needs a path where the client renders 1-bpp into system RAM and asks the server to upload it. Add `upload(src_sysram, surf, x, y, w, h)` for that; it costs aperture bandwidth and should be discouraged for per-frame use.

### 6.4 Text — the important one

Naive per-glyph blitting: a text-filled 1024×600 screen is 128×37 = 4736 glyphs. At ~50 cycles of command emission each that is **~20 ms of CPU**, four times the whole frame. Even a single 60×20 text pane is 1200 glyphs ≈ 5 ms. Unusable.

**Solution: server-side string cache.**

1. Font is a 1-bpp atlas surface in SDRAM, loaded by RP2040 at boot.
2. First time `wserver` sees a string, it composes it once into a **string cache strip** — one `COPY_RECT` per glyph, done once — and records `(hash, surface, x, y, w, h)`.
3. Every subsequent draw of that string is **one `COPY_RECT`**.
4. LRU eviction over a fixed strip, e.g. 256 KB, which holds several thousand rendered strings.

A window with 20 lines of text costs 20 commands (~1 ms → ~0.1 ms) on repaint, and hit rates in a desktop UI are very high: menu labels, button labels, filenames, and column headers all repeat.

Two further notes:
- Proportional fonts fall out of this for free, since the cache stores composed runs, not glyphs.
- Caret/selection is drawn as a separate `rect_fill`, not baked into the cached run.

**Highest-value Neon feature request:** a `DRAW_GLYPHS` command taking `(font_base, glyph_w, glyph_h, string_ptr_in_sdram, count, dx, dy)`. That makes text one command with no cache at all, and removes the only genuinely awkward part of this design. Worth costing in LUTs before committing to the cache.

---

## 7. Window server

### 7.1 Window object

```c
typedef struct window {
    struct window *next_z;     /* z-order list, front to back      */
    task_t      owner;
    port_t      evport;        /* client's event port (IDCMP-alike)*/
    surface_t   surf;          /* backing store, full window incl. */
                               /* decoration                       */
    rect_t      frame;         /* position on screen               */
    rect_t      content;       /* inset by decoration              */
    uint16_t    flags;         /* CLOSE|DRAG|SIZE|DEPTH|MODAL|...   */
    uint16_t    evmask;        /* which events the client wants     */
    uint32_t    cmdlist;       /* pre-built sublist in SDRAM        */
    char        title[32];
} window_t;
```

Decoration is drawn by the server into the window's own surface, above the content area. The client cannot draw into the decoration band — the server clips it out.

### 7.2 Input routing

RP2040 → HID driver → kernel input queue → `wserver`.

- **Pointer:** hardware cursor position is written directly by `wserver` on every motion event; no compositing involved. Hit-test walks the z-list front to back to find the target window. Motion events are coalesced — never deliver more than one motion event per frame to a client.
- **Buttons:** press hit-tests. If it lands on decoration, the server handles it (drag, close, resize, depth). Otherwise it is delivered to the client with window-relative coordinates.
- **Keyboard:** delivered to the focus window. Focus follows click by default.
- **Drag:** server-internal. Update `frame`, mark z-dirty, recomposite. No client involvement, no events sent until the drag ends (then one `EV_MOVED`).
- **Resize:** the server reallocates the backing store, clears it, sends `EV_RESIZED`. The client must repaint. This is the *only* case where a client is obliged to repaint on demand.

### 7.3 Events to clients

```c
enum { EV_MOUSEDOWN, EV_MOUSEUP, EV_MOUSEMOVE, EV_KEY,
       EV_RESIZED, EV_ACTIVATE, EV_DEACTIVATE, EV_CLOSE,
       EV_MENUPICK, EV_TIMER };
```

That is the whole set. Compare Amiga's IDCMP, which needed `REFRESHWINDOW`, `SIZEVERIFY`, `NEWSIZE`, `GADGETDOWN`, `GADGETUP`, and more — most of which exist to service the refresh model or to move gadget handling into the server. Here gadgets are entirely client-side, so `EV_MOUSEDOWN` is enough.

### 7.4 Menus

Menus are windows. The server opens a borderless, always-front window, composites it like anything else, closes it, and recomposites. No save-under, no special path. Amiga needed a special path because it blanked the screen bar; you don't.

---

## 8. Toolkit (`tk`) — client side

### 8.1 Class model

Not BOOPSI. BOOPSI's generality (dynamic classes, message-based method dispatch, `DoMethod` varargs) is expensive on a 65816 and buys little for a fixed widget set. Use a static vtable, and deliberately mirror the five-function driver interface already used in the kernel:

```c
typedef struct wg wg_t;

typedef struct {
    uint16_t instance_size;
    void (*init)  (wg_t *self, const void *attrs);
    void (*render)(wg_t *self, gc_t *gc);
    bool (*event) (wg_t *self, const ev_t *e);   /* true = consumed */
    void (*layout)(wg_t *self, rect_t bounds);
    int  (*attr)  (wg_t *self, uint16_t id, void *val, bool set);
} wg_class_t;

struct wg {
    const wg_class_t *cls;
    wg_t     *parent, *sibling, *child;
    rect_t    bounds;          /* window-relative */
    uint16_t  flags;           /* VISIBLE|ENABLED|FOCUS|DIRTY */
};
```

Widgets form a tree per window. `event` bubbles from the hit leaf toward the root. `render` walks the tree top-down, skipping clean subtrees.

**65816 notes on this structure:**
- Vtable dispatch is an indirect long call. Under Calypsi that is `JSL [ptr]`-shaped and costs ~15 cycles. Acceptable at widget granularity, wrong at pixel granularity.
- Keep the tree shallow. `layout` recursion must be depth-limited (suggest 8) because the stack lives in pinned bank `$00`.
- `wg_t` is 16 bytes; a dialog with 30 widgets is under 2 KB including subclass data.

### 8.2 Standard set (v1)

`label` · `button` · `checkbox` · `radio` · `textfield` · `listbox` · `scrollbar` · `slider` · `progress` · `separator` · `iconview`

Layout: one container class with a mode flag (`ROW`, `COLUMN`, `GRID`, `FIXED`) and per-child weight. Do not build a constraint solver.

### 8.3 1-bit visual language

- **Gray = pattern.** `FILL_PATTERN` with 50% checker for disabled states and window-inactive title bars.
- **Depth = 1px frames + shadow line.** Buttons: black rect, white interior, 1px black shadow on right/bottom. Pressed = invert.
- **Selection = invert.** Requires an XOR/invert raster op — see open items.
- **Focus = dotted rect** (pattern-filled frame).
- Read Susan Kare's Mac System 1 icon work and the Amiga 1.3 Workbench decoration set before drawing anything. In 1 bpp the constraints are severe and mostly already solved.

### 8.4 Application skeleton

```c
int main(void)
{
    win_t *w = win_open(&(win_attr_t){
        .title = "Files", .x = 100, .y = 80, .w = 420, .h = 300,
        .flags = WIN_CLOSE | WIN_DRAG | WIN_SIZE | WIN_DEPTH });

    wg_t *root = tk_column(w);
    wg_t *list = tk_listbox(root, .weight = 1);
    wg_t *ok   = tk_button (root, "Open");

    tk_layout(w);
    tk_render(w);                       /* one flush */

    for (;;) {
        uint32_t sig = task_wait(win_sigmask(w) | SIG_TIMER);
        msg_t m;
        while (port_recv(win_port(w), &m)) {
            if (m.ev == EV_CLOSE) goto done;
            tk_dispatch(w, &m);         /* routes to widget tree */
        }
        if (tk_dirty(w)) tk_render(w);  /* one flush */
    }
done:
    win_close(w);
    return 0;
}
```

Note what is absent: no refresh handling, no clipping, no damage rectangles, no double-buffer management. That is the payoff of §2.

---

## 9. Neon SDRAM map (indicative)

| Region | Size | Contents |
|---|---|---|
| `0x000000` | 150 KB | Two screen buffers, 75 KB each |
| `0x040000` | 64 KB | Command lists (top-level + per-window sublists) |
| `0x050000` | 64 KB | Font atlases |
| `0x060000` | 256 KB | String cache strip |
| `0x0A0000` | 512 KB | Icon / pattern / cursor assets |
| `0x120000` | ~31 MB | Window backing stores (heap) |

---

## 10. Open items

Ordered by how much they can change the design.

**N-1. Raster operations.** The command list in the spec has no mention of minterms or ROPs. The toolkit needs at minimum **XOR/invert** (selection highlight, pressed states, text caret) and ideally **AND/OR with mask** (though `COPY_KEYED` may cover masking). If `FILL_RECT` and `COPY_RECT` are replace-only, §8.3 has to be rewritten around explicit two-pass masked drawing, at real cost. *Resolve first.*

**N-2. Aperture addressing.** A 64 KB aperture must reach 32 MB of SDRAM, so there is a base/page register. Confirm it exists, its granularity, and the cost of reprogramming it — the compositor and the client-op translator both stride across distant regions and will hit it constantly. If reprogramming stalls, the map in §9 needs reordering to keep hot regions in one page.

**N-3. `DRAW_GLYPHS` command.** See §6.4. Highest-value addition; removes the string cache entirely. Needs a LUT estimate against the ~1,900 LUT of headroom.

**N-4. A 2-bpp hires mode.** 1024×600 @ 2 bpp = 150 KB, full-screen copy 300 KB ≈ 2.0 ms. Still comfortably inside budget, doubles the backing-store cost, and gives exactly the Amiga 1.3 Workbench look (4 colours) instead of Mac mono. This is a genuine fork in the project's identity and is cheap enough that it should be decided deliberately rather than by default. *Decide before drawing a single icon.*

**N-5. Blitter arbitration.** Single blitter, single owner (`wserver`). Confirm that no other subsystem — RP2040 service port during asset load, debug `PEEK` — can preempt a running command list, or define the arbitration.

**N-6. Interrupt line to CPU.** Listed as unverified (O-4) in the Neon spec. The compositor is vblank-driven; without an IRQ it must poll, which wastes CPU and jitters the swap. *Blocking for the compositor.*

**N-7. Mode switch protocol.** A colour app takes the whole display. Define: does the desktop's SDRAM state survive? Who restores the palette? What happens to windows of other running apps? Simplest answer: mode switch suspends the compositor, preserves all backing stores, and restores on switch back — costs one full recomposite.

**N-8. Timing closure at 102 MHz (R-1)** and **EBR at 32/32.** If EBR is genuinely at the exact limit, `DRAW_GLYPHS` (N-3) and any command-list prefetch buffer have nowhere to live. These two interact.

---

## 11. Staged plan

Mirrors the E0–E8 hardware convention.

| Stage | Goal | Success criterion |
|---|---|---|
| **G0** | `neon` driver, aperture, command-list builder | Static pattern on screen from a CPU-built list |
| **G1** | Compositor, N static surfaces, vblank swap | 8 rectangles composited, no tearing, hardware cursor tracks mouse |
| **G2** | `wserver`: window create/destroy/move/raise | Live drag at 60 Hz, measured recomposite cost |
| **G3** | Ports, signals, input routing, focus | Two client tasks receive their own events |
| **G4** | Shared command buffer, validation, op set | Client draws into its own window, cannot draw outside it |
| **G5** | Font atlas, string cache, `text()` | Full text pane repaint under 0.5 ms CPU |
| **G6** | `tk`: tree, layout, button/label/checkbox | Working dialog, tab focus, keyboard activation |
| **G7** | Menus, listbox, scrollbar, textfield | File-open dialog end to end |
| **G8** | Desktop shell | Icons, drag-and-drop, launch |

G0–G2 are the ones that prove the architecture. If full-screen recomposite does not come in near 1 ms at G2, everything downstream of §2 needs revisiting — so instrument that measurement carefully and treat it as the project's first real gate.

---

## 12. Design principles carried forward

1. **Minimise commands, not pixels.** CPU emission is the constraint. Cache anything that turns N commands into 1.
2. **Recomposite, don't repair.** Bandwidth is abundant; correctness of damage tracking is not.
3. **The aperture belongs to one task.** Protection comes free from the MMU already in FPGA-A.
4. **The server validates everything.** The shared command buffer is client-writable and must be treated as hostile.
5. **Clients never repaint on demand** — with the single exception of resize.
6. **Every op maps to a Neon primitive.** No client-visible drawing call that requires software rasterisation.
