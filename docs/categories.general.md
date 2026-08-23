---
layout: page
permalink: /categories/general
title: Category General
---

Entries filed under **general**. [All categories]({{ '/categories/' | relative_url }}).

<div class="idx" markdown="0">
  <div class="cap">GENERAL</div>
  <table>
    {%- for post in site.categories.general -%}
      <tr>
        <td class="de">G</td>
        <td><a href="{{ post.url | relative_url }}">{{ post.title | escape }}</a></td>
        <td class="fg">{{ post.date | date: "%b %-d, %Y" }}</td>
      </tr>
    {%- else -%}
      <tr><td>No entries in this category yet.</td></tr>
    {%- endfor -%}
  </table>
</div>
