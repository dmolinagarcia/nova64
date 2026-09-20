#!/usr/bin/env node
/* noVa64 · docsV3 — review progress, counted in lines.
 *
 * A sheet sits in the AI part until a human has read it (A1.1), so the share of
 * lines outside AI is how much of the document has been reviewed. The part of
 * each sheet comes from manifest.json, as everywhere else — not from the file
 * name — and a file in content/ the manifest does not list (index.md) is left
 * out of the total and named at the end.
 *
 *   node tools/progress.js            lines per part, and the share outside AI
 *   node tools/progress.js --sheets   the same, with every sheet under its part
 *   node tools/progress.js --prose    count only prose: no blank lines, and no
 *                                     fenced listings (sc_w6 carries a kit of
 *                                     scripts that would otherwise dominate)
 */
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const CONTENT = path.join(ROOT, 'content');
const manifest = JSON.parse(fs.readFileSync(path.join(ROOT, 'manifest.json'), 'utf8'));

const flags = new Set(process.argv.slice(2));
const prose = flags.has('--prose');

/* Lines as `wc -l` counts them, plus an unterminated last one. With --prose,
   a fence opens or closes a listing, and nothing between the fences counts —
   the same ``` / ~~~ pairing md.js uses. */
function count(src) {
  const lines = src.replace(/\r/g, '').split('\n');
  if (lines[lines.length - 1] === '') lines.pop();
  if (!prose) return lines.length;
  let n = 0, fence = null;
  for (const l of lines) {
    const m = /^\s*(```+|~~~+)/.exec(l);
    if (fence) { if (m && m[1][0] === fence) fence = null; continue; }
    if (m) { fence = m[1][0]; continue; }
    if (l.trim()) n++;
  }
  return n;
}

const parts = manifest.parts.map(p => ({ ...p, lines: 0, sheets: [] }));
const byId = Object.fromEntries(parts.map(p => [p.id, p]));
const listed = new Set();

for (const s of manifest.sheets) {
  const file = path.join(CONTENT, s.file + '.md');
  listed.add(s.file + '.md');
  if (!fs.existsSync(file)) { console.error(`missing: content/${s.file}.md`); continue; }
  const n = count(fs.readFileSync(file, 'utf8'));
  const p = byId[s.part];
  if (!p) { console.error(`unknown part "${s.part}" for ${s.file}`); continue; }
  p.lines += n;
  p.sheets.push({ letter: s.letter, file: s.file, lines: n });
}

const total = parts.reduce((a, p) => a + p.lines, 0);
const ai = byId.ai ? byId.ai.lines : 0;
const pct = n => (total ? (100 * n / total).toFixed(1) : '0.0').padStart(5) + ' %';
const w = String(total).length;

console.log(`Lines per part${prose ? ' (prose only)' : ''}\n`);
for (const p of parts) {
  console.log(`  ${p.num} · ${p.name.padEnd(16)} ${String(p.lines).padStart(w)}  ${pct(p.lines)}`);
  if (flags.has('--sheets')) {
    for (const s of p.sheets) {
      console.log(`        ${s.letter.padEnd(4)} ${s.file.padEnd(14)} ${String(s.lines).padStart(w)}`);
    }
  }
}
console.log(`\n  ${'Total'.padEnd(20)} ${String(total).padStart(w)}`);
console.log(`  ${'Outside AI'.padEnd(20)} ${String(total - ai).padStart(w)}  ${pct(total - ai)}`);

const unlisted = fs.readdirSync(CONTENT).filter(f => f.endsWith('.md') && !listed.has(f));
if (unlisted.length) console.log(`\n  Not in manifest, not counted: ${unlisted.join(', ')}`);
