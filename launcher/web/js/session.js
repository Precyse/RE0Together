import { h, setText } from './dom.js';
import { icon } from './icons.js';
import { reel, rollDigits, swapText } from './motion.js';
import { createNotice } from './notice.js';
import { createCards } from './cards.js';
import { createImage, setImage } from './image.js';
import { createSteps, fillList, setGlyph } from './steps.js';

const SCREEN_HOSTING = 'hosting';
const COPY_ICON_PX = 20;
const STATE_GLYPH_SIZE = 'large';
const STATE_MARKS = { inGame: 'done', reconnecting: 'active', waitingForHost: 'active', gameClosed: 'failed' };
const LABELS = { code: 'LOBBY CODE', players: 'PLAYERS', start: 'START GAME', waiting: 'WAITING FOR HOST', mini: 'MINI MODE', relaunch: 'RELAUNCH', leave: 'Leave' };

const makeOption = () => h('div', { class: 'session-option' }, h('span', { class: 'session-option-label' }), h('span'));

function refreshOption(row, [label, value]) {
  setText(row.firstChild, label);
  setText(row.lastChild, value);
}

function button(label, kind, onClick) {
  return h('button', { class: `btn ${kind}`, type: 'button', text: label, on: { click: onClick } });
}

/** The lobby code with its copy button; `copied()` plays the icon swap clipboard -> check -> clipboard (CSS, one animation including the hold). */
function createCode(ctx) {
  const swap = h('span', { class: 't-icon-swap', 'data-state': 'a' },
    h('span', { class: 't-icon', 'data-icon': 'a' }, icon('copy', COPY_ICON_PX)),
    h('span', { class: 't-icon', 'data-icon': 'b' }, icon('check', COPY_ICON_PX)));
  const copy = h('button', { class: 'btn-icon', type: 'button', 'aria-label': 'Copy lobby code', on: { click: () => ctx.send('session.copyCode') } }, swap);
  const value = h('span', { class: 'session-code mono' });
  const el = h('div', { class: 'session-code-block' }, h('span', { class: 'caption', text: LABELS.code }), h('div', { class: 'session-code-row' }, value, copy));
  return {
    el,
    update: (code) => setText(value, code),
    copied() {
      swap.classList.remove('is-copied');
      void swap.offsetWidth;
      swap.classList.add('is-copied');
    },
  };
}

/** Hosting: the step list, START GAME (a guest sees a disabled Waiting for host in its place, RELAUNCH when its game stalled) and Leave. */
function createHostingBody(ctx) {
  const steps = createSteps('large');
  steps.el.classList.add('steps-tight');
  let relaunch = false;
  const primary = button(LABELS.start, 'btn-primary session-primary', () => ctx.send(relaunch ? 'session.relaunch' : 'session.start'));
  const el = h('div', { class: 'session-body' }, steps.el, h('div', { class: 'session-buttons' }, primary, button(LABELS.leave, 'btn-ghost session-leave', () => ctx.send('session.leave'))));
  return {
    el,
    update(session) {
      steps.update(session.steps);
      relaunch = !session.isHost && session.relaunch;
      setText(primary, session.isHost ? LABELS.start : relaunch ? LABELS.relaunch : LABELS.waiting);
      primary.disabled = !session.startEnabled && !relaunch;
    },
  };
}

/** Lobby: the state line, the host options, the last events, MINI MODE (RELAUNCH after the game closed) and Leave. */
function createLobbyBody(ctx) {
  const glyph = h('span', { class: 'step-glyph' });
  const words = h('span', { class: 'session-state-words' });
  const timer = h('span', { class: 'session-state-timer mono' });
  const line = h('div', { class: 'session-state', hidden: true }, glyph, words, timer);
  const options = h('div', { class: 'session-options' });
  const events = h('div', { class: 'session-events mono' });
  let relaunch = false;
  const primary = h('button', { class: 'btn btn-primary session-primary', type: 'button', on: { click: () => ctx.send(relaunch ? 'session.relaunch' : 'session.mini') } });
  const el = h('div', { class: 'session-body' }, line, options, events, h('div', { class: 'session-buttons' }, primary, button(LABELS.leave, 'btn-ghost session-leave', () => ctx.send('session.leave'))));

  function showState(state) {
    line.hidden = !state;
    if (!state) return;
    if (line.dataset.kind !== state.kind) {
      setGlyph(glyph, STATE_MARKS[state.kind], STATE_GLYPH_SIZE, line.dataset.kind !== undefined);
      line.dataset.kind = state.kind;
    }
    swapText(words, state.words);
    timer.hidden = !state.timer;
    if (state.timer) rollDigits(timer, state.timer);
  }

  return {
    el,
    update(session) {
      showState(session.state);
      fillList(options, session.options, makeOption, refreshOption);
      fillList(events, session.events, () => h('div'), setText);
      relaunch = session.relaunch;
      setText(primary, relaunch ? LABELS.relaunch : LABELS.mini);
    },
  };
}

/**
 * The hosting and the lobby screen (two instances of this module, `ctx.screen` says which): backdrop art, logo, lobby code, then the hosting steps or
 * the lobby state, the buttons and the notice row, and on the right the PLAYERS counter (hosting) and the player cards.
 */
export function create(ctx) {
  const hosting = ctx.screen === SCREEN_HOSTING;
  const backdrop = createImage('art-backdrop');
  const logo = createImage('art-logo');
  const code = createCode(ctx);
  const body = hosting ? createHostingBody(ctx) : createLobbyBody(ctx);
  const notice = createNotice(ctx);
  const counter = h('span', { class: 'session-count' });
  const cards = createCards(ctx, 'wide');
  const right = h('div', { class: 'session-right' }, hosting ? h('div', { class: 'caption session-players' }, h('span', { text: LABELS.players }), counter) : null, cards.el);
  const left = h('div', { class: 'session-left' }, logo, code.el, body.el, notice.el);
  const el = h('section', { class: `session session-${ctx.screen}` }, backdrop, h('div', { class: 'session-grid' }, left, right));
  let shown = false;

  function update(state) {
    const { session } = state;
    if (state.screen !== ctx.screen || !session) {
      shown = false;
      return;
    }
    setImage(backdrop, state.art.hero);
    setImage(logo, state.art.logo);
    code.update(session.code);
    body.update(session);
    notice.update(session.notice);
    if (hosting) reel(counter, session.counter);
    cards.setLayout(session.layout);
    cards.update(session.seats, session.seatsClickable);
    if (!shown) {
      shown = true;
      queueMicrotask(() => cards.enter());
    }
  }

  return {
    el,
    update,
    event(name) {
      if (name === 'copied' && !el.hidden) code.copied();
    },
  };
}
