import { h, setText } from './dom.js';
import { createSteps } from './steps.js';

/**
 * A mod action's progress as one step row: the matrix spinner, the step text and the percent with a bar that tweens while it runs, the green check
 * at the end, the red cross with the reason on a failure and a Retry under it. `variant` is 'hero' (over the art) or 'line' (in a settings row).
 */
export function createModRun({ send }, variant) {
  const steps = createSteps('small');
  const retry = h('button', { class: 'btn btn-ghost mod-run-retry', type: 'button', hidden: true });
  const el = h('div', { class: `mod-run mod-run-${variant}`, hidden: true }, steps.el, retry);
  let gameId = null;
  retry.addEventListener('click', () => send('mod.retry', { gameId }));

  function update(run, id) {
    el.hidden = !run;
    steps.update(run ? [run.step] : []);
    if (!run) return;
    gameId = id;
    el.toggleAttribute('data-failed', run.failed);
    retry.hidden = !run.retry;
    if (!run.retry) return;
    setText(retry, run.retry.label);
    retry.disabled = !run.retry.enabled;
  }

  return { el, update };
}
