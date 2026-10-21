import { isReduced, tokenMs, tokenValue } from './motion.js';

const FLIPPING_CLASS = 'is-flipping';
const MIN_DELTA_PX = 0.5;

const clipTo = (bottomPx) => `inset(0 0 calc(100% - ${bottomPx}px) 0)`;

/**
 * The hero's height changes (full pane height while nothing is below it, its own height otherwise) without layout work per frame: the change is applied at once,
 * then the backdrop's clip edge and everything hanging from the hero's bottom (`movers`: the element and where its top is when it is hidden) move from where they were with transforms.
 * `run(apply)` batches every change of one task into one move. The move is skipped until `arm()`, while the hero is not drawn, and under reduced motion.
 */
export function createHeroLayout({ pane, hero, backdrop, movers }) {
  let armed = false;
  let before = null;

  const measure = () => {
    const paneTop = pane.getBoundingClientRect().top;
    const bottom = hero.getBoundingClientRect().bottom - paneTop;
    return { bottom, tops: movers.map((mover) => (mover.hidden ? bottom : mover.getBoundingClientRect().top - paneTop)) };
  };

  function move(from, to) {
    const duration = tokenMs('--resize-dur');
    const easing = tokenValue('--resize-ease');
    const running = [backdrop.animate({ clipPath: [clipTo(from.bottom), clipTo(to.bottom)] }, { duration, easing })];
    movers.forEach((mover, index) => {
      const delta = from.tops[index] - to.tops[index];
      if (mover.hidden || Math.abs(delta) < MIN_DELTA_PX) return;
      running.push(mover.animate({ transform: [`translateY(${delta}px)`, 'translateY(0)'] }, { duration, easing }));
    });
    pane.classList.add(FLIPPING_CLASS);
    Promise.allSettled(running.map((animation) => animation.finished)).then(() => pane.classList.remove(FLIPPING_CLASS));
  }

  function finish() {
    const from = before;
    before = null;
    if (hero.offsetHeight === 0) return;
    const to = measure();
    if (Math.abs(from.bottom - to.bottom) >= MIN_DELTA_PX || from.tops.some((top, index) => Math.abs(top - to.tops[index]) >= MIN_DELTA_PX)) move(from, to);
  }

  return {
    arm() {
      armed = true;
    },
    run(apply) {
      if (armed && !isReduced() && !before && hero.offsetHeight > 0) {
        before = measure();
        queueMicrotask(finish);
      }
      apply();
    },
  };
}
