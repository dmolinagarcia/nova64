---
layout: page
title: Blog Archive
---

{% for category in site.categories %}
  <h3>{{ category[0] }}</h3>
  <ul>
    {% for post in category[1] %}
      <li>
         <a href="/nova64{{ post.url }}">{{ post.date | date: "%B %Y" }} - {{ post.title }}</a>
      </li>
    {% endfor %}
  </ul>
{% endfor %}

{% for category in site.categories %}
    <li><a href="{{category.url}}"><strong>{{category|first}}</strong></a></li>
{% endfor %}