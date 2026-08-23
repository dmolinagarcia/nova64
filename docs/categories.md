---
layout: page
permalink: /categories/
title: Categories
menu: main
---

The entries, grouped by the part of the machine they are about.

<div class="idx" markdown="0">
  <div class="cap">BY CATEGORY</div>
  <table>
    {%- for category in site.categories -%}
      {%- assign category_name = category | first -%}
      <tr class="grp"><td colspan="3"><a href="{{ '/categories/' | append: category_name | relative_url }}">{{ category_name }}</a></td></tr>
      {%- for post in site.categories[category_name] -%}
      <tr>
        <td class="de">{{ category_name | slice: 0 | upcase }}</td>
        <td><a href="{{ post.url | relative_url }}">{{ post.title | escape }}</a></td>
        <td class="fg">{{ post.date | date: "%b %-d, %Y" }}</td>
      </tr>
      {%- endfor -%}
    {%- endfor -%}
  </table>
</div>
