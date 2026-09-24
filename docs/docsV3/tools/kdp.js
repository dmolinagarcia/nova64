#!/usr/bin/env node
/* noVa64 · docsV3 — does a built PDF clear Amazon KDP's paperback margins.
 *
 * KDP does not read the stylesheet; it looks at where the ink is. A margin
 * the @page rule states is the margin of the type area, and anything that
 * overflows it — a table wider than its measure is the usual one — lands in
 * the gutter all the same, and the upload is refused as "insufficient
 * gutter" on a book whose @page gutter is wider than asked. So this measures
 * the ink, page by page, against the minimums for that page count.
 *
 *   node tools/kdp.js pdfs/nova64_7.5_9.25_20260923-1633.pdf [more.pdf ...]
 *
 * tools/pdfs.js runs it after every book trim it builds. Exit 1 when any
 * page of any file falls short.
 *
 * The ink is text, paths and images, each at its extent on the page after
 * every transform. Clipping is not followed, so something drawn and then
 * clipped away counts as ink: the error is on the side of a false alarm,
 * never of a refused upload.
 *
 * The PDF reader is pdf.js, fetched through npx on first use as the
 * formatter is. This script re-runs itself under `npx -p pdfjs-dist` and
 * finds the package on the PATH npx hands it.
 */
'use strict';

const path = require('path');
const { spawnSync } = require('child_process');
const { pathToFileURL } = require('url');

const PDFJS = 'pdfjs-dist@4';

/* KDP's paperback table, no bleed: the inside margin grows with the page
   count, because a thicker book opens less far; outside, top and bottom are
   a flat quarter inch. Inches, as KDP states them. */
const GUTTERS = [
  [150, 0.375],
  [300, 0.5],
  [500, 0.625],
  [700, 0.75],
  [828, 0.875],
];
const OUTSIDE = 0.25;

const IN = 72;                      // PDF units per inch
const MM = IN / 25.4;
const mm = (pt) => (pt / MM).toFixed(1);

function gutterFor(pages) {
  for (const [upTo, g] of GUTTERS) if (pages <= upTo) return g;
  return null;                      // over KDP's paperback maximum
}

/* ── bootstrap ───────────────────────────────────────────────────────────*/
if (!process.env.NOVA64_KDP) {
  const r = spawnSync('npx', ['--yes', '-p', PDFJS, 'node', __filename, ...process.argv.slice(2)],
                      { stdio: 'inherit', env: { ...process.env, NOVA64_KDP: '1' } });
  process.exit(r.status === null ? 1 : r.status);
}

async function loadPdfjs() {
  const bin = process.env.PATH.split(path.delimiter)
    .find((p) => p.includes('_npx') && p.endsWith(path.join('node_modules', '.bin')));
  if (!bin) throw new Error('pdfjs-dist not found on the PATH npx set');
  const mod = path.join(bin, '..', 'pdfjs-dist', 'legacy', 'build', 'pdf.mjs');
  return import(pathToFileURL(mod).href);
}

/* ── the ink on one page ─────────────────────────────────────────────────
   Paths and images come out of the operator list in the space of whatever
   transform is current, so the transform is followed through save/restore
   and every box is taken through it. Text comes out of getTextContent
   already on the page. */
const mul = (m, n) => [
  m[0] * n[0] + m[2] * n[1], m[1] * n[0] + m[3] * n[1],
  m[0] * n[2] + m[2] * n[3], m[1] * n[2] + m[3] * n[3],
  m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5],
];

