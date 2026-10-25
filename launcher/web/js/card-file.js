import { h, setText, setVar } from './dom.js';
import { createImage, setImage } from './image.js';
import { roll, stampIn, swapText } from './motion.js';
import { createSpinner } from './spinner.js';

const STATE_PROGRESS = 'progress';
const LABELS = { player: 'Player', agent: 'Agent', kit: 'Field kit', ready: 'Ready' };
const SEAT_PREFIX = 'P';
const SEAT_SEPARATOR = ' / ';
const BAR_FULL = '1';
const WAITING = String(false);

/** The seat's number, then the card's corner (You, the hold countdown or the ping) when it has one. */
function seatText(card) {
  return SEAT_PREFIX + card.seat + (card.corner ? SEAT_SEPARATOR + card.corner : '');
}

/**
 * The field file of a game with characters, the card of the wide layout: the character's unit and the seat across the top, the player, agent and
 * outfit as rows, the outfit's figure on the right. While the player is not ready the foot holds the status line and the card's bottom edge the
 * save bar; when it is, the READY stamp. The stamp lands (and the status line and bar leave, by CSS transition on `data-ready`) only when a card
 * that was shown not ready becomes ready. `update(card)`.
 */
export function createFile() {
  const figure = createImage('cf-figure');
  const unit = h('span', { class: 'caption' });
  const seat = h('span', { class: 'caption' });
  const player = h('span', { class: 'cf-player' });
  const agent = h('span');
  const outfit = h('span');
  const rows = h('div', { class: 'cf-rows' },
    h('span', { class: 'cf-key', text: LABELS.player }), player,
    h('span', { class: 'cf-key', text: LABELS.agent }), agent,
    h('span', { class: 'cf-key', text: LABELS.kit }), outfit);
  const spinner = createSpinner('small');
  const words = h('span', { class: 'cf-words' });
  const percent = h('span', { class: 'cf-percent', hidden: true });
  const status = h('div', { class: 'cf-status' }, spinner, words, percent);
  const stamp = h('div', { class: 'cf-stamp', text: LABELS.ready });
  const flash = h('div', { class: 'cf-flash', 'aria-hidden': 'true' });
  const fill = h('div', { class: 'cf-fill' });
  const bar = h('div', { class: 'cf-bar', hidden: true }, fill);
  const el = h('div', { class: 'c-file' }, flash, figure, h('div', { class: 'cf-head' }, unit, seat), rows, status, stamp, bar);

  return {
    el,
    update(card) {
      setImage(figure, card.figure?.src);
      setText(unit, card.unit);
      roll(seat, seatText(card));
      setText(player, card.name);
      setText(agent, card.agent);
      setText(outfit, card.outfit);
      const arrived = el.dataset.ready === WAITING && card.ready;
      el.dataset.ready = String(card.ready);
      if (card.ready) {
        setVar(fill, '--v', BAR_FULL);
        if (arrived) stampIn(stamp, flash);
        return;
      }
      spinner.hidden = card.state !== STATE_PROGRESS;
      swapText(words, card.words);
      roll(percent, card.progress === null ? '' : card.right);
      bar.hidden = card.progress === null;
      if (card.progress !== null) setVar(fill, '--v', String(card.progress));
    },
  };
}
