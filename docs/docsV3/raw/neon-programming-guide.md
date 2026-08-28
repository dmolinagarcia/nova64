# Neon — Programming Guide

**Project:** DANI-65816
**Companion to:** *Neon — GPU Specification*, rev 1.0
**Date:** 2026-08-14
**Status:** Draft. Section 8 lists changes this guide forces back into the specification.

---

## 1. Purpose and Assumptions

Three worked examples — a text console, a windowed GUI, and a side-scrolling platformer — written as the software would actually be structured, not as pseudocode.

**Assumptions, all of which should be verified before this code is trusted:**

| Assumption | Status |
|---|---|
| Calypsi C, C99, large code model, 24-bit data pointers | Established for the project |
| `(volatile uint8_t *)0xFE0000` compiles to absolute-long addressing | **Unverified** — see O-13 |
| Bank `$FE` mapped and accessible to the calling code (kernel context) | Per existing protection model |
| Calypsi direct-page pseudo-register usage | **Audit outstanding** — a known project blocker |
| 65816 at 8 MHz | Established |

Code is written for clarity. The two loops that matter for performance — the console glyph store and the per-frame list patch — are marked as assembly candidates.

---

## 2. Layer 0 — Register and Aperture Access

```c
#include <stdint.h>

#define NEON_APERTURE   ((volatile uint8_t *)0xFE0000)
#define NEON_TEXT       ((volatile uint16_t *)0xFE0000)  /* mode 0 */
#define NEON_FONT       ((volatile uint8_t *)0xFE2000)   /* mode 0 */
#define NEON_PALETTE    ((volatile uint8_t *)0xFE8000)
#define NEON_REG        ((volatile uint8_t *)0xFEFF00)

/* Register offsets — see specification §17 */
enum {
    R_ID = 0x00, R_VER = 0x01, R_MODE = 0x02, R_CTRL = 0x03,
    R_STATUS = 0x04, R_BORDER = 0x05,
    R_RASTER = 0x06, R_RASTER_CMP = 0x08,
    R_IRQ_EN = 0x0A, R_IRQ_STATUS = 0x0B, R_IRQ_TAG = 0x0C,
    R_TEXT_START = 0x10, R_CURSOR_POS = 0x12, R_CURSOR_CTRL = 0x14,
    R_DISPLAY_BASE = 0x18, R_DRAW_BASE = 0x1B,
    R_MONO_FG = 0x1E, R_MONO_BG = 0x1F,
    R_VRAM_PAGE = 0x20, R_LINE_STRIDE = 0x21,
    R_CMD_PORT = 0x50, R_CMD_FIFO_FREE = 0x54, R_CMD_CTRL = 0x55,
    R_LIST_BASE = 0x56, R_LIST_PC = 0x59,
    R_PEEK_ADDR = 0x70, R_PEEK_DATA = 0x73
};

#define ST_VBLANK       0x01
#define ST_BLIT_BUSY    0x04
#define ST_CMD_BUSY     0x08
#define ST_PEEK_READY   0x10

#define MODE_TEXT       0
#define MODE_HIRES      1
#define MODE_320        2
#define MODE_512        3
```

Two access patterns recur and are worth isolating, because both depend on hardware latching behaviour:

```c
/* 16-bit registers latch on the HIGH byte write. A 16-bit store writes
   low then high, so the update is atomic with no explicit sequencing. */
static inline void neon_reg16(uint8_t off, uint16_t v) {
    *(volatile uint16_t *)(NEON_REG + off) = v;
}

/* 24-bit address registers: three byte writes, low to high.
   The register takes effect on the write to the high byte. */
static inline void neon_reg24(uint8_t off, uint32_t v) {
    NEON_REG[off]     = (uint8_t)(v);
    NEON_REG[off + 1] = (uint8_t)(v >> 8);
    NEON_REG[off + 2] = (uint8_t)(v >> 16);
}
```

Detecting Neon at all:

