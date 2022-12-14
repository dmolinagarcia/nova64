---
layout: page
permalink: /categories/video
title: Category Video
---


<div id="archives">
  <div class="archive-group">
    {% for post in site.categories.Video %}
       <li>
          <span>{{ post.date | date_to_string }}</span> &nbsp; 
          <a href="{{ post.url }}">{{ post.title }}</a>
       </li>
    {% endfor %}
  </div>
</div>