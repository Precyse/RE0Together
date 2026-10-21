import { h, setText, keyed, wasCreated, toggle } from './dom.js';
import { createImage, createAvatar } from './art.js';
import { createSegments } from './segments.js';
import { createDropdown } from './dropdown.js';
import { pingText } from './format.js';

const FILTERS = [{ id: 'all', label: 'All' }, { id: 'friends', label: 'Friends' }, { id: 'home', label: 'Home server', disabled: true }];
const GAME_LABEL = 'Game';
const ALL_GAMES = 'All games';
const ALL_GAMES_VALUE = '';
const HEADERS = ['Game', 'Host', 'Players', 'Mod', 'Ping', ''];
const SKELETON_ROWS = 4;

/** Lobbies tab: filter, game dropdown, header, 56 px rows (skeleton while the first search runs, then a cross-fade with staggered rows). */
export function create(ctx) {
  const { send, motion } = ctx;
  const filter = createSegments({ items: FILTERS, variant: 'filter', label: 'Lobby filter', onSelect: (id) => send('lobbies.filter', { filter: id }) });
  const game = createDropdown({ label: GAME_LABEL, onSelect: (value) => send('lobbies.game', { gameId: value === ALL_GAMES_VALUE ? null : value }) });
  const toolbar = h('div', { class: 'lobby-toolbar' }, filter.el, h('span', { class: 'lobby-game-label', text: GAME_LABEL }), game.el);
  const header = h('div', { class: 'lobby-head caption' }, ...HEADERS.map((text) => h('span', { text })));
  const rows = h('div', { class: 'lobby-rows' });
  const skeleton = h('div', { class: 'lobby-rows lobby-skeleton', hidden: true }, ...Array.from({ length: SKELETON_ROWS }, () => h('div', { class: 'lobby-row skeleton-row m-skeleton-pulse' })));
  const el = h('section', { class: 'lobbies' }, toolbar, header, h('div', { class: 'lobby-list' }, skeleton, rows));
  let skeletonShown = false;
  let seen = false;

  function createRow(row) {
    const capsule = createImage('lobby-capsule');
    const avatar = createAvatar('lobby-avatar');
    const gameName = h('span', { class: 'lobby-game-name' });
    const hostName = h('span', { class: 'lobby-host-name' });
    const players = h('span', { class: 'lobby-players' });
    const mod = h('span', { class: 'mono lobby-mod' });
    const ping = h('span', { class: 'mono lobby-ping' });
    const action = h('button', { class: 'btn lobby-action', type: 'button', on: { click: (event) => { event.stopPropagation(); send('lobby.action', { lobbyId: row.key }); } } });
    const el = h('div', { class: 'lobby-row', role: 'button', tabindex: '0', on: { click: () => send('lobby.open', { lobbyId: row.key }) } },
      h('span', { class: 'lobby-cell' }, h('span', { class: 'lobby-capsule-box' }, capsule.el), gameName),
      h('span', { class: 'lobby-cell lobby-host' }, avatar.el, hostName), players, mod, ping, action);
    el.addEventListener('keydown', (event) => {
      if (event.target === el && event.key === 'Enter') send('lobby.open', { lobbyId: row.key });
    });
    el.parts = { capsule, avatar, gameName, hostName, players, mod, ping, action };
    return el;
  }

  function updateRow(el, row) {
    const { capsule, avatar, gameName, hostName, players, mod, ping, action } = el.parts;
    capsule.set(row.capsule);
    avatar.set(row.host.avatar);
    setText(gameName, row.gameName);
    setText(hostName, row.host.name);
    setText(players, row.players);
    setText(mod, row.mod.text);
    toggle(mod, 'differs', row.mod.differs);
    setText(ping, pingText(row.ping));
    setText(action, row.action.label);
    action.disabled = !row.action.enabled;
    toggle(action, 'btn-primary', row.action.primary);
    toggle(action, 'btn-ghost', !row.action.primary);
  }

  function showSkeleton(on) {
    if (on === skeletonShown) return;
    skeletonShown = on;
    if (on) {
      skeleton.hidden = false;
      motion.clear(skeleton, 'skeleton-out');
    } else {
      motion.play(skeleton, 'skeleton-out', { keep: true }).then(() => {
        if (skeletonShown) return;
        skeleton.hidden = true;
        motion.clear(skeleton, 'skeleton-out');
      });
    }
  }

  return {
    el,
    update(state) {
      const { lobbies } = state;
      if (!lobbies) return;
      filter.setActive(lobbies.filter);
      game.set({ items: [{ value: ALL_GAMES_VALUE, label: ALL_GAMES }, ...lobbies.games.map((g) => ({ value: g.id, label: g.name }))], value: lobbies.gameId ?? ALL_GAMES_VALUE });
      showSkeleton(lobbies.searching && lobbies.rows.length === 0);
      keyed(rows, lobbies.rows, (row) => row.key, createRow, updateRow);
      const arrived = [...rows.children].filter((row) => wasCreated(row));
      if (seen) arrived.forEach((row, index) => motion.play(row, 'content-in', { index }));
      seen = true;
    },
  };
}
