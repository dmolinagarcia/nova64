---
layout: page
title: Blog Archive
permalink: /archive
menu: main
---

Every entry, newest first.

<div class="idx" markdown="0">
  <div class="cap">ALL ENTRIES</div>
  <table>
    {%- assign last_year = "" -%}
    {%- for post in site.posts -%}
      {%- assign year = post.date | date: "%Y" -%}
      {%- if year != last_year -%}
      <tr class="grp"><td colspan="3">{{ year }}</td></tr>
      {%- assign last_year = year -%}
      {%- endif -%}
      <tr>
        {%- assign cat = post.categories | first | default: "general" -%}
        <td class="de">{% include de.html track=cat %}</td>
        <td><a href="{{ post.url | relative_url }}">{{ post.title | escape }}</a></td>
        <td class="fg">{{ post.date | date: "%b %-d, %Y" }}</td>
      </tr>
    {%- endfor -%}
  </table>
</div>
