import { play, clear } from './motion.js';

const Z_TOP = 'z-top';
const PAGE_RECIPES = ['page-in', 'page-out'];
const PANEL_RECIPES = ['panel-in', 'panel-out'];

/**
 * One element per screen, all kept in the DOM; exactly one is shown at rest. `show(name, transition)` plays the transition of the
 * state: none (instant), page (page-side-by-side, direction -1 / 1), panelIn (the new screen rises over the old), panelOut (the old one sinks).
 */
export function createScreenHost(container, screens) {
  let current = null;
  let run = 0;

  for (const screen of Object.values(screens)) {
    screen.el.classList.add('screen');
    screen.el.hidden = true;
    container.append(screen.el);
  }

  function settle(name) {
    for (const [key, screen] of Object.entries(screens)) {
      screen.el.hidden = key !== name;
      screen.el.classList.remove(Z_TOP);
      for (const recipe of [...PAGE_RECIPES, ...PANEL_RECIPES]) clear(screen.el, recipe);
    }
  }

  function show(name, transition = { kind: 'none', direction: 0 }) {
    if (name === current) return;
    const previous = current === null ? null : screens[current].el;
    const next = screens[name].el;
    current = name;
    run += 1;
    const token = run;
    settle(name);
    if (!previous || transition.kind === 'none') return;
    const done = () => {
      if (token === run) settle(name);
    };
    previous.hidden = false;
    if (transition.kind === 'page') {
      const dir = transition.direction || 1;
      play(next, 'page-in', { dir });
      play(previous, 'page-out', { dir, keep: true }).then(done);
    } else if (transition.kind === 'panelIn') {
      next.classList.add(Z_TOP);
      play(next, 'panel-in', { keep: true }).then(done);
    } else if (transition.kind === 'panelOut') {
      previous.classList.add(Z_TOP);
      play(previous, 'panel-out', { keep: true }).then(done);
    } else {
      done();
    }
  }

  return { show, get current() { return current; } };
}
