/* noVa64 — light/dark switch, screen only. Shared with docsV3: same key,
 * same five custom properties, so a choice made on either side holds on both.
 *
 * Loaded from <head> so the choice is on the document before anything paints;
 * the button is wired up once the page exists. Printing is untouched: the
 * light palette lives inside @media screen, and the print rules restate every
 * colour themselves.
 */
(function () {
  'use strict';

  var KEY = 'nova64-theme';
  var root = document.documentElement;

  function stored() {
    try { return localStorage.getItem(KEY); } catch (e) { return null; }
  }

  function preferred() {
    return window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches
      ? 'light' : 'dark';
  }

  function apply(theme) {
    root.setAttribute('data-theme', theme);
    var b = document.getElementById('theme');
    if (b) {
      /* the button names the theme it switches to, not the one in force */
      b.textContent = theme === 'dark' ? 'Light' : 'Dark';
      b.setAttribute('aria-label', 'Switch to ' + (theme === 'dark' ? 'light' : 'dark') + ' theme');
    }
  }

  apply(stored() || preferred());

  document.addEventListener('DOMContentLoaded', function () {
    apply(root.getAttribute('data-theme'));          // label the button

    var b = document.getElementById('theme');
    if (b) b.addEventListener('click', function () {
      var next = root.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
      try { localStorage.setItem(KEY, next); } catch (e) { /* private mode: this session only */ }
      apply(next);
    });
  });

  /* With no choice of their own, follow the system as it changes. */
  if (window.matchMedia) {
    var mql = window.matchMedia('(prefers-color-scheme: light)');
    var follow = function () { if (!stored()) apply(preferred()); };
    if (mql.addEventListener) mql.addEventListener('change', follow);
    else if (mql.addListener) mql.addListener(follow);
  }
})();
