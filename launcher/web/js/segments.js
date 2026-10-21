import { h, toggle } from './dom.js';

/**
 * Segmented control with a sliding pill (tabs-sliding). items: [{ id, label, disabled }]; `variant` picks the size class (`tabs` or `filter`).
 * The pill moves by transform only; the first placement and any re-measure after a resize are instant.
 */
export function createSegments({ items, variant, label, onSelect }) {
  const pill = h('span', { class: 'seg-pill' });
  const buttons = new Map();
  const el = h('div', { class: `seg seg-${variant}`, role: 'tablist', 'aria-label': label }, pill);
  let active = null;

  for (const item of items) {
    const button = h('button', {
      class: 'seg-item', role: 'tab', type: 'button', text: item.label, 'aria-selected': 'false',
      disabled: item.disabled ? true : false,
      on: { click: () => onSelect(item.id) },
    });
    buttons.set(item.id, button);
    el.append(button);
  }

  function place(animate) {
    const button = buttons.get(active);
    if (!button || !button.offsetWidth) return;
    toggle(pill, 'no-anim', !animate);
    pill.style.setProperty('--pill-x', `${button.offsetLeft}px`);
    pill.style.setProperty('--pill-w', String(button.offsetWidth));
    if (!animate) {
      void pill.offsetWidth;
      toggle(pill, 'no-anim', false);
    }
  }

  new ResizeObserver(() => place(false)).observe(el);

  return {
    el,
    setActive(id) {
      if (id === active) return;
      const first = active === null;
      active = id;
      for (const [key, button] of buttons) button.setAttribute('aria-selected', String(key === id));
      place(!first);
    },
  };
}