```c
int neon_present(void) { return NEON_REG[R_ID] == 0x4E; }
```

---

## 3. Command Encoding

### 3.1 Revision: commands take coordinates, not addresses

An earlier draft of the specification had blitter commands carry 24-bit source and destination *addresses*. In writing the game example it became clear this is the wrong interface. Computing `dst = base + y * stride + x` on the 65816 costs a shift-and-add sequence per sprite per frame — the CPU has no multiplier — and it has to happen for every object every frame, which is precisely the cost this architecture exists to eliminate.

**Commands now carry `(x, y)` in pixels.** Neon computes the address using `DRAW_BASE` and `LINE_STRIDE`. The `y × stride` multiply is one sequential multiply per command in the command processor, roughly 16 clocks at 102 MHz — about 160 ns against a command that takes tens of microseconds. It costs Neon nothing and removes real work from every frame of every program.

A second, larger consequence follows: because the destination is a coordinate pair packed into one 32-bit word, **moving an object is a single 32-bit patch to one word of a display list**. This is what makes §7 cost 140 µs per frame instead of several milliseconds.

### 3.2 Word layouts

```c
enum {
    OP_NOP = 0x00, OP_SET_CLIP = 0x01,
    OP_FILL_RECT = 0x10, OP_FILL_PATTERN = 0x11,
    OP_COPY_RECT = 0x20, OP_COPY_KEYED = 0x21,
    OP_COPY_TILED = 0x22, OP_COPY_EXPAND = 0x23,
    OP_DRAW_LINE = 0x30, OP_DRAW_HLINE = 0x31, OP_DRAW_VLINE = 0x32,
    OP_WAIT_VBLANK = 0xF0, OP_WAIT_LINE = 0xF1, OP_SWAP = 0xF2,
    OP_SET_PALETTE = 0xF3, OP_SIGNAL = 0xF8,
    OP_JUMP = 0xFC, OP_CALL = 0xFD, OP_RET = 0xFE, OP_END = 0xFF
};

#define CMD_HDR(op, flags, n)  \
    (((uint32_t)(op) << 24) | ((uint32_t)(flags) << 16) | (uint16_t)(n))

#define XY(x, y)  (((uint32_t)(uint16_t)(y) << 16) | (uint16_t)(x))
#define WH(w, h)  (((uint32_t)(uint16_t)(h) << 16) | (uint16_t)(w))
```

`FILL_RECT` — 4 words:

```
w0  header, count = 3
w1  XY(x, y)
w2  WH(w, h)
w3  colour in bits 7:0
```

`COPY_KEYED` — 6 words. `COPY_RECT` is identical with the key field ignored:

```
w0  header, count = 5
w1  XY(src_x, src_y)        <- patch offset +4
w2  XY(dst_x, dst_y)        <- patch offset +8
w3  WH(w, h)
w4  src_base, 24-bit        (the atlas or level buffer)
w5  bits 31:24 key, bits 15:0 src_stride
```

Destination base and stride come from `DRAW_BASE` and `LINE_STRIDE`. Source base and stride are explicit, because atlases and level buffers have their own geometry.

`COPY_EXPAND` — 6 words, source 1-bpp, with foreground and background colours in w5.

### 3.3 Emitting to the FIFO

`CMD_PORT` latches on the write to offset `$53`. Two 16-bit stores in ascending order produce that naturally:

