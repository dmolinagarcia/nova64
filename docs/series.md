---
layout: page
title: Series
permalink: /series/
menu: main
---

A series is a run of entries meant to be read in order — the order the work
happened in, which is not always the order the entries were written. Each one
sits inside a [track]({{ '/tracks/' | relative_url }}).

<div class="idx" markdown="0">
  <div class="cap">SERIES</div>
  <table>
    {%- for s in site.data.series -%}
    {%- assign parts = site.posts | where: "series", s.id -%}
    {%- assign t = site.data.tracks | where: "id", s.track | first -%}
    <tr>
      <td class="de">{{ t.de | default: "··" }}</td>
      <td>
        <a href="{{ '/series/' | append: s.id | append: '/' | relative_url }}">{{ s.name }}</a>
        <div class="ex">{{ s.blurb }}</div>
      </td>
      <td class="fg">{{ parts | size }} part{% if parts.size != 1 %}s{% endif %}</td>
    </tr>
    {%- else -%}
    <tr><td>No series yet.</td></tr>
    {%- endfor -%}
  </table>
</div>
