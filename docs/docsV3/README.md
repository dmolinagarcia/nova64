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
manifest.json       the parts registry, and the sheet list: letter, part, area, nav
                    and index titles, figure
tools/prerender.js  assembles the printable edition ahead of time, for a PDF
                    formatter — page numbers live there, see Printing
tools/links.js      the cross-reference listing: source sheet -> linked section,
                    per sheet or whole-document, and the broken links, see Routes
style.css           docsV2's stylesheet, plus the few rules these two pages add
content/*.md        the prose — one file per sheet
figures/*.svg       the diagrams, one file each
```

## Parts, areas, sheets

Three levels, and only `manifest.json` knows about any of them.

**A part** comes from the `parts` array, which is a *registry* rather than a run:
it fixes the order of the parts and their display names, and it is why a part
holding no sheets still appears in the sidebar and the index. Each sheet names
one with a `part` field.

```json
"parts": [
  { "id": "hardware", "num": "2", "name": "Hardware", "note": "" }
],
"sheets": [
  { "file": "sec_c", "letter": "C", "part": "ai", "area": "Power", … }
]
```

**An area** is still a run: consecutive sheets *within a part* carrying the same
`area` get one heading in the sidebar, one band in the sheet index, and one rule
across the page on `full.html`. A part gets the same treatment one level up — a
framed bar, and its own page in print, with its first area following on it.

**The order is: parts as the registry lists them, sheets as the array lists them
within each part.** So moving a sheet to another part is a **one-field edit** —
change its `part`, and the sidebar, the index, the numbering and the printed
page breaks all follow. Moving a sheet within its part still means moving its
row. A sheet naming a part that is not in the registry is not dropped: it lands
in a trailing "Unassigned" group that says so.

**Positions are derived, never stored.** There is no `num` field: `NovaShell.prepare`
resolves parts and areas into one reading order and numbers the sheets from it,
so inserting a sheet anywhere renumbers the rest by itself.

**The letters follow neither, and are never reissued.** A letter is a sheet's
identity — the file name (`sec_r.md`), the route (`#/sec_r`), and every
cross-reference in the prose — while its number is only its position. The two
stopped agreeing when the areas were introduced, and that is the point:
reordering the document costs a manifest edit instead of several hundred link
rewrites.

**Everything currently sits in the `ai` part**, which is where a sheet lives
until a human has read it; it moves to its real part when it has been reviewed.
Six of the seven parts are therefore empty, and that is the state of the review
rather than a hole in the design.

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
masthead, sheet index, then the parts and their areas in order — and it is what
to send to the printer or save as PDF. It prints in sections: the masthead is
the cover and has the first page to itself, page two is left blank behind it,
the sheet index takes page three, and every part and every area opens on a fresh
page, while within an area the sheets run on rather than each claiming one. The
cover's drop — how far down the page the title block sits — is
`body.full .main{ padding-top }` in the print block, in millimetres. The print
stylesheet drops the sidebar and the navigation the way it always did. The sheet
index becomes a working table of contents, because on that page every
cross-reference is an in-page jump: ids are prefixed with their sheet
(`#sec_p-e2`) so that E.2 in sheet E and E2 in sheet P stop colliding. The
sidebar of the paged edition links to it at the bottom of the sheet list.

### Page numbers

Two things the browser cannot do: a running page number in the bottom margin,
and the page a sheet opens on beside its row in the index. Both are CSS paged
media — `@page` margin boxes and `target-counter()` — and **no browser
implements either**, so Chrome and Firefox drop those rules and print exactly
what they printed before. They need a formatter, and a formatter needs a static
file, because `full.html` assembles itself in JavaScript and WeasyPrint and
Prince do not run any.

```
node tools/prerender.js          # -> print.html, beside index.html
weasyprint print.html nova64.pdf
```

`tools/prerender.js` does not reimplement anything: it runs the real `md.js`,
`shell.js` and `full.js` under Node against the real manifest and markdown,
captures what they would have written into the page, inlines the figures the way
`shell.js` does in the browser, and writes one static file that links the same
stylesheet. Change a sheet or the manifest and it follows. `print.html` is a
build artefact and is not committed.

Any formatter that implements paged media will do — `npx @vivliostyle/cli build
print.html -o nova64.pdf` is the other open one, and Prince is the best output if
its licence suits. WeasyPrint prints a column of warnings about screen-only rules
it cannot parse (`::-webkit-scrollbar`, `position:sticky`, `fill` inside the SVG
rules); all of them are screen or SVG presentation and none affects the PDF.

**Only the sheet rows carry a number.** A part or an area opens on the sheet
immediately below it, so numbering those rows would print the same number twice.
The cover and the blank verso behind it carry none either, which keeps every
printed folio equal to the page number a PDF reader shows.

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

`tools/links.js` lists them. It reads every sheet the way `md.js` does, resolves
each target against `manifest.json` and the items the target sheet actually
defines, and prints one line per link — source file, then the section it points
at:

```
node tools/links.js              # the listing, grouped by source sheet
node tools/links.js --reverse    # the same links, grouped by target
node tools/links.js --broken     # only the ones that resolve to nothing
node tools/links.js --csv        # source,line,text,target,anchor,item,status
node tools/links.js --summary    # links out and in per sheet, and the orphans
```

Name a sheet — as `sec_q` or as `content/sec_q.md` — and the listing narrows to
it: what it links out to, then what links in, with the item on each end. That is
what to run before moving a sheet or rewriting one of its items, since the IN
list is exactly what would have to be corrected.

```
node tools/links.js sec_q             # both directions for sheet Q
node tools/links.js sec_q --broken    # only the broken ones, either way
node tools/links.js sec_q --csv       # the same rows, as CSV
```

A link to a sheet that does not exist, or to an item id no sheet defines, is
reported as broken and the exit status is 1, so it also stands as a check.

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
`part`, `area`, `nav` for the sidebar, `index` for the index table, `fig` for
the index's right-hand column). Sidebar, index, pager, numbering and title block
follow from that; no HTML to edit.

Put its row next to the other sheets of the same area **inside its part** — the
array's order is what the reader sees within a part. There is nothing to
renumber: positions are derived from the resolved order.

A new sheet starts in the `ai` part unless it was written by a human, in which
case it goes straight to the part it belongs to.

The appendix is deliberately the **last** thing in the document and deliberately
lettered **Z**, and it is two sheets: the glossary in `content/sec_z1.md` and the
index of figures in `content/sec_z2.md`. Content sheets run A, B, C… and a new
one takes the next free letter of that run, without the appendix having to move
or any letter having to be reissued — which is also why splitting the appendix
took a suffix rather than the next free letter. Letters and file names are kept
in step — sheet R lives in `sec_r.md`, appendix sheet Z2 in `sec_z2.md` —
because every cross-reference in the prose points at the file name.

**A numeric suffix means one subject too large for one sheet**, and there are two
such runs. `Z1`/`Z2` is the appendix. `Y1`/`Y2`/`Y3` is the filesystem stack —
the VFS layer, NVFS and ext2 — which is one subject three ways rather than three
subjects, and which took a suffix for the same reason the appendix did rather
than because the alphabet had run short. Item ids inside a suffixed sheet carry
the whole letter: `Y2.15` anchors at `#y215`, and cross-references are written
`[Y2.15](sec_y2#y215)`.