```c
static void cmd_push(uint32_t w) {
    *(volatile uint16_t *)(NEON_REG + R_CMD_PORT)     = (uint16_t)w;
    *(volatile uint16_t *)(NEON_REG + R_CMD_PORT + 2) = (uint16_t)(w >> 16);
}

/* Reserve space for a whole command before emitting any of it, so the
   engine never stalls part-way through decoding one. */
static void cmd_reserve(uint8_t words) {
    while (NEON_REG[R_CMD_FIFO_FREE] < words) { }
}

void neon_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t c) {
    cmd_reserve(4);
    cmd_push(CMD_HDR(OP_FILL_RECT, 0, 3));
    cmd_push(XY(x, y));
    cmd_push(WH(w, h));
    cmd_push(c);
}

void neon_copy_keyed(uint32_t src_base, uint16_t src_stride,
                     int16_t sx, int16_t sy, int16_t dx, int16_t dy,
                     uint16_t w, uint16_t h, uint8_t key) {
    cmd_reserve(6);
    cmd_push(CMD_HDR(OP_COPY_KEYED, 0x01, 5));   /* flag 0x01 = key enable */
    cmd_push(XY(sx, sy));
    cmd_push(XY(dx, dy));
    cmd_push(WH(w, h));
    cmd_push(src_base);
    cmd_push(((uint32_t)key << 24) | src_stride);
}
```

---

## 4. Example 1 — Text Console

Mode 0 needs no initialisation. Neon is already displaying when the first line of C runs. The console is a ring pointer, a cursor, and a current attribute.

```c
typedef struct {
    uint16_t cursor;   /* buffer-absolute cell index, 0..4095 */
    uint16_t top;      /* shadow of TEXT_START                */
    uint8_t  col;      /* 0..127                              */
    uint8_t  attr;     /* bg<<4 | fg                          */
} console_t;

static console_t con;

#define COLS 128
#define ROWS 32
#define CELLS (COLS * ROWS)     /* 4096, a power of two */

void con_init(uint8_t attr) {
    con.cursor = 0; con.top = 0; con.col = 0; con.attr = attr;
    for (uint16_t i = 0; i < CELLS; i++)
        NEON_TEXT[i] = ((uint16_t)attr << 8) | ' ';
    neon_reg16(R_TEXT_START, 0);
    neon_reg16(R_CURSOR_POS, 0);
    NEON_REG[R_CURSOR_CTRL] = 0x14;      /* underline, 1 Hz, enabled */
}
```

### 4.1 Scrolling is a pointer move

```c
static void con_scroll(void) {
    con.top = (con.top + COLS) & (CELLS - 1);
    neon_reg16(R_TEXT_START, con.top);

    /* Clear the row that just became the bottom line. */
    uint16_t i = (con.top + (ROWS - 1) * COLS) & (CELLS - 1);
    uint16_t fill = ((uint16_t)con.attr << 8) | ' ';
    for (uint8_t n = 0; n < COLS; n++) {
        NEON_TEXT[i] = fill;
        i = (i + 1) & (CELLS - 1);
    }
    con.cursor = (con.top + (ROWS - 1) * COLS) & (CELLS - 1);
    con.col = 0;
}
```

256 bytes written instead of 8,192 moved: **~160 µs instead of ~5 ms**. On a console that scrolls continuously during boot, this is the difference between visible lag and none.

### 4.2 Character output

```c
void con_putc(char c) {
    if (c == '\n') { con_newline(); return; }
    if (c == '\r') { con.cursor -= con.col; con.col = 0; return; }

    NEON_TEXT[con.cursor] = ((uint16_t)con.attr << 8) | (uint8_t)c;
    con.cursor = (con.cursor + 1) & (CELLS - 1);
    if (++con.col >= COLS) con_newline();
    else neon_reg16(R_CURSOR_POS, con.cursor);
}

static void con_newline(void) {
    con.cursor = (con.cursor + (COLS - con.col)) & (CELLS - 1);
    con.col = 0;
    uint16_t row = ((con.cursor - con.top) & (CELLS - 1)) / COLS;
    if (row >= ROWS) con_scroll();
    neon_reg16(R_CURSOR_POS, con.cursor);
}

void con_puts(const char *s) { while (*s) con_putc(*s++); }
```

> **Assembly candidate.** `con_putc` is called once per character during boot and once per character of every kernel message. A hand-written version storing directly with absolute-long indexed addressing avoids the pointer arithmetic Calypsi will generate around `NEON_TEXT[con.cursor]`. The C version is correct; the assembly version is perhaps 3× faster and worth writing once the direct-page convention audit is complete.

