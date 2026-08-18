/* noVa64 · docsV3 — the whole document on one page, for printing.
 *
 * Same content, same stylesheet, same manifest as index.html: only the
 * assembly differs. Every sheet is rendered one after another, each starting
 * a fresh sheet of paper, and every cross-reference becomes an in-page jump —
 * which is also what makes the sheet index a working table of contents in the
 * printed PDF.
 */
(function () {
  'use strict';

  var M = null, el = {};

  /* Item anchors are only unique within a sheet — E.2 in sheet E and E2 in
     sheet P both want `e2` — so on this page every id is prefixed with its
     sheet, and links are rewritten to match. */
  window.NovaLink = function (target) {
    if (/^(https?:|mailto:|#)/.test(target)) return target;
    var p = target.split('#');
    if (p[0] === 'index' || p[0] === '') return '#top';
    return '#' + p[0] + (p[1] ? '-' + p[1] : '');
  };

  function prefixIds(html, file) {
    return html.replace(/<li id="/g, '<li id="' + file + '-');
  }

  function indexTable() {
    var h = '<nav class="idx" aria-label="Sheet index"><div class="cap">SHEET INDEX</div><table><tbody>';
    var area = '';
    M.sheets.forEach(function (s) {
      if (s.area && s.area !== area) {
        area = s.area;
        h += '<tr class="grp"><td colspan="4"><a href="#area-' + slug(area) + '">' + area + '</a></td></tr>';
      }
      h += '<tr><td class="no">' + s.num + '</td><td class="de">' + s.letter + '</td>' +
           '<td><a href="#' + s.file + '">' + s.index + '</a></td>' +
           '<td class="fg">' + s.fig + '</td></tr>';
    });
    return h + '</tbody></table></nav>';
  }

  function slug(area) {
    return area.toLowerCase().replace(/[^a-z0-9]+/g, '-');
  }

  /* Each area opens with a band, so the printed document reads as ten parts
     rather than twenty-three sheets, and the index's area rows have a target. */
  function areaBand(area) {
    return '<div class="areaband" id="area-' + slug(area) + '">' + area + '</div>';
  }

  function section(sheet, doc) {
    return '<section class="hoja" id="' + sheet.file + '">' +
           '<div class="sechead"><span class="des">' + sheet.letter + '</span>' +
           '<h2>' + doc.title + '</h2>' +
           '<span class="aim">' + doc.aim + '</span>' +
           '<span class="sheet">SHEET ' + sheet.num + '</span></div>' +
           prefixIds(doc.html, sheet.file) + '</section>';
  }

  function masthead(doc) {
    var h = '<h1 id="top">' + M.title + '</h1><div class="sub">' + M.documentName + '</div>';
    if (doc.tags && doc.tags.length) {
      h += '<div class="meta">' + doc.tags.map(function (t) {
        return '<span class="tag' + (t.cls ? ' ' + t.cls : '') + '">' + t.text + '</span>';
      }).join('') + '</div>';
    }
    return h;
  }

  function titleBlock() {
    var f = M.footer, last = M.sheets[M.sheets.length - 1];
    return '<div class="cj"><div class="big">' + M.title + '</div>' +
           '<div>' + M.documentName.toUpperCase() + ' — COMPLETE, SHEETS ' +
           M.sheets[0].letter + '–' + last.letter + '</div></div>' +
           '<div class="cj"><div>' + f.referenceIndex + '</div><div>' + f.detail + '</div></div>' +
           '<div class="cj"><div>' + f.rules + '</div><div class="gold">' + f.rev + '</div></div>';
  }

  document.addEventListener('DOMContentLoaded', function () {
    el.mast = document.querySelector('.masthead');
    el.body = document.querySelector('#content');
    el.foot = document.querySelector('.cajetin');

    NovaShell.loadManifest().then(function (m) {
      M = m;
      return Promise.all([NovaShell.loadSheet('index')].concat(
        M.sheets.map(function (s) { return NovaShell.loadSheet(s.file); })));
    }).then(function (docs) {
      var index = docs[0];
      el.mast.innerHTML = masthead(index);
      el.body.className = '';
      el.body.innerHTML =
        index.html.replace('<div data-index></div>', index.hasIndex ? indexTable() : '') +
        (function () {
          var area = '';
          return M.sheets.map(function (s, i) {
            var band = '';
            if (s.area && s.area !== area) { area = s.area; band = areaBand(area); }
            return band + section(s, docs[i + 1]);
          }).join('');
        })();
      el.foot.innerHTML = titleBlock();
      NovaShell.inlineFigures(el.body);

      /* only now do the anchors exist, so honour a deep link arrived with */
      var target = location.hash && document.getElementById(location.hash.slice(1));
      if (target) target.scrollIntoView({ block: 'start' });
    }).catch(function (e) { NovaShell.fail(el.body, e, 'the document'); });
  });
})();
