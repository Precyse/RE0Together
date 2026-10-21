import { h } from './dom.js';

const PERF_KEY = 'P';
const SAMPLE_LIMIT = 600;
const LONG_TASK_MS = 50;
const REFRESH_MS = 500;
const P95 = 0.95;
const MS_PER_SECOND = 1000;

/** Debug overlay: frame intervals (fps, p95), long tasks and state apply time. Off unless `enable()` (hello.debug or ?perf=1); Ctrl+Shift+P toggles. */
export function createPerf() {
  const out = h('pre', { id: 'perf', hidden: true, class: 'perf' });
  const frames = [];
  let applyMs = 0;
  let applyMax = 0;
  let longTasks = 0;
  let longest = 0;
  let last = 0;
  let running = false;
  let frameId = 0;
  let timer = 0;
  let observer = null;

  function frame(now) {
    if (last) {
      frames.push(now - last);
      if (frames.length > SAMPLE_LIMIT) frames.shift();
    }
    last = now;
    if (running) frameId = requestAnimationFrame(frame);
  }

  function render() {
    if (!frames.length) return;
    const sorted = [...frames].sort((a, b) => a - b);
    const mean = frames.reduce((sum, v) => sum + v, 0) / frames.length;
    const p95 = sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * P95))];
    out.textContent = [
      `fps ${(MS_PER_SECOND / mean).toFixed(0)}`,
      `frame mean ${mean.toFixed(1)} ms  p95 ${p95.toFixed(1)} ms  max ${sorted[sorted.length - 1].toFixed(1)} ms`,
      `long tasks ${longTasks}  longest ${longest.toFixed(0)} ms`,
      `apply last ${applyMs.toFixed(2)} ms  max ${applyMax.toFixed(2)} ms`,
    ].join('\n');
  }

  function start() {
    if (running) return;
    running = true;
    out.hidden = false;
    last = 0;
    frameId = requestAnimationFrame(frame);
    timer = setInterval(render, REFRESH_MS);
    if (!observer && typeof PerformanceObserver !== 'undefined' && PerformanceObserver.supportedEntryTypes?.includes('longtask')) {
      observer = new PerformanceObserver((list) => {
        for (const entry of list.getEntries()) {
          if (entry.duration <= LONG_TASK_MS) continue;
          longTasks += 1;
          longest = Math.max(longest, entry.duration);
        }
      });
      observer.observe({ entryTypes: ['longtask'] });
    }
  }

  document.addEventListener('keydown', (event) => {
    if (!(event.ctrlKey && event.shiftKey && event.key === PERF_KEY)) return;
    if (running) {
      running = false;
      cancelAnimationFrame(frameId);
      clearInterval(timer);
      out.hidden = true;
    } else start();
  });

  return {
    el: out,
    enable: start,
    markApply(ms) {
      applyMs = ms;
      applyMax = Math.max(applyMax, ms);
    },
  };
}
