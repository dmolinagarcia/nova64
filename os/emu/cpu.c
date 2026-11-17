/* W65C816S core for the noVa64 reference machine.
 *
 * Functional, not cycle-exact: one "cycle" is charged per bus access, which is
 * close for most instructions and is only used to drive the millisecond
 * counter. Decimal mode is not implemented (the OS never sets D); an ADC or
 * SBC with D set halts loudly instead of computing a wrong answer.
 */
#include "nova.h"

cpu_t cpu;

#define FN 0x80
#define FV 0x40
#define FM 0x20
#define FX 0x10
#define FD 0x08
#define FI 0x04
#define FZ 0x02
#define FC 0x01

#define M8 (cpu.e || (cpu.p & FM))
#define X8 (cpu.e || (cpu.p & FX))

static uint8_t rd(uint32_t a) { cpu.cycles++; return bus_read(a & 0xFFFFFF); }
static void wr(uint32_t a, uint8_t v) { cpu.cycles++; bus_write(a & 0xFFFFFF, v); }

static uint8_t fetch8(void) { uint8_t v = rd(((uint32_t)cpu.pbr << 16) | cpu.pc); cpu.pc++; return v; }
static uint16_t fetch16(void) { uint16_t l = fetch8(); return l | (fetch8() << 8); }
static uint32_t fetch24(void) { uint32_t l = fetch16(); return l | ((uint32_t)fetch8() << 16); }

static void push8(uint8_t v) { wr(cpu.s, v); cpu.s--; if (cpu.e) cpu.s = 0x0100 | (cpu.s & 0xFF); }
static uint8_t pop8(void) { cpu.s++; if (cpu.e) cpu.s = 0x0100 | (cpu.s & 0xFF); return rd(cpu.s); }
static void push16(uint16_t v) { push8(v >> 8); push8(v & 0xFF); }
static uint16_t pop16(void) { uint16_t l = pop8(); return l | (pop8() << 8); }

/* Data accesses. An address carries a flag saying whether it wraps inside
 * bank 0 (direct page and stack) or runs on across a bank boundary. */
#define WRAP0 0x80000000u
static uint32_t nxt(uint32_t ea) {
    if (ea & WRAP0) return WRAP0 | ((ea + 1) & 0xFFFF);
    return (ea + 1) & 0xFFFFFF;
}
static uint8_t rd8(uint32_t ea) { return rd(ea & 0xFFFFFF); }
static uint16_t rd16(uint32_t ea) { uint16_t l = rd8(ea); return l | (rd8(nxt(ea)) << 8); }
static void wr8(uint32_t ea, uint8_t v) { wr(ea & 0xFFFFFF, v); }
static void wr16(uint32_t ea, uint16_t v) { wr8(ea, v & 0xFF); wr8(nxt(ea), v >> 8); }

static uint32_t dbr_ea(uint32_t a) { return (((uint32_t)cpu.dbr << 16) + a) & 0xFFFFFF; }
static uint32_t ea_dp(void) { return WRAP0 | ((cpu.d + fetch8()) & 0xFFFF); }
static uint32_t ea_dpx(void) { return WRAP0 | ((cpu.d + fetch8() + cpu.x) & 0xFFFF); }
static uint32_t ea_dpy(void) { return WRAP0 | ((cpu.d + fetch8() + cpu.y) & 0xFFFF); }
static uint32_t ea_abs(void) { return dbr_ea(fetch16()); }
static uint32_t ea_absx(void) { return dbr_ea((uint32_t)fetch16() + cpu.x); }
static uint32_t ea_absy(void) { return dbr_ea((uint32_t)fetch16() + cpu.y); }
static uint32_t ea_long(void) { return fetch24(); }
static uint32_t ea_longx(void) { return (fetch24() + cpu.x) & 0xFFFFFF; }
static uint32_t ea_ind(void) { return dbr_ea(rd16(ea_dp())); }
static uint32_t ea_indy(void) { return dbr_ea((uint32_t)rd16(ea_dp()) + cpu.y); }
static uint32_t ea_xind(void) { return dbr_ea(rd16(ea_dpx())); }
static uint32_t ea_indl(void) { uint32_t p = ea_dp(); uint32_t l = rd16(p); return l | ((uint32_t)rd8(nxt(nxt(p))) << 16); }
static uint32_t ea_indly(void) { return (ea_indl() + cpu.y) & 0xFFFFFF; }
static uint32_t ea_sr(void) { return WRAP0 | ((cpu.s + fetch8()) & 0xFFFF); }
static uint32_t ea_sry(void) { return dbr_ea((uint32_t)rd16(ea_sr()) + cpu.y); }

