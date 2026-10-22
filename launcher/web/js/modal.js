import { h } from './dom.js';
import { createConfirm } from './confirm.js';
import * as lobbyPopup from './popup-lobby.js';
import * as optionsPopup from './popup-options.js';
import * as updatePopup from './popup-update.js';

const POPUPS = { lobby: lobbyPopup, options: optionsPopup, update: updatePopup };

/**
 * The modal layer under the top bar: the scrim (62 %) fades with the panel (modal recipe 250 / 150 ms, scale 0.96). One popup at a time
 * from `state.popup`; Esc, a scrim click or the close button send popup.close. The close confirmation has its own layer over the whole window.
 */
export function create(ctx) {
  const { send, motion } = ctx;
  const popups = Object.fromEntries(Object.entries(POPUPS).map(([kind, module]) => [kind, module.create(ctx)]));
  const confirm = createConfirm(ctx);
  const scrim = h('div', { class: 'scrim', on: { click: () => send('popup.close') } });
  const slot = h('div', { class: 'modal-slot' }, ...Object.values(popups).map((popup) => popup.el));
  const layer = h('div', { class: 'modal-layer', hidden: true }, scrim, slot);
  const el = h('div', { class: 'modal-root' }, layer, confirm.el);
  let open = false;
  let kind = null;

  function openLayer() {
    open = true;
    motion.clear(slot, 'modal-out');
    motion.clear(scrim, 'fade-out');
    layer.hidden = false;
    motion.play(scrim, 'fade-in');
    motion.play(slot, 'modal-in');
  }

  function closeLayer() {
    open = false;
    Promise.all([motion.play(scrim, 'fade-out', { keep: true }), motion.play(slot, 'modal-out', { keep: true })]).then(() => {
      if (open) return;
      layer.hidden = true;
      motion.clear(slot, 'modal-out');
      motion.clear(scrim, 'fade-out');
    });
  }

  return {
    el,
    update(state) {
      const { popup } = state;
      if (!popup) {
        if (open) closeLayer();
        return;
      }
      if (popup.kind !== kind) {
        kind = popup.kind;
        for (const [name, module] of Object.entries(popups)) module.el.hidden = name !== kind;
      }
      popups[kind].update(popup, state);
      if (!open) openLayer();
    },
    openConfirm: confirm.open,
    confirmOpen: confirm.isOpen,
    stay: confirm.stay,
  };
}
