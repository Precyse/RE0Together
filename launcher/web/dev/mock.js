const FIXTURE_PARAM = 'fixture';
const SEQUENCE_PARAM = 'sequence';
const FRAME_PARAM = 'frame';
const SMOKE_PARAM = 'smoke';
const REDUCED_PARAM = 'reduced';
const MINI_PAGE = 'mini.html';
const SETTLE_LIMIT_MS = 1500;
const SETTLE_POLL_MS = 50;
const READY_ATTRIBUTE = 'data-ready';
const HOUSEKEEPING = ['ready', 'page.rendered'];

const params = new URLSearchParams(location.search);
const isMini = location.pathname.endsWith(MINI_PAGE);

const frames = (count) => new Promise((resolve) => {
  const step = (left) => (left === 0 ? resolve() : requestAnimationFrame(() => step(left - 1)));
  step(count);
});
const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

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
  const first = await loadFixture(keys[0]);
  deliver({ type: 'hello', page: isMini ? 'mini' : 'main', debug: false, ...first.hello, reducedMotion: params.get(REDUCED_PARAM) === '1' });
  if (first.log) deliver({ type: 'log', lines: first.log });
  let seq = 0;
  for (const [index, key] of keys.entries()) {
    const fixture = index === 0 ? first : await loadFixture(key);
    seq += 1;
    deliver({ ...(isMini ? fixture.mini : fixture.state), type: 'state', seq });
    if (index < keys.length - 1) await settle();
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
