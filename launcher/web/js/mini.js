import { h } from './dom.js';
import { icon } from './icons.js';
import { swapText, rollDigits } from './motion.js';
import { createCards, createPortrait } from './cards.js';
import { createImage, setImage } from './image.js';
import { createSpinner } from './spinner.js';

const EXPAND_PX = 12;
const KIND_PING = 'ping';
const KIND_JOINING = 'joining';
const PLAY_SOLO_LABEL = 'Play solo';
const LEAVE_LABEL = 'Leave';

function footerButton(label, onClick) {
  return h('button', { class: 'btn btn-ghost mini-button', type: 'button', text: label, on: { click: onClick } });
}

/** The partner-lost alert that replaces the seats: portrait, name, words, a bar shrinking with the hold, the holding line. */
function createAlert() {
  const portrait = createPortrait();
  const name = h('span', { class: 'mini-alert-name' });
  const words = h('span');
  const fill = h('div', { class: 'mini-alert-fill' });
  const bar = h('div', { class: 'mini-alert-bar' }, fill);
  const holding = h('span', { class: 'mini-alert-holding mono' });
  const el = h('div', { class: 'mini-alert', hidden: true }, portrait.el, h('div', { class: 'mini-alert-text' }, name, h('div', { class: 'mini-alert-words' }, createSpinner('small'), words), bar, holding));
  return {
    el,
    update(alert) {
      el.hidden = !alert;
      if (!alert) return;
      portrait.update(alert.card);
      swapText(name, alert.card.name);
      swapText(words, alert.words);
      bar.hidden = alert.remaining === null;
      if (alert.remaining !== null) fill.style.setProperty('--v', String(alert.remaining));
      holding.hidden = !alert.holding;
      swapText(holding, alert.holding);
    },
  };
}

/** The always-on-top mini window: header (game, ping or joining or lost, Expand), the seats or the alert, the event line with Play solo and Leave. */
export function createMini(ctx) {
  const { send } = ctx;
  const capsule = createImage('mini-capsule');
  const gameName = h('span', { class: 'mini-game' });
  const spinner = createSpinner('small');
  const status = h('span', { class: 'mini-status' });
  const expand = h('button', { class: 'mini-expand no-drag', type: 'button', 'aria-label': 'Expand', on: { click: () => send('mini.expand') } }, icon('expand', EXPAND_PX));
  const header = h('header', { class: 'mini-header drag' }, capsule, gameName, h('div', { class: 'mini-spacer' }), spinner, status, expand);
  const cards = createCards(ctx, 'mini');
  const alert = createAlert();
  const body = h('div', { class: 'mini-body' }, cards.el, alert.el);
  const event = h('span', { class: 'mini-event mono' });
  const playSolo = footerButton(PLAY_SOLO_LABEL, () => send('session.playSolo'));
  const footer = h('footer', { class: 'mini-footer' }, event, playSolo, footerButton(LEAVE_LABEL, () => send('session.leave')));
  const el = h('div', { class: 'mini' }, header, body, footer);

  function showHeader({ kind, text }) {
    header.dataset.kind = kind;
    spinner.hidden = kind !== KIND_JOINING;
    if (kind === KIND_PING) rollDigits(status, text);
    else {
      delete status.dataset.rolled;
      swapText(status, text);
    }
  }

  return {
    el,
    update(message) {
      const { mini, art } = message;
      setImage(capsule, art.capsule);
      swapText(gameName, mini.gameName);
      showHeader(mini.header);
      cards.setLayout(mini.layout);
      cards.update(mini.seats);
      cards.el.hidden = mini.alert !== null;
      alert.update(mini.alert);
      el.toggleAttribute('data-alert', mini.alert !== null);
      swapText(event, mini.event);
      playSolo.hidden = !mini.playSolo;
    },
  };
}
