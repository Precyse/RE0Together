import { h, toggle } from './dom.js';

const LABEL = 'Lobby code';
const JOIN_LABEL = 'Join';
const MAX_CODE_LENGTH = 64;

/** The lobby code row: label, field, Join. A refused code shakes the field, turns its border red for the hold and fades back; typing clears it. */
export function createCodeField(ctx) {
  const { send, motion } = ctx;
  const input = h('input', { id: 'lobby-code', class: 'field code-input', type: 'text', maxlength: MAX_CODE_LENGTH, spellcheck: 'false', autocomplete: 'off' });
  const join = h('button', { class: 'btn btn-over-art code-join', type: 'button', text: JOIN_LABEL });
  const label = h('label', { class: 'caption code-label', for: 'lobby-code', text: LABEL });
  const el = h('div', { class: 'code-row' }, label, input, join);
  let revert = 0;

  function clearError() {
    clearTimeout(revert);
    toggle(input, 'is-error', false);
  }

  function submit() {
    send('join.code', { code: input.value });
  }

  join.addEventListener('click', submit);
  input.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') submit();
  });
  input.addEventListener('input', clearError);

  return {
    el,
    update(home) {
      input.disabled = !home.codeEnabled;
      join.disabled = !home.joinEnabled;
    },
    refuse() {
      toggle(input, 'is-error', true);
      motion.shake(input);
      clearTimeout(revert);
      revert = setTimeout(clearError, motion.tokenMs('--revert-hold'));
    },
  };
}
