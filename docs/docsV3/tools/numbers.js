#!/usr/bin/env node
/* noVa64 · docsV3 — the figure and table numbers.
 *
 * Everything else about this document is derived and never stored: a sheet's
 * position, its part, the counts in the title block. A figure's number cannot
 * be, and the reason is the paged edition. `index.html` renders **one sheet at
 * a time** and never sees the others, but the number of a figure in sheet F
 * depends on how many figures stand ahead of it in the whole document — so the
 * browser would have to fetch all fifty-seven sheets before it could print
 * `Figure 7`, on every load, to answer a question whose answer only changes
 * when the sources do.
 *
 * So this walks the reading order once, here, and writes the answer down:
 *
 *   node tools/numbers.js            assign the numbers, write numbering.json
 *   node tools/numbers.js --check    verify it is current — exit 1 if not
 *   node tools/numbers.js --quiet    write without the report
 *
 * The reading order is not rebuilt: `NovaShell.prepare` from shell.js resolves
 * it, exactly as index.html, full.html and tools/prerender.js do, and the
 * markdown is read by the real `md.js` rather than by a regex of this script's
 * own — `parse()` hands back the marks it saw, so the generator cannot come to
 * a different conclusion than the page.
 *
 *   --root <dir>       a whole docsV3 tree elsewhere — for the fixtures
 *   --content <dir>    just the sheets
 *   --manifest <file>  just the manifest
 *   --out <file>       where numbering.json goes
 *
 * `numbering.json` **is committed**, unlike print.html and pdfs/: GitHub Pages
 * serves docs/ verbatim with no build step, so an artefact left uncommitted is
 * a 404 in production. Regenerate it whenever a caption or a sheet order moves,
 * and let `--check` be what remembers.
 */
'use strict';

const fs = require('fs');
const path = require('path');

const argv = process.argv.slice(2);

function opt(name, fallback) {
  const i = argv.indexOf(name);
  return i === -1 ? fallback : argv[i + 1];
}
const flags = new Set(argv.filter(a => a.startsWith('--') && !['--root', '--content', '--manifest', '--out'].includes(a)));

if (flags.has('--help') || flags.has('-h')) {
  console.log(`noVa64 \u00b7 docsV3 \u2014 the figure and table numbers.

A caption declares an identifier \u2014 ![F.memmap.Memory structure\u2026](f.svg) \u2014 and the
prose points at that identifier rather than at a number. This walks the reading
order manifest.json resolves, numbers what it finds, and writes numbering.json,
which the pages read. Nothing else needs the whole document to render one sheet.

  node tools/numbers.js              assign the numbers, write numbering.json
  node tools/numbers.js --check      verify it is current \u2014 exit 1 if not
  node tools/numbers.js --quiet      write without the report

--check also fails on a duplicate identifier, a reference that resolves to
nothing, a caption with no table under it, and a missing SVG. numbering.json is
committed: GitHub Pages serves docs/ with no build step behind it.

  --root <dir>       a whole docsV3 tree elsewhere \u2014 for the fixtures
  --content <dir>    just the sheets
  --manifest <file>  just the manifest
  --out <file>       where numbering.json goes
  -h, --help         show this help`);
  process.exit(0);
}

const ROOT = path.resolve(opt('--root', path.join(__dirname, '..')));
const CONTENT = path.resolve(opt('--content', path.join(ROOT, 'content')));
const MANIFEST = path.resolve(opt('--manifest', path.join(ROOT, 'manifest.json')));
const OUT = path.resolve(opt('--out', path.join(ROOT, 'numbering.json')));
const CHECK = flags.has('--check');
const QUIET = flags.has('--quiet');

/* shell.js is browser code that touches the DOM in none of the functions used
   here; md.js is the same. One global is enough to load both. */
global.window = global;
require(path.join(__dirname, '..', 'shell.js'));
require(path.join(__dirname, '..', 'md.js'));

/* The document is parsed with no numbering in hand — that is what is being
   worked out — so every reference is a miss. md.js says so on the console,
   which is right in a browser and noise here. */
const realWarn = console.warn;
console.warn = () => {};

// ── the reading order, and what each sheet declares ────────────────────────

const M = window.NovaShell.prepare(JSON.parse(fs.readFileSync(MANIFEST, 'utf8')));

/* `index` first, then the sheets: the order full.js assembles the printed
   edition in, and therefore the order the page numbers will run in. */
const order = ['index'].concat(M.sheets.map(s => s.file));
const letterOf = {};
M.sheets.forEach(s => { letterOf[s.file] = s.letter; });

const defs = { F: [], T: [] };           // declarations, in reading order
const refs = [];                         // every ![F.id] / ![T.id] in the prose
const orphans = [];                      // ![T.id.…] with no table under it
const legacy = [];                       // ![Fig. N — …] still written by hand
const loose = [];                        // "Fig. 3" / "Table 4" left in prose
const missingSvg = [];

