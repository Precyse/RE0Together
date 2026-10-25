import { h, setVar } from './dom.js';
import { icon } from './icons.js';
import { isReduced, swapText, tokenMs, tokenValue } from './motion.js';
import { createImage, setImage } from './image.js';

const SCREEN = 'select';
const HALVES = 2;
const ARROW_PX = 14;
const SWAP_ICON_PX = 26;
const LOCK_ICON_PX = 18;
const SLIDE_PX = 24;
const SWAP_MS = 400;
const DOT_PITCH_PX = 11;
const KEY_STEPS = { ArrowLeft: -1, ArrowRight: 1 };
const KIND_READONLY = 'readonly';

/** Which way the outfit moved: +1 next, -1 previous (wrapping), from the dot index before and after. */
function directionOf(previous, next, dots) {
  if (previous === null || dots < 2 || previous === next) return 1;
  return (previous + 1) % dots === next ? 1 : (next + 1) % dots === previous ? -1 : 1;
}

function figureFrames(offsetX, blur) {
  return { transform: `translateX(${offsetX}px)`, opacity: 0, filter: `blur(${blur})` };
}

const REST_FRAME = { transform: 'translateX(0)', opacity: 1, filter: 'blur(0)' };

/** One half of the stage: figure, name and role, outfit arrows with the label, the sliding dots. */
function createHalf(ctx, index) {
  const box = h('div', { class: 'select-figure' });
  const name = h('span', { class: 'select-name' });
  const role = h('span', { class: 'select-role' });
  const arrow = (direction, label, glyph) => h('button', {
    class: 'select-arrow', type: 'button', 'aria-label': label, on: { click: () => ctx.send('select.step', { half: index, direction }) },
  }, icon(glyph, ARROW_PX));
  const previous = arrow(-1, 'Previous outfit', 'arrowLeft');
  const next = arrow(1, 'Next outfit', 'arrowRight');
  const label = h('span', { class: 'select-label' });
  const marker = h('i', { class: 'select-dot-active' });
  const dots = h('div', { class: 'select-dots' });
  const el = h('div', { class: 'select-half' }, box, h('div', { class: 'select-names' }, name, role), h('div', { class: 'select-arrows' }, previous, label, next), dots);
  let figure = null;
  let dotCount = -1;
  let shownIndex = null;

  function animate(img, from, to, duration, easing) {
    if (isReduced() || !img.animate) return null;
    return img.animate([from, to], { duration, easing, fill: 'both' });
  }

  function showFigure(src, direction, crossFrom) {
    if (figure && figure.getAttribute('src') === src) return;
    const old = figure;
    figure = createImage('select-image');
    setImage(figure, src);
    box.append(figure);
    if (!old) return;
    if (crossFrom !== null) {
      animate(figure, { transform: `translateX(${crossFrom}px)` }, { transform: 'translateX(0)' }, SWAP_MS, 'ease-in-out');
      old.remove();
      return;
    }
    const duration = tokenMs('--page-slide-dur');
    const easing = tokenValue('--page-slide-ease');
    const blur = tokenValue('--page-blur');
    animate(figure, figureFrames(SLIDE_PX * direction, blur), REST_FRAME, duration, easing);
    const leaving = animate(old, REST_FRAME, figureFrames(-SLIDE_PX * direction, blur), duration, easing);
    if (leaving) leaving.finished.then(() => old.remove(), () => old.remove());
    else old.remove();
  }

  function showDots(half) {
    if (dotCount !== half.dots) {
      dotCount = half.dots;
      dots.replaceChildren(...Array.from({ length: dotCount }, () => h('i')), marker);
    }
    marker.hidden = dotCount === 0;
    setVar(marker, '--at', String(half.index * DOT_PITCH_PX));
  }

  return {
    el,
    box,
    update(half, { focused, crossFrom }) {
      const direction = directionOf(shownIndex, half.index, half.dots);
      shownIndex = half.index;
      el.dataset.kind = half.kind;
      el.toggleAttribute('data-focus', focused && half.kind !== KIND_READONLY);
      setVar(box, '--o', String(half.opacity));
      showFigure(half.figure.src, direction, crossFrom);
      swapText(name, half.name);
      swapText(role, half.role);
      swapText(label, half.label);
      previous.disabled = !half.canCycle;
      next.disabled = !half.canCycle;
      showDots(half);
    },
  };
}

/** The character select (S1 before a session, S3 in one): two halves, the swap button or the lock between them, keyboard stepping. */
export function create(ctx) {
  const backdrop = createImage('art-backdrop');
  const halves = Array.from({ length: HALVES }, (_, index) => createHalf(ctx, index));
  const swap = h('button', { class: 'select-swap', type: 'button', 'aria-label': 'Swap characters', on: { click: () => ctx.send('select.swap') } }, icon('swapStage', SWAP_ICON_PX));
  const lock = h('div', { class: 'select-lock', role: 'img', 'aria-label': 'Character set by the host' }, icon('lock', LOCK_ICON_PX));
  const stage = h('div', { class: 'select-stage' }, halves[0].el, h('div', { class: 'select-centre' }, swap, lock), halves[1].el);
  const el = h('section', { class: 'select-screen' }, backdrop, h('div', { class: 'select-fade' }), stage);
  let model = null;
  let active = false;
  let focused = 0;
  let focusFrom = null;
  let keyboard = false;

  function crossOffsets(next) {
    if (!model || next.centre !== 'swap' || next.left.name === model.left.name) return [null, null];
    if (next.left.name !== model.right.name || next.right.name !== model.left.name) return [null, null];
    const dx = halves[1].box.getBoundingClientRect().left - halves[0].box.getBoundingClientRect().left;
    return dx ? [dx, -dx] : [null, null];
  }

  function render(next) {
    const [leftFrom, rightFrom] = crossOffsets(next);
    halves[0].update(next.left, { focused: keyboard && focused === 0, crossFrom: leftFrom });
    halves[1].update(next.right, { focused: keyboard && focused === 1, crossFrom: rightFrom });
    swap.hidden = next.centre !== 'swap';
    lock.hidden = next.centre !== 'lock';
    model = next;
  }

  function update(state) {
    active = state.screen === SCREEN && Boolean(state.select);
    if (!active) return;
    const { select } = state;
    if (select.focusHalf !== focusFrom) {
      focusFrom = select.focusHalf;
      focused = select.focusHalf;
    }
    setImage(backdrop, state.art.hero);
    render(select);
  }

  document.addEventListener('keydown', (event) => {
    if (!active || event.defaultPrevented) return;
    if (event.key === 'Tab') {
      event.preventDefault();
      keyboard = true;
      focused = (focused + 1) % HALVES;
      render(model);
    } else if (event.key in KEY_STEPS && (focused === 0 ? model.left : model.right).canCycle) {
      event.preventDefault();
      keyboard = true;
      render(model);
      ctx.send('select.step', { half: focused, direction: KEY_STEPS[event.key] });
    }
  });

  return { el, update };
}
