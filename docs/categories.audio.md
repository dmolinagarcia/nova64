---
layout: page
permalink: /categories/audio
title: Category Audio
---

Entries filed under **audio**. [All categories]({{ '/categories/' | relative_url }}).

<div class="idx" markdown="0">
  <div class="cap">AUDIO</div>
  <table>
    {%- for post in site.categories.audio -%}
      <tr>
        <td class="de">A</td>
        <td><a href="{{ post.url | relative_url }}">{{ post.title | escape }}</a></td>
        <td class="fg">{{ post.date | date: "%b %-d, %Y" }}</td>
      </tr>
    {%- else -%}
      <tr><td>No entries in this category yet.</td></tr>
    {%- endfor -%}
  </table>
</div>
