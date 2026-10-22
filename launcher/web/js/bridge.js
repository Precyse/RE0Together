/** The page's command names (SPEC 3.3), one per line; C# keeps the same table in UiCommands.cs. */
export const COMMANDS = [
  'ready',
  'page.rendered',
  'window.minimize',
  'window.close',
  'close.confirm',
  'tab.select',
  'game.select',
  'host',
  'join.code',
  'join.friend',
  'lobby.open',
  'lobby.action',
  'lobbies.filter',
  'lobbies.game',
  'popup.options',
  'popup.close',
  'option.set',
  'mod.install',
  'mod.retry',
  'notice.fix',
  'report.create',
  'session.cancel',
  'session.start',
  'session.leave',
  'session.copyCode',
  'session.relaunch',
  'session.mini',
  'session.playSolo',
  'mini.expand',
  'mini.closed',
  'view.open',
  'view.close',
  'select.step',
  'select.swap',
  'settings.updates',
  'settings.visibility',
  'settings.logs',
  'game.browse',
  'game.resetFolder',
  'game.uninstall',
  'game.archive',
  'update.click',
];

let transport = null;

function webViewTransport() {
  const { webview } = window.chrome;
  return {
    post: (message) => webview.postMessage(message),
    listen: (handler) => webview.addEventListener('message', (event) => handler(event.data)),
  };
}

/**
 * Connects to the host (WebView2 when present, else the dev mock) and sends `ready`.
 * handlers: hello(msg), state(msg), log(lines), event(name), timing(ms) (how long applying a state took).
 */
export async function connect(handlers) {
  transport = window.chrome?.webview ? webViewTransport() : (await import('../dev/mock.js')).createTransport();
  transport.listen((message) => {
    switch (message.type) {
      case 'hello': handlers.hello(message); break;
      case 'state': {
        const start = performance.now();
        handlers.state(message);
        handlers.timing?.(performance.now() - start);
        break;
      }
      case 'log': handlers.log(message.lines); break;
      case 'event': handlers.event(message.name); break;
      default: break;
    }
  });
  send('ready');
}

/** Sends one command; `type` must be in COMMANDS. */
export function send(type, fields = {}) {
  if (!COMMANDS.includes(type)) throw new Error(`Unknown command ${type}`);
  transport.post({ type, ...fields });
}
