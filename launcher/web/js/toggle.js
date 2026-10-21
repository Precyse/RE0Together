import { h, setText } from './dom.js';

/** A switch with a label: the thumb slides with the toggle recipe (the first state is drawn without motion). `set(on, disabled)`. */
export function createToggle({ label, onChange }) {
  const thumb = h('span', { class: 'toggle-thumb' });
  const text = h('span', { class: 'toggle-text' });
  const el = h('button', { class: 'toggle', type: 'button', role: 'switch', 'aria-checked': 'false' }, h('span', { class: 'toggle-track' }, thumb), text);
  let on = null;

  setText(text, label);
  el.addEventListener('click', () => onChange(!on));

  return {
    el,
    set(next, disabled = false) {
      if (on !== null && on !== next) el.classList.add('is-init');
      on = next;
      el.setAttribute('aria-checked', String(next));
      el.disabled = disabled;
    },
  };
}
