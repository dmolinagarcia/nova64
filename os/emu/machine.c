/* noVa64 reference machine — just enough of the specified hardware to bring the
 * console-mode operating system up and to test it.
 *
 * Everything the guest sees follows the specification sheets (docs/docsV3):
 *
 *   $FE:0000-$FE:1FFF  Neon Mode 0 text ring, 4096 cells of 2 bytes   (T1.26, T1.27)
 *   $FE:2000-$FE:2FFF  Neon font store                               (T1.28)
 *   $FF:0000-$FF:00FF  Helium MMU / cache block                      (sheet M)
 *   $FF:8000-$FF:80FF  Neon register file                            (T1.21)
 *
 * and, where the sheets leave an address or an encoding open, the provisional
 * choices recorded in os/include/hw.inc and os/README.md:
 *
 *   $FF:0022..         Helium capability / register-map / platform   (Q168)
 *   $FF:0100           free-running millisecond counter              (L.15, Q22)
 *   $FF:0200           HID event queue                               (CN1.28, Q180)
 *   $FF:0300           SD storage block, register set of sheet G     (G.6, Q22)
 *
 * The MMU is not modelled: the monolithic console build runs with it off
 * (CN1 "The two builds"), so physical = virtual over a flat 16 MB.
 */
#include "nova.h"
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>

#define CYCLES_PER_MS 8000          /* 8 MHz PHI2 */
#define SD_BUSY_CYCLES 1300         /* ~164 us per sector at 25 MHz SPI (G.7) */

static uint8_t *ram;                /* 16 MB, banks $00-$FF; $FE/$FF overlaid */
static uint8_t neon_reg[256];
static uint16_t he_reg[128];

/* ---- options ---- */
static int opt_no_kbd, opt_quiet, opt_no_clock;
static FILE *trace_fp;
static long opt_write_limit = -1;   /* crash injection: stop after N sector writes */
static long opt_cmd_limit = -1;     /* ... or after N write commands, each one whole */
static long sd_write_cmds;
static uint64_t opt_max_cycles = 4000000000ull;

/* ---- SD card ---- */
static FILE *sd_fp; static int sd_ro; static uint64_t sd_sectors;
static uint16_t sd_reg[16];
static int sd_init_state;           /* 0 cold, 1 idle (CMD0), 2 ready */
static int sd_acmd, sd_acmd41_tries;
static uint16_t sd_status, sd_resp;
static uint64_t sd_busy_until;
static long sd_writes;

/* ---- HID queue ---- */
#define HIDQ 16
static struct { uint16_t key, flags; } hidq[HIDQ];
static int hid_head, hid_count, hid_overflow;
static uint64_t idle_polls;         /* HID_STATUS reads since the last sign of work */

static void hid_push(uint8_t usage, uint8_t mods, int press, int repeat) {
    if (hid_count == HIDQ) { hid_overflow = 1; return; }
    int t = (hid_head + hid_count) % HIDQ;
    hidq[t].key = usage | (mods << 8);
    hidq[t].flags = (press ? 1 : 0) | (repeat ? 2 : 0);
    hid_count++;
}

static int screen_dirty;
static void activity(void) { idle_polls = 0; }

