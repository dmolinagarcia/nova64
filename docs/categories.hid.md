---
layout: page
permalink: /categories/hid
title: Category HID
---

Entries filed under **hid**. [All categories]({{ '/categories/' | relative_url }}).

<div class="idx" markdown="0">
  <div class="cap">HID</div>
  <table>
    {%- for post in site.categories.hid -%}
      <tr>
        <td class="de">H</td>
        <td><a href="{{ post.url | relative_url }}">{{ post.title | escape }}</a></td>
        <td class="fg">{{ post.date | date: "%b %-d, %Y" }}</td>
      </tr>
    {%- else -%}
      <tr><td>No entries in this category yet.</td></tr>
    {%- endfor -%}
  </table>
</div>
