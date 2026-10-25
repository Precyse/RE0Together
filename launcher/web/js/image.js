import { h } from './dom.js';

/** An <img> that hides itself when it fails to load, so the placeholder under it shows. */
export function createImage(className) {
  const img = h('img', { class: className, alt: '', draggable: 'false' });
  img.addEventListener('error', () => { img.hidden = true; });
  return img;
}

/** Points the image at `src`; no source hides it. */
export function setImage(img, src) {
  if (!src) {
    img.hidden = true;
    img.removeAttribute('src');
    return;
  }
  if (img.getAttribute('src') !== src) {
    img.hidden = false;
    img.setAttribute('src', src);
  }
}
