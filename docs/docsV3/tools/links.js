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

/* A sheet can be named as `sec_q`, `content/sec_q.md` or anything between. */
const named = argv.filter(a => !a.startsWith('--'));
const FOCUS = named.length ? path.basename(named[0]).replace(/\.md$/, '') : '';

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

function anchorsOf(src) {
  const found = new Map();                      // anchor -> item id
  let fenced = false;
  for (const raw of src.replace(/\r/g, '').split('\n')) {
    if (/^```/.test(raw)) { fenced = !fenced; continue; }
    if (fenced || !/^-\s+/.test(raw)) continue;
    let body = raw.replace(/^-\s+/, '');
    const box = /^(\[[ x~?]\])\s+/.exec(body);
    if (box) body = body.slice(box[0].length);
    const cut = body.indexOf(' — ');
    if (cut <= 0) continue;
    const id = body.slice(0, cut);
    if (!ID.test(id) || !/^[A-Za-z]/.test(id)) continue;
    found.set(id.toLowerCase().replace(/\./g, ''), id);
  }
  return found;
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
files.forEach(f => anchors.set(f.replace(/\.md$/, ''),
  anchorsOf(fs.readFileSync(path.join(CONTENT, f), 'utf8'))));

if (FOCUS && !anchors.has(FOCUS)) {
  console.error('no such sheet: ' + FOCUS + '\nsheets: ' +
    [...anchors.keys()].sort().join(', '));
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
  /* `--broken` narrows the two lists the same way it narrows the whole run. */
  const only = flags.has('--broken') ? l => l.filter(r => r.status !== 'ok') : l => l;
  const out = only(shown.filter(r => r.from === FOCUS));
  const inn = only(shown.filter(r => r.to === FOCUS && r.from !== FOCUS));
  const self = only(shown.filter(r => r.from === FOCUS && r.to === FOCUS));
  const reach = l => new Set(l.map(r => r.from === FOCUS ? r.to : r.from)).size;
  const n = (c, w) => c + ' ' + w + (c === 1 ? '' : 's');

  console.log('\n' + FOCUS + '   ' + sheetName(FOCUS));

  console.log('\nOUT — ' + n(out.length, 'link') + ' to ' + n(reach(out), 'sheet'));
  out.forEach(r => console.log('  ' + pad('L' + r.line, 7) + '-> ' + describe(r) +
    '   \u201c' + r.text + '\u201d'));

  console.log('\nIN — ' + n(inn.length, 'link') + ' from ' + n(reach(inn), 'sheet'));
  inn.sort((a, b) => a.from.localeCompare(b.from) || a.line - b.line)
    .forEach(r => console.log('  ' + pad('content/' + r.file + ':' + r.line, 24) +
      pad(r.anchor ? '#' + r.anchor : '', 8) +
      pad(r.item ? 'item ' + r.item : '', 12) + '\u201c' + r.text + '\u201d'));

  console.log('\n' + out.length + ' out · ' + inn.length + ' in' +
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
        pad(r.anchor ? '#' + r.anchor : '', 8) + r.text));
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
