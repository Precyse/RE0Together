import { h, setText } from './dom.js';
import { play, clear } from './motion.js';

/** The notice row (refusal or invite): the words, an optional fix button and, for a refusal, Report. A new notice rises in like a toast, a cleared one sinks out. */
export function createNotice(ctx) {
  const { send } = ctx;
  const words = h('span', { class: 'notice-words' });
  const action = h('button', { class: 'btn notice-button', type: 'button', on: { click: () => send('notice.fix') } });
  const report = h('button', { class: 'btn btn-ghost notice-button', type: 'button', text: 'Report', on: { click: () => send('report.create') } });
  const el = h('div', { class: 'notice', hidden: true }, words, action, report);
  let key = null;
  let leaving = false;

  function leave() {
    if (key === null || leaving) return;
    leaving = true;
    play(el, 'toast-out', { keep: true }).then(() => {
      if (!leaving) return;
      leaving = false;
      key = null;
      el.hidden = true;
      clear(el, 'toast-out');
    });
  }

  function update(notice) {
    if (!notice) {
      leave();
      return;
    }
    const isNew = notice.key !== key || leaving;
    leaving = false;
    clear(el, 'toast-out');
    key = notice.key;
    setText(words, notice.words);
    action.hidden = !notice.action;
    if (notice.action) setText(action, notice.action);
    report.hidden = !notice.report;
    el.hidden = false;
    if (isNew) play(el, 'toast-in');
  }

  return { el, update };
}
