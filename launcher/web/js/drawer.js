import { h, toggle } from './dom.js';

const LABEL = 'Log';
const ARROW_WIDTH = 14;
const NEAR_BOTTOM_PX = 24;

/**
 * The bottom of Home and Lobbies: the footer strip with the newest player event behind a big arrow, and the log (last `--log-lines` lines)
 * rising over the page above it. The log panel moves by transform only, so the page behind is never re-laid out.
 */
export function create(ctx) {
  const { motion, icons } = ctx;
  const maxLines = parseInt(motion.tokenValue('--log-lines'), 10);
  const arrowUp = icons.icon('arrowUp', ARROW_WIDTH);
  const arrowDown = icons.icon('arrowDown', ARROW_WIDTH);
  const eventText = h('span', { class: 'footer-event' });
  const footer = h('button', { class: 'footer-bar', type: 'button', 'aria-label': LABEL, 'aria-expanded': 'false' }, h('span', { class: 'footer-arrow' }, arrowUp, arrowDown), eventText);
  const lines = h('div', { class: 'log-lines mono' });
  const panel = h('div', { class: 'log-panel', 'aria-hidden': 'true' }, lines);
  const el = h('footer', { class: 'drawer' }, panel, footer);
  let expanded = false;

  function setExpanded(on) {
    expanded = on;
    toggle(el, 'open', on);
    footer.setAttribute('aria-expanded', String(on));
    panel.setAttribute('aria-hidden', String(!on));
    arrowUp.style.display = on ? 'none' : '';
    arrowDown.style.display = on ? '' : 'none';
  }

  footer.addEventListener('click', () => setExpanded(!expanded));
  setExpanded(false);

  return {
    el,
    update(state) {
      motion.swapText(eventText, state.footer.event);
    },
    show(on) {
      el.hidden = !on;
      if (!on && expanded) setExpanded(false);
    },
    addLines(newLines) {
      const stick = lines.scrollHeight - lines.scrollTop - lines.clientHeight < NEAR_BOTTOM_PX;
      for (const text of newLines) lines.append(h('div', { class: 'log-line', text }));
      while (lines.children.length > maxLines) lines.firstChild.remove();
      if (stick) lines.scrollTop = lines.scrollHeight;
    },
  };
}
