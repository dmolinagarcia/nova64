---
layout: page
permalink: /categories/cpu
title: Category CPU
---

Entries filed under **cpu**. [All categories]({{ '/categories/' | relative_url }}).

<div class="idx" markdown="0">
  <div class="cap">CPU</div>
  <table>
    {%- for post in site.categories.cpu -%}
      <tr>
        <td class="de">C</td>
        <td><a href="{{ post.url | relative_url }}">{{ post.title | escape }}</a></td>
        <td class="fg">{{ post.date | date: "%b %-d, %Y" }}</td>
      </tr>
    {%- else -%}
      <tr><td>No entries in this category yet.</td></tr>
    {%- endfor -%}
  </table>
</div>