### 4.3 Loading a font

```c
void con_load_font(const uint8_t *font4k) {
    while (!(NEON_REG[R_STATUS] & ST_VBLANK)) { }   /* avoid a torn frame */
    for (uint16_t i = 0; i < 4096; i++) NEON_FONT[i] = font4k[i];
}
```

### 4.4 What the OS adds

The hardware path is identical for BIOS and kernel. The kernel wraps it:

```c
/* Kernel driver, five-function interface, reached via /dev/tty */
static int tty_init(void)  { con_init(0x0F); return 0; }
static int tty_write(const void *buf, size_t n) {
    const char *p = buf;
    for (size_t i = 0; i < n; i++) con_putc(p[i]);
    return (int)n;
}
static int tty_ioctl(int req, void *arg);   /* colour, cursor shape, font */
```

**One constraint must be designed in from the start.** The text buffer occupies EBR that graphics modes may reclaim (specification O-3). The tty driver must therefore keep its own scrollback in system memory and be able to repaint the screen from it, because switching to a graphics mode and back can destroy the buffer contents. This is normal for a virtual-console design, but it cannot be retrofitted cheaply.

---

## 5. Example 2 — Windowed GUI

Mode 1: 1024 × 600, 1 bpp, 75 KB per buffer.

### 5.1 VRAM layout

```c
#define VRAM_FB0        0x000000UL      /*  76,800 B  front buffer   */
#define VRAM_FB1        0x013000UL      /*  76,800 B  back buffer    */
#define VRAM_GLYPHS     0x026000UL      /* 1-bpp font atlas          */
#define VRAM_ICONS      0x030000UL      /* 1-bpp icon atlas          */
#define VRAM_SAVEUNDER  0x040000UL      /* scratch for occlusion     */

#define FB_STRIDE 128                   /* 1024 px / 8 px per byte   */

void gui_init(void) {
    NEON_REG[R_MODE] = MODE_HIRES;
    neon_reg24(R_DISPLAY_BASE, VRAM_FB0);
    neon_reg24(R_DRAW_BASE,    VRAM_FB0);   /* single-buffered: GUI is
                                               event-driven, not animated */
    neon_reg16(R_LINE_STRIDE,  FB_STRIDE);
    NEON_REG[R_MONO_FG] = 0x0F;
    NEON_REG[R_MONO_BG] = 0x00;
    NEON_REG[R_CTRL]    = 0x03;             /* display + backlight    */
}
```

A GUI is repaint-on-event, not redraw-per-frame, so single buffering is correct here and halves the memory. Double buffering would add tearing-free full-screen transitions; it is available if wanted and costs 75 KB.

### 5.2 Text in graphics mode

The EBR font of Mode 0 does not exist here. UI text comes from a 1-bpp glyph atlas in VRAM, drawn with `COPY_EXPAND`, with a width table in system memory so the font can be proportional.

```c
extern const uint8_t glyph_w[96];     /* widths for ' '..'~'         */
extern const uint16_t glyph_x[96];    /* x offset into the atlas     */
#define GLYPH_H 14
#define GLYPH_ATLAS_STRIDE 256

int gui_text(int16_t x, int16_t y, const char *s, uint8_t fg, uint8_t bg) {
    while (*s) {
        uint8_t g = (uint8_t)*s++ - ' ';
        if (g < 96) {
            cmd_reserve(6);
            cmd_push(CMD_HDR(OP_COPY_EXPAND, 0, 5));
            cmd_push(XY(glyph_x[g], 0));
            cmd_push(XY(x, y));
            cmd_push(WH(glyph_w[g], GLYPH_H));
            cmd_push(VRAM_GLYPHS);
            cmd_push(((uint32_t)bg << 24) | ((uint32_t)fg << 16)
                     | GLYPH_ATLAS_STRIDE);
            x += glyph_w[g];
        }
    }
    return x;
}
```

