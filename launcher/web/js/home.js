import { h, setText } from './dom.js';
import { createImage } from './art.js';
import { createRail } from './rail.js';
import { createFriends } from './friends.js';
import { createCodeField } from './codefield.js';
import { createNotice } from './notice.js';
import { createHeroLayout } from './hero-layout.js';

const CHARACTERS_LABEL = 'Characters';
const OPTIONS_LABEL = 'Options';
const ICON_SIZE = 20;

/** Home (Play tab): the rail, the 296 px hero with the action row and notice row, then FRIENDS PLAYING. */
export function create(ctx) {
  const { send, icons } = ctx;
  const rail = createRail(ctx);
  const heroArt = createImage('fit-cover hero-art');
  const logo = createImage('hero-logo');
  const host = h('button', { class: 'btn btn-primary hero-host', type: 'button', on: { click: () => send('host') } });
  const mod = h('button', { class: 'btn btn-over-art hero-mod', type: 'button', hidden: true });
  const options = h('button', { class: 'btn-icon hero-icon', type: 'button', 'aria-label': OPTIONS_LABEL, on: { click: () => send('popup.options') } }, icons.icon('cog', ICON_SIZE));
  const characters = h('button', {
    class: 'btn btn-over-art hero-characters', type: 'button', on: { click: () => send('view.open', { view: 'select' }) },
  }, icons.icon('person', ICON_SIZE), h('span', { text: CHARACTERS_LABEL }));
  const backdrop = h('div', { class: 'hero-backdrop' }, heroArt.el, h('div', { class: 'hero-shade' }));
  const fade = h('div', { class: 'hero-fade' });
  const code = createCodeField(ctx);
  const row = h('div', { class: 'hero-row' }, host, code.el, options);
  const noticeSlot = h('div', { class: 'hero-notice' });
  const dock = h('div', { class: 'hero-dock' }, row, noticeSlot);
  const hero = h('section', { class: 'hero' }, fade, logo.el, h('div', { class: 'hero-corner' }, mod, characters), dock);
  const pane = h('div', { class: 'home-pane' });
  let layout = null;

  /** Whether the hero has the pane to itself (nothing below it) and whether its notice row is drawn: the two things its height follows. */
  function syncLayout() {
    pane.toggleAttribute('data-full', friends.el.hidden);
    pane.toggleAttribute('data-notice', !notice.el.hidden);
  }
  const changeLayout = (apply) => layout.run(() => {
    apply();
    syncLayout();
  });
  const friends = createFriends(ctx, changeLayout);
  const notice = createNotice(ctx, changeLayout);
  noticeSlot.append(notice.el);
  pane.append(backdrop, hero, friends.el);
  layout = createHeroLayout({ pane, hero, backdrop, movers: [fade, dock, friends.el] });
  syncLayout();
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
    layout.arm();
  }

  return {
    el,
    update,
    event(name) {
      if (name === 'codeRefused') code.refuse();
    },
  };
}
