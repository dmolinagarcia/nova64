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

  function loadManifest() {
    return fetch('manifest.json').then(function (res) {
      if (!res.ok) throw new Error('HTTP ' + res.status + ' on manifest.json');
      return res.json();
    });
  }

  global.NovaShell = { inlineFigures: inlineFigures, fail: fail,
                       loadSheet: loadSheet, loadManifest: loadManifest };
})(window);
