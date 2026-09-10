/* noVa64 · docsV3 — markdown → HTML for the synthesis document.
 *
 * A small, dependency-free renderer for the dialect described in README.md.
 * It is deliberately not a general markdown engine: it knows exactly the
 * constructs the sheets use, and each one maps onto a class the stylesheet
 * already defines (.lead, ol.steps, .note, .test, .chip, .pad, .hito,
 * table.simple, figure).
 */
(function (global) {
  'use strict';

  // ── inline ────────────────────────────────────────────────────────────

  function escapeCode(s) {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  /* End of a `((…))` note: the first `))` at paren balance zero, so a note
     that itself ends in a parenthesis — "(→ Q23)" — does not close early. */
  function noteEnd(s, from) {
    var depth = 0;
    for (var i = from; i < s.length; i++) {
      if (s[i] === ')' && s[i + 1] === ')' && depth === 0) return i;
      if (s[i] === '(') depth++;
      else if (s[i] === ')') depth--;
    }
    return -1;
  }

  function inline(s) {
    var out = '', i = 0;
    while (i < s.length) {
      var c = s[i], j;

      if (c === '`') {                                    // `code`
        j = s.indexOf('`', i + 1);
        if (j > 0) { out += '<code>' + escapeCode(s.slice(i + 1, j)) + '</code>'; i = j + 1; continue; }
      }
      if (c === '(' && s[i + 1] === '(') {                // ((aside))
        j = noteEnd(s, i + 2);
        if (j > 0) { out += '<span class="note">' + inline(s.slice(i + 2, j)) + '</span>'; i = j + 2; continue; }
      }
      if (c === '[' && s[i + 1] === '[') {                // [[chip]] · [[!gold chip]]
        j = s.indexOf(']]', i + 2);
        if (j > 0) {
          var chip = s.slice(i + 2, j), gold = chip[0] === '!';
          out += '<span class="chip' + (gold ? ' g' : '') + '">' + inline(gold ? chip.slice(1) : chip) + '</span>';
          i = j + 2; continue;
        }
      }
      if (c === '*' && s[i + 1] === '*') {                // **bold**
        j = s.indexOf('**', i + 2);
        if (j > 0) { out += '<b>' + inline(s.slice(i + 2, j)) + '</b>'; i = j + 2; continue; }
      }
      if (c === '*') {                                    // *italic*
        j = s.indexOf('*', i + 1);
        if (j > 0) { out += '<i>' + inline(s.slice(i + 1, j)) + '</i>'; i = j + 1; continue; }
      }
      if (c === '[') {                                    // [text](target)
        var m = /^\[([^\]]*)\]\(([^)\s]*)\)/.exec(s.slice(i));
        if (m) { out += '<a href="' + global.NovaLink(m[2]) + '">' + inline(m[1]) + '</a>'; i += m[0].length; continue; }
      }
      out += c;                                           // literal, raw HTML included
      i++;
    }
    return out;
  }

  // ── block scanner ─────────────────────────────────────────────────────

  var PAD = { '[ ]': '', '[x]': ' done', '[~]': ' half', '[?]': ' optl' };
  var ID = /^(?:[A-Z][A-Za-z0-9.]{0,7}|\+)$/;

  /* A fenced code block: ``` or ~~~ with an optional language tag, through the
     next line that is a bare fence of the same character. The content is
     verbatim — no inline pass, no trailer scanning — and the fence's own
     indentation is stripped, so a block hanging off an item reads the same as
     one at the margin.

     The `~~~` form exists for one case: a listing that itself contains a line
     of backticks, which is what a script that extracts fenced blocks out of
     this document looks like ([EM0](sec_ai_em0)). */
  var FENCE = { '`': /^\s*```+\s*$/, '~': /^\s*~~~+\s*$/ };

  function fence(lines, k) {
    var open = /^\s*(```+|~~~+)[ \t]*([A-Za-z0-9_+#-]*)[ \t]*$/.exec(lines[k] || '');
    if (!open) return null;
    var close = FENCE[open[1][0]];
    var body = [], i = k + 1;
    while (i < lines.length && !close.test(lines[i])) { body.push(lines[i]); i++; }

    var strip = Infinity;
    body.forEach(function (l) {
      if (l.trim()) strip = Math.min(strip, /^[ \t]*/.exec(l)[0].length);
    });
    if (!isFinite(strip)) strip = 0;
    body = body.map(function (l) { return l.slice(strip); });
    while (body.length && !body[0].trim()) body.shift();
    while (body.length && !body[body.length - 1].trim()) body.pop();

    return {
      lang: open[2] || '',
      code: body.join('\n'),
      next: i < lines.length ? i + 1 : i
    };
  }

  /* Highlighting happens here rather than in the browser, so that the printable
     edition carries the same markup — hl.js runs under Node too. Absent, the
     block is merely escaped, which is also what an untagged fence gets. */
  function codeHtml(f) {
    var body = (f.lang && global.NovaHighlight)
      ? global.NovaHighlight.highlight(f.code, f.lang)
      : escapeCode(f.code);
    return '<pre class="code"' + (f.lang ? ' data-lang="' + f.lang + '"' : '') +
           '><code>' + body + '</code></pre>';
  }

  /* Lines belonging to the block just parsed: NOTE:, TEST:, a fenced code
     block, and any other indented line, which continues the block as a further
     paragraph at the same weight — the way A1.1 carries its second and third
     paragraphs. The fence is tested before the indent rule, or an indented
     fence would be read as prose. */
  function trailers(lines, k) {
    var conts = [], notes = [], codes = [], test = null;
    while (k < lines.length) {
      var raw = lines[k], t = raw.trim(), f = fence(lines, k);
      if (f) { codes.push(f); k = f.next; }
      else if (/^NOTE:/.test(t)) { notes.push(t.slice(5).trim()); k++; }
      else if (/^TEST:/.test(t)) { test = t.slice(5).trim(); k++; }
      else if (/^\s{2,}\S/.test(raw)) { conts.push(t); k++; }
      else break;
    }
    return { conts: conts, notes: notes, codes: codes, test: test, next: k };
  }

  function tail(tr) {
    var out = tr.conts.map(function (c) { return '<span class="cont">' + inline(c) + '</span>'; }).join('');
    out += tr.codes.map(codeHtml).join('');
    out += tr.notes.map(function (n) { return ' <span class="note">' + inline(n) + '</span>'; }).join('');
    if (tr.test) out += '<span class="test">TEST ▸ ' + inline(tr.test) + '</span>';
    return out;
  }

  function step(line) {
    var body = line.replace(/^-\s+/, ''), pad = null;
    var m = /^(\[[ x~?]\])\s+/.exec(body);
    if (m) { pad = PAD[m[1]]; body = body.slice(m[0].length); }

    var cut = body.indexOf(' — '), id = null;
    if (cut > 0 && ID.test(body.slice(0, cut))) {
      id = body.slice(0, cut);
      body = body.slice(cut + 3);
    }
    /* The anchor is the id with its dots dropped — A.7 → a7, E0.1 → e01 —
       which is what the cross-references in the sheets point at. */
    var anchor = id && /^[A-Za-z]/.test(id) ? id.toLowerCase().replace(/\./g, '') : '';
    return { pad: pad, id: id, text: body, anchor: anchor };
  }

  /* ── the kit ──────────────────────────────────────────────────────────

     A listing preceded by `<!-- file: path -->` is not an illustration: it is
     one of the files the document carries, and the marker is what the
     extractor in sheet EM0 reads to write it out. The renderer reads the same
     marker, and the difference it draws from it is what keeps the printed
     edition a book rather than a program listing.

     A file the reader is told never to retype is worth nothing on paper, so
     past LONG lines the body is printed as a stub: the path, the size, and
     where the command that materialises it lives. On screen the same blocks
     fold, so the sheet stays a document one can scroll.

     The size is bytes and lines rather than a hash, because it has to be
     checkable at the other end and `wc -lc` is on every machine this document
     is read on. */
  var LONG = 60;

  function kitCap(file, code, long) {
    var bytes = 0;
    for (var i = 0; i < code.length; i++) {
      var c = code.charCodeAt(i);
      bytes += c < 0x80 ? 1 : c < 0x800 ? 2 : (c & 0xF800) === 0xD800 ? 2 : 3;
    }
    bytes += 1;                                  // the newline the extractor writes last
    var lines = code.split('\n').length;
    return '<b>' + escapeCode(file) + '</b>' +
           '<span class="kitsz">' + lines + ' lines · ' + bytes + ' bytes</span>' +
           (long ? '<span class="kitnote">not printed — extract it with the command in section 4,' +
                   ' and check it with <code>wc -lc</code></span>' : '');
  }

  /* `<!-- file: path -->` on its own line, and the fence under it. */
  function kit(lines, k) {
    var m = /^<!--\s*file:\s*(\S+)\s*-->$/.exec((lines[k] || '').trim());
    if (!m) return null;
    var f = fence(lines, k + 1);
    if (!f) return null;
    var long = f.code.split('\n').length > LONG;
    var html = '<div class="kit' + (long ? ' long' : '') + '" data-file="' + escapeCode(m[1]) + '">';
    html += long
      ? '<details><summary class="kitcap">' + kitCap(m[1], f.code, true) + '</summary>' +
        codeHtml(f) + '</details>'
      : '<div class="kitcap">' + kitCap(m[1], f.code, false) + '</div>' + codeHtml(f);
    return { html: html + '</div>', next: f.next };
  }

  /* Parses a whole document into { title, aim, html, tags, hasIndex }. */
  function parse(src) {
    var lines = src.replace(/\r/g, '').split('\n');
    var doc = { title: '', aim: '', tags: null, hasIndex: false, html: '' };
    var out = [], i = 0;

    if (/^#\s+/.test(lines[0] || '')) { doc.title = lines[i].replace(/^#\s+/, '').trim(); i++; }
    if (/^>\s+/.test(lines[i] || '')) { doc.aim = lines[i].replace(/^>\s+/, '').trim(); i++; }

    while (i < lines.length) {
      var line = lines[i], t = line.trim();

      if (!t) { i++; continue; }

      var kt = kit(lines, i);                                       // a file of the kit
      if (kt) { out.push(kt.html); i = kt.next; continue; }

      var fen = fence(lines, i);                                    // ```code```
      if (fen) { out.push(codeHtml(fen)); i = fen.next; continue; }

      if (t === 'INDEX') {                                          // sheet-index table
        doc.hasIndex = true; out.push('<div data-index></div>'); i++; continue;
      }

      if (t === 'TAGS:') {                                          // masthead tags
        doc.tags = []; i++;
        while (i < lines.length && /^-\s+/.test(lines[i].trim())) {
          var tag = lines[i].trim().replace(/^-\s+/, '');
          var cm = /^\[([mg])\]\s*/.exec(tag);
          doc.tags.push({ cls: cm ? cm[1] : '', text: cm ? tag.slice(cm[0].length) : tag });
          i++;
        }
        continue;
      }

      if (/^(-{3,}|\*{3,}|_{3,})$/.test(t)) { i++; continue; }      // rule — spacing already says it

      if (/^!!!\s+/.test(t)) {                                      // milestone banner
        out.push('<div class="hito">■ ' + inline(t.replace(/^!!!\s+/, '')) + '</div>');
        i++; continue;
      }

      var hd = /^(###|##)\s+/.exec(t);                              // sub-heading, two levels
      if (hd) {
        var h = t.slice(hd[0].length), dash = h.indexOf(' — ');
        var bold = dash > 0 ? h.slice(0, dash) : h;
        var rest = dash > 0 ? ' — ' + h.slice(dash + 3) : '';
        var trh = trailers(lines, i + 1);
        out.push('<p class="lead sub' + (hd[1] === '###' ? ' min' : '') + '"><b>' +
                 inline(bold) + '</b>' + inline(rest) + tail(trh) + '</p>');
        i = trh.next; continue;
      }

      var fig = /^!\[(.*)\]\(([^)]+)\)$/.exec(t);                   // figure
      if (fig) {
        var f = '<figure data-svg="' + fig[2] + '"><div class="svg-slot"></div>' +
                '<figcaption>' + inline(fig[1]) + '</figcaption>';
        i++;
        if (i < lines.length && /^LEGEND:/.test(lines[i].trim())) {
          f += '<div class="legend">' + inline(lines[i].trim().slice(7).trim()) + '</div>';
          i++;
        }
        out.push(f + '</figure>'); continue;
      }

      if (t[0] === '|') {                                           // table
        var rows = [];
        while (i < lines.length && lines[i].trim()[0] === '|') {
          var cells = lines[i].trim().replace(/^\|/, '').replace(/\|$/, '').split('|');
          if (!/^[\s|:-]+$/.test(lines[i])) rows.push(cells.map(function (c) { return c.trim(); }));
          i++;
        }
        var html = '<table class="simple"><thead><tr>' +
          rows[0].map(function (c) { return '<th>' + inline(c) + '</th>'; }).join('') +
          '</tr></thead><tbody>';
        for (var r = 1; r < rows.length; r++) {
          html += '<tr>' + rows[r].map(function (c) { return '<td>' + inline(c) + '</td>'; }).join('') + '</tr>';
        }
        out.push(html + '</tbody></table>'); continue;
      }

      /* A numbered procedure — `1.` through `18.` — which is a runbook's
         backbone and reads as the same row as an item, its number where the
         item id would be. The number is not an anchor: cross-references point
         at ids, and a step's position is not one. */
      if (/^\d+\.\s+/.test(t)) {
        var nums = '';
        while (i < lines.length && /^\d+\.\s+/.test(lines[i].trim())) {
          var nm = /^(\d+)\.\s+/.exec(lines[i].trim());
          var ntr = trailers(lines, i + 1);
          nums += '<li><span class="id">' + nm[1] + '</span><div class="itx">' +
                  inline(lines[i].trim().slice(nm[0].length)) + tail(ntr) + '</div></li>';
          i = ntr.next;
        }
        out.push('<ol class="steps">' + nums + '</ol>'); continue;
      }

      if (/^-\s+/.test(t)) {                                        // steps list
        var items = '';
        while (i < lines.length && /^-\s+/.test(lines[i].trim())) {
          var s = step(lines[i].trim());
          var tr = trailers(lines, i + 1);
          items += '<li' + (s.anchor ? ' id="' + s.anchor + '"' : '') + '>' +
                   (s.pad !== null ? '<span class="pad' + s.pad + '"></span>' : '') +
                   (s.id ? '<span class="id">' + s.id + '</span>' : '') +
                   '<div class="itx">' + inline(s.text) + tail(tr) + '</div></li>';
          i = tr.next;
        }
        out.push('<ol class="steps">' + items + '</ol>'); continue;
      }

      var para = [];                                                // paragraph
      while (i < lines.length && lines[i].trim() && !/^(NOTE:|TEST:)/.test(lines[i].trim()) &&
             !/^\s*(```|~~~)/.test(lines[i]) &&
             !(para.length && /^\s{2,}\S/.test(lines[i]))) {
        para.push(lines[i].trim()); i++;
      }
      var trp = trailers(lines, i);
      out.push('<p class="lead">' + inline(para.join(' ')) + tail(trp) + '</p>');
      i = trp.next;
    }

    doc.html = out.join('\n');
    return doc;
  }

  /* Link targets are routes, not files — app.js owns the mapping. */
  if (!global.NovaLink) global.NovaLink = function (t) { return '#/' + t; };

  global.NovaMarkdown = { parse: parse, inline: inline };
})(window);
