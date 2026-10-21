import { h, svg, setText, setAttr, setVar, keyed, wasCreated } from './dom.js';
import { play, shake, stagger, swapText, rollDigits } from './motion.js';
import { createSpinner } from './spinner.js';
import { createSteps } from './steps.js';

const COMET_LENGTHS = [18, 15, 12, 9, 6, 3];
const OPEN_DOTS = 3;
const HOST_WORD = 'Host';
const STATE_PROBLEM = 'problem';
const STATE_PROGRESS = 'progress';
const STATE_INGAME = 'ingame';
const LAYOUT_TALL = 'tall';
const PAIR_SEATS = 2;
const DIGIT = /\d/;
const ENTER_KEYS = ['Enter', ' '];

/** An <img> that hides itself when it fails to load, so the placeholder under it shows. */
export function createImage(className) {
  const img = h('img', { class: className, alt: '', draggable: 'false' });
  img.addEventListener('error', () => { img.hidden = true; });
  return img;
}

/** Points the image at `src`; no source hides it. */
export function setImage(img, src) {
  if (!src) {
    img.hidden = true;
    img.removeAttribute('src');
    return;
  }
  if (img.getAttribute('src') !== src) {
    img.hidden = false;
    img.setAttribute('src', src);
  }
}

/** A square avatar: the initial on gray until the picture loads. `update(img)` takes `{ src, initial }`. */
export function createAvatar(className) {
  const initial = h('span', { class: 'avatar-initial' });
  const image = createImage('avatar-image');
  const el = h('div', { class: `avatar ${className}` }, initial, image);
  return {
    el,
    update(avatar) {
      setText(initial, avatar.initial);
      setImage(image, avatar.src);
    },
  };
}

/** The portrait box of a card: the outfit cutout, the avatar or the initial, or the open seat's waiting dots. `update(card)`. */
export function createPortrait() {
  const initial = h('span', { class: 'c-initial' });
  const avatar = createImage('c-avatar');
  const figure = createImage('c-figure');
  const dots = h('div', { class: 'c-dots', 'aria-hidden': 'true' }, ...Array.from({ length: OPEN_DOTS }, () => h('i')));
  const el = h('div', { class: 'c-portrait' }, initial, avatar, figure, dots);
  return {
    el,
    update(card) {
      el.classList.toggle('host', card.footer === HOST_WORD || card.corner === HOST_WORD);
      setText(initial, card.figure ? card.figure.initial : card.avatar.initial);
      setImage(figure, card.figure?.src);
      setImage(avatar, card.figure ? null : card.avatar.src);
    },
  };
}

/** A number-bearing text (ping, percent, timer) rolls its digits; plain words are written as they are. */
function roll(el, text) {
  const numeric = DIGIT.test(text);
  el.hidden = !text;
  el.classList.toggle('mono', numeric);
  if (numeric) rollDigits(el, text);
  else {
    delete el.dataset.rolled;
    setText(el, text);
  }
}

function buildComet() {
  const rects = COMET_LENGTHS.map((length) => {
    const rect = svg('rect', { pathLength: 100 });
    rect.style.setProperty('--len', String(length));
    return rect;
  });
  return svg('svg', { class: 'comet', 'aria-hidden': 'true' }, ...rects);
}

function buildLayer(state, ...extra) {
  return h('div', { class: `fx fx-${state}`, 'aria-hidden': 'true' }, h('div', { class: 'tint' }), h('div', { class: 'glow' }), ...extra, h('div', { class: 'edge' }));
}

function buildFx() {
  const dashed = svg('svg', { class: 'dashed' }, svg('rect', {}));
  return [
    buildLayer(STATE_INGAME),
    buildLayer(STATE_PROGRESS, buildComet()),
    buildLayer(STATE_PROBLEM),
    h('div', { class: 'fx fx-open', 'aria-hidden': 'true' }, dashed),
    h('div', { class: 'ring', 'aria-hidden': 'true' }),
  ];
}