/* ---- SD ---- */
static void sd_fail(uint16_t bit) { sd_status |= bit; }
static void sd_xfer(int write, unsigned count) {
    sd_status &= ~0x0001;
    if (!sd_fp) { sd_fail(0x40); return; }
    if (sd_init_state != 2) { sd_fail(0x10); return; }
    uint32_t lba = sd_reg[0] | ((uint32_t)sd_reg[1] << 16);
    uint32_t addr = ((uint32_t)sd_reg[2] << 11) + sd_reg[3];
    if (sd_reg[3] & 0x1FF) { sd_fail(0x20); return; }          /* not block-aligned */
    if (write && !sd_ro && opt_cmd_limit >= 0 && sd_write_cmds++ >= opt_cmd_limit) {
        fflush(sd_fp);
        fprintf(stderr, "emu: power cut after %ld write commands\n", opt_cmd_limit);
        cpu.stopped = 3; return;
    }
    for (unsigned i = 0; i < count; i++, lba++, addr += 512) {
        if (lba >= sd_sectors || addr + 512 > 0xFE0000) { sd_fail(0x10); return; }
        fseek(sd_fp, (long)lba * 512, SEEK_SET);
        if (write) {
            if (sd_ro) { sd_fail(0x10); return; }
            if (opt_write_limit >= 0 && sd_writes >= opt_write_limit) {
                fflush(sd_fp);
                fprintf(stderr, "emu: power cut after %ld sector writes\n", sd_writes);
                cpu.stopped = 3; return;
            }
            fwrite(ram + addr, 1, 512, sd_fp); sd_writes++;
        } else if (fread(ram + addr, 1, 512, sd_fp) != 512) { sd_fail(0x10); return; }
    }
    if (write) fflush(sd_fp);
    sd_busy_until = cpu.cycles + (uint64_t)count * SD_BUSY_CYCLES;
}
static void sd_command(uint16_t idx) {
    uint32_t arg = sd_reg[13] | ((uint32_t)sd_reg[14] << 16);
    sd_resp = 0x04;                                              /* illegal command */
    if (!sd_fp) { sd_resp = 0xFF; return; }
    if (sd_acmd && idx == 41) {
        sd_acmd = 0;
        if (sd_init_state == 0) return;
        if (++sd_acmd41_tries < 3) { sd_resp = 0x01; return; }  /* still initialising */
        sd_init_state = 2; sd_resp = 0x00; return;
    }
    sd_acmd = 0;
    switch (idx) {
    case 0: sd_init_state = 1; sd_acmd41_tries = 0; sd_resp = 0x01; break;
    case 8: sd_resp = (sd_init_state ? 0x01 : 0x05) | ((arg & 0xFF) << 8); break;
    case 55: sd_acmd = 1; sd_resp = sd_init_state == 2 ? 0x00 : 0x01; break;
    case 58: sd_resp = sd_init_state == 2 ? 0x00 : 0x01; break;
    case 13: sd_resp = 0x00; break;
    }
}
static uint16_t sd_read(unsigned off) {
    switch (off) {
    case 0x12: return (sd_status & ~1) | (cpu.cycles < sd_busy_until ? 1 : 0) | (sd_fp ? 0 : 0x40);
    case 0x14: return (sd_fp ? 1 : 0) | (sd_init_state == 2 ? 2 | 4 : 0) | (sd_ro ? 8 : 0);
    case 0x1E: return sd_resp;
    default: return sd_reg[off >> 1];
    }
}
static void sd_write(unsigned off, uint16_t v) {
    activity();
    if (off != 0x16 && cpu.cycles < sd_busy_until && (off == 0x08 || off == 0x0A)) return; /* ignored while BUSY */
    sd_reg[off >> 1] = v;
    switch (off) {
    case 0x08: sd_xfer(0, v); break;
    case 0x0A: sd_xfer(1, v); break;
    case 0x0C: case 0x0E: sd_fail(0x20); break;   /* PIO path not modelled: its data port pops on read (Q181) */
    case 0x16: if (v & 4) sd_status &= ~0x38; break;
    case 0x18: sd_command(v); break;
    }
}

