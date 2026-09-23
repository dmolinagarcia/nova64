#!/usr/bin/env node
/* noVa64 · docsV3 — the sheet index, in plain text.
 *
 * The same three levels the document prints — part, area, sheet — in the same
 * reading order, on a terminal. It is the listing to read when the question is
 * "what is in here, and in what order", without a browser and without a PDF.
 *
 * The order is not rebuilt here: this runs the real `NovaShell.prepare` from
 * shell.js against the real manifest.json, which is what index.html, full.html
 * and tools/prerender.js all order themselves with. So it cannot drift from the
 * printed edition — if the two ever disagree, the bug is in the manifest.
 *
 * Two of the three levels carry an identifier, and they are the two the
 * manifest stores: a part its `num`, a sheet its `letter`, with the sheet's
 * position — the number ahead of the letter — derived from the reading order
 * exactly as `prepare` derives it for the printed page. An area carries none:
 * it is a run of consecutive sheets, not a registry entry, so there is nothing
 * to print but its name.
 *
 *   node tools/index.js            index titles, the ones the sheet index shows
 *   node tools/index.js --nav      the short sidebar titles instead
 *   node tools/index.js --files    with each sheet's content/<file>.md
 *   node tools/index.js --notes    with the parts' notes, wrapped
 */
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');

/* shell.js is browser code: an IIFE over `window` that defines functions and
   touches the DOM in none of them until called. One global is enough to load
   it, and `prepare` needs nothing else — it is pure manifest arithmetic. */
global.window = global;
require(path.join(ROOT, 'shell.js'));

/* `prepare` also resolves {figures} and {tables} in the masthead line, but only
   if the numbers are in hand — it must not require the artefact tools/numbers.js
   writes, since that tool calls prepare itself. Handing it over here is what
   keeps this listing's last line from reading `{figures} figures`. */
const numbering = path.join(ROOT, 'numbering.json');
if (fs.existsSync(numbering)) {
  global.NovaNumbers = JSON.parse(fs.readFileSync(numbering, 'utf8'));
}

const flags = new Set(process.argv.slice(2));
const title = flags.has('--nav') ? 'nav' : 'index';

const M = window.NovaShell.prepare(
  JSON.parse(fs.readFileSync(path.join(ROOT, 'manifest.json'), 'utf8')));

/* The masthead line, already filled in by prepare: {parts}, {areas}, {sheets}
   resolved against the order just built. */
const f = M.footer || {};
console.log(`${M.title} · ${M.documentName}`);
console.log([f.version, f.date, f.referenceIndex].filter(Boolean).join(' · ') + '\n');

const wNum = Math.max(...M.sheets.map(s => s.num.length));
const wLet = Math.max(...M.sheets.map(s => s.letter.length));

/* A part's note runs to a sentence or two, so it gets its own lines under the
   part rather than a column that pushes everything else off the screen. */
function wrap(text, width) {
  const out = [];
  let line = '';
  for (const word of String(text).split(/\s+/)) {
    if (line && (line + ' ' + word).length > width) { out.push(line); line = word; }
    else line = line ? line + ' ' + word : word;
  }
  if (line) out.push(line);
  return out;
}

for (const g of M.groups) {
  console.log(`${g.part.num.padStart(2)}  ${g.part.name}`);
  if (flags.has('--notes') && g.part.note) {
    for (const l of wrap(g.part.note, 68)) console.log(`      ${l}`);
  }
  if (!g.sheets.length) { console.log('      — no sheets yet\n'); continue; }

  for (const a of g.areas) {
    if (a.name) console.log(`    ${a.name}`);
    for (const s of a.sheets) {
      console.log(`      ${s.num.padStart(wNum)}  ${s.letter.padEnd(wLet)}  ${s[title]}` +
                  (flags.has('--files') ? `   content/${s.file}.md` : ''));
    }
  }
  console.log('');
}
