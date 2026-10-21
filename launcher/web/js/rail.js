import { h, setText, setAttr, keyed, toggle } from './dom.js';
import { createImage } from './art.js';

const HEADER = 'Games';
const SELECTION_CHANNEL = '--rail-index';

/** The games rail: the selection fill slides between rows, hover fades, locked rows show their art dimmed, Up / Down move the selection. */
export function createRail(ctx) {
  const { send } = ctx;
  const selection = h('div', { class: 'rail-selection' });
  const rows = h('div', { class: 'rail-rows' });
  const list = h('div', { class: 'rail-list', role: 'listbox', 'aria-label': HEADER, tabindex: '0' }, selection, rows);
  const el = h('nav', { class: 'rail' }, h('div', { class: 'caption rail-head', text: HEADER }), list);
  let games = [];

  function createRow(game) {
    const art = createImage('rail-art-img');
    const name = h('span', { class: 'rail-name' });
    const sub = h('span', { class: 'rail-sub' });
    const line = h('span', { class: 'rail-line' });
    const row = h('button', { class: 'rail-row', type: 'button', role: 'option', tabindex: '-1', on: { click: () => send('game.select', { gameId: game.id }) } },
      h('span', { class: 'rail-art' }, art.el), h('span', { class: 'rail-text' }, name, sub, line));
    row.parts = { art, name, sub, line };
    return row;
  }

  function updateRow(row, game) {
    const { art, name, sub, line } = row.parts;
    art.set(game.capsule);
    setText(name, game.name);
    setText(sub, game.sub);
    setText(line, game.line);
    row.dataset.level = game.tone;
    toggle(row, 'locked', game.locked);
    toggle(row, 'selected', game.selected);
    row.disabled = game.locked;
    setAttr(row, 'aria-selected', String(game.selected));
  }

  list.addEventListener('keydown', (event) => {
    const step = event.key === 'ArrowDown' ? 1 : event.key === 'ArrowUp' ? -1 : 0;
    if (!step) return;
    event.preventDefault();
    const index = games.findIndex((game) => game.selected);
    const target = games[index + step];
    if (target && !target.locked) send('game.select', { gameId: target.id });
  });

  return {
    el,
    update(state) {
      games = state.games;
      keyed(rows, games, (game) => game.id, createRow, updateRow);
      const index = Math.max(0, games.findIndex((game) => game.selected));
      selection.hidden = !games.some((game) => game.selected);
      if (selection.style.getPropertyValue(SELECTION_CHANNEL) !== String(index)) selection.style.setProperty(SELECTION_CHANNEL, String(index));
    },
  };
}
