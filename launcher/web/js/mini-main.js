import { connect, send } from './bridge.js';
import * as motion from './motion.js';
import * as icons from './icons.js';
import { createMini } from './mini.js';

const mini = createMini({ send, motion, icons });
document.getElementById('app').append(mini.el);

connect({
  hello(message) {
    document.documentElement.classList.toggle('reduced', Boolean(message.reducedMotion));
  },
  state(message) {
    mini.update(message);
    requestAnimationFrame(() => send('page.rendered', { seq: message.seq }));
  },
  log() {},
  event(name) {
    if (name === 'shown') motion.play(mini.el, 'modal-in');
    else if (name === 'miniClose') motion.play(mini.el, 'modal-out', { keep: true }).then(() => send('mini.closed'));
  },
});
