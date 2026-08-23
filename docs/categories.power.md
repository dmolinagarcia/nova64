---
layout: page
permalink: /categories/power
title: Category Power
---

Entries filed under **power**. [All categories]({{ '/categories/' | relative_url }}).

<div class="idx" markdown="0">
  <div class="cap">POWER</div>
  <table>
    {%- for post in site.categories.power -%}
      <tr>
        <td class="de">P</td>
        <td><a href="{{ post.url | relative_url }}">{{ post.title | escape }}</a></td>
        <td class="fg">{{ post.date | date: "%b %-d, %Y" }}</td>
      </tr>
    {%- else -%}
      <tr><td>No entries in this category yet.</td></tr>
    {%- endfor -%}
  </table>
</div>
