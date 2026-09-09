#!/usr/bin/env node
/* noVa64 · docsV3 — every trim the document is published at, in one run.
 *
 * The document has one stylesheet and one assembly; a trim is nothing but a
 * page size and the handful of lengths that measure against the leaf rather
 * than against the type. So this script states a trim as a small stylesheet,
 * hands it to tools/prerender.js to inline into the head — where it lands
 * after style.css and so on top of it — and runs the formatter. Once per
 * trim, into pdfs/.
 *
 *   node tools/pdfs.js                 -> all four trims
 *   node tools/pdfs.js 6x9 8x10        -> just those
 *   node tools/pdfs.js --scale 1.2     -> the same trims, type 20% larger
 *   node tools/pdfs.js --list          -> the table below, and nothing else
 *   node tools/pdfs.js --help          -> the same, for someone at a prompt
 *
 * The formatter is Vivliostyle, and it is not interchangeable here:
 * WeasyPrint 69 drops `fill` and `stroke` as CSS properties, and every figure
 * in this document is coloured by CSS class, so it prints all thirteen of
 * them as black rectangles. Vivliostyle is a real browser engine and gets
 * them right — and `--scale` needs one too.
 */
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const { execFileSync } = require('child_process');

const root = path.resolve(__dirname, '..');
const outDir = path.join(root, 'pdfs');

/* ── the trims ───────────────────────────────────────────────────────────
   A trim is three numbers. Everything style.css declares as a knob is
   derived from them below, so a trim that is not here yet is one line. */
const trims = [
  { w: 6,   h: 9,    margin: 14 },
  { w: 7.5, h: 9.25, margin: 16 },
  { w: 8,   h: 10,   margin: 17 },
  { w: 8.5, h: 11,   margin: 18 },
];

/* ── scale ───────────────────────────────────────────────────────────────
   `--scale s` sets `zoom` on the root element, which is a real property in
   a real browser engine and the only one that reaches every `px` in a
   stylesheet with 52 font sizes in it. The page box is not inside the root
   element, so the margins do not zoom: the type area keeps its physical
   size and the type grows inside it. That is the useful reading of "bigger
   type" — not a photocopy enlargement, which would grow the margins too.

   Two consequences the numbers below have to answer for. The type wraps
   over a measure that is effectively s times narrower, and any length in
   mm inside the root element is multiplied by s on its way to the paper —
   so the two cotas are computed in the unzoomed frame and divided by s.

   node tools/pdfs.js --scale 1.25   ->  pdfs/nova64_6_9_s1.25.pdf
   Page count grows with about s², since type scales in both directions. */

/* How tall the closing title block lays out, in mm, over a given measure.
   Measured rather than derived — it wraps, so it is taller on a narrow page
   — by rendering the block alone at each of the four trim widths. Linear
   between those, flat outside them. Redo it the same way if the block's
   text in manifest.json changes length.

   These four were measured with WeasyPrint, which lays the block out a hair
   taller than Vivliostyle does, and that is the safe direction: the drop
   below comes out a little short, so the block closes the page slightly
   high rather than spilling onto one more. */
function cajetinHeight(measure) {
  const pts = [[124.4, 72.1], [158.5, 62.1], [169.2, 57.9], [179.9, 57.5]];
  if (measure <= pts[0][0]) return pts[0][1];
  if (measure >= pts[pts.length - 1][0]) return pts[pts.length - 1][1];
  for (let i = 1; i < pts.length; i++) {
    const [x0, y0] = pts[i - 1], [x1, y1] = pts[i];
    if (measure <= x1) return y0 + (y1 - y0) * (measure - x0) / (x1 - x0);
  }
}

/* The table type, by measure. A four-column table over a 124mm measure gives
   each column about 25mm, and the prose in it ratchets down into ribbons a
   word wide — one row to a page. The type comes down a step and the padding
   gives itself back to the text. Above ~175mm the screen values are right. */
function tableTier(measure) {
  if (measure < 140) return { head: 8,  track: 1,   cell: 10.5, pad: '4px 5px' };
  if (measure < 175) return { head: 9,  track: 1.5, cell: 11,   pad: '5px 6px' };
  return                     { head: 10, track: 2,   cell: 12,   pad: '6px 8px' };
}

/* Everything style.css cannot work out for itself, for one trim at one scale.
   `measure` and `avail` are the type area as the type sees it: the physical
   area divided by the zoom, since that is the frame the layout happens in. */
function knobs(t, scale) {
  const measure = (t.w * 25.4 - 2 * t.margin) / scale;
  const avail = (t.h * 25.4 - 2 * t.margin) / scale;
  const tbl = tableTier(measure);
  return {
    measure, avail, tbl,
    /* the masthead sits a third of the way down the cover; the fraction is
       what was drawn for A4 */
    coverAir: Math.round(avail * 0.30),
    /* the title block closes the document low on a page of its own, pushed
       there by a spacer, and the spacer is whatever is left over: 8mm is the
       block's own top margin, 4mm is slack. Too tall and the block spills
       onto one more page, leaving a blank one behind it. */
    cajetinDrop: Math.max(0, Math.round(avail - 8 - cajetinHeight(measure) - 4)),
    /* the floor under every one of the title block's cells — they share the
       row equally — from the screen design. It only binds on a measure under
       ~150mm; above that it is moot. */
    cjCell: measure < 150 ? 120 : 220,
  };
}

