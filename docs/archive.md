---
layout: page
title: Blog Archive
permalink: /archive
menu: main
---

  <ul>
    {% for post in site.posts %}
      <li>
         <a href="/nova64{{ post.url }}">{{ post.date | date: "%B %Y" }} - {{ post.title }}</a>
      </li>
    {% endfor %}
  </ul>


