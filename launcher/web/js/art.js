import { h, setText, toggle } from './dom.js';

/** An <img> that hides itself while it has no source or fails to load (the placeholder behind it stays). `set(src)` writes the URL only when it changed. */
export function createImage(className = '') {
  const el = h('img', { class: className, alt: '', draggable: 'false', hidden: true });
  el.addEventListener('load', () => { el.hidden = false; });
  el.addEventListener('error', () => { el.hidden = true; });
  return {
    el,
    set(src) {
      if (!src) {
        el.hidden = true;
        el.removeAttribute('src');
      } else if (el.getAttribute('src') !== src) {
        el.hidden = true;
        el.setAttribute('src', src);
      }
    },
  };
}

/** A square avatar: the initial on the placeholder, the picture over it once it arrived. `host` uses the raised grey. */
export function createAvatar(className) {
  const initial = h('span', { class: 'avatar-initial' });
  const image = createImage();
  const el = h('div', { class: `avatar ${className}` }, initial, image.el);
  return {
    el,
    set(img, host = false) {
      setText(initial, img.initial);
      image.set(img.src);
      toggle(el, 'host', host);
    },
  };
}
