import { h, keyed, setText, toggle } from './dom.js';

const TITLE = 'Missing dependencies';
const INSTALL_LABEL = 'Install';
const CANCEL_LABEL = 'Cancel';
const MANUAL_LABEL = 'Manual';

/** The dependencies popup: what the selected game lacks before its mod can install, one row each (name, where it comes from);
 *  Install installs them and then the mod, Cancel (also Esc and the scrim) closes it. */
export function create(ctx) {
  const { send } = ctx;
  const game = h('span', { class: 'popup-update-build' });
  const rows = h('div', { class: 'popup-deps-rows' });
  const install = h('button', { class: 'btn btn-primary popup-update-yes', type: 'button', text: INSTALL_LABEL, on: { click: () => send('dependencies.install') } });
  const cancel = h('button', { class: 'btn btn-ghost popup-update-no', type: 'button', text: CANCEL_LABEL, on: { click: () => send('popup.close') } });
  const el = h('section', { class: 'popup popup-update popup-deps', role: 'dialog', 'aria-label': TITLE },
    h('p', { class: 'popup-update-title' }, TITLE, game),
    rows,
    h('div', { class: 'popup-update-buttons' }, install, cancel));

  return {
    el,
    update(popup) {
      setText(game, popup.gameName);
      keyed(rows, popup.rows, (row) => row.name,
        () => h('div', { class: 'popup-deps-row' }, h('span', { class: 'popup-deps-name' }), h('span', { class: 'popup-deps-source mono' })),
        (rowEl, row) => {
          setText(rowEl.firstChild, row.name);
          setText(rowEl.lastChild, row.installable ? row.source : MANUAL_LABEL);
          toggle(rowEl, 'is-manual', !row.installable);
        });
      install.disabled = !popup.installEnabled;
    },
  };
}
