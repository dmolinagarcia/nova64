---
layout: page
permalink: /categories/architecture
title: Category Architecture
---


<div id="archives">
  <div class="archive-group">
    {% for post in site.categories.architecture %}
       <li>
          <span>{{ post.date | date_to_string }}</span> &nbsp; 
          <a href="/nova64{{ post.url }}">{{ post.title }}</a>
       </li>
    {% endfor %}
  </div>
</div>