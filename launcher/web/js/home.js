import { h, setText, setAttr } from './dom.js';
import { createImage } from './art.js';
import { createRail } from './rail.js';
import { createFriends } from './friends.js';
import { createCodeField } from './codefield.js';
import { createNotice } from './notice.js';

const CHARACTERS_LABEL = 'Characters';
const OPTIONS_LABEL = 'Options';
const ICON_SIZE = 20;

/** Home (Play tab): the rail, the 296 px hero with the action row and notice row, then FRIENDS PLAYING. */
export function create(ctx) {
  const { send, icons } = ctx;
  const rail = createRail(ctx);
  const friends = createFriends(ctx);
  const code = createCodeField(ctx);
  const notice = createNotice(ctx);
  const heroArt = createImage('fit-cover hero-art');
  const logo = createImage('hero-logo');
  const host = h('button', { class: 'btn btn-primary hero-host', type: 'button', on: { click: () => send('host') } });
  const mod = h('button', { class: 'btn btn-over-art hero-mod', type: 'button', hidden: true });
  const options = h('button', { class: 'btn-icon hero-icon', type: 'button', 'aria-label': OPTIONS_LABEL, on: { click: () => send('popup.options') } }, icons.icon('cog', ICON_SIZE));
  const characters = h('button', {
    class: 'btn-icon hero-icon', type: 'button', 'aria-label': CHARACTERS_LABEL, on: { click: () => send('view.open', { view: 'select' }) },
  }, icons.icon('person', ICON_SIZE));
  const row = h('div', { class: 'hero-row' }, host, mod, code.el, options, characters);
  const hero = h('section', { class: 't-resize hero' }, heroArt.el, h('div', { class: 'hero-shade' }), h('div', { class: 'hero-fade' }), logo.el, row, h('div', { class: 'hero-notice' }, notice.el));
  const pane = h('div', { class: 'home-pane' }, hero, friends.el);
  const el = h('section', { class: 'home' }, rail.el, pane);
  let gameId = null;

  mod.addEventListener('click', () => send('mod.install', { gameId }));

  function update(state) {
    const { home } = state;
    if (!home) return;
    gameId = state.games.find((game) => game.selected)?.id ?? null;
    rail.update(state);
    friends.update(state);
    heroArt.set(state.art.hero);
    logo.set(state.art.logo);
    setText(host, home.hostLabel);
    host.disabled = !home.hostEnabled;
    mod.hidden = !home.mod;
    if (home.mod) {
      setText(mod, home.mod.label);
      mod.disabled = !home.mod.enabled;
    }
    options.hidden = !home.hasOptions;
    options.classList.toggle('active', home.optionsOpen);
    characters.hidden = !home.hasCharacters;
    code.update(home);
    notice.update(home.notice);
    setAttr(hero, 'data-notice', home.notice ? '1' : null);
  }

  return {
    el,
    update,
    event(name) {
      if (name === 'codeRefused') code.refuse();
    },
  };
}
