import { h, setText } from './dom.js';

const TITLE = 'Update ready';
const RESTART_LABEL = 'Restart';
const LATER_LABEL = 'Later';

/** The update popup: shown once for a staged build; Restart runs the restart path, Later (also Esc and the scrim) closes it. */
export function create(ctx) {
  const { send } = ctx;
  const build = h('span', { class: 'popup-update-build mono' });
  const restart = h('button', { class: 'btn btn-primary popup-update-yes', type: 'button', text: RESTART_LABEL, on: { click: () => send('update.restart') } });
  const later = h('button', { class: 'btn btn-ghost popup-update-no', type: 'button', text: LATER_LABEL, on: { click: () => send('popup.close') } });
  const el = h('section', { class: 'popup popup-update', role: 'dialog', 'aria-label': TITLE },
    h('p', { class: 'popup-update-title' }, TITLE, build),
    h('div', { class: 'popup-update-buttons' }, restart, later));

  return {
    el,
    update(popup) {
      setText(build, popup.build);
    },
  };
}