static void setnz8(uint8_t v) { cpu.p = (cpu.p & ~(FN | FZ)) | (v & 0x80) | (v ? 0 : FZ); }
static void setnz16(uint16_t v) { cpu.p = (cpu.p & ~(FN | FZ)) | ((v >> 8) & 0x80) | (v ? 0 : FZ); }
static void setc(int c) { cpu.p = (cpu.p & ~FC) | (c ? FC : 0); }

static void setp(uint8_t v) {
    cpu.p = v;
    if (cpu.e) cpu.p |= FM | FX;
    if (cpu.p & FX) { cpu.x &= 0xFF; cpu.y &= 0xFF; }
}

static void halt(const char *why, uint8_t op) {
    fprintf(stderr, "emu: CPU halted: %s (opcode $%02X at $%02X:%04X)\n", why, op, cpu.pbr, (uint16_t)(cpu.pc - 1));
    cpu.stopped = 2;
}

static void adc(uint16_t v) {
    if (cpu.p & FD) { halt("decimal mode ADC not implemented", 0x69); return; }
    if (M8) {
        uint8_t a = cpu.a & 0xFF; unsigned r = a + (v & 0xFF) + (cpu.p & FC);
        cpu.p = (cpu.p & ~FV) | ((~(a ^ v) & (a ^ r) & 0x80) ? FV : 0);
        setc(r > 0xFF); cpu.a = (cpu.a & 0xFF00) | (r & 0xFF); setnz8(r);
    } else {
        unsigned r = cpu.a + v + (cpu.p & FC);
        cpu.p = (cpu.p & ~FV) | ((~(cpu.a ^ v) & (cpu.a ^ r) & 0x8000) ? FV : 0);
        setc(r > 0xFFFF); cpu.a = r; setnz16(r);
    }
}
static void sbc(uint16_t v) {
    if (cpu.p & FD) { halt("decimal mode SBC not implemented", 0xE9); return; }
    adc(M8 ? (uint8_t)~v : (uint16_t)~v);
}
static void cmpv(uint16_t r, uint16_t v, int eight) {
    if (eight) { unsigned t = (r & 0xFF) - (v & 0xFF); setc((r & 0xFF) >= (v & 0xFF)); setnz8(t); }
    else { unsigned t = r - v; setc(r >= v); setnz16(t); }
}

static uint16_t rdm(uint32_t ea) { return M8 ? rd8(ea) : rd16(ea); }
static void wrm(uint32_t ea, uint16_t v) { if (M8) wr8(ea, v); else wr16(ea, v); }
static uint16_t rdx(uint32_t ea) { return X8 ? rd8(ea) : rd16(ea); }
static void wrx(uint32_t ea, uint16_t v) { if (X8) wr8(ea, v); else wr16(ea, v); }
static uint16_t immm(void) { return M8 ? fetch8() : fetch16(); }
static uint16_t immx(void) { return X8 ? fetch8() : fetch16(); }
static void seta(uint16_t v) { if (M8) { cpu.a = (cpu.a & 0xFF00) | (v & 0xFF); setnz8(v); } else { cpu.a = v; setnz16(v); } }
static void setx(uint16_t v) { if (X8) { cpu.x = v & 0xFF; setnz8(v); } else { cpu.x = v; setnz16(v); } }
static void sety(uint16_t v) { if (X8) { cpu.y = v & 0xFF; setnz8(v); } else { cpu.y = v; setnz16(v); } }