One command per glyph — six words, 24 bytes. A 40-character window title is 960 bytes through the aperture, about **600 µs of CPU**. For a title drawn on window creation this is fine. For a text editor repainting a full screen of text it is not, and such a program should build a display list once and patch it, exactly as the game in §7 does.

### 5.3 Windows

```c
typedef struct window {
    int16_t x, y;
    uint16_t w, h;
    const char *title;
    struct window *next;      /* front to back */
} window_t;

static window_t *z_top;

#define C_FACE   0
#define C_EDGE   1
#define C_TITLE  1
#define TITLE_H  18

void win_paint(const window_t *win) {
    cmd_reserve(4);
    cmd_push(CMD_HDR(OP_SET_CLIP, 0, 3));
    cmd_push(XY(win->x, win->y));
    cmd_push(XY(win->x + win->w - 1, win->y + win->h - 1));
    cmd_push(0);

    neon_fill_rect(win->x, win->y, win->w, win->h, C_FACE);
    neon_fill_rect(win->x, win->y, win->w, TITLE_H, C_TITLE);
    neon_hline(win->x, win->y, win->w, C_EDGE);
    neon_hline(win->x, win->y + win->h - 1, win->w, C_EDGE);
    neon_vline(win->x, win->y, win->h, C_EDGE);
    neon_vline(win->x + win->w - 1, win->y, win->h, C_EDGE);

    neon_copy_keyed(VRAM_ICONS, 64, 0, 0,
                    win->x + 3, win->y + 2, 16, 16, 0);
    gui_text(win->x + 24, win->y + 3, win->title, C_FACE, C_TITLE);
}
```

Roughly a dozen commands plus one per title character. **The CPU never writes a pixel.** It emits perhaps 1.2 KB of command words; the blitter does the drawing while the CPU returns to the event loop.

### 5.4 Moving a window without repainting it

```c
void win_move(window_t *win, int16_t nx, int16_t ny) {
    int16_t ox = win->x, oy = win->y;

    cmd_reserve(6);
    cmd_push(CMD_HDR(OP_COPY_RECT, 0, 5));
    cmd_push(XY(ox, oy));
    cmd_push(XY(nx, ny));
    cmd_push(WH(win->w, win->h));
    cmd_push(VRAM_FB0);            /* source is the screen itself */
    cmd_push(FB_STRIDE);

    win->x = nx; win->y = ny;
    desktop_repaint_exposed(ox, oy, win->w, win->h, win);
}
```

A 400 × 300 window at 1 bpp is 15,000 bytes; read plus write is **~0.3 ms**. Dragging at 60 Hz is comfortable.

`COPY_RECT` is overlap-safe: the engine selects the traversal direction from the sign of the displacement, so a small drag that overlaps source and destination is correct without software intervention.

### 5.5 Save-under, despite the write-only aperture

The specification forbids the CPU reading VRAM. It does **not** forbid the blitter reading VRAM. Save-under therefore works normally, as long as the saved pixels never travel through the CPU:

```c
void menu_open(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    cmd_reserve(6);
    cmd_push(CMD_HDR(OP_COPY_RECT, 0, 5));
    cmd_push(XY(x, y));
    cmd_push(XY(0, 0));                 /* into the save-under buffer  */
    cmd_push(WH(w, h));
    cmd_push(VRAM_FB0);
    cmd_push(FB_STRIDE);
    /* draw the menu over the top */
}

void menu_close(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    cmd_reserve(6);
    cmd_push(CMD_HDR(OP_COPY_RECT, 0, 5));
    cmd_push(XY(0, 0));
    cmd_push(XY(x, y));
    cmd_push(WH(w, h));
    cmd_push(VRAM_SAVEUNDER);
    cmd_push(w > 1024 ? w : 1024);
}
```

The only genuinely unavailable operation is the CPU *inspecting* pixels — screen capture and a colour-picker tool. Both can use `PEEK` and tolerate roughly 20 cycles per byte.