function buildCard() {
  const portrait = createPortrait();
  const name = h('span', { class: 'c-name' });
  const corner = h('span', { class: 'c-corner', hidden: true });
  const right = h('span', { class: 'c-right', hidden: true });
  const detail = h('div', { class: 'c-detail', hidden: true });
  const spinner = createSpinner('small');
  const words = h('span', { class: 'c-words' });
  const foot = h('span', { class: 'c-foot', hidden: true });
  const state = h('div', { class: 'c-state' }, spinner, words, foot);
  const steps = createSteps('small');
  const fill = h('div', { class: 'c-fill' });
  const bar = h('div', { class: 'c-bar', hidden: true }, fill);
  const text = h('div', { class: 'c-text' }, h('div', { class: 'c-head' }, name, corner, right), detail, state, steps.el, bar);
  const body = h('div', { class: 'c-body' }, portrait.el, text);
  const el = h('div', { class: 'card' }, ...buildFx(), body);
  el.refs = { bloom: el.querySelector('.fx-ingame .glow'), portrait, name, corner, right, detail, spinner, words, foot, state, steps, bar, fill, body };
  return el;
}

function renderCard(el, card, layout) {
  const { bloom, portrait, name, corner, right, detail, spinner, words, foot, state, steps, bar, fill, body } = el.refs;
  const created = wasCreated(el);
  const previous = el.dataset.state;
  el.dataset.state = card.state;
  if (previous && previous !== card.state) {
    if (card.state === STATE_PROBLEM) shake(el);
    else if (card.state === STATE_INGAME) play(bloom, 'bloom');
  }
  if (!created && el.dataset.occupant !== card.occupant) play(body, 'content-in');
  el.dataset.occupant = card.occupant;
  el.toggleAttribute('data-local', card.local);

  portrait.update(card);
  setText(name, card.name);
  roll(corner, card.corner);
  roll(right, card.right);
  setText(detail, card.detail);
  detail.hidden = !card.detail;

  const listed = card.steps.length > 0;
  const footText = layout === LAYOUT_TALL && card.progress !== null ? card.right : card.footer;
  steps.el.hidden = !listed;
  state.hidden = listed || !(card.words || footText);
  if (listed) steps.update(card.steps);
  spinner.hidden = card.state !== STATE_PROGRESS;
  swapText(words, card.words);
  roll(foot, footText);

  bar.hidden = card.progress === null || listed;
  if (card.progress !== null) setVar(fill, '--v', String(card.progress));
}

/**
 * The player cards of a session in one layout: 'wide', 'tall', 'mini' or 'miniPortrait'. Cards are keyed by seat: a seat whose occupant changes
 * cross-fades its content, a state change cross-fades tint, edge and glow (CSS, by `data-state`) and plays the entry effect, `enter()` brings the
 * cards in staggered. `update(cards, clickable)`: a clickable grid opens the character select from the local card.
 */
export function createCards(ctx, layout) {
  const el = h('div', { class: 'cards', 'data-layout': layout });
  let current = layout;
  let clickable = false;

  function build() {
    const card = buildCard();
    card.addEventListener('click', () => open(card));
    card.addEventListener('keydown', (event) => {
      if (ENTER_KEYS.includes(event.key)) open(card);
    });
    return card;
  }

  function open(card) {
    if (!clickable || !card.hasAttribute('data-local')) return;
    const { character } = card.dataset;
    ctx.send('view.open', character ? { view: 'select', character } : { view: 'select' });
  }

  function update(cards, canClick = false) {
    clickable = canClick;
    setAttr(el, 'data-count', cards.length > PAIR_SEATS ? 'many' : 'pair');
    keyed(el, cards, (card) => card.key, build, (card, data) => {
      renderCard(card, data, current);
      setAttr(card, 'data-character', data.character);
      card.toggleAttribute('data-clickable', clickable && data.local);
      setAttr(card, 'tabindex', clickable && data.local ? '0' : null);
    }, (card, done) => play(card, 'leave', { keep: true }).then(done));
  }

  return {
    el,
    update,
    setLayout(next) {
      current = next;
      el.dataset.layout = next;
    },
    enter: () => stagger([...el.children]),
  };
}
