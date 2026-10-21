const FIXTURE_PARAM = 'fixture';
const SEQUENCE_PARAM = 'sequence';
const FRAME_PARAM = 'frame';
const SMOKE_PARAM = 'smoke';
const REDUCED_PARAM = 'reduced';
const MINI_PAGE = 'mini.html';
const SETTLE_LIMIT_MS = 1500;
const SETTLE_POLL_MS = 50;
const FRAME_MS = 17;
const READY_ATTRIBUTE = 'data-ready';
const HOUSEKEEPING = ['ready', 'page.rendered'];

const params = new URLSearchParams(location.search);
const isMini = location.pathname.endsWith(MINI_PAGE);

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
/**
 * Frames are waited on a timer: in headless virtual time requestAnimationFrame callbacks arrive only when the time budget is used up. A rAF loop
 * that does nothing still runs so the compositor keeps producing frames and animations start.
 */
const frames = (count) => wait(count * FRAME_MS);
(function keepFrames() { requestAnimationFrame(keepFrames); }());

function finiteAnimations() {
  return document.getAnimations().filter((a) => a.effect.getComputedTiming().iterations !== Infinity && a.playState === 'running');
}

/** Waits until no finite animation is running (or the limit passes), then finishes any that never got going (headless virtual time may not start them). */
async function settle() {
  await frames(2);
  for (let waited = 0; waited < SETTLE_LIMIT_MS && finiteAnimations().length; waited += SETTLE_POLL_MS) await wait(SETTLE_POLL_MS);
  for (const animation of finiteAnimations()) animation.finish();
  await frames(2);
}

const IMAGE_PATTERN = /\/dev\/art\/[^"]+/g;

/** Loads every image a fixture names into the browser cache first, so a frozen frame never shows a picture that is still on its way. */
function preloadImages(fixtures) {
  const sources = new Set(JSON.stringify(fixtures).match(IMAGE_PATTERN));
  return Promise.all([...sources].map((src) => new Promise((resolve) => {
    const image = new Image();
    image.onload = resolve;
    image.onerror = resolve;
    image.src = src;
  })));
}

async function loadFixture(key) {
  return (await import(`./fixtures/${key}.js`)).default;
}

async function sequenceKeys(name) {
  try {
    return (await import(`./sequences/${name}.js`)).default.keys;
  } catch {
    return name.split(',');
  }
}

function freeze(ms) {
  for (const animation of document.getAnimations()) {
    animation.pause();
    animation.currentTime = ms;
  }
}

const smokeKit = {
  wait,
  click: (selector) => document.querySelector(selector).click(),
  key: (key) => document.dispatchEvent(new KeyboardEvent('keydown', { key, bubbles: true, cancelable: true })),
  type(selector, text) {
    const input = document.querySelector(selector);
    input.value = text;
    input.dispatchEvent(new Event('input', { bubbles: true }));
  },
  frames,
};

async function runSmoke(name) {
  await (await import(`./smoke/${name}.js`)).default(smokeKit);
  await settle();
  const report = document.createElement('pre');
  report.id = 'smoke';
  report.textContent = JSON.stringify(window.__sent.filter((m) => !HOUSEKEEPING.includes(m.type)), null, 1);
  document.body.append(report);
}

async function play(deliver) {
  const keys = params.has(SEQUENCE_PARAM) ? await sequenceKeys(params.get(SEQUENCE_PARAM)) : [params.get(FIXTURE_PARAM)];
  const fixtures = await Promise.all(keys.map(loadFixture));
  await preloadImages(fixtures);
  const first = fixtures[0];
  deliver({ type: 'hello', page: isMini ? 'mini' : 'main', debug: false, ...first.hello, reducedMotion: params.get(REDUCED_PARAM) === '1' });
  if (first.log) deliver({ type: 'log', lines: first.log });
  let seq = 0;
  for (const [index, fixture] of fixtures.entries()) {
    seq += 1;
    deliver({ ...(isMini ? fixture.mini : fixture.state), type: 'state', seq });
    for (const name of fixture.events ?? []) deliver({ type: 'event', name });
    if (index < fixtures.length - 1) await settle();
  }
  if (params.has(FRAME_PARAM)) {
    await frames(2);
    freeze(Number(params.get(FRAME_PARAM)));
  } else await settle();
  if (params.has(SMOKE_PARAM)) await runSmoke(params.get(SMOKE_PARAM));
  await frames(2);
  document.documentElement.setAttribute(READY_ATTRIBUTE, '');
}

/** The dev transport: same shape as the WebView2 one; every command the page sends lands in window.__sent. */
export function createTransport() {
  let listener = null;
  window.__sent = [];
  return {
    listen: (handler) => { listener = handler; },
    post(message) {
      window.__sent.push(message);
      if (message.type === 'ready') play((reply) => listener(reply));
    },
  };
}
