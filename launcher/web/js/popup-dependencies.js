import { h, keyed, setText, toggle } from './dom.js';

const TITLE = 'Missing dependencies';
const OTHER_VERSION_TITLE = 'Dependencies differ';
const INSTALL_LABEL = 'Install';
const REPLACE_LABEL = 'Replace';
const CANCEL_LABEL = 'Cancel';
const MANUAL_LABEL = 'Manual';

/** The dependencies popup: what the selected game lacks or has another version of before its mod can install, one row each (name, where
 *  it comes from); Install (Replace when a row is another version, which moves the old file to the archive) installs them and then the mod,
 *  Cancel (also Esc and the scrim) closes it. */
export function create(ctx) {
  const { send } = ctx;
  const game = h('span', { class: 'popup-update-build' });
  const rows = h('div', { class: 'popup-deps-rows' });
  const install = h('button', { class: 'btn btn-primary popup-update-yes', type: 'button', text: INSTALL_LABEL, on: { click: () => send('dependencies.install') } });
  const cancel = h('button', { class: 'btn btn-ghost popup-update-no', type: 'button', text: CANCEL_LABEL, on: { click: () => send('popup.close') } });
  const title = h('span', { text: TITLE });
  const el = h('section', { class: 'popup popup-update popup-deps', role: 'dialog', 'aria-label': TITLE },
    h('p', { class: 'popup-update-title' }, title, game),
    rows,
    h('div', { class: 'popup-update-buttons' }, install, cancel));

  return {
    el,
    update(popup) {
      const otherVersion = popup.rows.some((row) => row.otherVersion);
      setText(game, popup.gameName);
      setText(title, otherVersion ? OTHER_VERSION_TITLE : TITLE);
      setText(install, otherVersion ? REPLACE_LABEL : INSTALL_LABEL);
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