/* ---- bank $FF ---- */
static uint16_t io_read16(uint32_t a) {
    unsigned off = a & 0xFFFE;
    if (off < 0x100) {
        switch (off) {
        case 0x1E: return 0;                                  /* MMU_STATUS: never busy */
        case 0x22: return (opt_no_kbd ? 0 : 1) | 2;           /* CAPS: b0 HID queue, b1 SD block */
        case 0x24: return 0x0001;                             /* register-map version */
        case 0x26: return 0x00E0;                             /* platform: reference emulator */
        case 0x28: return 8192;                               /* RAM visible with the MMU off, 2 KB frames */
        default: return he_reg[off >> 1];
        }
    }
    if (off >= 0x100 && off < 0x200) {
        uint32_t ms = opt_no_clock ? 0 : (uint32_t)(cpu.cycles / CYCLES_PER_MS);
        if (off == 0x100) return ms & 0xFFFF;
        if (off == 0x102) return ms >> 16;
        return 0;
    }
    if (off >= 0x200 && off < 0x300) {
        switch (off - 0x200) {
        case 0: idle_polls++; return (hid_count ? 1 : 0) | (hid_overflow ? 2 : 0);
        case 2: return hid_count;
        case 4: return hid_count ? hidq[hid_head].key : 0;
        case 6: return hid_count ? hidq[hid_head].flags : 0;
        }
        return 0;
    }
    if (off >= 0x300 && off < 0x400) return sd_read(off - 0x300);
    return 0;
}
static void io_write16(uint32_t a, uint16_t v) {
    unsigned off = a & 0xFFFE;
    if (off < 0x100) { he_reg[off >> 1] = v; return; }
    if (off >= 0x200 && off < 0x300) {
        if (off == 0x208 && hid_count) { hid_head = (hid_head + 1) % HIDQ; hid_count--; activity(); }
        if (off == 0x20A && (v & 2)) hid_overflow = 0;
        return;
    }
    if (off >= 0x300 && off < 0x400) sd_write(off - 0x300, v);
}

/* Helium's registers are 16 bits wide on an 8-bit bus: the low byte is held
 * and the write takes effect on the high byte, as CURSOR_POS does (T1.54). */
static uint8_t io_latch;
uint8_t bus_read(uint32_t a) {
    if (a < 0xFF0000) return ram[a];
    if (a >= 0xFF8000) return neon_reg[a & 0xFF];
    uint16_t v = io_read16(a);
    return (a & 1) ? v >> 8 : v & 0xFF;
}
void bus_write(uint32_t a, uint8_t v) {
    if (a < 0xFE0000) { ram[a] = v; return; }
    if (a < 0xFF0000) {
        if (a < 0xFE3000) { ram[a] = v; if (a < 0xFE2000) { screen_dirty = 1; activity(); } }
        return;
    }
    if (a >= 0xFF8000) {
        unsigned r = a & 0xFF;
        if (r == 0 || r == 1 || r == 4) return;               /* read-only */
        neon_reg[r] = v;
        if (r == 0x11 || r == 0x13) neon_reg[r] &= 0x0F;       /* 12-bit cell indices */
        screen_dirty = 1;
        return;
    }
    if (a & 1) io_write16(a, io_latch | (v << 8)); else io_latch = v;
}

/* ---- screen ---- */
#define COLS 128
#define ROWS 32
static const char *latin9(uint8_t c, char *buf) {
    static const struct { uint8_t c; const char *s; } x[] = {
        {0xA4, "€"}, {0xA6, "Š"}, {0xA8, "š"}, {0xB4, "Ž"}, {0xB8, "ž"}, {0xBC, "Œ"}, {0xBD, "œ"}, {0xBE, "Ÿ"} };
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0)) return " ";
    if (c < 0x80) { buf[0] = c; buf[1] = 0; return buf; }
    for (unsigned i = 0; i < sizeof x / sizeof x[0]; i++) if (x[i].c == c) return x[i].s;
    buf[0] = 0xC0 | (c >> 6); buf[1] = 0x80 | (c & 0x3F); buf[2] = 0; return buf;
}
static void screen_dump(FILE *f, int trim) {
    unsigned start = neon_reg[0x10] | (neon_reg[0x11] << 8);
    char b[4];
    for (int r = 0; r < ROWS; r++) {
        int last = COLS - 1;
        if (trim) while (last >= 0 && ram[0xFE0000 + (((start + r * COLS + last) & 0xFFF) << 1)] == 0x20) last--;
        for (int c = 0; c <= last; c++) fputs(latin9(ram[0xFE0000 + (((start + r * COLS + c) & 0xFFF) << 1)], b), f);
        fputc('\n', f);
    }
}

