import { connect, send } from './bridge.js';
import * as motion from './motion.js';
import * as icons from './icons.js';
import { createScreenHost } from './screen-host.js';
import { createPerf } from './perf.js';
import * as topbar from './topbar.js';
import * as home from './home.js';
import * as lobbies from './lobbies.js';
import * as joining from './joining.js';
import * as session from './session.js';
import * as settings from './settings.js';
import * as select from './select.js';
import * as drawer from './drawer.js';
import * as modal from './modal.js';

const PERF_PARAM = 'perf';
const FOOTER_SCREENS = ['home', 'lobbies'];

/** Which module renders each `screen` of the state; hosting and lobby are two instances of the session screen so a transition keeps both frames. */
const SCREEN_MODULES = { home, lobbies, joining, hosting: session, lobby: session, settings, select };

const ctx = { send, motion, icons };
const root = document.getElementById('app');
const screensEl = document.getElementById('screens');
const bar = topbar.create(ctx);
const log = drawer.create(ctx);
const layer = modal.create(ctx);
const perf = createPerf();
const screens = Object.fromEntries(Object.entries(SCREEN_MODULES).map(([name, module]) => [name, module.create({ ...ctx, screen: name })]));
const host = createScreenHost(screensEl, screens);
const parts = [bar, log, layer, ...Object.values(screens)];
let latest = null;

root.prepend(bar.el);
root.append(log.el, layer.el, perf.el);

function apply(message) {
  latest = message;
  for (const part of parts) part.update(message);
  host.show(message.screen, message.transition);
  log.show(FOOTER_SCREENS.includes(message.screen));
  requestAnimationFrame(() => send('page.rendered', { seq: message.seq }));
}

document.addEventListener('keydown', (event) => {
  if (event.key !== 'Escape' || event.defaultPrevented || !latest) return;
  if (layer.confirmOpen()) layer.stay();
  else if (latest.popup) send('popup.close');
  else if (latest.top.mode === 'view') send('view.close');
});

document.addEventListener('visibilitychange', () => {
  document.documentElement.classList.toggle('paused', document.hidden);
});

if (new URLSearchParams(location.search).has(PERF_PARAM)) perf.enable();

connect({
  hello(message) {
    document.documentElement.classList.toggle('reduced', Boolean(message.reducedMotion));
    if (message.debug) perf.enable();
  },
  state: apply,
  log: (lines) => log.addLines(lines),
  event(name) {
    if (name === 'confirmClose') layer.openConfirm();
    for (const part of parts) part.event?.(name);
  },
  timing: (ms) => perf.markApply(ms),
});
