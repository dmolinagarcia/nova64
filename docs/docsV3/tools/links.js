#!/usr/bin/env node
/* noVa64 · docsV3 — the cross-reference listing.
 *
 * Every sheet points at other sheets — `[sheet Q](sec_q)`, `[E.13](sec_e#e13)`,
 * `[Sheet index](index)` — and nothing until now said which, or whether the
 * item on the far end still exists. This reads the markdown the way md.js does,
 * resolves each target against manifest.json, and prints one line per link:
 *
 *     source file  ->  linked section
 *
 *   node tools/links.js              the listing, grouped by source sheet
 *   node tools/links.js --reverse    the same links, grouped by target
 *   node tools/links.js --broken     only the ones that resolve to nothing
 *   node tools/links.js --csv        source,line,text,target,anchor,status
 *   node tools/links.js --summary    counts, and the sheets nobody links to
 *
 * Name a sheet and the listing narrows to it — what it links out to, and what
 * links in — which is what to run before moving or rewriting one:
 *
 *   node tools/links.js sec_q        both directions for sheet Q
 *   node tools/links.js sec_q#q3     just item Q.3 within sheet Q, both directions
 *   node tools/links.js content/sec_q.md --csv   the same rows, as CSV
 *
 * Exit status is 1 when a link is broken — among the ones shown, when a sheet
 * was named — so it can stand in a check.
 */
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const CONTENT = path.join(ROOT, 'content');

const argv = process.argv.slice(2);
const flags = new Set(argv.filter(a => a.startsWith('--')));

/* A sheet can be named as `sec_q`, `content/sec_q.md` or anything between,
   and narrowed further to one item with `sec_q#q3`. */
const named = argv.filter(a => !a.startsWith('--'));
const [FOCUS_SHEET_RAW, FOCUS_ITEM_RAW] = (named[0] || '').split('#');
const FOCUS = named.length ? path.basename(FOCUS_SHEET_RAW).replace(/\.md$/, '') : '';
const FOCUS_ITEM = (FOCUS_ITEM_RAW || '').toLowerCase();

const MODE = flags.has('--csv') ? 'csv'
  : FOCUS ? 'sheet'
  : flags.has('--reverse') ? 'reverse'
  : flags.has('--broken') ? 'broken'
  : flags.has('--summary') ? 'summary'
  : 'forward';

// ── the document, as the manifest describes it ────────────────────────────

const manifest = JSON.parse(fs.readFileSync(path.join(ROOT, 'manifest.json'), 'utf8'));
const sheets = new Map();                       // file -> manifest row
manifest.sheets.forEach(s => sheets.set(s.file, s));

/* What a target is called in the listing: its letter and its sidebar name. */
function sheetName(file) {
  if (file === 'index') return 'index · Sheet index';
  const s = sheets.get(file);
  return s ? s.letter + ' · ' + s.nav : file;
}

// ── the anchors each sheet offers ─────────────────────────────────────────

/* md.js: an item is `- A.7 — text`, optionally with a `[ ]` checkbox, and its
   anchor is the id lowercased with the dots dropped. Same rule here, so that
   an anchor this script accepts is one the page can actually reach. */
const ID = /^(?:[A-Z][A-Za-z0-9.]{0,7}|\+)$/;

/* Every item, in document order, with the line its bullet starts on — so a
   link line can be traced back to the item it falls under. */
