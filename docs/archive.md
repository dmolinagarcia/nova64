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

<ul>
  {% for category in site.categories %}
    {% capture category_name %}{{ category | first }}{% endcapture %}
      <li><a href="{{site.baseurl}}/{{category_name}}">{{category_name}}</a></li>
  {% endfor %}
</ul>