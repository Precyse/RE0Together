import { h } from './dom.js';

/** A switch (the label is its accessible name): light track with the knob right when on, dark track with the knob left when off; the knob moves with the toggle recipe (the first state is drawn without motion). `set(on, disabled)`. */
export function createToggle({ label, onChange }) {
  const thumb = h('span', { class: 'toggle-thumb' });
  const el = h('button', { class: 'toggle', type: 'button', role: 'switch', 'aria-label': label, 'aria-checked': 'false' }, h('span', { class: 'toggle-track' }, thumb));
  let on = null;

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
