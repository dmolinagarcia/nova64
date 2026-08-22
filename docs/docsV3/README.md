# docsV3 — the synthesis document, in Markdown

Same document as `docsV2`, same stylesheet, same look: only the source changed.
Each sheet is now a `.md` file under `content/`, and `index.html` turns it into
the page at load time with `md.js` (renderer) and `app.js` (shell and router).
Nothing in `docsV2` was touched — this is a parallel copy.

```
index.html          the paged edition: one sheet at a time
full.html           every sheet in one scroll, for printing
theme.js            the light/dark switch, applied before the first paint
md.js               markdown → HTML for the dialect below
shell.js            figure inlining and the failure state, shared by both pages
app.js              sidebar, masthead, sheet index, pager, title block, routing
full.js             the same pieces, assembled as one continuous document
manifest.json       the sheet list: letter, number, area, nav and index titles, figure
style.css           docsV2's stylesheet, plus the few rules these two pages add
content/*.md        the prose — one file per sheet
figures/*.svg       the diagrams, one file each
```

## Areas

The sheets are grouped into ten areas — Introduction, Architecture, Power,
Embedded control and boot, CPU and system, Memory and storage, Video + audio,
Software, Project and build, Appendix — and the grouping lives entirely in
`manifest.json`: **the order of the array is the reading order**, and a run of
consecutive sheets carrying the same `area` gets one heading in the sidebar, one
band in the sheet index, and, on `full.html`, one rule across the page before
the first sheet of the area. Nothing else knows areas exist; there is no second
structure to keep in step, and moving a sheet between areas means moving its row
and fixing the `num` fields.

**The letters do not follow the areas, and are never renumbered.** A letter is a
sheet's identity — the file name (`sec_r.md`), the route (`#/sec_r`), and every
cross-reference in the prose — while `num` is its position in the reading order.
The two stopped agreeing when the areas were introduced, and that is the point:
reordering the document costs a manifest edit instead of several hundred link
rewrites.

## Running it

The sheets are read with `fetch()`, which browsers refuse over `file://`, so
open it through a server — any server:

```
python3 -m http.server -d docs/docsV3
```

then <http://localhost:8000/>. On GitHub Pages it works as-is: the `.md` files
carry no YAML front matter, so Jekyll copies them through untouched.

## Printing

`full.html` is the same content with the sheets chained one after another —
masthead, sheet index, then the ten areas in order — and it is what to send to
the printer or save as PDF. It prints in parts: the masthead is the cover and has the
first page to itself, page two is left blank behind it, the sheet index takes
page three, and every area opens on a fresh page, while within an area the
sheets run on rather than each claiming one. The cover's drop — how far down the
page the title block sits — is `body.full .main{ padding-top }` in the print
block, in millimetres. The print stylesheet drops the sidebar and the navigation the way it always
did. The sheet index becomes a
working table of contents, because on that page every cross-reference is an
in-page jump: ids are prefixed with their sheet (`#sec_p-e2`) so that E.2 in
sheet E and E2 in sheet P stop colliding. The sidebar of the paged edition links
to it at the bottom of the sheet list.

## Light and dark

The button above the masthead switches themes; the choice is remembered in
`localStorage`, and until one is made the page follows the system setting. The
palette is five custom properties on `:root` — ground, ink, panel, mint, gold —
and the light theme only redefines those, so nothing else in the stylesheet
knows a theme exists.

**It stops at the screen.** The light theme lives inside `@media screen`, and
the print block restates every colour itself, as it already did for docsV2's
dark ground. Printing from either theme gives the same black-on-white page.
The one thing a new colour needs is care in two places: shapes inside the
figures that carry their colour as an attribute rather than a class are listed
explicitly in both the print block and the light theme.

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

Give it an `area` too, and put its row next to the other sheets of that area —
the array's order is what the reader sees. Adding a sheet in the middle shifts
the `num` of everything after it, and nothing else.

The appendix — glossary and index of figures — is deliberately the **last**
sheet and deliberately lettered **Z**, in `content/sec_z.md`: content sheets run
A, B, C… and a new one takes the next free letter of that run, without the
appendix having to move or any letter having to be reissued. Letters and file
names are kept in step — sheet R lives in `sec_r.md` — because every
cross-reference in the prose points at the file name.
