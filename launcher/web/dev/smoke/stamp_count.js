/** Reports how many stamp animations are on show (a frozen frame keeps them): one after a card turns ready, none for a card that was ready from the first render. */
export default async function stampCount({ wait }) {
  await wait(100);
  const stamps = document.getAnimations().filter((animation) => animation.animationName === 't-stamp').length;
  window.__sent.push({ type: 'stamps', stamps });
}