### 5.6 Mouse pointer

The hardware cursor moves independently of everything above. No repaint, no save-under, no interaction with the command stream:

```c
static inline void gui_pointer(uint16_t x, uint16_t y) {
    neon_reg16(0x60, x);
    neon_reg16(0x62, y);
}
```

This is the one case where dedicated hardware beat the blitter, and it is why the cursor survived when the sprite engine did not.

---

## 6. Example 3 — Side-Scrolling Platformer

Mode 2a: 320 × 200, 256 colours, double buffered.

### 6.1 The approach that does not work

The obvious method is a tilemap in system memory redrawn each frame, one `COPY_RECT` per tile. The blitter manages it easily — 260 tiles of 16 × 16 in about 1.3 ms.

**The CPU cannot feed it.** 260 commands × 6 words × 4 bytes = 6,240 bytes through the aperture at ~1.3 MB/s = **4.8 ms per frame**, nearly a third of the budget spent emitting commands rather than drawing. The bottleneck moves from the blitter to the command stream.

This is the trap the display list exists to avoid, and it is worth understanding before reading the alternative.

### 6.2 The approach that works: pre-rendered level, one command

SDRAM is 32 MB. Spend it.

At level load, render the whole level background once into a wide SDRAM buffer. A level 8,192 px wide by 200 high at 8 bpp is **1.64 MB** — five percent of memory. The tile-by-tile rendering happens once, during a load screen, where its cost is irrelevant.

```c
#define VRAM_FB0      0x000000UL     /*  64,000 B */
#define VRAM_FB1      0x010000UL     /*  64,000 B */
#define VRAM_LEVEL    0x020000UL     /* 8192 x 200 = 1,638,400 B */
#define VRAM_SKY      0x1C0000UL     /* 2048 x 200 parallax layer */
#define VRAM_SPRITES  0x200000UL     /* sprite atlas */
#define VRAM_LIST     0x280000UL     /* display list */

#define LEVEL_STRIDE  8192
#define SKY_STRIDE    2048
#define ATLAS_STRIDE  512
#define SCREEN_W      320
#define SCREEN_H      200
#define MAX_SPRITES   24
```

Per frame, the entire scrolling background is **one command**:

```
COPY_RECT  src = (scroll_x, 0)  in VRAM_LEVEL, stride 8192
           dst = (0, 0)         in the back buffer, stride 320
           w = 320, h = 200
```

A source stride of 8,192 windows a 320-pixel viewport out of the level. Scrolling is a change to `scroll_x` — **one 16-bit store**. Cost: ~1.0 ms of blitter time, six command words emitted once and never again.

### 6.3 Building the display list

The list is written through the aperture at load time. Each command's byte offset is retained so its parameters can be patched later.

```c
typedef uint16_t ref_t;      /* byte offset into the list */

static uint16_t list_off;    /* write cursor within the list */

static void list_word(uint32_t w) {
    *(volatile uint32_t *)(NEON_APERTURE + list_off) = w;
    list_off += 4;
}

static void list_begin(void) {
    NEON_REG[R_VRAM_PAGE] = (uint8_t)(VRAM_LIST >> 15);
    list_off = (uint16_t)(VRAM_LIST & 0x7FFF);
}
```

The whole list is well under 32 KB, so `VRAM_PAGE` is set once at build time and again once before the first patch, then never touched. This matters: if patching required a page switch it would cost a register write per sprite.

