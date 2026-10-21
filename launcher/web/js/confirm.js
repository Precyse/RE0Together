import { h } from './dom.js';

const QUESTION = 'Leave the session and close?';
const LEAVE_LABEL = 'Leave and close';
const STAY_LABEL = 'Stay';

/** The close confirmation: a card over its own scrim. Focus starts on Stay; Esc is Stay; Tab stays between the two buttons. */
export function createConfirm(ctx) {
  const { send, motion } = ctx;
  const leave = h('button', { class: 'btn btn-primary confirm-yes', type: 'button', text: LEAVE_LABEL });
  const stay = h('button', { class: 'btn btn-ghost confirm-no', type: 'button', text: STAY_LABEL });
  const card = h('div', { class: 'confirm-card', role: 'alertdialog', 'aria-label': QUESTION }, h('p', { class: 'confirm-question', text: QUESTION }), h('div', { class: 'confirm-buttons' }, leave, stay));
  const scrim = h('div', { class: 'scrim' });
  const el = h('div', { class: 'confirm-layer', hidden: true }, scrim, card);
  let open = false;
  let before = null;

  function show() {
    if (open) return;
    open = true;
    before = document.activeElement;
    motion.clear(card, 'modal-out');
    motion.clear(scrim, 'fade-out');
    el.hidden = false;
    motion.play(scrim, 'fade-in');
    motion.play(card, 'modal-in');
    stay.focus();
  }

  function answer(leaving) {
    if (!open) return;
    open = false;
    send('close.confirm', { leave: leaving });
    Promise.all([motion.play(scrim, 'fade-out', { keep: true }), motion.play(card, 'modal-out', { keep: true })]).then(() => {
      if (open) return;
      el.hidden = true;
      motion.clear(card, 'modal-out');
      motion.clear(scrim, 'fade-out');
    });
    if (before?.isConnected) before.focus();
  }

  leave.addEventListener('click', () => answer(true));
  stay.addEventListener('click', () => answer(false));
  el.addEventListener('keydown', (event) => {
    if (event.key !== 'Tab') return;
    event.preventDefault();
    (document.activeElement === stay ? leave : stay).focus();
  });

  return { el, open: show, isOpen: () => open, stay: () => answer(false) };
}
