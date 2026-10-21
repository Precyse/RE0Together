import { svg } from './dom.js';

/** Fixed drawings: viewBox [width, height] and the shapes (path d, or line / rect / circle attributes). Arrows and the swap glyph are filled. */
const ICONS = {
  check: { box: [18, 18], shapes: [['path', { d: 'M3 9.5 L7.5 14 L15 4.5', pathLength: 100 }]] },
  cross: { box: [18, 18], shapes: [['path', { d: 'M4 4 L14 14 M14 4 L4 14' }]] },
  circle: { box: [18, 18], shapes: [['circle', { cx: 9, cy: 9, r: 4.5 }]] },
  arrowLeft: { box: [12, 14], fill: true, shapes: [['path', { d: 'M12 0 L0 7 L12 14 Z' }]] },
  arrowRight: { box: [12, 14], fill: true, shapes: [['path', { d: 'M0 0 L12 7 L0 14 Z' }]] },
  arrowUp: { box: [14, 8], fill: true, shapes: [['path', { d: 'M0 8 L7 0 L14 8 Z' }]] },
  arrowDown: { box: [14, 8], fill: true, shapes: [['path', { d: 'M0 0 L14 0 L7 8 Z' }]] },
  cog: {
    box: [24, 24],
    shapes: [
      ['circle', { cx: 12, cy: 12, r: 3 }],
      ['path', { d: 'M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M4.9 19.1L7 17M17 7l2.1-2.1' }],
    ],
  },
  close: { box: [12, 12], shapes: [['path', { d: 'M1 1 L11 11 M11 1 L1 11' }]] },
  minimize: { box: [12, 12], shapes: [['path', { d: 'M1 6 L11 6' }]] },
  copy: { box: [18, 18], shapes: [['path', { d: 'M6.5 2.5h5v2h-5z M5 4.5H4v11h10v-11h-1' }]] },
  expand: { box: [12, 12], shapes: [['path', { d: 'M1 5 L1 1 L5 1 M7 11 L11 11 L11 7' }]] },
  lock: { box: [18, 22], shapes: [['path', { d: 'M4 10V7a5 5 0 0 1 10 0v3' }], ['rect', { x: 2, y: 10, width: 14, height: 10 }]] },
  swap: {
    box: [26, 22],
    fill: true,
    shapes: [['path', { d: 'M0 3h16V0l10 5.5L16 11V8H0z M26 14H10v-3L0 16.5 10 22v-3h16z' }]],
  },
  swapStage: { box: [26, 22], fill: true, shapes: [['path', { d: 'M8 0 L0 6 L8 12 Z M18 10 L26 16 L18 22 Z M6 5h16v2H6z M4 15h16v2H4z' }]] },
  person: {
    box: [20, 20],
    shapes: [['circle', { cx: 10, cy: 6, r: 3.5 }], ['path', { d: 'M3 18c0-4 3-6 7-6s7 2 7 6' }]],
  },
};

/** A fixed SVG glyph in `currentColor`; `size` is its width in px, the height keeps the drawing's proportions. */
export function icon(name, size) {
  const def = ICONS[name];
  if (!def) throw new Error(`Unknown icon ${name}`);
  const [w, hgt] = def.box;
  const el = svg('svg', {
    class: `ic ic-${name}${def.fill ? ' ic-fill' : ''}`,
    width: size,
    height: Math.round((size * hgt) / w * 100) / 100,
    viewBox: `0 0 ${w} ${hgt}`,
    'aria-hidden': 'true',
  });
  for (const [tag, attrs] of def.shapes) el.append(svg(tag, attrs));
  return el;
}

export const ICON_NAMES = Object.keys(ICONS);
