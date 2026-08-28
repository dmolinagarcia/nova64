---
layout: page
title: Tracks
permalink: /tracks/
redirect_from: /categories/
menu: main
---

The diary runs on two axes. A **track** says which part of the machine an entry
is about — one per entry, and they are meant to be few enough, and separate
enough, that filing an entry is never a judgement call. A
[series]({{ '/series/' | relative_url }}) says in what order a run of entries
is read. Most entries have a track and no series.

<div class="idx" markdown="0">
  <div class="cap">BY TRACK</div>
  <table>
    {%- for t in site.data.tracks -%}
    {%- assign entries = site.categories[t.id] -%}
    <tr>
      <td class="de">{{ t.de }}</td>
      <td>
        <a href="{{ '/tracks/' | append: t.id | append: '/' | relative_url }}">{{ t.name }}</a>
        <div class="ex">{{ t.blurb }}</div>
      </td>
      <td class="fg">{{ entries | size }}</td>
    </tr>
    {%- endfor -%}
  </table>
</div>