```c
static ref_t emit_copy(uint8_t op, uint8_t flags, uint32_t base,
                       uint16_t stride, uint16_t sx, uint16_t sy,
                       uint16_t dx, uint16_t dy,
                       uint16_t w, uint16_t h, uint8_t key) {
    ref_t r = list_off;
    list_word(CMD_HDR(op, flags, 5));
    list_word(XY(sx, sy));            /* r + 4  */
    list_word(XY(dx, dy));            /* r + 8  */
    list_word(WH(w, h));              /* r + 12 */
    list_word(base);
    list_word(((uint32_t)key << 24) | stride);
    return r;
}

static ref_t r_sky, r_bg;
static ref_t r_spr[MAX_SPRITES];

void build_frame_list(void) {
    list_begin();
    uint16_t start = list_off;

    list_word(CMD_HDR(OP_WAIT_VBLANK, 0, 0));
    list_word(CMD_HDR(OP_SWAP, 0, 0));

    r_sky = emit_copy(OP_COPY_RECT, 0, VRAM_SKY, SKY_STRIDE,
                      0, 0, 0, 0, SCREEN_W, SCREEN_H, 0);
    r_bg  = emit_copy(OP_COPY_KEYED, 0x01, VRAM_LEVEL, LEVEL_STRIDE,
                      0, 0, 0, 0, SCREEN_W, SCREEN_H, 0);

    for (int i = 0; i < MAX_SPRITES; i++)
        r_spr[i] = emit_copy(OP_COPY_KEYED, 0x01, VRAM_SPRITES,
                             ATLAS_STRIDE, 0, 0, 0, 400, 32, 32, 0);
        /* y = 400 parks unused sprites off-screen; the clip
           rectangle rejects them at zero cost */

    list_word(CMD_HDR(OP_SIGNAL, 0, 1));
    list_word(1);
    list_word(CMD_HDR(OP_JUMP, 0, 1));
    list_word(VRAM_LIST);

    (void)start;
}
```

Note the parking convention. An unused sprite slot is not removed from the list — removing it would mean rewriting the list every time an enemy dies. It is moved off-screen, where the clip rectangle rejects it before any memory traffic occurs. The list is structurally static for the whole level.

### 6.4 Starting it

```c
void game_start(void) {
    NEON_REG[R_MODE] = MODE_320;
    neon_reg16(R_LINE_STRIDE, SCREEN_W);
    neon_reg24(R_DISPLAY_BASE, VRAM_FB0);
    neon_reg24(R_DRAW_BASE,    VRAM_FB1);
    build_frame_list();
    neon_reg24(R_LIST_BASE, VRAM_LIST);
    NEON_REG[R_CMD_CTRL] = 0x02;          /* LIST_RUN */
}
```

**From this point Neon renders continuously with no CPU involvement.** If the game logic stopped entirely, the display would keep refreshing at 60 Hz with the last-patched contents.

### 6.5 The per-frame loop

```c
static inline void patch32(ref_t r, uint32_t v) {
    *(volatile uint32_t *)(NEON_APERTURE + (r & 0x7FFF)) = v;
}

typedef struct { int16_t x, y; uint8_t frame; uint8_t alive; } actor_t;
static actor_t actor[MAX_SPRITES];
static uint16_t scroll_x;

void game_frame(void) {
    /* 1. Wait for the frame just drawn. */
    while (!(NEON_REG[R_IRQ_STATUS] & 0x04)) { }
    NEON_REG[R_IRQ_STATUS] = 0x04;

    /* 2. Game logic — physics, collision, AI, input.
          ~99% of the frame is available here. */
    game_update();

    /* 3. Patch the list. This is the entire cost of rendering. */
    patch32(r_bg  + 4, XY(scroll_x, 0));
    patch32(r_sky + 4, XY(scroll_x >> 2, 0));      /* parallax */

    for (int i = 0; i < MAX_SPRITES; i++) {
        if (actor[i].alive) {
            patch32(r_spr[i] + 4, XY((actor[i].frame & 7) * 32,
                                     (actor[i].frame >> 3) * 32));
            patch32(r_spr[i] + 8, XY(actor[i].x - scroll_x, actor[i].y));
        } else {
            patch32(r_spr[i] + 8, XY(0, 400));     /* park off-screen */
        }
    }
}
```

> **Assembly candidate.** The patch loop runs 48 32-bit stores per frame. A hand-written version using absolute-long indexed addressing with a fixed unroll would roughly halve it. Not required — 140 µs is already under 1% of the frame — but it is the natural place to look if the budget ever tightens.

