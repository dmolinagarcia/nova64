/* noVa64 · docsV3 — shell and router.
 *
 * Everything around the prose — sidebar, masthead, sheet index, pager and
 * title block — is generated from manifest.json, so a sheet's .md file only
 * ever carries its own content. Sheets sit three levels deep: a **part** from
 * the manifest's `parts` registry, an **area** within it, then the sheet. The
 * registry fixes the order of the parts and lets an empty one still show;
 * the sheet array's order is the order within a part, and a run of sheets
 * sharing an `area` gets one heading. Positions are derived, never stored —
 * `NovaShell.prepare` resolves all of it before anything here runs.
 *
 * Routes are hashes: #/ is the index, #/sec_a a sheet, #/sec_a/a3 a sheet
 * scrolled to one of its items.
 */
(function () {
  'use strict';

  var M = null;                       // manifest.json
  var docCache = {};                  // route file -> promise of a parsed document

  // ── links and routes ──────────────────────────────────────────────────

  /* Markdown writes targets the way docsV2 wrote filenames: `sec_e`,
     `sec_q#q8`, `index`. Both forms become hash routes here. */
  window.NovaLink = function (target) {
    if (/^(https?:|mailto:|#)/.test(target)) return target;
    var parts = target.split('#');
    if (parts[0] === 'index' || parts[0] === '') return '#/' + (parts[1] ? '/' + parts[1] : '');
    return '#/' + parts[0] + (parts[1] ? '/' + parts[1] : '');
  };

  function route() {
    var h = location.hash.replace(/^#\/?/, '').split('/');
    var file = h[0] || 'index';
    if (file !== 'index' && !sheetOf(file)) file = 'index';
    return { file: file, anchor: h[1] || '', isIndex: file === 'index' };
  }

  function sheetOf(file) {
    for (var i = 0; i < M.sheets.length; i++) if (M.sheets[i].file === file) return M.sheets[i];
    return null;
  }

  function indexOfSheet(file) {
    for (var i = 0; i < M.sheets.length; i++) if (M.sheets[i].file === file) return i;
    return -1;
  }

  // ── shell pieces ──────────────────────────────────────────────────────

  function sidebar(r) {
    var h = '<div class="cap">' + M.title + ' · SHEETS</div><ol>' +
      '<li><a href="#/"' + (r.isIndex ? ' class="here"' : '') + '><span class="de">·</span>Sheet index</a></li>';
    M.groups.forEach(function (g) {
      h += '<li class="part"><span class="pn">' + g.part.num + '</span>' + g.part.name + '</li>';
      if (!g.sheets.length) { h += '<li class="none">no sheets yet</li>'; return; }
      g.areas.forEach(function (a) {
        if (a.name) h += '<li class="grp">' + a.name + '</li>';
        a.sheets.forEach(function (s) {
          h += '<li><a href="#/' + s.file + '"' + (s.file === r.file ? ' class="here"' : '') + '>' +
               '<span class="de">' + s.letter + '</span>' + s.nav +
               '<span class="fg">' + s.num + '</span></a></li>';
        });
      });
    });
    /* the printable edition sits at the end of the list, off the numbering */
    h += '<li><a href="full.html"><span class="de">≡</span>All sheets · one page' +
         '<span class="fg">print</span></a></li>';
    return h + '</ol>';
  }

  function masthead(r, doc) {
    if (r.isIndex) {
      var h = '<h1>' + M.title + '</h1><div class="sub">' + M.documentName + '</div>';
      if (doc.tags && doc.tags.length) {
        h += '<div class="meta">' + doc.tags.map(function (t) {
          return '<span class="tag' + (t.cls ? ' ' + t.cls : '') + '">' + t.text + '</span>';
        }).join('') + '</div>';
      }
      return h;
    }
    var s = sheetOf(r.file);
    return '<h1><a href="#/">' + M.title + '</a></h1>' +
           '<div class="sub">' + (s.partName ? s.partName + ' · ' : '') +
           (s.area ? s.area + ' · ' : '') +
           'Sheet ' + s.num + ' · ' + s.letter + ' — ' + doc.title + '</div>';
  }

  function indexTable() {
    var h = '<nav class="idx" aria-label="Sheet index"><div class="cap">SHEET INDEX</div><table><tbody>';
    M.groups.forEach(function (g) {
      h += partRow(g);
      g.areas.forEach(function (a) {
        if (a.name) h += '<tr class="grp"><td colspan="4">' + a.name + '</td></tr>';
        a.sheets.forEach(function (s) {
          h += '<tr><td class="no">' + s.num + '</td><td class="de">' + s.letter + '</td>' +
               '<td><a href="#/' + s.file + '">' + s.index + '</a></td>' +
               '<td class="fg">' + s.fig + '</td></tr>';
        });
      });
    });
    return h + '</tbody></table></nav>';
  }

  /* A part always shows, empty or not: while sheets are being migrated into
     their parts, the empty ones are the part of the picture worth seeing. */
  function partRow(g) {
    return '<tr class="part"><td class="no">' + g.part.num + '</td>' +
           '<td colspan="3">' + g.part.name +
           (g.part.note ? '<span class="pnote">' + g.part.note + '</span>' : '') +
           (g.sheets.length ? '' : '<span class="pnote">no sheets yet</span>') +
           '</td></tr>';
  }

  function content(r, doc) {
    var html = doc.html.replace('<div data-index></div>', doc.hasIndex ? indexTable() : '');
    if (r.isIndex) return html;
    var s = sheetOf(r.file);
    return '<section class="hoja" id="' + s.letter.toLowerCase() + '">' +
           '<div class="sechead"><span class="des">' + s.letter + '</span><h2>' + doc.title + '</h2>' +
           '<span class="aim">' + doc.aim + '</span>' +
           '<span class="sheet">SHEET ' + s.num + '</span></div>' + html + '</section>';
  }

  function pager(r) {
    var i = r.isIndex ? -1 : indexOfSheet(r.file);
    var prev = i === 0 ? { href: '#/', label: '← Sheet index' }
             : i > 0 ? { href: '#/' + M.sheets[i - 1].file,
                         label: '← ' + M.sheets[i - 1].letter + ' · ' + M.sheets[i - 1].nav } : null;
    var n = M.sheets[i + 1];
    var next = n ? { href: '#/' + n.file, label: n.letter + ' · ' + n.nav + ' →' } : null;
    if (!prev && !next) return '';
    var h = '';
    if (prev) h += '<div class="pv"><b>Previous</b><a href="' + prev.href + '">' + prev.label + '</a></div>';
    if (next) h += '<div class="nx"><b>Next</b><a href="' + next.href + '">' + next.label + '</a></div>';
    return h;
  }

  function titleBlock(r, doc) {
    var line = r.isIndex ? M.documentName.toUpperCase()
             : ('Sheet ' + sheetOf(r.file).num + ' · ' + sheetOf(r.file).letter + ' — ' + doc.title).toUpperCase();
    var f = M.footer;
    return '<div class="cj"><div class="big">' + M.title + '</div><div>' + line + '</div></div>' +
           '<div class="cj"><div>' + (r.isIndex ? f.referenceIndex : f.referenceSheet) + '</div>' +
           '<div>' + f.detail + '</div></div>' +
           '<div class="cj"><div>' + f.rules + '</div><div class="gold">' + f.rev + '</div></div>';
  }

  // ── rendering ─────────────────────────────────────────────────────────

  var el = {};
  var baseTitle = '';                 // the <title> index.html ships with

  function render() {
    var r = route();
    load(r.file).then(function (doc) {
      el.side.innerHTML = sidebar(r);
      el.mast.innerHTML = masthead(r, doc);
      el.body.className = '';
      el.body.innerHTML = content(r, doc);
      el.pager.innerHTML = pager(r);
      el.pager.style.display = el.pager.innerHTML ? '' : 'none';
      el.foot.innerHTML = titleBlock(r, doc);
      document.title = r.isIndex ? (baseTitle || M.title + ' — ' + M.documentName)
                                 : M.title + ' · ' + sheetOf(r.file).letter + ' — ' + doc.title;
      NovaShell.inlineFigures(el.body);

      var target = r.anchor && document.getElementById(r.anchor);
      if (target) target.scrollIntoView({ block: 'start' });
      else window.scrollTo(0, 0);
    }).catch(function (e) { NovaShell.fail(el.body, e, 'content/' + r.file + '.md'); });
  }

  function load(file) {
    if (!docCache[file]) docCache[file] = NovaShell.loadSheet(file);
    return docCache[file];
  }

  // ── boot ──────────────────────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', function () {
    baseTitle = document.title;
    el.side = document.querySelector('.side');
    el.mast = document.querySelector('.masthead');
    el.body = document.querySelector('#content');
    el.pager = document.querySelector('.pager');
    el.foot = document.querySelector('.cajetin');

    NovaShell.loadManifest().then(function (m) {
      M = m;
      window.addEventListener('hashchange', render);
      render();
    }).catch(function (e) { NovaShell.fail(el.body, e, 'manifest.json'); });
  });
})();
