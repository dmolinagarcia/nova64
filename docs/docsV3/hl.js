/* noVa64 · docsV3 — syntax highlighting for fenced code blocks.
 *
 * Dependency-free and deliberately shallow: one scanner driven by a per-language
 * table, not a parser. It knows comments, strings, numbers, keywords and types,
 * and nothing about grammar — which is all a listing in this document needs, and
 * all that can be done without carrying a real tokeniser into the PDF build.
 *
 * It runs in the browser and under Node, because tools/prerender.js builds the
 * printable edition by running the real renderer: highlighting has to happen at
 * render time, or the PDF would come out plain.
 *
 * Five token classes, because the stylesheet has five colours and no more:
 *
 *   .c  comment      dimmed, italic
 *   .s  string       gold
 *   .n  number       gold
 *   .k  keyword      mint, bold
 *   .t  type         mint
 *   .d  directive    mint, dimmed — #include, .segment, $(VAR)
 *
 * An unknown or absent language is escaped and left alone, which is the right
 * answer for the register bit-layouts and vector tables in sheet EM7.
 */
(function (global) {
  'use strict';

  function esc(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  var C_KW = ('auto break case const continue default do else enum extern for goto if ' +
    'inline register restrict return sizeof static struct switch typedef union volatile while ' +
    '_Bool asm').split(' ');
  var C_TY = ('bool char double float int long short signed unsigned void size_t ' +
    'int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t ' +
    'va_t pa_t cycle_t access_t mem_t instr_t system_t cpu_t params_t cache_t mmu_t ' +
    'neon_t tlb_ent_t mbx_event_t abort_cause_t testcase_t true false NULL').split(' ');

  var JS_KW = ('async await break case catch class const continue default delete do else ' +
    'export extends finally for function if import in instanceof let new of return super ' +
    'switch this throw try typeof var void while yield').split(' ');
  var JS_TY = 'true false null undefined NaN Infinity'.split(' ');

  var PY_KW = ('and as assert break class continue def del elif else except finally for from ' +
    'global if import in is lambda nonlocal not or pass raise return try while with yield').split(' ');
  var PY_TY = 'True False None self bytes bytearray int str len open range print'.split(' ');

  var SH_KW = ('case do done elif else esac fi for function if in select then until while ' +
    'break continue return export local readonly shift source').split(' ');
  var SH_TY = 'echo cd cp mv rm mkdir cat grep sed awk curl tar make clang node apt sudo set'.split(' ');

  var MK_KW = 'ifeq ifneq ifdef ifndef else endif include export define endef'.split(' ');

  /* 65816 in ca65 syntax. Mnemonics are the keywords; the assembler's own
     directives are directives and carry their leading dot. */
  var ASM_KW = ('adc and asl bcc bcs beq bit bmi bne bpl bra brk brl bvc bvs clc cld cli clv ' +
    'cmp cop cpx cpy dec dex dey eor inc inx iny jml jmp jsl jsr lda ldx ldy lsr mvn mvp nop ' +
    'ora pea pei per pha phb phd phk php phx phy pla plb pld plp plx ply rep rol ror rti rtl ' +
    'rts sbc sec sed sei sep sta stp stx sty stz tax tay tcd tcs tdc trb tsb tsc tsx txa txs ' +
    'txy tya tyx wai wdm xba xce').split(' ');

  var LANG = {
    c:      { line: '//', block: ['/*', '*/'], str: ['"', "'"], hash: 'd', kw: C_KW,  ty: C_TY },
    js:     { line: '//', block: ['/*', '*/'], str: ['"', "'", '`'],        kw: JS_KW, ty: JS_TY },
    json:   {                                  str: ['"'],                  kw: [],    ty: JS_TY },
    python: { line: '#',                       str: ['"', "'"],             kw: PY_KW, ty: PY_TY },
    sh:     { line: '#',                       str: ['"', "'"], dollar: true, kw: SH_KW, ty: SH_TY },
    make:   { line: '#',                       str: ['"', "'"], dollar: true, kw: MK_KW, ty: [] },
    asm:    { line: ';',                       str: ['"', "'"], dollar: true, kw: ASM_KW, ty: [],
              dot: true, nocase: true }
  };

  var ALIAS = { cpp: 'c', h: 'c', javascript: 'js', py: 'python', bash: 'sh', shell: 'sh',
                makefile: 'make', asm65: 'asm', ca65: 'asm' };

  function span(cls, text) { return '<span class="' + cls + '">' + esc(text) + '</span>'; }

  function isWordChar(ch) { return /[A-Za-z0-9_]/.test(ch); }

  /* Consumes a quoted run, honouring backslash escapes, and returns its end
     index — the character after the closing quote, or the end of the line for
     an unterminated one, which is what a `'` inside prose would otherwise
     swallow the rest of the listing over. */
  function strEnd(src, i, quote) {
    var j = i + 1;
    while (j < src.length) {
      if (src[j] === '\\') { j += 2; continue; }
      if (src[j] === '\n') return j;           /* unterminated: stop at the line */
      if (src[j] === quote) return j + 1;
      j++;
    }
    return src.length;
  }

  function highlight(code, lang) {
    var g = LANG[ALIAS[lang] || lang];
    if (!g) return esc(code);

    var kw = {}, ty = {};
    g.kw.forEach(function (w) { kw[w] = 1; });
    (g.ty || []).forEach(function (w) { ty[w] = 1; });

    var out = '', i = 0, n = code.length;

    while (i < n) {
      var ch = code[i], j;

      /* block comment */
      if (g.block && code.startsWith(g.block[0], i)) {
        j = code.indexOf(g.block[1], i + g.block[0].length);
        j = j < 0 ? n : j + g.block[1].length;
        out += span('c', code.slice(i, j)); i = j; continue;
      }

      /* line comment — in C `#` is a directive, not a comment, so `hash`
         and `line` are separate settings rather than one */
      if (g.line && code.startsWith(g.line, i)) {
        j = code.indexOf('\n', i); j = j < 0 ? n : j;
        out += span('c', code.slice(i, j)); i = j; continue;
      }

      /* string or character literal */
      if (g.str && g.str.indexOf(ch) >= 0) {
        j = strEnd(code, i, ch);
        out += span('s', code.slice(i, j)); i = j; continue;
      }

      /* C preprocessor: the directive word only, so the header that follows
         keeps its own colouring */
      if (g.hash === 'd' && ch === '#' && /(^|\n)[ \t]*$/.test(code.slice(0, i))) {
        j = i + 1;
        while (j < n && /[A-Za-z]/.test(code[j])) j++;
        out += span('d', code.slice(i, j)); i = j; continue;
      }

      /* assembler directive, .p816 and friends */
      if (g.dot && ch === '.' && /[A-Za-z]/.test(code[i + 1] || '') && !isWordChar(code[i - 1] || ' ')) {
        j = i + 1;
        while (j < n && isWordChar(code[j])) j++;
        out += span('d', code.slice(i, j)); i = j; continue;
      }

      /* $hex in assembler · $(VAR) and $VAR in make and sh */
      if (g.dollar && ch === '$') {
        if (code[i + 1] === '(' || code[i + 1] === '{') {
          var close = code[i + 1] === '(' ? ')' : '}';
          j = code.indexOf(close, i + 2); j = j < 0 ? n : j + 1;
          out += span('d', code.slice(i, j)); i = j; continue;
        }
        j = i + 1;
        if (/[0-9A-Fa-f]/.test(code[j] || '')) {              /* $FF, a number */
          while (j < n && /[0-9A-Fa-f]/.test(code[j])) j++;
          out += span('n', code.slice(i, j)); i = j; continue;
        }
        while (j < n && isWordChar(code[j])) j++;             /* $VAR, a name  */
        if (j > i + 1) { out += span('d', code.slice(i, j)); i = j; continue; }
      }

      /* number, including 0x and a trailing type suffix */
      if (/[0-9]/.test(ch) && !isWordChar(code[i - 1] || ' ')) {
        j = i;
        if (ch === '0' && /[xX]/.test(code[i + 1] || '')) {
          j = i + 2;
          while (j < n && /[0-9A-Fa-f]/.test(code[j])) j++;
        } else {
          while (j < n && /[0-9.]/.test(code[j])) j++;
        }
        while (j < n && /[uUlLfF]/.test(code[j])) j++;
        out += span('n', code.slice(i, j)); i = j; continue;
      }

      /* word — but not one hanging off a `-` or a `/`, which is how a command
         line spells a flag or a path. Without this, `--export-dynamic` picks up
         a shell keyword and `/usr/bin/make` picks up a command name. */
      if (isWordChar(ch) && !/[0-9]/.test(ch)) {
        j = i;
        while (j < n && isWordChar(code[j])) j++;
        var w = code.slice(i, j), key = g.nocase ? w.toLowerCase() : w;
        var attached = /[-\/]/.test(code[i - 1] || '');
        if (!attached && kw[key]) out += span('k', w);
        else if (!attached && ty[key]) out += span('t', w);
        else out += esc(w);
        i = j; continue;
      }

      out += esc(ch);
      i++;
    }
    return out;
  }

  global.NovaHighlight = { highlight: highlight, languages: LANG };
})(typeof window !== 'undefined' ? window : globalThis);
