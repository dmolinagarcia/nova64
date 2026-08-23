---
layout: page
permalink: /categories/video
title: Category Video
---

Entries filed under **video**. [All categories]({{ '/categories/' | relative_url }}).

<div class="idx" markdown="0">
  <div class="cap">VIDEO</div>
  <table>
    {%- for post in site.categories.video -%}
      <tr>
        <td class="de">V</td>
        <td><a href="{{ post.url | relative_url }}">{{ post.title | escape }}</a></td>
        <td class="fg">{{ post.date | date: "%b %-d, %Y" }}</td>
      </tr>
    {%- else -%}
      <tr><td>No entries in this category yet.</td></tr>
    {%- endfor -%}
  </table>
</div>