/* read-modify-write group */
enum { R_ASL, R_ROL, R_LSR, R_ROR, R_INC, R_DEC, R_TSB, R_TRB };
static uint16_t rmw(int k, uint16_t v) {
    int e = M8; uint16_t top = e ? 0x80 : 0x8000, mask = e ? 0xFF : 0xFFFF, r = 0, a;
    switch (k) {
    case R_ASL: setc(v & top); r = (v << 1) & mask; break;
    case R_ROL: r = ((v << 1) | (cpu.p & FC)) & mask; setc(v & top); break;
    case R_LSR: setc(v & 1); r = (v & mask) >> 1; break;
    case R_ROR: r = ((v & mask) >> 1) | ((cpu.p & FC) ? top : 0); setc(v & 1); break;
    case R_INC: r = (v + 1) & mask; break;
    case R_DEC: r = (v - 1) & mask; break;
    case R_TSB: a = cpu.a & mask; cpu.p = (cpu.p & ~FZ) | ((a & v) ? 0 : FZ); return (v | a) & mask;
    case R_TRB: a = cpu.a & mask; cpu.p = (cpu.p & ~FZ) | ((a & v) ? 0 : FZ); return (v & ~a) & mask;
    }
    if (e) setnz8(r); else setnz16(r);
    return r;
}
static void rmw_mem(int k, uint32_t ea) { wrm(ea, rmw(k, rdm(ea))); }
static void rmw_acc(int k) { uint16_t r = rmw(k, M8 ? (cpu.a & 0xFF) : cpu.a); if (M8) cpu.a = (cpu.a & 0xFF00) | r; else cpu.a = r; }

static void bit(uint16_t v, int imm) {
    uint16_t mask = M8 ? 0xFF : 0xFFFF;
    cpu.p = (cpu.p & ~FZ) | ((cpu.a & v & mask) ? 0 : FZ);
    if (!imm) {
        if (M8) cpu.p = (cpu.p & ~(FN | FV)) | (v & 0xC0);
        else cpu.p = (cpu.p & ~(FN | FV)) | ((v >> 8) & 0xC0);
    }
}

static void branch(int cond) { int8_t o = (int8_t)fetch8(); if (cond) { cpu.pc += o; cpu.cycles++; } }

static void interrupt(uint16_t nvec, uint16_t evec, int brk) {
    if (!cpu.e) {
        push8(cpu.pbr); push16(cpu.pc); push8(cpu.p);
        cpu.p = (cpu.p | FI) & ~FD; cpu.pbr = 0;
        cpu.pc = bus_read(nvec) | (bus_read(nvec + 1) << 8);
    } else {
        push16(cpu.pc); push8(brk ? (cpu.p | 0x10) : (cpu.p & ~0x10));
        cpu.p = (cpu.p | FI) & ~FD; cpu.pbr = 0;
        cpu.pc = bus_read(evec) | (bus_read(evec + 1) << 8);
    }
}

void cpu_reset(void) {
    memset(&cpu, 0, sizeof cpu);
    cpu.e = 1; cpu.p = FM | FX | FI; cpu.s = 0x01FF;
    cpu.pc = bus_read(0xFFFC) | (bus_read(0xFFFD) << 8);
}

