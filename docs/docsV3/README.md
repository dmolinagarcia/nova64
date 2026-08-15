# docsV3 — the synthesis document, in Markdown

Same document as `docsV2`, same stylesheet, same look: only the source changed.
Each sheet is now a `.md` file under `content/`, and `index.html` turns it into
the page at load time with `md.js` (renderer) and `app.js` (shell and router).
Nothing in `docsV2` was touched — this is a parallel copy.

```
index.html          the only page; shell + two scripts
md.js               markdown → HTML for the dialect below
app.js              sidebar, masthead, sheet index, pager, title block, routing
manifest.json       the sheet list: letter, number, nav title, index title, figure
style.css           docsV2's stylesheet, plus three rules docsV2 carried inline
content/*.md        the prose — one file per sheet
figures/*.svg       the seven diagrams, one file each
```

## Running it

The sheets are read with `fetch()`, which browsers refuse over `file://`, so
open it through a server — any server:

```
python3 -m http.server -d docs/docsV3
```

then <http://localhost:8000/>. On GitHub Pages it works as-is: the `.md` files
carry no YAML front matter, so Jekyll copies them through untouched.

## Routes

`#/` is the sheet index, `#/sec_e` a sheet, `#/sec_e/e4` a sheet scrolled to one
of its items. Inside the markdown you write the target the way docsV2 wrote
filenames — `[sheet E](sec_e)`, `[F.4](sec_f#f4)`, `[Sheet index](index)` — and
the renderer turns it into the route.

## The dialect

A sheet file opens with its title and its aim, and everything after that is
content:

```markdown
# Vision and philosophy
> what it is · what for · under what rules

Project framework: what is being built, and what principles govern it.
```

`#` becomes the sheet heading, `>` the small uppercase aim beside it. The sheet
letter and number are not repeated here — they live in `manifest.json`.

### Blocks

| You write | You get |
|---|---|
| a plain paragraph | `p.lead` — the standing text of a sheet |
| `## Register map — base block $FF:0000` | sub-heading: bold up to the ` — `, plain after it |
| `- A.1 — text` | a numbered item: `A.1` in mint, text beside it, anchored at `#a1` |
| `- [ ] E0.1 — text` | the same, with a build checkbox in front |
| `!!! APPLE II MILESTONE — …` | the gold milestone banner |
| `\| Term \| Meaning \|` + `\|---\|---\|` | `table.simple` |
| `![Fig. 1 — caption](figures/f.svg)` | the figure, its SVG inlined so the stylesheet reaches it |
| `LEGEND: …` after a figure | the small trace legend under the caption |
| `INDEX` | the sheet-index table, built from `manifest.json` |
| `TAGS:` + `- [g] …` list | the masthead tag row (index page only) |

Checkbox states: `[ ]` pending · `[x]` done · `[~]` in progress · `[?]` optional.
The item id is free text — `A.1`, `E0.1`, `D07`, `+` — and its anchor is that id
lowercased with the dots dropped, which is what every cross-reference points at.

### Lines that attach to the block above

```markdown
- E1.2 — **Cross-review before layout.**
  TEST: clean ERC, plus a manual pin-by-pin pass of every TQFP.
- E0.6 — **Freeze the block diagram** as the schematic's reference.
  NOTE: Currently at REV B and well behind this document (→ [A.7](sec_a#a7)).
```

`TEST:` renders as the gold *TEST ▸* line, `NOTE:` as the dimmed aside. Both
also work under a paragraph or a `##` sub-heading.

### Inline

| You write | You get |
|---|---|
| `**bold**` · `*italic*` · `` `code` `` | `<b>` · `<i>` · `<code>` |
| `[text](sec_q#q8)` | a link, rewritten to the route `#/sec_q/q8` |
| `[[open]]` | a mint chip |
| `[[!blocking]]` | a gold chip |
| `((an aside))` | an inline `.note`, for when it sits mid-sentence |
| raw HTML | passed through — the escape hatch, used once, for the legend |

## Adding a sheet

Write `content/sec_s.md`, add its row to `manifest.json` (`file`, `letter`,
`num`, `nav` for the sidebar, `index` for the index table, `fig` for the index's
right-hand column). Sidebar, index, pager and title block follow from that; no
HTML to edit.

The glossary is deliberately the **last** sheet and deliberately lettered **Z**,
in `content/sec_z.md`: content sheets run A, B, C… and a new one is appended at
the end of that run, taking the next free letter, without the glossary having to
move or anything having to be renumbered. Letters and file names are kept in
step — sheet R lives in `sec_r.md` — because every cross-reference in the prose
points at the file name.
