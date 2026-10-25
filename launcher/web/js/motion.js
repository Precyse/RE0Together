import { h, setText } from './dom.js';

const root = document.documentElement;
const CLASS_PREFIX = 'm-';
const DIGIT_PATTERN = /[0-9]/;
const DIGIT_STAGGER_SLOTS = 2;
const REEL_DIGITS = 10;

export const isReduced = () => root.classList.contains('reduced');

export function tokenValue(name) {
  return getComputedStyle(root).getPropertyValue(name).trim();
}

/** A duration token as milliseconds. */
export function tokenMs(name) {
  const value = parseFloat(tokenValue(name));
  return tokenValue(name).endsWith('ms') ? value : value * 1000;
}

function hasRunningAnimation(el) {
  return el.getAnimations().some((animation) => animation.effect.getComputedTiming().activeDuration > 0);
}

/**
 * Plays a recipe from motion.css on the element: adds `m-<recipe>`, resolves when it ends. `keep` leaves the class (and its end state)
 * on the element, `dir` sets --dir (page recipes), `index` sets --i (stagger delay). Reduced motion resolves at once and adds nothing.
 */
export function play(el, recipe, { keep = false, dir, index } = {}) {
  const cls = CLASS_PREFIX + recipe;
  if (isReduced()) return Promise.resolve();
  if (dir !== undefined) el.style.setProperty('--dir', String(dir));
  if (index !== undefined) el.style.setProperty('--i', String(index));
  if (el.classList.contains(cls)) {
    el.classList.remove(cls);
    void el.offsetWidth;
  }
  el.classList.add(cls);
  if (!hasRunningAnimation(el)) {
    if (!keep) el.classList.remove(cls);
    return Promise.resolve();
  }
  return new Promise((resolve) => {
    const finish = (event) => {
      if (event.target !== el) return;
      el.removeEventListener('animationend', finish);
      el.removeEventListener('animationcancel', finish);
      if (!keep) el.classList.remove(cls);
      resolve();
    };
    el.addEventListener('animationend', finish);
    el.addEventListener('animationcancel', finish);
  });
}

/** Removes a recipe class that `play(..., { keep: true })` left behind. */
export function clear(el, recipe) {
  el.classList.remove(CLASS_PREFIX + recipe);
}

/** texts-reveal: every element rises in, each one `--stagger-stagger` behind the one before. */
export function stagger(els, recipe = 'reveal') {
  return Promise.all(els.map((el, index) => play(el, recipe, { index })));
}

const swaps = new WeakMap();

/** text-states-swap: the old words exit up, the new ones enter from below. */
export function swapText(el, text) {
  const state = swaps.get(el);
  if (state?.busy) {
    state.next = text;
    return Promise.resolve();
  }
  if (el.textContent === text) return Promise.resolve();
  if (!el.textContent || isReduced()) {
    setText(el, text);
    return Promise.resolve();
  }
  const current = { busy: true, next: text };
  swaps.set(el, current);
  return play(el, 'swap-out', { keep: true })
    .then(() => {
      setText(el, current.next);
      clear(el, 'swap-out');
      return play(el, 'swap-in');
    })
    .then(() => {
      current.busy = false;
      if (el.textContent !== current.next) return swapText(el, current.next);
      return undefined;
    });
}

/** number-pop-in: only the characters that changed (compared from the right) pop in, the last two staggered. */
export function rollDigits(el, text) {
  const previous = el.dataset.rolled;
  if (previous === text) return;
  el.dataset.rolled = text;
  el.classList.add('t-digit-group');
  const old = previous ?? '';
  const spans = [...text].map((ch) => h('span', { class: 't-digit', text: ch }));
  el.replaceChildren(...spans);
  if (previous === undefined) return;
  let slot = 0;
  for (let i = spans.length - 1; i >= 0; i--) {
    if (old[old.length - (text.length - i)] === text[i]) continue;
    spans[i].style.setProperty('--j', String(Math.min(slot, DIGIT_STAGGER_SLOTS)));
    play(spans[i], 'digit');
    slot += 1;
  }
}

/** A number-bearing text (ping, percent, timer) rolls its digits; plain words are written as they are; no text hides the element. */
export function roll(el, text) {
  const numeric = DIGIT_PATTERN.test(text);
  el.hidden = !text;
  el.classList.toggle('mono', numeric);
  if (numeric) rollDigits(el, text);
  else {
    delete el.dataset.rolled;
    setText(el, text);
  }
}

const reels = new WeakMap();

function reelPattern(text) {
  return [...text].map((ch) => (DIGIT_PATTERN.test(ch) ? 'd' : ch)).join('');
}

function buildReel(el, text, cell) {
  const cols = [...text].map((ch) => {
    if (!DIGIT_PATTERN.test(ch)) return { el: h('span', { class: 't-reel-fixed', text: ch }), strip: null, digit: null };
    const strip = h('span', { class: 't-reel-strip' }, ...Array.from({ length: REEL_DIGITS }, (_, d) => h('span', { class: 't-reel-digit', text: String(d) })));
    const digit = Number(ch);
    strip.style.transform = `translateY(${-digit * cell}px)`;
    return { el: h('span', { class: 't-reel-col' }, strip), strip, digit };
  });
  el.classList.add('t-reel');
  el.replaceChildren(...cols.map((col) => col.el));
  return cols;
}

/** spinning-counter: each digit column spins to its new digit, columns staggered, vertical blur while moving. */
export function reel(el, text) {
  const state = reels.get(el);
  if (state?.text === text) return;
  const cell = state?.cell ?? parseFloat(getComputedStyle(el).getPropertyValue('--reel-cell'));
  if (!state || state.pattern !== reelPattern(text)) {
    reels.set(el, { text, pattern: reelPattern(text), cell, cols: buildReel(el, text, cell) });
    return;
  }
  const duration = tokenMs('--reel-dur');
  const staggerMs = tokenMs('--reel-stagger');
  const blur = tokenValue('--reel-spin-blur');
  const easing = tokenValue('--reel-ease');
  let column = 0;
  state.cols.forEach((col, i) => {
    if (!col.strip) return;
    const digit = Number(text[i]);
    if (digit === col.digit) return;
    const from = `translateY(${-col.digit * cell}px)`;
    const to = `translateY(${-digit * cell}px)`;
    col.strip.style.transform = to;
    if (!isReduced()) {
      col.strip.animate([{ transform: from, filter: `blur(${blur})` }, { transform: to, filter: 'blur(0)' }], { duration, delay: column * staggerMs, easing, fill: 'backwards' });
    }
    col.digit = digit;
    column += 1;
  });
  state.text = text;
}

/** error-state-shake: replays the shake on the element. */
export function shake(el) {
  return play(el, 'shake');
}

/** stamp-in: the stamp lands with a flash behind it (what it replaces leaves by a CSS transition on the same state change). */
export function stampIn(stamp, flash) {
  return Promise.all([play(stamp, 'stamp'), play(flash, 'stamp-flash')]);
}

/** icon-swap: the element holds two `.t-icon` children (data-icon a / b); shows `toIcon` ('a' or 'b'). */
export function iconSwap(el, toIcon) {
  if (el.getAttribute('data-state') !== toIcon) el.setAttribute('data-state', toIcon);
}