void cpu_step(void) {
    uint8_t op = fetch8();
    uint32_t ea; uint16_t v; int low5 = op & 0x1F;

    /* group 1: ORA AND EOR ADC STA LDA CMP SBC over fifteen addressing modes */
    int g1 = 1, imm = 0;
    if (op == 0x89) g1 = 0;
    else switch (low5) {
        case 0x01: ea = ea_xind(); break;   case 0x03: ea = ea_sr(); break;
        case 0x05: ea = ea_dp(); break;     case 0x07: ea = ea_indl(); break;
        case 0x09: imm = 1; ea = 0; break;  case 0x0D: ea = ea_abs(); break;
        case 0x0F: ea = ea_long(); break;   case 0x11: ea = ea_indy(); break;
        case 0x12: ea = ea_ind(); break;    case 0x13: ea = ea_sry(); break;
        case 0x15: ea = ea_dpx(); break;    case 0x17: ea = ea_indly(); break;
        case 0x19: ea = ea_absy(); break;   case 0x1D: ea = ea_absx(); break;
        case 0x1F: ea = ea_longx(); break;  default: g1 = 0; ea = 0;
    }
    if (g1) {
        int k = op >> 5;
        if (k == 4) { wrm(ea, cpu.a); return; }
        v = imm ? immm() : rdm(ea);
        switch (k) {
        case 0: seta(cpu.a | v); break;
        case 1: seta(M8 ? ((cpu.a & v) & 0xFF) : (cpu.a & v)); break;
        case 2: seta(cpu.a ^ v); break;
        case 3: adc(v); break;
        case 5: seta(v); break;
        case 6: cmpv(cpu.a, v, M8); break;
        case 7: sbc(v); break;
        }
        return;
    }

    switch (op) {
    case 0x00: fetch8(); interrupt(0xFFE6, 0xFFFE, 1); break;                 /* BRK */
    case 0x02: fetch8(); interrupt(0xFFE4, 0xFFF4, 0); break;                 /* COP */
    case 0x42: fetch8(); break;                                               /* WDM: two-byte no-op */
    case 0xEA: break;
    case 0xDB: cpu.stopped = 1; break;                                        /* STP */
    case 0xCB: cpu.waiting = 1; break;                                        /* WAI */

    case 0x04: rmw_mem(R_TSB, ea_dp()); break;  case 0x0C: rmw_mem(R_TSB, ea_abs()); break;
    case 0x14: rmw_mem(R_TRB, ea_dp()); break;  case 0x1C: rmw_mem(R_TRB, ea_abs()); break;
    case 0x06: rmw_mem(R_ASL, ea_dp()); break;  case 0x0E: rmw_mem(R_ASL, ea_abs()); break;
    case 0x16: rmw_mem(R_ASL, ea_dpx()); break; case 0x1E: rmw_mem(R_ASL, ea_absx()); break;
    case 0x26: rmw_mem(R_ROL, ea_dp()); break;  case 0x2E: rmw_mem(R_ROL, ea_abs()); break;
    case 0x36: rmw_mem(R_ROL, ea_dpx()); break; case 0x3E: rmw_mem(R_ROL, ea_absx()); break;
    case 0x46: rmw_mem(R_LSR, ea_dp()); break;  case 0x4E: rmw_mem(R_LSR, ea_abs()); break;
    case 0x56: rmw_mem(R_LSR, ea_dpx()); break; case 0x5E: rmw_mem(R_LSR, ea_absx()); break;
    case 0x66: rmw_mem(R_ROR, ea_dp()); break;  case 0x6E: rmw_mem(R_ROR, ea_abs()); break;
    case 0x76: rmw_mem(R_ROR, ea_dpx()); break; case 0x7E: rmw_mem(R_ROR, ea_absx()); break;
    case 0xE6: rmw_mem(R_INC, ea_dp()); break;  case 0xEE: rmw_mem(R_INC, ea_abs()); break;
    case 0xF6: rmw_mem(R_INC, ea_dpx()); break; case 0xFE: rmw_mem(R_INC, ea_absx()); break;
    case 0xC6: rmw_mem(R_DEC, ea_dp()); break;  case 0xCE: rmw_mem(R_DEC, ea_abs()); break;
    case 0xD6: rmw_mem(R_DEC, ea_dpx()); break; case 0xDE: rmw_mem(R_DEC, ea_absx()); break;
    case 0x0A: rmw_acc(R_ASL); break; case 0x2A: rmw_acc(R_ROL); break;
    case 0x4A: rmw_acc(R_LSR); break; case 0x6A: rmw_acc(R_ROR); break;
    case 0x1A: rmw_acc(R_INC); break; case 0x3A: rmw_acc(R_DEC); break;

    case 0x24: bit(rdm(ea_dp()), 0); break;   case 0x2C: bit(rdm(ea_abs()), 0); break;
    case 0x34: bit(rdm(ea_dpx()), 0); break;  case 0x3C: bit(rdm(ea_absx()), 0); break;
    case 0x89: bit(immm(), 1); break;

    case 0x64: wrm(ea_dp(), 0); break;   case 0x74: wrm(ea_dpx(), 0); break;
    case 0x9C: wrm(ea_abs(), 0); break;  case 0x9E: wrm(ea_absx(), 0); break;

    case 0x84: wrx(ea_dp(), cpu.y); break;  case 0x8C: wrx(ea_abs(), cpu.y); break;  case 0x94: wrx(ea_dpx(), cpu.y); break;
    case 0x86: wrx(ea_dp(), cpu.x); break;  case 0x8E: wrx(ea_abs(), cpu.x); break;  case 0x96: wrx(ea_dpy(), cpu.x); break;
    case 0xA0: sety(immx()); break;  case 0xA4: sety(rdx(ea_dp())); break;  case 0xAC: sety(rdx(ea_abs())); break;
    case 0xB4: sety(rdx(ea_dpx())); break;  case 0xBC: sety(rdx(ea_absx())); break;
    case 0xA2: setx(immx()); break;  case 0xA6: setx(rdx(ea_dp())); break;  case 0xAE: setx(rdx(ea_abs())); break;
    case 0xB6: setx(rdx(ea_dpy())); break;  case 0xBE: setx(rdx(ea_absy())); break;
    case 0xC0: cmpv(cpu.y, immx(), X8); break;  case 0xC4: cmpv(cpu.y, rdx(ea_dp()), X8); break;  case 0xCC: cmpv(cpu.y, rdx(ea_abs()), X8); break;
    case 0xE0: cmpv(cpu.x, immx(), X8); break;  case 0xE4: cmpv(cpu.x, rdx(ea_dp()), X8); break;  case 0xEC: cmpv(cpu.x, rdx(ea_abs()), X8); break;

    case 0xE8: setx(cpu.x + 1); break;  case 0xCA: setx(cpu.x - 1); break;
    case 0xC8: sety(cpu.y + 1); break;  case 0x88: sety(cpu.y - 1); break;

    case 0x10: branch(!(cpu.p & FN)); break;  case 0x30: branch(cpu.p & FN); break;
    case 0x50: branch(!(cpu.p & FV)); break;  case 0x70: branch(cpu.p & FV); break;
    case 0x90: branch(!(cpu.p & FC)); break;  case 0xB0: branch(cpu.p & FC); break;
    case 0xD0: branch(!(cpu.p & FZ)); break;  case 0xF0: branch(cpu.p & FZ); break;
    case 0x80: branch(1); break;
    case 0x82: { int16_t o = (int16_t)fetch16(); cpu.pc += o; } break;

    case 0x4C: cpu.pc = fetch16(); break;
    case 0x5C: { uint32_t t = fetch24(); cpu.pc = t; cpu.pbr = t >> 16; } break;
    case 0x6C: { uint16_t p = fetch16(); cpu.pc = rd16(WRAP0 | p); } break;
    case 0x7C: { uint16_t p = fetch16() + cpu.x; cpu.pc = rd16(((uint32_t)cpu.pbr << 16) | p); } break;
    case 0xDC: { uint16_t p = fetch16(); uint32_t t = rd16(WRAP0 | p) | ((uint32_t)rd8(WRAP0 | ((p + 2) & 0xFFFF)) << 16); cpu.pc = t; cpu.pbr = t >> 16; } break;
    case 0x20: { uint16_t t = fetch16(); push16(cpu.pc - 1); cpu.pc = t; } break;
    case 0x22: { uint32_t t = fetch24(); push8(cpu.pbr); push16(cpu.pc - 1); cpu.pc = t; cpu.pbr = t >> 16; } break;
    case 0xFC: { uint16_t p = fetch16(); push16(cpu.pc - 1); p += cpu.x; cpu.pc = rd16(((uint32_t)cpu.pbr << 16) | p); } break;
    case 0x60: cpu.pc = pop16() + 1; break;
    case 0x6B: cpu.pc = pop16() + 1; cpu.pbr = pop8(); break;
    case 0x40: setp(pop8()); cpu.pc = pop16(); if (!cpu.e) cpu.pbr = pop8(); break;

    case 0x18: cpu.p &= ~FC; break;  case 0x38: cpu.p |= FC; break;
    case 0x58: cpu.p &= ~FI; break;  case 0x78: cpu.p |= FI; break;
    case 0xB8: cpu.p &= ~FV; break;
    case 0xD8: cpu.p &= ~FD; break;  case 0xF8: cpu.p |= FD; break;
    case 0xC2: setp(cpu.p & ~fetch8()); break;
    case 0xE2: setp(cpu.p | fetch8()); break;
    case 0xFB: { int c = cpu.p & FC; setc(cpu.e); cpu.e = c;
                 if (cpu.e) { cpu.p |= FM | FX; cpu.x &= 0xFF; cpu.y &= 0xFF; cpu.s = 0x0100 | (cpu.s & 0xFF); } } break;
    case 0xEB: cpu.a = (cpu.a >> 8) | (cpu.a << 8); setnz8(cpu.a); break;

    case 0x48: if (M8) push8(cpu.a); else push16(cpu.a); break;
    case 0x68: if (M8) seta(pop8()); else seta(pop16()); break;
    case 0xDA: if (X8) push8(cpu.x); else push16(cpu.x); break;
    case 0xFA: if (X8) setx(pop8()); else setx(pop16()); break;
    case 0x5A: if (X8) push8(cpu.y); else push16(cpu.y); break;
    case 0x7A: if (X8) sety(pop8()); else sety(pop16()); break;
    case 0x08: push8(cpu.p); break;
    case 0x28: setp(pop8()); break;
    case 0x8B: push8(cpu.dbr); break;
    case 0xAB: cpu.dbr = pop8(); setnz8(cpu.dbr); break;
    case 0x0B: push16(cpu.d); break;
    case 0x2B: cpu.d = pop16(); setnz16(cpu.d); break;
    case 0x4B: push8(cpu.pbr); break;
    case 0xF4: push16(fetch16()); break;
    case 0xD4: push16(rd16(ea_dp())); break;
    case 0x62: { int16_t o = (int16_t)fetch16(); push16(cpu.pc + o); } break;

    case 0xAA: setx(cpu.a); break;  case 0xA8: sety(cpu.a); break;
    case 0x8A: seta(cpu.x); break;  case 0x98: seta(cpu.y); break;
    case 0x9B: sety(cpu.x); break;  case 0xBB: setx(cpu.y); break;
    case 0xBA: setx(cpu.s); break;
    case 0x9A: cpu.s = cpu.e ? (0x0100 | (cpu.x & 0xFF)) : cpu.x; break;
    case 0x1B: cpu.s = cpu.e ? (0x0100 | (cpu.a & 0xFF)) : cpu.a; break;
    case 0x3B: cpu.a = cpu.s; setnz16(cpu.a); break;
    case 0x5B: cpu.d = cpu.a; setnz16(cpu.d); break;
    case 0x7B: cpu.a = cpu.d; setnz16(cpu.a); break;

    case 0x54: case 0x44: {                                                   /* MVN / MVP */
        uint8_t db = fetch8(), sb = fetch8();
        cpu.dbr = db;
        for (;;) {
            wr(((uint32_t)db << 16) | cpu.y, rd(((uint32_t)sb << 16) | cpu.x));
            if (op == 0x54) { cpu.x++; cpu.y++; } else { cpu.x--; cpu.y--; }
            if (X8) { cpu.x &= 0xFF; cpu.y &= 0xFF; }
            if (cpu.a-- == 0) break;
        }
    } break;

    default: halt("unimplemented opcode", op);
    }
}
