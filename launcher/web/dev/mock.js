const FIXTURE_PARAM = 'fixture';
const SEQUENCE_PARAM = 'sequence';
const FRAME_PARAM = 'frame';
const SMOKE_PARAM = 'smoke';
const REDUCED_PARAM = 'reduced';
const MINI_PAGE = 'mini.html';
const SETTLE_LIMIT_MS = 1500;
const SETTLE_POLL_MS = 50;
const FRAME_FALLBACK_MS = 100;
const READY_ATTRIBUTE = 'data-ready';
const ERROR_ATTRIBUTE = 'data-error';
const HOUSEKEEPING = ['ready', 'page.rendered'];

const params = new URLSearchParams(location.search);
const isMini = location.pathname.endsWith(MINI_PAGE);

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const nextFrame = () => Promise.race([new Promise((resolve) => requestAnimationFrame(resolve)), wait(FRAME_FALLBACK_MS)]);
async function frames(count) {
  for (let i = 0; i < count; i += 1) await nextFrame();
}

function finiteAnimations() {
  return document.getAnimations().filter((a) => a.effect.getComputedTiming().iterations !== Infinity && a.playState === 'running');
}

/** Waits until no finite animation is running (or the limit passes). */
async function settle() {
  await frames(2);
  for (let waited = 0; waited < SETTLE_LIMIT_MS && finiteAnimations().length; waited += SETTLE_POLL_MS) await wait(SETTLE_POLL_MS);
  await frames(2);
}

async function loadFixture(key) {
  return (await import(`./fixtures/${key}.js`)).default;
}

/** A sequence file: { keys: [fixture...], transitions?: [transition per key], events?: { index: [event names after that state] }, ms }; anything else is a comma list of fixture keys. */
async function sequenceOf(name) {
  try {
    return (await import(`./sequences/${name}.js`)).default;
  } catch {
    return { keys: name.split(',') };
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
  report.hidden = true;
  report.textContent = JSON.stringify(window.__sent.filter((m) => !HOUSEKEEPING.includes(m.type)), null, 1);
  document.body.append(report);
}

async function play(deliver) {
  const { keys, transitions = [], events = {} } = params.has(SEQUENCE_PARAM) ? await sequenceOf(params.get(SEQUENCE_PARAM)) : { keys: [params.get(FIXTURE_PARAM)] };
  const first = await loadFixture(keys[0]);
  deliver({ type: 'hello', page: isMini ? 'mini' : 'main', debug: false, ...first.hello, reducedMotion: params.get(REDUCED_PARAM) === '1' });
  if (first.log) deliver({ type: 'log', lines: first.log });
  let seq = 0;
  for (const [index, key] of keys.entries()) {
    const fixture = index === 0 ? first : await loadFixture(key);
    seq += 1;
    const body = isMini ? fixture.mini : fixture.state;
    deliver({ ...body, ...(transitions[index] && { transition: transitions[index] }), type: 'state', seq });
    for (const name of events[index] ?? []) deliver({ type: 'event', name });
    if (index < keys.length - 1) await settle();
  }
  for (const name of first.events ?? []) deliver({ type: 'event', name });
  if (params.has(FRAME_PARAM)) freeze(Number(params.get(FRAME_PARAM)));
  else await settle();
  const smoke = params.get(SMOKE_PARAM) ?? first.smoke;
  if (smoke) await runSmoke(smoke);
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
      if (message.type === 'ready') play((reply) => listener(reply)).catch((error) => document.documentElement.setAttribute(ERROR_ATTRIBUTE, String(error?.stack ?? error)));
    },
  };
}