async function ink(page, OPS) {
  const box = { x0: Infinity, y0: Infinity, x1: -Infinity, y1: -Infinity };
  const add = (x, y) => {
    if (!isFinite(x) || !isFinite(y)) return;
    box.x0 = Math.min(box.x0, x); box.x1 = Math.max(box.x1, x);
    box.y0 = Math.min(box.y0, y); box.y1 = Math.max(box.y1, y);
  };
  const rect = (m, ax, ay, bx, by) => {
    for (const [x, y] of [[ax, ay], [bx, ay], [ax, by], [bx, by]])
      add(m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]);
  };

  /* A path is ink only once something paints it. Chrome opens every page by
     clipping to the whole leaf, a path that is never filled; a white fill is
     paper on paper. Both would read as ink to the edge. */
  const STROKE = [OPS.stroke, OPS.closeStroke, OPS.fillStroke, OPS.eoFillStroke,
                  OPS.closeFillStroke, OPS.closeEOFillStroke];
  const FILL = [OPS.fill, OPS.eoFill];
  const white = (c) => (typeof c === 'string' ? /^#?f{6}$/i.test(c)
                                              : c && c[0] === 255 && c[1] === 255 && c[2] === 255);

  const list = await page.getOperatorList();
  let ctm = [1, 0, 0, 1, 0, 0], fill = null, pending = null;
  const stack = [];
  for (let i = 0; i < list.fnArray.length; i++) {
    const fn = list.fnArray[i], a = list.argsArray[i];
    if (fn === OPS.save) stack.push([ctm, fill]);
    else if (fn === OPS.restore) [ctm, fill] = stack.pop() || [ctm, fill];
    else if (fn === OPS.transform) ctm = mul(ctm, a);
    else if (fn === OPS.setFillRGBColor) fill = a;
    else if (fn === OPS.setFillGray) fill = a && a[0] === 1 ? [255, 255, 255] : a;
    else if (fn === OPS.constructPath) {
      const mm4 = a[2];             // [minX, minY, maxX, maxY] of the path
      pending = mm4 && mm4[0] <= mm4[2] ? [ctm, mm4] : null;
    } else if (pending && (STROKE.includes(fn) || (FILL.includes(fn) && !white(fill)))) {
      rect(pending[0], ...pending[1]);
      pending = null;
    } else if (fn === OPS.endPath) {
      pending = null;
    } else if (fn === OPS.paintImageXObject || fn === OPS.paintInlineImageXObject ||
               fn === OPS.paintImageMaskXObject) {
      rect(ctm, 0, 0, 1, 1);        // an image fills the unit square
    }
  }

  const text = await page.getTextContent();
  for (const it of text.items) {
    if (!it.str || !it.str.trim()) continue;
    const [, , , , e, f] = it.transform;
    add(e, f); add(e + it.width, f + it.height);
  }
  return box;
}

/* ── one file ────────────────────────────────────────────────────────────
   Page 1 is a recto, so odd pages bind on the left and even ones on the
   right — the same reading `@page :right` / `:left` give in pdfs.js. */
async function check(file, pdfjs) {
  const doc = await pdfjs.getDocument({ url: file, verbosity: 0 }).promise;
  const pages = doc.numPages;
  const g = gutterFor(pages);
  const rel = path.relative(process.cwd(), file);
  if (g === null) {
    console.log(`${rel}: ${pages} pages, over KDP's paperback maximum of 828`);
    return false;
  }

  const need = { inner: g * IN, outer: OUTSIDE * IN, top: OUTSIDE * IN, bottom: OUTSIDE * IN };
  const least = { inner: [Infinity], outer: [Infinity], top: [Infinity], bottom: [Infinity] };
  const short = [];

  for (let n = 1; n <= pages; n++) {
    const page = await doc.getPage(n);
    const [px0, py0, px1, py1] = page.view;
    const b = await ink(page, pdfjs.OPS);
    page.cleanup();
    if (b.x0 > b.x1) continue;       // a blank page has no margins to break

    const left = b.x0 - px0, right = px1 - b.x1;
    const recto = n % 2 === 1;
    const m = {
      inner: recto ? left : right,
      outer: recto ? right : left,
      top: py1 - b.y1,               // PDF space runs bottom-up
      bottom: b.y0 - py0,
    };
    const bad = [];
    for (const k of Object.keys(m)) {
      if (m[k] < least[k][0]) least[k] = [m[k], n];
      if (m[k] < need[k] - 0.01) bad.push(`${k} ${mm(m[k])}`);
    }
    if (bad.length) short.push(`p${n} ${bad.join(', ')}`);
  }
  await doc.destroy();

  const head = `${rel}: ${pages} pages · KDP asks inside ${g}" (${mm(need.inner)} mm), ` +
               `outside/top/bottom ${OUTSIDE}" (${mm(need.outer)} mm)`;
  const low = Object.keys(least).map((k) => `${k} ${mm(least[k][0])} (p${least[k][1]})`).join(' · ');
  console.log(head + '\n  narrowest: ' + low);
  if (short.length) {
    console.log(`  ${short.length} page${short.length > 1 ? 's' : ''} short, in mm: ` +
                short.slice(0, 12).join('; ') + (short.length > 12 ? '; …' : ''));
  } else {
    console.log('  every page clears');
  }
  return !short.length;
}

(async () => {
  const files = process.argv.slice(2);
  if (!files.length || files.includes('--help') || files.includes('-h')) {
    console.log('usage: node tools/kdp.js file.pdf [...]  — ink against KDP paperback margins');
    process.exit(files.length ? 0 : 1);
  }
  const pdfjs = await loadPdfjs();
  let ok = true;
  for (const f of files) {
    try { ok = (await check(path.resolve(f), pdfjs)) && ok; }
    catch (err) { ok = false; console.log(`${f}: ${err.message.split('\n')[0]}`); }
  }
  process.exit(ok ? 0 : 1);
})();
