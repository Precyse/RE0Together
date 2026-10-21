import { h, setText } from './dom.js';
import { createSegments } from './segments.js';

const BRAND = 'CO-OP';
const BACK_LABEL = 'Back';
const SETTINGS_LABEL = 'Settings';
const TABS = [{ id: 'play', label: 'Play' }, { id: 'lobbies', label: 'Lobbies' }];
const ARROW_WIDTH = 12;
const GLYPH_WIDTH = 12;

/** The top bar: brand, Play / Lobbies tabs, build, update button, Settings (browse); Back and the brand (view); the brand alone (session). */
export function create(ctx) {
  const { send, icons } = ctx;
  const back = h('button', { class: 'btn top-back no-drag', type: 'button', on: { click: () => send('view.close') } }, icons.icon('arrowLeft', ARROW_WIDTH), BACK_LABEL);
  const brand = h('div', { class: 'top-brand', text: BRAND });
  const tabs = createSegments({ items: TABS, variant: 'tabs', label: 'Section', onSelect: (tab) => send('tab.select', { tab }) });
  const build = h('div', { class: 'top-build mono' });
  const updateButton = h('button', { class: 'btn top-button no-drag', type: 'button', on: { click: () => send('update.click') } });
  const settings = h('button', { class: 'btn top-button no-drag', type: 'button', text: SETTINGS_LABEL, on: { click: () => send('view.open', { view: 'settings' }) } });
  const minimize = h('button', { class: 'btn-icon top-window', type: 'button', 'aria-label': 'Minimize', on: { click: () => send('window.minimize') } }, icons.icon('minimize', GLYPH_WIDTH));
  const close = h('button', { class: 'btn-icon top-window', type: 'button', 'aria-label': 'Close', on: { click: () => send('window.close') } }, icons.icon('close', GLYPH_WIDTH));
  const el = h('header', { class: 'topbar drag' }, back, brand, tabs.el, h('div', { class: 'top-spacer' }), build, updateButton, settings, h('div', { class: 'top-window-group' }, minimize, close));

  function render(state) {
    const { top } = state;
    const { mode } = top;
    el.dataset.mode = mode;
    back.hidden = mode !== 'view';
    tabs.el.hidden = mode !== 'browse';
    build.hidden = mode !== 'browse';
    settings.hidden = mode === 'view';
    updateButton.hidden = mode === 'view' || !top.update;
    if (top.update) {
      setText(updateButton, top.update.label);
      updateButton.disabled = !top.update.enabled;
    }
    setText(build, top.build);
    tabs.setActive(top.tab);
  }

  return { el, update: render };
}
