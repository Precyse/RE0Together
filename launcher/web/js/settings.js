import { h, setText, keyed } from './dom.js';
import { createDropdown } from './dropdown.js';
import { createToggle } from './toggle.js';

const LAUNCHER_TITLE = 'Launcher';
const UPDATES_LABEL = 'Check for updates at start';
const VISIBILITY_LABEL = 'Lobby visibility';
const VISIBILITY_ITEMS = [{ value: 'friends', label: 'Friends' }, { value: 'everyone', label: 'Everyone' }];
const ELLIPSIS = '…';
const MIN_PATH_CHARS = 8;

/** Shortens a path in the middle until it fits its box. */
function fitPath(el, full) {
  el.textContent = full;
  if (el.scrollWidth <= el.clientWidth) return;
  let low = MIN_PATH_CHARS;
  let high = full.length;
  while (low < high) {
    const keep = Math.ceil((low + high) / 2);
    const head = Math.ceil(keep / 2);
    el.textContent = full.slice(0, head) + ELLIPSIS + full.slice(full.length - (keep - head));
    if (el.scrollWidth > el.clientWidth) high = keep - 1;
    else low = keep;
  }
  const head = Math.ceil(low / 2);
  el.textContent = full.slice(0, head) + ELLIPSIS + full.slice(full.length - (low - head));
}

/** A section: its caption (kept as `.caption` for later titles) over the content stack. */
function section(title, ...content) {
  const caption = h('div', { class: 'caption settings-caption', text: title });
  const root = h('section', { class: 'settings-section' }, caption, h('div', { class: 'settings-content' }, ...content));
  root.caption = caption;
  return root;
}

/** Settings (a view over the page): the Launcher section, then one section per game. Every control sends its command; values come back in the state. */
export function create(ctx) {
  const { send } = ctx;
  const updates = createToggle({ label: UPDATES_LABEL, onChange: (on) => send('settings.updates', { on }) });
  const visibility = createDropdown({ label: VISIBILITY_LABEL, onSelect: (value) => send('settings.visibility', { value }) });
  const logs = h('button', { class: 'btn settings-tool', type: 'button', text: 'Logs folder', on: { click: () => send('settings.logs') } });
  const report = h('button', { class: 'btn settings-tool', type: 'button', text: 'Create report', on: { click: () => send('report.create') } });
  const launcher = section(LAUNCHER_TITLE, updates.el, h('div', { class: 'settings-row' }, h('span', { class: 'option-label', text: VISIBILITY_LABEL }), visibility.el), h('div', { class: 'settings-row' }, logs, report));
  const games = h('div', { class: 'settings-games' });
  const el = h('section', { class: 'settings' }, h('div', { class: 'settings-scroll' }, launcher, games));
  const resize = new ResizeObserver(() => {
    for (const path of el.querySelectorAll('.settings-path')) fitPath(path, path.dataset.full);
  });
  resize.observe(el);

  function createGame(game) {
    const path = h('span', { class: 'settings-path mono' });
    const browse = h('button', { class: 'btn settings-tool', type: 'button', text: 'Browse', on: { click: () => send('game.browse', { gameId: game.id }) } });
    const reset = h('button', { class: 'btn btn-ghost settings-tool', type: 'button', text: 'Reset', on: { click: () => send('game.resetFolder', { gameId: game.id }) } });
    const uninstall = h('button', { class: 'btn btn-ghost settings-tool', type: 'button', text: 'Uninstall', on: { click: () => send('game.uninstall', { gameId: game.id }) } });
    const line = h('div', { class: 'settings-line' });
    const options = h('div', { class: 'settings-options' });
    const archive = h('button', { class: 'btn settings-tool', type: 'button', text: 'Archive', on: { click: () => send('game.archive', { gameId: game.id }) } });
    const root = section('', h('div', { class: 'settings-row' }, path, browse, reset, uninstall), line, options, archive);
    root.parts = { path, browse, reset, uninstall, line, options, archive };
    return root;
  }

  function updateGame(root, game) {
    const { path, browse, reset, uninstall, line, options, archive } = root.parts;
    setText(root.caption, game.name);
    path.dataset.full = game.folder;
    fitPath(path, game.folder);
    browse.disabled = !game.browseEnabled;
    reset.disabled = !game.resetEnabled;
    uninstall.disabled = !game.uninstallEnabled;
    setText(line, game.line);
    options.hidden = game.options.length === 0;
    keyed(options, game.options, (option) => option.key, (option) => {
      const label = h('span', { class: 'option-label', text: option.label });
      const dropdown = createDropdown({ label: option.label, onSelect: (value) => send('option.set', { gameId: game.id, key: option.key, value }) });
      const row = h('div', { class: 'settings-option' }, label, dropdown.el);
      row.parts = { label, dropdown };
      return row;
    }, (row, option) => {
      setText(row.parts.label, option.label);
      row.parts.dropdown.set({ items: option.values, value: option.value, disabled: option.locked });
    });
    archive.hidden = !game.archive;
  }

  return {
    el,
    update(state) {
      const { settings } = state;
      if (!settings) return;
      updates.set(settings.checkForUpdates);
      visibility.set({ items: VISIBILITY_ITEMS, value: settings.visibility, disabled: settings.visibilityLocked });
      keyed(games, settings.games, (game) => game.id, createGame, updateGame);
    },
  };
}
