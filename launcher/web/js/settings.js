import { h, setText, keyed } from './dom.js';
import { createDropdown } from './dropdown.js';
import { createToggle } from './toggle.js';
import { createModRun } from './mod-run.js';
import { createSpinner } from './spinner.js';
import { createImage } from './art.js';

const LAUNCHER_TITLE = 'Launcher';
const UPDATES_LABEL = 'Check for updates at start';
const CHECK_LABEL = 'Check for updates';
const UP_TO_DATE_LABEL = 'Up to date';
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

/** One row of the label column and the control column (two grid cells). */
function labelled(label, ...controls) {
  return [h('span', { class: 'settings-label', text: label }), h('div', { class: 'settings-control' }, ...controls)];
}

const tool = (label, onClick) => h('button', { class: 'btn btn-ghost settings-tool', type: 'button', text: label, on: { click: onClick } });

/** Settings (a view over the page): the Launcher section, then one section per game. Every control sends its command; values come back in the state. */
export function create(ctx) {
  const { send } = ctx;
  const updates = createToggle({ label: UPDATES_LABEL, onChange: (on) => send('settings.updates', { on }) });
  const checkText = h('span');
  const checkSpinner = createSpinner('small');
  const check = h('button', { class: 'btn btn-ghost settings-check', type: 'button', 'aria-label': CHECK_LABEL, on: { click: () => send('update.check') } }, checkText, checkSpinner);
  const visibility = createDropdown({ label: VISIBILITY_LABEL, onSelect: (value) => send('settings.visibility', { value }) });
  const logs = tool('Logs folder', () => send('settings.logs'));
  const report = tool('Create report', () => send('report.create'));
  const launcher = section(LAUNCHER_TITLE, h('div', { class: 'settings-grid' }, ...labelled(UPDATES_LABEL, updates.el, check), ...labelled(VISIBILITY_LABEL, visibility.el), ...labelled('', logs, report)));
  const games = h('div', { class: 'settings-games' });
  const art = createImage('page-art-img');
  const el = h('section', { class: 'settings' }, h('div', { class: 'page-art' }, art.el), h('div', { class: 'settings-scroll' }, launcher, games));
  const resize = new ResizeObserver(() => {
    for (const path of el.querySelectorAll('.settings-path')) fitPath(path, path.dataset.full);
  });
  resize.observe(el);

  function createGame(game) {
    const path = h('span', { class: 'settings-path mono' });
    const browse = tool('Browse', () => send('game.browse', { gameId: game.id }));
    const reset = tool('Reset', () => send('game.resetFolder', { gameId: game.id }));
    const uninstall = tool('Uninstall', () => send('game.uninstall', { gameId: game.id }));
    const archive = tool('Archive', () => send('game.archive', { gameId: game.id }));
    const modLine = h('span', { class: 'settings-mod' });
    const gameLine = h('span', { class: 'settings-game' });
    const modRun = createModRun(ctx, 'line');
    const options = h('div', { class: 'settings-grid' });
    const buttons = h('div', { class: 'settings-buttons' }, browse, reset, uninstall, archive);
    const root = section('', h('div', { class: 'settings-row' }, path, buttons), h('div', { class: 'settings-line' }, modLine, modRun.el, gameLine), options);
    root.parts = { path, browse, reset, uninstall, modLine, modRun, gameLine, options, archive };
    return root;
  }

  function updateGame(root, game) {
    const { path, browse, reset, uninstall, modLine, modRun, gameLine, options, archive } = root.parts;
    setText(root.caption, game.name);
    path.dataset.full = game.folder;
    fitPath(path, game.folder);
    browse.disabled = !game.browseEnabled;
    reset.disabled = !game.resetEnabled;
    uninstall.disabled = !game.uninstallEnabled;
    archive.hidden = !game.archive;
    setText(modLine, game.modLine);
    modLine.hidden = Boolean(game.modRun);
    modRun.update(game.modRun, game.id);
    setText(gameLine, game.gameLine);
    options.hidden = game.options.length === 0;
    keyed(options, game.options, (option) => option.key, (option) => {
      const label = h('span', { class: 'settings-label' });
      const dropdown = createDropdown({ label: option.label, onSelect: (value) => send('option.set', { gameId: game.id, key: option.key, value }) });
      const row = h('div', { class: 'settings-option' }, label, h('div', { class: 'settings-control' }, dropdown.el));
      row.parts = { label, dropdown };
      return row;
    }, (row, option) => {
      setText(row.parts.label, option.label);
      row.parts.dropdown.set({ items: option.values, value: option.value, disabled: option.locked });
    });
  }

  return {
    el,
    update(state) {
      const { settings } = state;
      if (!settings) return;
      art.set(state.art.hero);
      updates.set(settings.checkForUpdates);
      const checking = settings.updateCheck === 'checking';
      check.disabled = checking;
      checkText.hidden = checking;
      checkSpinner.hidden = !checking;
      setText(checkText, settings.updateCheck === 'upToDate' ? UP_TO_DATE_LABEL : CHECK_LABEL);
      visibility.set({ items: VISIBILITY_ITEMS, value: settings.visibility, disabled: settings.visibilityLocked });
      keyed(games, settings.games, (game) => game.id, createGame, updateGame);
    },
  };
}
