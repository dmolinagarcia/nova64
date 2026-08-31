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
    return M;
  }

  function loadManifest() {
    return fetch('manifest.json').then(function (res) {
      if (!res.ok) throw new Error('HTTP ' + res.status + ' on manifest.json');
      return res.json();
    }).then(prepare);
  }

  global.NovaShell = { inlineFigures: inlineFigures, fail: fail,
                       loadSheet: loadSheet, loadManifest: loadManifest, prepare: prepare };
})(window);
