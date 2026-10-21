import { h, setText, keyed } from './dom.js';
import { createDropdown } from './dropdown.js';
import { createPopupHeader } from './popup-header.js';

/** The options popup: logo header, then one row per session option (label and dropdown); a guest sees the host's values read-only. */
export function create(ctx) {
  const { send } = ctx;
  const header = createPopupHeader(ctx);
  const rows = h('div', { class: 'option-rows' });
  const el = h('section', { class: 'popup popup-options', role: 'dialog' }, header.el, h('div', { class: 'popup-body option-body' }, rows));
  let gameId = null;

  function createRow(option) {
    const label = h('span', { class: 'option-label' });
    const dropdown = createDropdown({ label: option.label, onSelect: (value) => send('option.set', { gameId, key: option.key, value }) });
    const row = h('div', { class: 'option-row' }, label, dropdown.el);
    row.parts = { label, dropdown };
    return row;
  }

  return {
    el,
    update(popup, state) {
      gameId = state.games.find((game) => game.selected)?.id ?? null;
      header.set(popup.art);
      el.setAttribute('aria-label', popup.gameName);
      keyed(rows, popup.rows, (option) => option.key, createRow, (row, option) => {
        setText(row.parts.label, option.label);
        row.parts.dropdown.set({ items: option.values, value: option.value, disabled: popup.readOnly || option.locked });
      });
    },
  };
}
