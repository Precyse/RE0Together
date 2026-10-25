import { h, setText } from './dom.js';
import { swapText } from './motion.js';
import { createAvatar } from './cards.js';
import { createImage, setImage } from './image.js';
import { createSteps } from './steps.js';

const SCREEN = 'joining';
const CANCEL_LABEL = 'Cancel';
const START_AGAIN_LABEL = 'Start again';

/** The joining screen: backdrop art, the host's header and the large step list, then Cancel, the fix of a refusal and Start again. */
export function create(ctx) {
  const { send } = ctx;
  const backdrop = createImage('art-backdrop');
  const logo = createImage('art-logo');
  const host = createAvatar('joining-avatar');
  const title = h('span', { class: 'joining-title' });
  const sub = h('span', { class: 'joining-sub', hidden: true });
  const steps = createSteps('large');
  const fix = h('button', { class: 'btn btn-primary joining-fix', type: 'button', hidden: true, on: { click: () => send('notice.fix') } });
  const startAgain = h('button', { class: 'btn btn-ghost joining-again', type: 'button', text: START_AGAIN_LABEL, hidden: true, on: { click: () => send('session.relaunch') } });
  const cancel = h('button', { class: 'btn btn-ghost joining-cancel', type: 'button', text: CANCEL_LABEL, on: { click: () => send('session.cancel') } });
  const buttons = h('div', { class: 'joining-buttons' }, fix, startAgain, cancel);
  const column = h('div', { class: 'joining-column' }, logo, h('div', { class: 'joining-header' }, host.el, h('div', { class: 'joining-names' }, title, sub)), steps.el, buttons);
  const el = h('section', { class: 'joining' }, backdrop, column);

  function update(state) {
    const { joining } = state;
    if (state.screen !== SCREEN || !joining) return;
    setImage(backdrop, state.art.hero);
    setImage(logo, state.art.logo);
    host.update(joining.host);
    swapText(title, joining.title);
    sub.hidden = !joining.sub;
    swapText(sub, joining.sub);
    steps.update(joining.steps);
    fix.hidden = !joining.fix;
    if (joining.fix) {
      setText(fix, joining.fix.label);
      fix.disabled = !joining.fix.enabled;
    }
    startAgain.hidden = !joining.startAgain;
    buttons.classList.toggle('has-fix', Boolean(joining.fix));
  }

  return { el, update };
}
