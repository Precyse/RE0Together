export default async function selectKeys({ key, wait }) {
  await wait(100);
  key('ArrowRight');
  key('Tab');
  key('ArrowLeft');
  key('ArrowRight');
}
