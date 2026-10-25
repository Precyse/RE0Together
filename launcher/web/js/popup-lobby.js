import { h, setText, keyed, toggle } from './dom.js';
import { createAvatar } from './art.js';
import { createPopupHeader } from './popup-header.js';
import { pingText } from './format.js';

const PLAYERS_LABEL = 'Players';
const OPEN_LABEL = 'Open';
const HOST_LABEL = 'Host';
const FACT_LABELS = ['Ping', 'Mod', 'Game', 'Network', 'Playing'];
const GLYPH = 12;
const STATE_CLASS = { ingame: 'tint-neutral', progress: 'tint-amber', problem: 'tint-red' };

/** A check or cross glyph plus a value: `set(ok, text)`. */
function createVerdict(icons) {
  const check = icons.icon('check', GLYPH);
  const cross = icons.icon('cross', GLYPH);
  const text = h('span', { class: 'mono' });
  const el = h('span', { class: 'fact-verdict' }, check, cross, text);
  return {
    el,
    set(ok, value) {
      check.style.display = ok ? '' : 'none';
      cross.style.display = ok ? 'none' : '';
      setText(text, value);
    },
  };
}

/** The lobby popup (C2_Detail): art header, the roster tinted by state with dashed open seats, the fact grid and the action. */
export function create(ctx) {
  const { send, motion, icons } = ctx;
  const header = createPopupHeader(ctx);
  const count = h('span', { class: 'popup-count' });
  const roster = h('div', { class: 'roster' });
  const openSeats = h('div', { class: 'roster' });
  const ping = h('span', { class: 'mono' });
  const mod = createVerdict(icons);
  const game = createVerdict(icons);
  const network = h('span');
  const playing = h('span', { class: 'mono' });
  const facts = h('div', { class: 'facts' });
  FACT_LABELS.forEach((label, index) => facts.append(h('span', { class: 'fact-label', text: label }), [ping, mod.el, game.el, network, playing][index]));
  const action = h('button', { class: 'btn popup-action', type: 'button' });
  const left = h('div', { class: 'popup-left' }, h('div', { class: 'caption popup-players' }, PLAYERS_LABEL, count), roster, openSeats);
  const right = h('div', { class: 'popup-right' }, facts, h('div', { class: 'popup-spacer' }), action);
  const el = h('section', { class: 'popup popup-lobby', role: 'dialog' }, header.el, h('div', { class: 'popup-body' }, left, right));
  let key = null;

  action.addEventListener('click', () => send('lobby.action', { lobbyId: key }));

  function createSeat(entry) {
    const avatar = createAvatar('seat-avatar');
    const name = h('span', { class: 'seat-name' });
    const tag = h('span', { class: 'seat-tag', text: HOST_LABEL });
    const seat = h('div', { class: 'seat' }, avatar.el, name, tag);
    seat.parts = { avatar, name, tag };
    return seat;
  }

  function updateSeat(seat, entry) {
    const { avatar, name, tag } = seat.parts;
    avatar.set(entry.avatar, entry.host);
    setText(name, entry.name);
    toggle(name, 'host', entry.host);
    tag.hidden = !entry.host;
    seat.className = `seat ${STATE_CLASS[entry.state] ?? ''}`;
  }

  function update(popup) {
    key = popup.key;
    header.set(popup.art);
    el.setAttribute('aria-label', popup.caption);
    motion.reel(count, `${popup.roster.length}/${popup.roster.length + popup.openSeats}`);
    keyed(roster, popup.roster, (entry) => entry.key, createSeat, updateSeat);
    if (openSeats.children.length !== popup.openSeats) {
      openSeats.replaceChildren(...Array.from({ length: popup.openSeats }, () => h('div', { class: 'seat seat-open' }, h('span', { class: 'seat-box' }), h('span', { class: 'seat-name', text: OPEN_LABEL }))));
    }
    setText(ping, pingText(popup.ping));
    mod.set(popup.mod.ok, popup.mod.text);
    game.set(popup.game.ok, popup.game.text);
    setText(network, popup.network);
    motion.rollDigits(playing, popup.playing);
    setText(action, popup.action.label);
    action.disabled = !popup.action.enabled;
    toggle(action, 'btn-primary', popup.action.primary);
    toggle(action, 'btn-ghost', !popup.action.primary);
  }

  return { el, update };
}
