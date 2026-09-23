/* noVa64 · docsV3 — the two things both editions need: figures and failure.
 * Used by index.html (paged, one sheet at a time) and full.html (everything
 * at once, for printing). */
(function (global) {
  'use strict';

  var svgCache = {};

  /* Figures live as their own .svg files so the markdown stays readable;
     they are inlined rather than <img>-ed so the stylesheet — including the
     print rules that recolour every trace — still reaches them. */
  function inlineFigures(root) {
    Array.prototype.forEach.call(root.querySelectorAll('figure[data-svg]'), function (fig) {
      var src = fig.getAttribute('data-svg'), slot = fig.querySelector('.svg-slot');
      if (!slot) return;
      var put = function (svg) { if (slot.parentNode) slot.outerHTML = svg; };
      if (svgCache[src]) { put(svgCache[src]); return; }
      fetch(src).then(function (res) {
        if (!res.ok) throw new Error(res.status + ' ' + src);
        return res.text();
      }).then(function (txt) {
        svgCache[src] = txt.replace(/<\?xml[^>]*\?>\s*/, '');
        put(svgCache[src]);
      }).catch(function (e) {
        slot.className = 'status err';
        slot.textContent = 'Figure unavailable: ' + e.message;
      });
    });
  }

  /* The one failure worth explaining: fetch() refuses to read a sibling file
     over file://, which is how this looks when the folder is opened by hand. */
  function fail(el, err, what) {
    el.className = 'status err';
    el.innerHTML = '<b>Could not read ' + what + '</b> — ' + err.message +
      (location.protocol === 'file:'
        ? '<br><br>The pages are read with <code>fetch()</code>, which a browser refuses to do over ' +
          '<code>file://</code>. Serve the folder instead:<br><br>' +
          '<code>python3 -m http.server -d docs/docsV3</code><br><br>then open ' +
          '<code>http://localhost:8000/</code>.'
        : '');
  }

  /* content/<file>.md, parsed. */
  function loadSheet(file) {
    return fetch('content/' + file + '.md').then(function (res) {
      if (!res.ok) throw new Error('HTTP ' + res.status + ' on content/' + file + '.md');
      return res.text();
    }).then(function (txt) { return global.NovaMarkdown.parse(txt); });
  }

  /* The document has three levels — part, area, sheet — and only two of them
     are stored per sheet. This resolves them into one reading order and hands
     both editions the same answer.

     `parts` is a registry, not a run: it fixes the order and the display names,
     and it lets a part that holds no sheets still appear, which is the whole
     point while sheets are being migrated into their parts one at a time. The
     sheet array's order survives as the order *within* a part, exactly as it
     was the order within an area before.

     A sheet's position — the number under its letter — is therefore derived
     rather than stored: changing a sheet's `part` is a one-field edit and the
     numbering follows. A sheet naming a part that does not exist is not
     dropped; it lands in a trailing group that says so. */
  function prepare(M) {
    var parts = M.parts || [{ id: '', num: '', name: '' }];
    var known = {};
    parts.forEach(function (p) { known[p.id] = p; });

    var groups = parts.map(function (p) { return { part: p, sheets: [] }; });
    var orphan = null;
    M.sheets.forEach(function (s) {
      var g;
      if (known[s.part]) {
        g = groups[parts.indexOf(known[s.part])];
      } else {
        if (!orphan) {
          orphan = { part: { id: '', num: '·', name: 'Unassigned',
                             note: 'These sheets name a part that is not in the registry.' },
                     sheets: [] };
          groups.push(orphan);
        }
        g = orphan;
      }
      s.partName = g.part.name;
      s.partNum = g.part.num;
      g.sheets.push(s);
    });

    /* areas are still runs, but now runs inside a part */
    groups.forEach(function (g) {
      g.areas = [];
      var area = null;
      g.sheets.forEach(function (s) {
        if (!g.areas.length || s.area !== area) { area = s.area; g.areas.push({ name: area, sheets: [] }); }
        g.areas[g.areas.length - 1].sheets.push(s);
      });
    });

    M.sheets = groups.reduce(function (acc, g) { return acc.concat(g.sheets); }, []);
    M.sheets.forEach(function (s, i) { s.num = (i + 1 < 10 ? '0' : '') + (i + 1); });
    M.groups = groups;

    /* The masthead's reference line used to spell the sheet letters out, which
       meant every split and every new sheet left it a little further from the
       truth. It is now a count, and the count is taken from what was just
       resolved: {parts} the registry, {areas} the named runs inside it, and
       {sheets} the reading order itself. */
    var areas = 0;
    groups.forEach(function (g) {
      g.areas.forEach(function (a) { if (a.name) areas++; });
    });
    var counts = { parts: groups.length, areas: areas, sheets: M.sheets.length };

    /* `{figures}` and `{tables}` answer from numbering.json, which prepare() is
       deliberately not allowed to require — tools/index.js runs on the manifest
       alone. Absent, the token is left standing rather than resolved to a lie. */
    var N = global.NovaNumbers;
    if (N) {
      counts.figures = Object.keys(N.figures || {}).length;
      counts.tables = Object.keys(N.tables || {}).length;
    }
    if (M.footer && M.footer.referenceIndex) {
      M.footer.referenceIndex = M.footer.referenceIndex.replace(
        /\{(parts|areas|sheets|figures|tables)\}/g,
        function (m, k) { return counts[k] === undefined ? m : counts[k]; });
    }
    return M;
  }

  /* The title block's revision cell. One revision to a line, because the run
     of them read as a single sentence once it passed a dozen areas, and the
     line that closes it carries the document's own version and date. Both
     editions build the same cell, so it is built here. */
  function revCell(footer) {
    if (!footer) return '';
    var revs = footer.revs || [];
    return '<div class="gold">' +
           revs.map(function (r) { return '<div>' + r + '</div>'; }).join('') +
           '<div class="ver">' + footer.version + ' \u00b7 ' + footer.date + '</div></div>';
  }

  /* ── figure and table numbers ───────────────────────────────────────────
     `numbering.json` is the one derived thing this document stores, and it is
     stored because it cannot be derived from one sheet: a figure's number comes
     from the reading order of all of them. `tools/numbers.js` writes it, it is
     committed like any other served file, and md.js reads it through the global
     set here — before the first parse, which is why this lives in loadManifest
     and not in prepare(). tools/index.js and tools/numbers.js call prepare()
     directly, and neither can depend on the artefact one of them generates.

     A missing file is not an error: the pages must still render from a checkout
     where the markdown landed before the artefact was regenerated. md.js then
     prints `?` for every number, which the renderer states make loud. */
  function loadNumbering() {
    return fetch('numbering.json').then(function (res) {
      return res.ok ? res.json() : null;
    }, function () { return null; });
  }

  /* One row per numbered figure or table, in numeric order, reusing the sheet
     index's own table so the print rules that put a page number on every row
     reach it unchanged. **Exactly one <a> per row**: `target-counter` fires per
     link, and a second one would print the page number twice. */
  function marksTable(kind, M) {
    var N = global.NovaNumbers;
    var word = kind === 'figures' ? 'FIGURES' : 'TABLES';
    if (!N) {
      return '<div class="status err"><b>Index of ' + kind + ' unavailable</b> — ' +
             '<code>numbering.json</code> is missing or unreadable. Run ' +
             '<code>node tools/numbers.js</code> and commit it.</div>';
    }
    var letter = {};
    (M.sheets || []).forEach(function (s) { letter[s.file] = s.letter; });

    var rows = Object.keys(N[kind] || {}).map(function (id) {
      var e = N[kind][id];
      return { n: e.n, letter: letter[e.sheet] || '·', desc: e.desc,
               href: global.NovaLink(e.sheet + '#' + e.anchor) };
    }).sort(function (a, b) { return a.n - b.n; });

    var h = '<nav class="idx lst" aria-label="Index of ' + kind + '">' +
            '<div class="cap">INDEX OF ' + word + '</div><table><tbody>';
    if (!rows.length) {
      h += '<tr><td colspan="3"><span class="pnote">none carry a caption yet</span></td></tr>';
    }
    rows.forEach(function (r) {
      h += '<tr><td class="no">' + r.n + '</td><td class="de">' + r.letter + '</td>' +
           '<td><a href="' + r.href + '">' + global.NovaMarkdown.inline(r.desc) + '</a></td></tr>';
    });
    return h + '</tbody></table></nav>';
  }

  /* Both editions substitute the same way, so they cannot disagree about it.
     The replacement is a **function**: a string one would interpret `$&` and
     `$'`, and this document is full of `$FF`. */
  function expandLists(html, M) {
    return html.replace(/<div data-list="(figures|tables)"><\/div>/g, function (_, kind) {
      return marksTable(kind, M);
    });
  }

  function loadManifest() {
    return Promise.all([
      fetch('manifest.json').then(function (res) {
        if (!res.ok) throw new Error('HTTP ' + res.status + ' on manifest.json');
        return res.json();
      }),
      loadNumbering()
    ]).then(function (r) {
      global.NovaNumbers = r[1];
      return prepare(r[0]);
    });
  }

  global.NovaShell = { inlineFigures: inlineFigures, fail: fail, revCell: revCell,
                       loadSheet: loadSheet, loadManifest: loadManifest, prepare: prepare,
                       marksTable: marksTable, expandLists: expandLists };
})(window);
