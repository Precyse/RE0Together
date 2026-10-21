import { h, setText, setVar, keyed, wasCreated } from './dom.js';
import { icon } from './icons.js';
import { play, shake, rollDigits } from './motion.js';
import { createSpinner } from './spinner.js';

const GLYPH_PX = { small: 12, large: 18 };
const MARK_ACTIVE = 'active';
const MARK_DONE = 'done';
const MARK_FAILED = 'failed';

/** The glyph of a step mark inside `holder`: hollow circle, matrix spinner, green check or red cross; `animate` draws the check in or fades the cross in. */
export function setGlyph(holder, mark, size, animate) {
  const px = GLYPH_PX[size];
  let glyph;
  if (mark === MARK_ACTIVE) glyph = createSpinner(size);
  else if (mark === MARK_DONE) glyph = icon('check', px);
  else if (mark === MARK_FAILED) glyph = icon('cross', px);
  else glyph = h('span', { class: 'step-circle' });
  holder.replaceChildren(glyph);
  if (!animate) return;
  if (mark === MARK_DONE) play(glyph, 'check', { keep: true });
  else if (mark === MARK_FAILED) play(glyph, 'cross-in', { keep: true });
}

/** Rewrites the children of `container` to one element per item (built by `make`, refreshed by `refresh`), reusing the ones already there. */
export function fillList(container, items, make, refresh) {
  while (container.children.length > items.length) container.lastChild.remove();
  while (container.children.length < items.length) container.append(make());
  items.forEach((item, index) => refresh(container.children[index], item));
}

const makeLine = () => h('div', { class: 'step-line mono' });
const makePair = () => h('div', { class: 'step-pair' }, h('span', { class: 'step-pair-label' }), h('span', { class: 'step-pair-value' }));

function refreshPair(el, [label, value]) {
  setText(el.firstChild, label);
  setText(el.lastChild, value);
}

function buildRow(size) {
  const glyph = h('span', { class: 'step-glyph' });
  const text = h('span', { class: 'step-text' });
  const trailing = h('span', { class: 'step-trailing mono', hidden: true });
  const fill = h('div', { class: 'step-fill' });
  const bar = h('div', { class: 'step-bar', hidden: true }, fill);
  const lines = h('div', { class: 'step-lines', hidden: true });
  const pairs = h('div', { class: 'step-pairs', hidden: true });
  const el = h('li', { class: 'step' }, h('div', { class: 'step-main' }, glyph, text, trailing), bar, lines, pairs);
  el.refs = { glyph, text, trailing, fill, bar, lines, pairs, size };
  return el;
}

function renderRow(el, step) {
  const { glyph, text, trailing, fill, bar, lines, pairs, size } = el.refs;
  const created = wasCreated(el);
  const previous = el.dataset.mark;
  if (previous !== step.mark) {
    el.dataset.mark = step.mark;
    setGlyph(glyph, step.mark, size, !created);
    if (step.mark === MARK_FAILED && !created) shake(el);
  }
  setText(text, step.text);
  trailing.hidden = !step.trailing;
  if (step.trailing) rollDigits(trailing, step.trailing);
  else delete trailing.dataset.rolled;
  bar.hidden = step.bar === null && !step.indeterminate;
  bar.classList.toggle('indeterminate', step.indeterminate);
  if (step.bar !== null) setVar(fill, '--v', String(step.bar));
  lines.hidden = step.lines.length === 0;
  fillList(lines, step.lines, makeLine, setText);
  pairs.hidden = step.pairs.length === 0;
  fillList(pairs, step.pairs, makePair, refreshPair);
}

/**
 * The step list (concepts 7). `size` is 'small' or 'large'. Rows are keyed by position, so only the rows that changed animate: a row that turns
 * Done draws its check in, one that fails fades its cross in and shakes once, trailing digits roll, bars tween, the indeterminate bar loops.
 */
export function createSteps(size) {
  const el = h('ol', { class: `steps steps-${size}` });
  function update(steps) {
    const rows = steps.map((step, index) => ({ ...step, index }));
    keyed(el, rows, (row) => row.index, () => buildRow(size), renderRow);
  }

  return { el, update };
}