function itemsOf(src) {
  const items = [];
  let fenced = false;
  src.replace(/\r/g, '').split('\n').forEach((raw, i) => {
    if (/^```/.test(raw)) { fenced = !fenced; return; }
    if (fenced || !/^-\s+/.test(raw)) return;
    let body = raw.replace(/^-\s+/, '');
    const box = /^(\[[ x~?]\])\s+/.exec(body);
    if (box) body = body.slice(box[0].length);
    const cut = body.indexOf(' — ');
    if (cut <= 0) return;
    const id = body.slice(0, cut);
    if (!ID.test(id) || !/^[A-Za-z]/.test(id)) return;
    items.push({ line: i + 1, id, anchor: id.toLowerCase().replace(/\./g, '') });
  });
  return items;
}

function anchorsOf(items) {
  const found = new Map();                      // anchor -> item id
  items.forEach(it => found.set(it.anchor, it.id));
  return found;
}

/* The item a given line falls under: the last bullet at or before it. */
function itemAt(items, line) {
  let cur = '';
  for (const it of items) {
    if (it.line > line) break;
    cur = it.anchor;
  }
  return cur;
}

// ── the links each sheet makes ────────────────────────────────────────────

const LINK = /\[([^\]]*)\]\(([^)\s]*)\)/g;

/* The inline scanner of md.js skips code spans and reads `[[chip]]` before it
   reads a link, so both are dropped here before the links are picked out. */
function scannable(line) {
  return line.replace(/`[^`]*`/g, m => ' '.repeat(m.length))
             .replace(/\[\[[^\]]*\]\]/g, m => ' '.repeat(m.length));
}

function linksOf(src) {
  const out = [];
  let fenced = false;
  src.replace(/\r/g, '').split('\n').forEach((raw, i) => {
    if (/^```/.test(raw)) { fenced = !fenced; return; }
    if (fenced) return;
    const line = scannable(raw);
    let m;
    LINK.lastIndex = 0;
    while ((m = LINK.exec(line))) {
      if (line[m.index - 1] === '!') continue;   // ![figure](…), not a link
      out.push({ line: i + 1, text: m[1], target: m[2] });
    }
  });
  return out;
}

// ── resolving a target ────────────────────────────────────────────────────

const files = fs.readdirSync(CONTENT).filter(f => f.endsWith('.md')).sort();
const anchors = new Map();                      // file (no .md) -> anchor map
const itemLines = new Map();                    // file (no .md) -> items, in line order
files.forEach(f => {
  const name = f.replace(/\.md$/, '');
  const items = itemsOf(fs.readFileSync(path.join(CONTENT, f), 'utf8'));
  itemLines.set(name, items);
  anchors.set(name, anchorsOf(items));
});

if (FOCUS && !anchors.has(FOCUS)) {
  console.error('no such sheet: ' + FOCUS + '\nsheets: ' +
    [...anchors.keys()].sort().join(', '));
  process.exit(2);
}

if (FOCUS && FOCUS_ITEM && !anchors.get(FOCUS).has(FOCUS_ITEM)) {
  console.error('no such item: ' + FOCUS + '#' + FOCUS_ITEM + '\nitems: ' +
    itemLines.get(FOCUS).map(it => it.id).join(', '));
  process.exit(2);
}

/* NovaLink's own reading of a target, plus a verdict on where it lands. */
function resolve(target) {
  if (/^(https?:|mailto:)/.test(target)) return { kind: 'external', status: 'ok' };
  if (/^#/.test(target)) return { kind: 'anchor', status: 'ok' };
  if (/\//.test(target) || /\.(svg|png|pdf|html)$/.test(target)) {
    const ok = fs.existsSync(path.join(ROOT, target));
    return { kind: 'asset', status: ok ? 'ok' : 'missing file' };
  }

  const [file, anchor] = target.split('#');
  const to = (file === '' || file === 'index') ? 'index' : file;
  const r = { kind: 'sheet', to: to, anchor: anchor || '', status: 'ok' };

  if (!anchors.has(to)) { r.status = 'no such sheet'; return r; }
  if (to !== 'index' && !sheets.has(to)) r.status = 'not in manifest';
  if (anchor) {
    const id = anchors.get(to).get(anchor);
    if (id) r.item = id; else r.status = 'no such item';
  }
  return r;
}

// ── the listing ───────────────────────────────────────────────────────────

const rows = [];
for (const f of files) {
  const from = f.replace(/\.md$/, '');
  for (const l of linksOf(fs.readFileSync(path.join(CONTENT, f), 'utf8'))) {
    const r = resolve(l.target);
    rows.push({ from, file: f, line: l.line, text: l.text, target: l.target, ...r });
  }
}

/* With a sheet named, everything below — the listing and the exit status —
   is about the links that touch it, in either direction. */
const shown = FOCUS ? rows.filter(r => r.from === FOCUS || r.to === FOCUS) : rows;
const broken = shown.filter(r => r.status !== 'ok');
const internal = shown.filter(r => r.kind === 'sheet');
const pad = (s, n) => String(s) + ' '.repeat(Math.max(0, n - String(s).length));

/* `content/sec_a.md  ->  sec_q  E · The CPU`, with the item when there is one. */
function describe(r) {
  if (r.kind !== 'sheet') return pad(r.target, 22) + r.kind;
  const dest = r.to + (r.anchor ? '#' + r.anchor : '');
  let s = pad(dest, 22) + sheetName(r.to);
  if (r.item) s += ' · item ' + r.item;
  if (r.status !== 'ok') s += '   [' + r.status + ']';
  return s;
}

if (MODE === 'csv') {
  console.log('source,line,text,target,sheet,anchor,item,status');
  const q = v => '"' + String(v == null ? '' : v).replace(/"/g, '""') + '"';
  shown.forEach(r => console.log([
    'content/' + r.file, r.line, r.text, r.target,
    r.kind === 'sheet' ? r.to : '', r.anchor || '', r.item || '', r.status
  ].map(q).join(',')));

} else if (MODE === 'sheet') {
  /* `--broken` narrows the two lists the same way it narrows the whole run.
     With an item named, OUT keeps only links whose source line falls under
     it, and IN only links whose target anchor is exactly that item. */
  const only = flags.has('--broken') ? l => l.filter(r => r.status !== 'ok') : l => l;
  const fromItem = r => itemAt(itemLines.get(FOCUS), r.line);
  const out = only(shown.filter(r => r.from === FOCUS &&
    (!FOCUS_ITEM || fromItem(r) === FOCUS_ITEM)));
  const inn = only(shown.filter(r => r.to === FOCUS && r.from !== FOCUS &&
    (!FOCUS_ITEM || r.anchor === FOCUS_ITEM)));
  const self = only(shown.filter(r => r.from === FOCUS && r.to === FOCUS &&
    (!FOCUS_ITEM || fromItem(r) === FOCUS_ITEM || r.anchor === FOCUS_ITEM)));
  const innAll = [...inn, ...self.filter(r => !FOCUS_ITEM || r.anchor === FOCUS_ITEM)]
    .sort((a, b) => a.from.localeCompare(b.from) || a.line - b.line);
  const reach = l => new Set(l.map(r => r.from === FOCUS ? r.to : r.from)).size;
  const n = (c, w) => c + ' ' + w + (c === 1 ? '' : 's');

  console.log('\n' + FOCUS + (FOCUS_ITEM ? '#' + FOCUS_ITEM + '   item ' +
    anchors.get(FOCUS).get(FOCUS_ITEM) + ' · ' + sheetName(FOCUS)
    : '   ' + sheetName(FOCUS)));

  console.log('\nOUT — ' + n(out.length, 'link') + ' to ' + n(reach(out), 'sheet'));
  out.forEach(r => console.log('  ' + pad('L' + r.line, 7) + '-> ' + describe(r) +
    '   \u201c' + r.text + '\u201d'));

  console.log('\nIN — ' + n(innAll.length, 'link') + ' from ' + n(reach(innAll), 'sheet'));
  innAll.forEach(r => console.log('  ' + pad('content/' + r.file + ':' + r.line, 24) +
    pad(r.anchor ? '#' + r.anchor : '', 8) +
    pad(r.item ? 'item ' + r.item : '', 12) + '\u201c' + r.text + '\u201d' +
    (r.status !== 'ok' ? '   [' + r.status + ']' : '')));

  console.log('\n' + out.length + ' out · ' + innAll.length + ' in' +
    (self.length ? ' · ' + self.length + ' within the sheet' : '') +
    ' · ' + broken.length + ' broken');

} else if (MODE === 'reverse') {
  const by = new Map();
  internal.forEach(r => {
    if (!by.has(r.to)) by.set(r.to, []);
    by.get(r.to).push(r);
  });
  [...by.keys()].sort().forEach(to => {
    console.log('\n' + to + '   ' + sheetName(to) + '   ← ' + by.get(to).length + ' links');
    by.get(to).sort((a, b) => a.from.localeCompare(b.from) || a.line - b.line)
      .forEach(r => console.log('  ' + pad('content/' + r.file + ':' + r.line, 24) +
        pad(r.anchor ? '#' + r.anchor : '', 8) + r.text +
        (r.status !== 'ok' ? '   [' + r.status + ']' : '')));
  });

} else if (MODE === 'summary') {
  const out = new Map(), inn = new Map();
  internal.forEach(r => {
    out.set(r.from, (out.get(r.from) || 0) + 1);
    inn.set(r.to, (inn.get(r.to) || 0) + 1);
  });
  console.log(pad('SHEET', 10) + pad('OUT', 6) + pad('IN', 6) + 'NAME');
  ['index', ...manifest.sheets.map(s => s.file)].forEach(f =>
    console.log(pad(f, 10) + pad(out.get(f) || 0, 6) + pad(inn.get(f) || 0, 6) + sheetName(f)));
  const orphans = manifest.sheets.map(s => s.file).filter(f => !inn.has(f));
  console.log('\n' + rows.length + ' links · ' + internal.length + ' between sheets · ' +
    broken.length + ' broken');
  if (orphans.length) console.log('linked from nowhere: ' + orphans.join(', '));

} else {
  const list = MODE === 'broken' ? broken : shown;
  let current = '';
  list.forEach(r => {
    if (r.file !== current) { current = r.file; console.log('\ncontent/' + current); }
    console.log('  ' + pad('L' + r.line, 7) + '-> ' + describe(r) + '   “' + r.text + '”');
  });
  console.log('\n' + list.length + (MODE === 'broken' ? ' broken links' : ' links · ' +
    internal.length + ' between sheets · ' + broken.length + ' broken'));
}

process.exit(broken.length ? 1 : 0);