### 6.6 Synchronisation

The list ends with `SIGNAL 1` immediately before it loops back to `WAIT_VBLANK`. The CPU patches in the interval between that signal and the start of the next frame's drawing. No lock is taken, the engine never stalls, and a patch that arrives late affects the following frame rather than tearing the current one.

`CMD_CTRL.LIST_LOCK` is reserved for structural changes — adding commands, switching levels — where the list is being rewritten rather than patched.

### 6.7 Cost accounting

| Blitter, per frame | |
|---|---|
| Parallax sky | ~1.0 ms |
| Level background, keyed | ~1.4 ms |
| 24 sprites at 32 × 32 | ~0.4 ms |
| **Total** | **~2.8 ms of 15.5 ms available** |

| CPU, per frame | |
|---|---|
| 50 patch words | ~180 µs |
| **Fraction of the 16.7 ms frame** | **~1.1%** |

**Roughly 18% of the drawing budget and 1% of the CPU.** What remains is enough for a third parallax layer, a full-screen palette effect, or several times as many actors — and, more to the point, essentially the entire 65816 for game logic.

### 6.8 Level loading

The level buffer is 1.64 MB. Through the CPU aperture at ~1.3 MB/s that is roughly 1.2 seconds of fully occupied CPU. Through the service port from microSD at ~2.5 MB/s it is roughly 0.6 seconds and costs the CPU nothing.

For a load screen either is acceptable, but the service-port path is strictly better and is the argument for resolving specification item O-11 in favour of giving the service port SDRAM access.

---

## 7. Summary

| | Mechanism | Init required | CPU per frame / operation |
|---|---|---|---|
| Boot display | Bitstream-initialised EBR | none | none — no CPU yet |
| BIOS text | Direct stores to `$FE:0000` | none | ~2 µs per character |
| OS text | Same, wrapped in a driver | none | ~2 µs per character |
| GUI | Command FIFO, blitter draws | mode + bases | ~1 ms per window paint |
| Game | SDRAM display list | mode + list build | **~180 µs per frame** |

Each layer works without the one below it in this table. A failure in the command processor still leaves a working console; a failure in SDRAM still leaves a working boot display.

---

## 8. Changes This Guide Forces Back Into the Specification

| Change | Section affected |
|---|---|
| Blitter commands take `(x, y)` coordinates, not 24-bit addresses; Neon computes `base + y × stride + x` | §14.5 command format |
| `COPY_KEYED` and `COPY_RECT` are 6 words, not 7 | §14.5 |
| Destination base and stride come from `DRAW_BASE` / `LINE_STRIDE`; source base and stride are per-command | §14.5, §17 |
| Command processor requires a sequential multiplier (~16 clocks per command, ~80 LUT) | §18 logic budget |
| `LINE_STRIDE` register added at `$21`–`$22` | §17 |
| Off-screen parking of unused list entries is the supported idiom; the clip rectangle must reject fully-off-screen rectangles before any memory traffic | §12.1 |
| Command processor must sign-extend coordinates and clip negatives, so objects can be partially off the left or top edge | §12.1 |

---

## 9. New Open Items

| ID | Item | Blocks |
|---|---|---|
| **O-13** | Verify Calypsi generates absolute-long addressing for `(volatile uint8_t *)0xFE0000` without a bank-register reload per access | All Neon C code |
| **O-14** | Ratify the coordinate-based command format and budget the multiplier | N4 |
| **O-15** | Confirm the clip unit rejects off-screen rectangles before issuing memory traffic — the parking idiom depends on it | N2 |
| **O-16** | Define who owns SDRAM partitioning between framebuffers, atlases, level buffers, and lists; the constants in §5.1 and §6.2 are placeholders | N2 |
| **O-17** | Whether `COPY_EXPAND` reads its 1-bpp source with a bit-granular x offset, or glyph atlas entries must be byte-aligned | N3 |
