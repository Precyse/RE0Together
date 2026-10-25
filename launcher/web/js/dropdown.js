import { h, setText, toggle } from './dom.js';
import { play, clear } from './motion.js';
import { icon } from './icons.js';

const ARROW_WIDTH = 10;
const MENU_GAP = 2;          // between the trigger and the menu, as in base.css
const MENU_MAX_HEIGHT = 240;  // base.css .select-menu
const VIEW_MARGIN = 8;        // kept free between the menu and the window's edge

/**
 * A painted dropdown: trigger plus a menu that opens with the menu-dropdown recipe. Keyboard: Up / Down / Enter / Space open it, Up / Down move,
 * Enter picks, Esc closes (and is swallowed so it does not close the popup or view behind). `set({ items: [{ value, label }], value, disabled })`.
 */
export function createDropdown({ label, onSelect }) {
  const text = h('span', { class: 'select-value' });
  const trigger = h('button', { class: 'select-trigger', type: 'button', 'aria-haspopup': 'listbox', 'aria-label': label }, text, h('span', { class: 'select-caret' }, icon('arrowDown', ARROW_WIDTH)));
  const menu = h('div', { class: 'select-menu', role: 'listbox', hidden: true });
  const el = h('div', { class: 'select' }, trigger, menu);
  let items = [];
  let value = null;
  let highlight = 0;
  let open = false;
  let itemsKey = '';

  function paintHighlight() {
    [...menu.children].forEach((option, index) => toggle(option, 'highlight', index === highlight));
  }

  /** Opens the menu below the trigger, or above it when there is more room there, never past the window's edge. */
  function placeMenu() {
    const box = trigger.getBoundingClientRect();
    const below = window.innerHeight - box.bottom - MENU_GAP - VIEW_MARGIN;
    const above = box.top - MENU_GAP - VIEW_MARGIN;
    const up = below < Math.min(menu.scrollHeight, MENU_MAX_HEIGHT) && above > below;
    toggle(menu, 'up', up);
    menu.style.maxHeight = `${Math.min(MENU_MAX_HEIGHT, Math.max(0, up ? above : below))}px`;
  }

  function openMenu() {
    if (open || trigger.disabled) return;
    open = true;
    highlight = Math.max(0, items.findIndex((item) => item.value === value));
    clear(menu, 'dropdown-out');
    menu.hidden = false;
    placeMenu();
    paintHighlight();
    play(menu, 'dropdown-in');
    trigger.setAttribute('aria-expanded', 'true');
    document.addEventListener('pointerdown', outside, true);
  }

  function closeMenu() {
    if (!open) return;
    open = false;
    document.removeEventListener('pointerdown', outside, true);
    trigger.setAttribute('aria-expanded', 'false');
    play(menu, 'dropdown-out', { keep: true }).then(() => {
      if (open) return;
      menu.hidden = true;
      clear(menu, 'dropdown-out');
    });
  }

  function outside(event) {
    if (!el.contains(event.target)) closeMenu();
  }

  function pick(index) {
    closeMenu();
    if (items[index] && items[index].value !== value) onSelect(items[index].value);
  }

  trigger.addEventListener('click', () => (open ? closeMenu() : openMenu()));
  el.addEventListener('keydown', (event) => {
    const { key } = event;
    if (key === 'Escape' && open) {
      event.stopPropagation();
      event.preventDefault();
      closeMenu();
    } else if (key === 'ArrowDown' || key === 'ArrowUp') {
      event.preventDefault();
      if (!open) openMenu();
      else {
        highlight = Math.min(items.length - 1, Math.max(0, highlight + (key === 'ArrowDown' ? 1 : -1)));
        paintHighlight();
      }
    } else if ((key === 'Enter' || key === ' ') && open) {
      event.preventDefault();
      pick(highlight);
    } else if (key === 'Tab') closeMenu();
  });

  return {
    el,
    set(next) {
      value = next.value;
      trigger.disabled = Boolean(next.disabled);
      const key = JSON.stringify(next.items);
      if (key !== itemsKey) {
        itemsKey = key;
        items = next.items;
        menu.replaceChildren(...items.map((item, index) => h('button', {
          class: 'select-option', type: 'button', role: 'option', tabindex: '-1', text: item.label,
          on: { click: () => pick(index), pointerenter: () => { highlight = index; paintHighlight(); } },
        })));
      }
      [...menu.children].forEach((option, index) => toggle(option, 'current', items[index].value === value));
      setText(text, items.find((item) => item.value === value)?.label ?? '');
      if (trigger.disabled) closeMenu();
    },
  };
}
