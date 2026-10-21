import { h } from './dom.js';
import { createImage } from './art.js';

const CLOSE_LABEL = 'Close';
const CLOSE_GLYPH = 14;

/** The art header of a popup: the game's hero, a fade into the panel, its logo bottom-left and the 40x40 close button. */
export function createPopupHeader(ctx) {
  const { send, icons } = ctx;
  const hero = createImage('fit-cover popup-hero');
  const logo = createImage('popup-logo');
  const close = h('button', { class: 'popup-close', type: 'button', 'aria-label': CLOSE_LABEL, on: { click: () => send('popup.close') } }, icons.icon('close', CLOSE_GLYPH));
  const el = h('div', { class: 'popup-header' }, hero.el, h('div', { class: 'popup-header-fade' }), logo.el, close);
  return {
    el,
    set(art) {
      hero.set(art.hero);
      logo.set(art.logo);
    },
  };
}