const name = (t) => t.w + 'x' + t.h;
const file = (t, scale) =>
  'nova64_' + t.w + '_' + t.h + (scale === 1 ? '' : '_s' + scale) + '.pdf';

/* The trim, as the stylesheet that states it. `@page size` is written out
   rather than held in a custom property because a formatter is entitled not
   to resolve `var()` inside `@page`, and a dropped page size is silent. */
function sheet(t, scale) {
  const k = knobs(t, scale);
  return `@media print{
  @page{ size:${t.w}in ${t.h}in; margin:${t.margin}mm; }
  :root{
    zoom:${scale};
    --cover-air:${k.coverAir}mm;
    --cajetin-drop:${k.cajetinDrop}mm;
    --cj-cell:${k.cjCell}px;
    --tbl-head:${k.tbl.head}px;
    --tbl-head-track:${k.tbl.track}px;
    --tbl-cell:${k.tbl.cell}px;
    --tbl-pad:${k.tbl.pad};
  }
}
`;
}

/* ── the run ─────────────────────────────────────────────────────────────*/
const argv = process.argv.slice(2);

/* First, so that it still answers when the rest of the line is wrong. */
if (argv.includes('--help') || argv.includes('-h')) {
  console.log(`noVa64 · docsV3 — the whole document as a PDF, at every trim it
is published at. Assembles print.html with tools/prerender.js once per trim and
hands it to the formatter.

usage: node tools/pdfs.js [trim ...] [options]

  trim         ${trims.map(name).join(', ')}
               name one or more, or none at all to build every one

options
  --scale s    type size, as a multiplier on the whole document. Default 1,
               anything from 0.2 to 5. The page box is outside what scales,
               so the margins hold still and the type grows inside the same
               area — the page count goes with about s², not s. A scaled
               build is written to its own name and never overwrites the
               canonical one.
  --list       print what each trim resolves to, and build nothing
  --help, -h   this

output         pdfs/nova64_<width>_<height>.pdf, in inches, and _s<scale>
               on the end of the name when --scale is not 1. The directory
               is a build artifact; docs/.gitignore already knows it.

The formatter is Vivliostyle, fetched through npx on first use, and it is not
interchangeable: WeasyPrint drops \`fill\` and \`stroke\` as CSS properties, and
every figure here is coloured by CSS class, so it prints all thirteen of them
as black rectangles.`);
  process.exit(0);
}

let scale = 1;
const at = argv.indexOf('--scale');
if (at !== -1) {
  scale = Number(argv.splice(at, 2)[1]);
  if (!(scale > 0.2 && scale < 5)) {
    console.error('--scale wants a number between 0.2 and 5');
    process.exit(1);
  }
}

if (argv.includes('--list')) {
  trims.forEach((t) => {
    const k = knobs(t, scale);
    console.log(`${name(t).padEnd(9)} ${file(t, scale).padEnd(26)} margin ${t.margin}mm · ` +
                `type area ${k.measure.toFixed(1)} × ${k.avail.toFixed(1)} mm` +
                (scale === 1 ? '' : ` (at zoom ${scale})`) +
                ` · cover ${k.coverAir}mm · drop ${k.cajetinDrop}mm`);
  });
  process.exit(0);
}

const wanted = argv.length ? trims.filter((t) => argv.includes(name(t))) : trims;
if (!wanted.length) {
  console.error('no such trim: ' + argv.join(' ') + '\nknown: ' + trims.map(name).join(' '));
  process.exit(1);
}

fs.mkdirSync(outDir, { recursive: true });

/* print.html has to sit beside style.css and figures/, so every trim is
   assembled over the same path in turn rather than into a directory of its
   own. It is a build artifact and docs/.gitignore already knows it. */
const html = path.join(root, 'print.html');
const css = path.join(os.tmpdir(), 'nova64-trim.css');

let failed = 0;
for (const t of wanted) {
  const out = path.join(outDir, file(t, scale));
  process.stderr.write(`${name(t)} in · `);
  fs.writeFileSync(css, sheet(t, scale), 'utf8');
  try {
    execFileSync(process.execPath, [path.join(__dirname, 'prerender.js'), '--head', css, html],
                 { stdio: ['ignore', 'ignore', 'inherit'] });
    execFileSync('npx', ['--yes', '@vivliostyle/cli', 'build', html, '-o', out],
                 { stdio: ['ignore', 'ignore', 'ignore'] });
    const mb = (fs.statSync(out).size / 1048576).toFixed(1);
    console.error(`${path.relative(process.cwd(), out)} — ${mb} MB`);
  } catch (err) {
    failed++;
    console.error('failed: ' + err.message.split('\n')[0]);
  }
}

fs.rmSync(css, { force: true });
if (failed) { console.error(`\n${failed} of ${wanted.length} trims failed`); process.exit(1); }
