#!/usr/bin/env node
/* noVa64 · docsV3 — the printable edition, assembled ahead of time.
 *
 * `full.html` builds itself in the browser out of manifest.json and the
 * markdown, which is what keeps the sources readable — and it is also why a
 * paged-media formatter cannot read it: WeasyPrint and friends do not run
 * JavaScript, and a browser's own print path implements neither `@page`
 * margin boxes nor `target-counter()`, so it can produce neither a running
 * page number nor a page number in the sheet index.
 *
 * This script closes that gap without duplicating anything: it runs the real
 * md.js, shell.js and full.js under Node against the real files, captures what
 * they would have written into the page, inlines the figures the way shell.js
 * does in the browser, and writes one static file. Nothing here knows the
 * document's structure — change the manifest or a sheet and this follows.
 *
 *   node tools/prerender.js               -> writes print.html beside index.html
 *   node tools/prerender.js out.html      -> writes it somewhere else
 *   node tools/prerender.js --head trim.css   -> with that CSS in the head
 *
 * For every trim the document is published at, run tools/pdfs.js, which
 * drives this script and the formatter once per trim.
 *
 * Then hand that file to a formatter that implements paged media:
 *
 *   weasyprint print.html nova64.pdf
 *   npx @vivliostyle/cli build print.html -o nova64.pdf
 *
 * The stylesheet is linked, not inlined, so it stays single-sourced; the
 * output therefore has to sit beside style.css and figures/.
 */
'use strict';

const fs = require('fs');
const path = require('path');

/* `--head <file>` inlines that file as a stylesheet in the head, after
   style.css and so on top of it: it is how tools/pdfs.js states a trim
   without a second copy of the stylesheet. `--root <dir>` assembles a docsV3
   tree somewhere else, which is how the renderer is exercised against a
   throwaway fixture without touching the document. Anything else is the
   output path. */
const argv = process.argv.slice(2);
const flag = argv.indexOf('--head');
const head = flag === -1 ? '' : fs.readFileSync(argv.splice(flag, 2)[1], 'utf8');
const rootFlag = argv.indexOf('--root');
const root = rootFlag === -1 ? path.resolve(__dirname, '..')
                             : path.resolve(argv.splice(rootFlag, 2)[1]);
const out = path.resolve(argv[0] || path.join(root, 'print.html'));

/* ── the browser, reduced to what full.js actually touches ───────────────
   Three elements get written to and nothing gets read back, so an element is
   a box that remembers the last string assigned to it. */
const written = {};
const stub = (name) => ({
  set innerHTML(v) { written[name] = v; },
  get innerHTML() { return written[name] || ''; },
  set className(v) { /* the loading state, discarded */ },
  style: {},
  querySelectorAll: () => [],   // figures are inlined below instead
  scrollIntoView() {},
});

let ready;
const done = new Promise((resolve) => { ready = resolve; });

global.window = global;
global.location = { hash: '', protocol: 'file:' };
global.addEventListener = () => {};
global.scrollTo = () => {};
global.document = {
  title: '',
  _onload: null,
  addEventListener(ev, cb) { if (ev === 'DOMContentLoaded') this._onload = cb; },
  querySelector(sel) { return stub(sel); },
  getElementById() { return null; },
};

/* fetch() over the working tree. Anything the page asks for is a file. */
global.fetch = function (rel) {
  const file = path.join(root, rel);
  return fs.promises.readFile(file, 'utf8').then((text) => ({
    ok: true, status: 200,
    text: () => Promise.resolve(text),
    json: () => Promise.resolve(JSON.parse(text)),
  }), () => ({ ok: false, status: 404, text: () => Promise.resolve(''), json: () => Promise.resolve(null) }));
};

require(path.join(root, 'hl.js'));
require(path.join(root, 'md.js'));
require(path.join(root, 'shell.js'));

/* full.js reports a failure by writing an explanation into #content; catch it
   here rather than shipping a PDF whose body is an error message. */
const fail = window.NovaShell.fail;
let failed = null;
window.NovaShell.fail = function (el, err, what) { failed = (err && err.message) + ' — ' + what; ready(); };

require(path.join(root, 'full.js'));

/* The title block is the last thing full.js writes, so its arrival is the
   signal that assembly finished. */
const cajetin = stub('.cajetin');
const realQuery = global.document.querySelector;
global.document.querySelector = function (sel) {
  if (sel !== '.cajetin') return realQuery(sel);
  return { set innerHTML(v) { written['.cajetin'] = v; ready(); },
           get innerHTML() { return written['.cajetin'] || ''; },
           set className(v) {}, style: {}, querySelectorAll: () => [], scrollIntoView() {} };
};

global.document._onload();

/* ── figures ─────────────────────────────────────────────────────────────
   shell.js swaps each placeholder for the file's own markup so the print
   rules that recolour every trace still reach the shapes. Same thing here,
   on the string. */
/* A numbered figure also carries an `id`, so every other attribute on the tag
   has to survive the swap — matched and put back rather than rebuilt, or the
   anchor would vanish and every page number in the list of figures with it. */
function inlineFigures(html) {
  return html.replace(/<figure data-svg="([^"]+)"([^>]*)><div class="svg-slot"><\/div>/g,
                      (m, src, attrs) => {
    const file = path.join(root, src);
    if (!fs.existsSync(file)) {
      process.exitCode = 1;
      console.error('missing figure: ' + src);
      return m;
    }
    return '<figure data-svg="' + src + '"' + attrs + '>' +
           fs.readFileSync(file, 'utf8').replace(/<\?xml[^>]*\?>\s*/, '');
  });
}

const page = (parts) => `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>noVa64 — Synthesis document · printable</title>
<link rel="stylesheet" href="style.css">
${parts.head}</head>
<body class="full paged">
<div class="shell">
  <div class="main">
    <header class="masthead">${parts.masthead}</header>
    <div class="coverback" aria-hidden="true"></div>
    <div id="content">${parts.content}</div>
    <footer class="cajetin" aria-label="Title block">${parts.cajetin}</footer>
  </div>
</div>
</body>
</html>
`;

done.then(() => {
  if (failed) { console.error('assembly failed: ' + failed); process.exit(1); }
  const content = inlineFigures(written['#content'] || '');
  if (!content) { console.error('assembly produced no content'); process.exit(1); }
  fs.writeFileSync(out, page({
    head: head ? '<style>\n' + head.trim() + '\n</style>\n' : '',
    masthead: written['.masthead'] || '',
    content: content,
    cajetin: written['.cajetin'] || '',
  }), 'utf8');
  const sheets = (content.match(/<section class="hoja"/g) || []).length;
  const figures = (content.match(/<figure data-svg=/g) || []).length;
  console.error(path.relative(process.cwd(), out) + ' — ' + sheets + ' sheets, ' + figures + ' figures inlined');

  /* Two failures that a PDF hides rather than shows: an unsubstituted list
     directive leaves an empty div where an index should be, and a dangling
     figure reference prints as `⟨F.id⟩`. Neither stops the build, and
     `@media print{ .status{display:none} }` means no banner appears on paper
     either — so they are named here or they are not named at all. */
  const lists = (content.match(/data-list="/g) || []).length;
  const misses = (content.match(/class="xref miss"/g) || []).length;
  if (lists) console.error(lists + ' list directive(s) never substituted — see full.js');
  if (misses) console.error(misses + ' figure/table reference(s) resolve to nothing — run tools/numbers.js');
  if (lists || misses) process.exitCode = 1;
});

setTimeout(() => { console.error('timed out assembling the document'); process.exit(1); }, 20000).unref();