/* ---- keyboard: host characters to HID usages, Spanish ISO layout ---- */
#define M_CTRL 0x01
#define M_SHIFT 0x02
#define M_ALTGR 0x40
struct keydef { uint32_t cp; uint8_t usage, mods, dead; };  /* dead: usage/mods of a dead key struck first */
static const struct keydef keytab[] = {
    {'\n', 0x28, 0}, {'\r', 0x28, 0}, {'\t', 0x2B, 0}, {' ', 0x2C, 0}, {0x7F, 0x2A, 0}, {0x08, 0x2A, 0}, {0x1B, 0x29, 0},
    {'1', 0x1E, 0}, {'2', 0x1F, 0}, {'3', 0x20, 0}, {'4', 0x21, 0}, {'5', 0x22, 0}, {'6', 0x23, 0}, {'7', 0x24, 0},
    {'8', 0x25, 0}, {'9', 0x26, 0}, {'0', 0x27, 0},
    {'!', 0x1E, M_SHIFT}, {'"', 0x1F, M_SHIFT}, {0xB7, 0x20, M_SHIFT}, {'$', 0x21, M_SHIFT}, {'%', 0x22, M_SHIFT},
    {'&', 0x23, M_SHIFT}, {'/', 0x24, M_SHIFT}, {'(', 0x25, M_SHIFT}, {')', 0x26, M_SHIFT}, {'=', 0x27, M_SHIFT},
    {'|', 0x1E, M_ALTGR}, {'@', 0x1F, M_ALTGR}, {'#', 0x20, M_ALTGR}, {'~', 0x21, M_ALTGR}, {0x20AC, 0x22, M_ALTGR},
    {0xAC, 0x23, M_ALTGR},
    {'\'', 0x2D, 0}, {'?', 0x2D, M_SHIFT}, {0xA1, 0x2E, 0}, {0xBF, 0x2E, M_SHIFT},
    {'[', 0x2F, M_ALTGR}, {'+', 0x30, 0}, {'*', 0x30, M_SHIFT}, {']', 0x30, M_ALTGR},
    {0xE7, 0x31, 0}, {0xC7, 0x31, M_SHIFT}, {'}', 0x31, M_ALTGR},
    {0xF1, 0x33, 0}, {0xD1, 0x33, M_SHIFT}, {'{', 0x34, M_ALTGR},
    {0xBA, 0x35, 0}, {0xAA, 0x35, M_SHIFT}, {'\\', 0x35, M_ALTGR},
    {',', 0x36, 0}, {';', 0x36, M_SHIFT}, {'.', 0x37, 0}, {':', 0x37, M_SHIFT}, {'-', 0x38, 0}, {'_', 0x38, M_SHIFT},
    {'<', 0x64, 0}, {'>', 0x64, M_SHIFT},
    {'`', 0x2C, 0, 1}, {'^', 0x2C, 0, 2},                   /* dead key, then space */
    {0, 0, 0}
};
static void key_tap(uint8_t usage, uint8_t mods) { hid_push(usage, mods, 1, 0); hid_push(usage, mods, 0, 0); }
static void dead_tap(int which) {              /* 1 grave  2 circumflex  3 acute  4 diaeresis */
    switch (which) {
    case 1: key_tap(0x2F, 0); break;        case 2: key_tap(0x2F, M_SHIFT); break;
    case 3: key_tap(0x34, 0); break;        case 4: key_tap(0x34, M_SHIFT); break;
    }
}
static int type_cp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') { key_tap(0x04 + cp - 'a', 0); return 1; }
    if (cp >= 'A' && cp <= 'Z') { key_tap(0x04 + cp - 'A', M_SHIFT); return 1; }
    if (cp >= 1 && cp <= 26 && cp != '\n' && cp != '\r' && cp != '\t' && cp != 8) { key_tap(0x04 + cp - 1, M_CTRL); return 1; }
    for (const struct keydef *k = keytab; k->usage; k++)
        if (k->cp == cp) { if (k->dead) dead_tap(k->dead); key_tap(k->usage, k->mods); return 1; }
    static const char *acc[4] = { "àèìòùÀÈÌÒÙ", "âêîôûÂÊÎÔÛ", "áéíóúÁÉÍÓÚýÝ", "äëïöüÄËÏÖÜÿ" };
    static const char *base[4] = { "aeiouAEIOU", "aeiouAEIOU", "aeiouAEIOUyY", "aeiouAEIOUy" };
    for (int d = 0; d < 4; d++) {
        const unsigned char *s = (const unsigned char *)acc[d];
        for (int i = 0; *s; i++, s += 2) {
            uint32_t c = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
            if (c == cp) { dead_tap(d + 1); return type_cp(base[d][i]); }
        }
    }
    return 0;
}
static uint32_t utf8_next(const unsigned char **p) {
    const unsigned char *s = *p; uint32_t c = *s++;
    if (c >= 0xF0) { c = ((c & 7) << 18) | ((s[0] & 0x3F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); s += 3; }
    else if (c >= 0xE0) { c = ((c & 0xF) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F); s += 2; }
    else if (c >= 0xC0) { c = ((c & 0x1F) << 6) | (s[0] & 0x3F); s += 1; }
    *p = s; return c;
}

/* ---- key script ----
 * Plain text is typed as it stands, newline = Enter. Escapes in braces:
 *   {K:uu[:mm]}  raw HID usage, hex, with an optional modifier byte
 *   {C-x}        Ctrl + letter          {BS} {DEL} {TAB} {ESC} {CAPS}
 *   {SNAP name}  dump the screen to <outdir>/name.screen once the machine is idle
 *   {WAIT n}     let n milliseconds of guest time pass
 */
static unsigned char *script; static size_t script_pos, script_len;
static const char *snap_dir = ".";
static uint64_t wait_until;

static void snapshot(const char *name) {
    char path[512]; snprintf(path, sizeof path, "%s/%s.screen", snap_dir, name);
    FILE *f = fopen(path, "w"); if (!f) { perror(path); exit(1); }
    screen_dump(f, 1); fclose(f);
}
static int script_step(void) {                /* returns 0 when the script is exhausted */
    if (script_pos >= script_len) return 0;
    const unsigned char *p = script + script_pos;
    if (*p == '{') {
        const char *e = strchr((const char *)p, '}');
        if (!e) { fprintf(stderr, "emu: unterminated { in key script\n"); exit(1); }
        char tok[128]; size_t n = e - (const char *)p - 1; if (n >= sizeof tok) n = sizeof tok - 1;
        memcpy(tok, p + 1, n); tok[n] = 0;
        script_pos = (const unsigned char *)e + 1 - script;
        unsigned u, m = 0;
        if (sscanf(tok, "K:%x:%x", &u, &m) >= 1) key_tap(u, m);
        else if (!strncmp(tok, "C-", 2)) key_tap(0x04 + (tok[2] | 0x20) - 'a', M_CTRL);
        else if (!strcmp(tok, "BS")) key_tap(0x2A, 0);
        else if (!strcmp(tok, "DEL")) key_tap(0x4C, 0);
        else if (!strcmp(tok, "TAB")) key_tap(0x2B, 0);
        else if (!strcmp(tok, "ESC")) key_tap(0x29, 0);
        else if (!strcmp(tok, "CAPS")) key_tap(0x39, 0);
        else if (!strncmp(tok, "SNAP ", 5)) snapshot(tok + 5);
        else if (!strncmp(tok, "WAIT ", 5)) wait_until = cpu.cycles + strtoull(tok + 5, 0, 10) * CYCLES_PER_MS;
        else { fprintf(stderr, "emu: unknown script token {%s}\n", tok); exit(1); }
        if (script_pos < script_len && script[script_pos] == '\n' && (!strncmp(tok, "SNAP", 4) || !strncmp(tok, "WAIT", 4))) script_pos++;
        return 1;
    }
    uint32_t cp = utf8_next(&p);
    script_pos = p - script;
    if (!type_cp(cp)) fprintf(stderr, "emu: no key for U+%04X\n", cp);
    return 1;
}

/* ---- loading ---- */
static long load_file(const char *path, uint32_t addr) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    long n = fread(ram + addr, 1, 0xFE0000 - addr, f); fclose(f); return n;
}