const LEGACY_CAP = /^!\[(Fig\.\s*[0-9a-z]+)\s*—/;
const LOOSE = /(?:^|[^!\w])(Fig\.\s*\d+[a-z]?|Figures?\s+\d+|Tables?\s+\d+)\b/g;

for (const file of order) {
  const src = path.join(CONTENT, file + '.md');
  if (!fs.existsSync(src)) continue;
  const text = fs.readFileSync(src, 'utf8');
  const doc = window.NovaMarkdown.parse(text);

  for (const m of doc.marks) {
    if (m.role === 'def') defs[m.kind].push(Object.assign({ sheet: file }, m));
    else if (m.role === 'ref') refs.push(Object.assign({ sheet: file }, m));
    else if (m.role === 'orphan') orphans.push(Object.assign({ sheet: file }, m));
  }

  /* The two things that say how far the migration has got. Both are read off
     the source rather than the parsed document, because what is being looked
     for is precisely what the renderer treats as ordinary prose. */
  let fenced = false;
  text.replace(/\r/g, '').split('\n').forEach((line, i) => {
    const t = line.trim();
    if (/^(```|~~~)/.test(t)) { fenced = !fenced; return; }
    if (fenced) return;
    const lg = LEGACY_CAP.exec(t);
    if (lg) { legacy.push({ sheet: file, line: i + 1, was: lg[1] }); return; }
    let m;
    LOOSE.lastIndex = 0;
    while ((m = LOOSE.exec(t))) loose.push({ sheet: file, line: i + 1, text: m[1] });
  });
}

console.warn = realWarn;

// ── the numbers ───────────────────────────────────────────────────────────

const numbering = { figures: {}, tables: {} };
const dupes = [];

for (const kind of ['F', 'T']) {
  const set = kind === 'F' ? numbering.figures : numbering.tables;
  let n = 0;
  for (const d of defs[kind]) {
    if (set[d.id]) { dupes.push({ kind, id: d.id, first: set[d.id].sheet, again: d.sheet }); continue; }
    set[d.id] = {
      n: ++n,
      sheet: d.sheet,
      anchor: (kind === 'F' ? 'f-' : 't-') + d.id,
      desc: d.desc
    };
    if (kind === 'F') {
      set[d.id].svg = d.svg;
      if (d.svg && !fs.existsSync(path.join(ROOT, d.svg))) missingSvg.push({ id: d.id, svg: d.svg });
    }
  }
}

const dangling = refs.filter(r => !(r.kind === 'F' ? numbering.figures : numbering.tables)[r.id]);

const never = [];
for (const kind of ['figures', 'tables']) {
  for (const id of Object.keys(numbering[kind])) {
    const k = kind === 'figures' ? 'F' : 'T';
    if (!refs.some(r => r.kind === k && r.id === id)) never.push(k + '.' + id);
  }
}

const json = JSON.stringify(numbering, null, 2) + '\n';
const onDisk = fs.existsSync(OUT) ? fs.readFileSync(OUT, 'utf8') : null;
const stale = onDisk !== json;

// ── report ────────────────────────────────────────────────────────────────

const problems = [];
if (dupes.length) dupes.forEach(d => problems.push(`duplicate id ${d.kind}.${d.id} — ${d.first} and ${d.again}`));
if (dangling.length) dangling.forEach(r => problems.push(`${r.sheet}: ![${r.kind}.${r.id}] refers to nothing`));
if (orphans.length) orphans.forEach(o => problems.push(`${o.sheet}: caption T.${o.id} has no table under it`));
if (missingSvg.length) missingSvg.forEach(s => problems.push(`F.${s.id}: missing figure ${s.svg}`));

function report() {
  const nf = Object.keys(numbering.figures).length;
  const nt = Object.keys(numbering.tables).length;
  const legF = legacy.length;
  console.log(`  FIGURES  ${nf} numbered · ${legF} still written by hand`);
  console.log(`  TABLES   ${nt} numbered`);
  console.log('');
  for (const kind of ['figures', 'tables']) {
    const k = kind === 'figures' ? 'F' : 'T';
    Object.keys(numbering[kind])
      .map(id => Object.assign({ id }, numbering[kind][id]))
      .sort((a, b) => a.n - b.n)
      .forEach(e => {
        const label = (k === 'F' ? 'Figure ' : 'Table ') + e.n;
        console.log(`  ${label.padEnd(10)} ${(letterOf[e.sheet] || '·').padEnd(4)} ` +
                    `${(k + '.' + e.id).padEnd(14)} ${e.desc.slice(0, 54)}`);
      });
    if (Object.keys(numbering[kind]).length) console.log('');
  }

  const notes = [];
  if (legacy.length) {
    notes.push(`${legacy.length} hand-numbered captions remain: ` +
               legacy.slice(0, 6).map(l => `${l.sheet}:${l.line} ${l.was}`).join(', ') +
               (legacy.length > 6 ? ', …' : ''));
  }
  if (loose.length) {
    notes.push(`${loose.length} number written out in prose: ` +
               loose.slice(0, 6).map(l => `${l.sheet}:${l.line} "${l.text}"`).join(', ') +
               (loose.length > 6 ? ', …' : ''));
  }
  if (never.length) notes.push(`defined but never referred to: ${never.join(', ')}`);
  if (notes.length) {
    console.log('  notes');
    notes.forEach(n => console.log('    ' + n));
    console.log('');
  }
}

if (!QUIET) report();

if (problems.length) {
  console.error('  problems');
  problems.forEach(p => console.error('    ' + p));
  console.error('');
}

if (CHECK) {
  if (stale) {
    console.error(onDisk === null
      ? `  ${path.relative(process.cwd(), OUT)} does not exist — run \`node tools/numbers.js\``
      : `  ${path.relative(process.cwd(), OUT)} is out of date — run \`node tools/numbers.js\``);
  }
  process.exit(stale || problems.length ? 1 : 0);
}

fs.writeFileSync(OUT, json, 'utf8');
console.error(`  ${path.relative(process.cwd(), OUT)} — ` +
              `${Object.keys(numbering.figures).length} figures, ` +
              `${Object.keys(numbering.tables).length} tables` +
              (stale ? '' : ' (unchanged)'));
if (problems.length) process.exit(1);
