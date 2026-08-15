/* noVa64 · docsV3 — shell and router.
 *
 * Everything around the prose — sidebar, masthead, sheet index, pager and
 * title block — is generated from manifest.json, so a sheet's .md file only
 * ever carries its own content. Routes are hashes: #/ is the index,
 * #/sec_a a sheet, #/sec_a/a3 a sheet scrolled to one of its items.
 */
(function () {
  'use strict';

  var M = null;                       // manifest.json
  var docCache = {};                  // route file -> parsed document
  var svgCache = {};                  // figure path -> svg source

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
    M.sheets.forEach(function (s) {
      h += '<li><a href="#/' + s.file + '"' + (s.file === r.file ? ' class="here"' : '') + '>' +
           '<span class="de">' + s.letter + '</span>' + s.nav +
           '<span class="fg">' + s.num + '</span></a></li>';
    });
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
           '<div class="sub">Sheet ' + s.num + ' · ' + s.letter + ' — ' + doc.title + '</div>';
  }

  function indexTable() {
    var h = '<nav class="idx" aria-label="Sheet index"><div class="cap">SHEET INDEX</div><table><tbody>';
    M.sheets.forEach(function (s) {
      h += '<tr><td class="no">' + s.num + '</td><td class="de">' + s.letter + '</td>' +
           '<td><a href="#/' + s.file + '">' + s.index + '</a></td>' +
           '<td class="fg">' + s.fig + '</td></tr>';
    });
    return h + '</tbody></table></nav>';
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

  // ── figures ───────────────────────────────────────────────────────────

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

  // ── rendering ─────────────────────────────────────────────────────────

  var el = {};

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
      document.title = r.isIndex ? M.title + ' — ' + M.documentName
                                 : M.title + ' · ' + sheetOf(r.file).letter + ' — ' + doc.title;
      inlineFigures(el.body);

      var target = r.anchor && document.getElementById(r.anchor);
      if (target) target.scrollIntoView({ block: 'start' });
      else window.scrollTo(0, 0);
    }).catch(function (e) { fail(e, 'content/' + r.file + '.md'); });
  }

  function load(file) {
    if (docCache[file]) return Promise.resolve(docCache[file]);
    return fetch('content/' + file + '.md').then(function (res) {
      if (!res.ok) throw new Error('HTTP ' + res.status);
      return res.text();
    }).then(function (txt) {
      docCache[file] = window.NovaMarkdown.parse(txt);
      return docCache[file];
    });
  }

  function fail(err, what) {
    var local = location.protocol === 'file:';
    el.body.className = 'status err';
    el.body.innerHTML = '<b>Could not read ' + what + '</b> — ' + err.message +
      (local ? '<br><br>The pages are read with <code>fetch()</code>, which a browser refuses to do over ' +
               '<code>file://</code>. Serve the folder instead:<br><br>' +
               '<code>python3 -m http.server -d docs/docsV3</code><br><br>then open ' +
               '<code>http://localhost:8000/</code>.' : '');
  }

  // ── boot ──────────────────────────────────────────────────────────────

  document.addEventListener('DOMContentLoaded', function () {
    el.side = document.querySelector('.side');
    el.mast = document.querySelector('.masthead');
    el.body = document.querySelector('#content');
    el.pager = document.querySelector('.pager');
    el.foot = document.querySelector('.cajetin');

    fetch('manifest.json').then(function (res) {
      if (!res.ok) throw new Error('HTTP ' + res.status);
      return res.json();
    }).then(function (m) {
      M = m;
      window.addEventListener('hashchange', render);
      render();
    }).catch(function (e) { fail(e, 'manifest.json'); });
  });
})();