static struct termios saved_tio; static int tio_saved;
static void restore_tty(void) { if (tio_saved) { tcsetattr(0, TCSANOW, &saved_tio); printf("\x1b[?25h\x1b[0m\n"); } }

static void usage(void) {
    fprintf(stderr,
        "usage: nova64 --bios bios.bin [options]\n"
        "  --card image        attach an SD card image (read-write, in place)\n"
        "  --card-ro image     attach it write-protected\n"
        "  --stage image       pre-load a kernel image into the BIOS staging area (CN1.6)\n"
        "  --keys script       scripted keystrokes; exits when the script is done and the machine idle\n"
        "  --snap-dir dir      where {SNAP name} writes its screens\n"
        "  --screen file       write the final screen to file ('-' for stdout)\n"
        "  --no-kbd            report no HID queue\n"
        "  --no-clock          a millisecond counter that never advances\n"
        "  --power-cut n       cut power after n SD write commands, each completing whole\n"
        "  --power-cut-sectors n   cut power after n sectors: a command can be torn part-way\n"
        "  --max-cycles n      stop after n PHI2 cycles\n"
        "  --quiet             no summary on stderr\n"
        "  --trace file        one line per instruction: PC, registers, opcode\n"
        "With neither --keys nor --screen the emulator is interactive: the terminal is the\n"
        "keyboard and the 128x32 text screen is drawn in it. Ctrl-] quits.\n");
    exit(2);
}

