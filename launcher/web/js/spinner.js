import { h } from './dom.js';

const GRID_DOTS = 16;
const CORNERS = [0, 3, 12, 15];
const CENTRE = [5, 6, 9, 10];
/** Grid positions of the ring dots in orbit order; dot k is delayed k / RING.length of the cycle (CSS reads --k). */
const RING = [1, 2, 7, 11, 14, 13, 8, 4];

/** The matrix loader, orbit variant: a 4x4 dot grid without corners, the ring dots pulse one after another. `size` is 'small' (14 px) or 'large' (18 px). */
export function createSpinner(size) {
  const dots = Array.from({ length: GRID_DOTS }, (_, index) => {
    const dot = h('i', { class: CORNERS.includes(index) ? 'g' : CENTRE.includes(index) ? 'c' : '' });
    if (RING.includes(index)) dot.style.setProperty('--k', String(RING.indexOf(index)));
    return dot;
  });
  return h('span', { class: `spin spin-${size}`, 'aria-hidden': 'true' }, ...dots);
}