int main(int argc, char **argv) {
    const char *bios = 0, *stage = 0, *keys = 0, *screen = 0, *card = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
#define ARG (i + 1 < argc ? argv[++i] : (usage(), ""))
        if (!strcmp(a, "--bios")) bios = ARG;
        else if (!strcmp(a, "--card")) card = ARG;
        else if (!strcmp(a, "--card-ro")) { card = ARG; sd_ro = 1; }
        else if (!strcmp(a, "--stage")) stage = ARG;
        else if (!strcmp(a, "--keys")) keys = ARG;
        else if (!strcmp(a, "--snap-dir")) snap_dir = ARG;
        else if (!strcmp(a, "--screen")) screen = ARG;
        else if (!strcmp(a, "--no-kbd")) opt_no_kbd = 1;
        else if (!strcmp(a, "--no-clock")) opt_no_clock = 1;
        else if (!strcmp(a, "--power-cut")) opt_cmd_limit = atol(ARG);
        else if (!strcmp(a, "--power-cut-sectors")) opt_write_limit = atol(ARG);
        else if (!strcmp(a, "--max-cycles")) opt_max_cycles = strtoull(ARG, 0, 10);
        else if (!strcmp(a, "--quiet")) opt_quiet = 1;
        else if (!strcmp(a, "--trace")) trace_fp = fopen(ARG, "w");
        else usage();
    }
    if (!bios) usage();
    ram = calloc(1, 0x1000000);
    for (int i = 0; i < 4096; i++) { ram[0xFE0000 + 2 * i] = 0x20; ram[0xFE0001 + 2 * i] = 0x0F; }  /* T1.29 */
    neon_reg[0] = 0x4E; neon_reg[1] = 0x10; neon_reg[3] = 1; neon_reg[0x14] = 0x10;
    load_file(bios, 0x00E000);
    if (stage) load_file(stage, 0x100000);
    if (card) {
        sd_fp = fopen(card, sd_ro ? "rb" : "r+b"); if (!sd_fp) { perror(card); return 1; }
        fseek(sd_fp, 0, SEEK_END); sd_sectors = ftell(sd_fp) / 512;
    }
    if (keys) {
        FILE *f = fopen(keys, "rb"); if (!f) { perror(keys); return 1; }
        fseek(f, 0, SEEK_END); script_len = ftell(f); rewind(f);
        script = malloc(script_len + 1); if (fread(script, 1, script_len, f) != script_len) return 1;
        script[script_len] = 0; fclose(f);
    }
    int interactive = !keys && !screen;
    if (interactive) {
        if (tcgetattr(0, &saved_tio) == 0) {
            struct termios t = saved_tio; cfmakeraw(&t); t.c_oflag |= OPOST;
            tcsetattr(0, TCSANOW, &t); tio_saved = 1; atexit(restore_tty);
        }
        fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
        printf("\x1b[2J\x1b[?25l");
    }

    cpu_reset();
    uint64_t next_host = 0; int script_done = !keys;
    while (!cpu.stopped && cpu.cycles < opt_max_cycles) {
        if (trace_fp)
            fprintf(trace_fp, "%02X:%04X A=%04X X=%04X Y=%04X S=%04X D=%04X B=%02X P=%02X op=%02X\n", cpu.pbr, cpu.pc,
                    cpu.a, cpu.x, cpu.y, cpu.s, cpu.d, cpu.dbr, cpu.p, ram[((uint32_t)cpu.pbr << 16) | cpu.pc]);
        cpu_step();
        if (keys) {
            /* a key goes in only while the guest is waiting for one, so a script
             * can never overrun the queue and the result does not depend on timing */
            if (idle_polls >= 64 && hid_count == 0 && cpu.cycles >= wait_until) {
                if (!script_done) { if (!script_step()) script_done = 1; idle_polls = 0; }
                else if (idle_polls >= 4000) break;
            }
        } else if (!interactive) {
            if (idle_polls >= 4000) break;
        } else if (cpu.cycles >= next_host) {
            next_host = cpu.cycles + 40000;
            /* host input waits here and goes in as the queue has room: a paste must not overrun it */
            static unsigned char pend[4096]; static int pend_n, pend_pos;
            if (pend_pos == pend_n) {
                pend_pos = 0;
                pend_n = read(0, pend, sizeof pend - 8);
                if (pend_n < 0) pend_n = 0;
                pend[pend_n] = 0;
            }
            while (pend_pos < pend_n && hid_count <= HIDQ - 6) {
                const unsigned char *p = pend + pend_pos;
                if (*p == 0x1D) return 0;
                if (*p == 0x1B && p[1] == '[') { pend_pos += 3; continue; }  /* cursor keys: nothing yet (CN1.11) */
                type_cp(utf8_next(&p));
                pend_pos = p - pend;
            }
            if (screen_dirty) {
                screen_dirty = 0; printf("\x1b[H"); screen_dump(stdout, 0);
                unsigned start = neon_reg[0x10] | (neon_reg[0x11] << 8), cur = neon_reg[0x12] | (neon_reg[0x13] << 8);
                unsigned rel = (cur - start) & 0xFFF;
                printf("\x1b[%u;%uH\x1b[?25h", rel / COLS + 1, rel % COLS + 1); fflush(stdout);
            }
            if (idle_polls > 200) { struct timespec ts = {0, 2000000}; nanosleep(&ts, 0); }
        }
    }
    if (screen) {
        FILE *f = strcmp(screen, "-") ? fopen(screen, "w") : stdout;
        if (!f) { perror(screen); return 1; }
        screen_dump(f, 1); if (f != stdout) fclose(f);
    }
    if (!opt_quiet)
        fprintf(stderr, "emu: %s after %llu cycles (%llu ms), PC=$%02X:%04X, %ld sector writes\n",
                cpu.stopped == 1 ? "STP" : cpu.stopped == 2 ? "halt" : cpu.stopped == 3 ? "power cut" :
                cpu.cycles >= opt_max_cycles ? "cycle limit" : "idle",
                (unsigned long long)cpu.cycles, (unsigned long long)(cpu.cycles / CYCLES_PER_MS), cpu.pbr, cpu.pc, sd_writes);
    if (cpu.stopped == 2 || cpu.cycles >= opt_max_cycles) return 3;
    return 0;
}
