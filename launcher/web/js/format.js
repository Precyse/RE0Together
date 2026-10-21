const PING_UNIT = 'ms';

/** A ping as `N ms`; text from the host passes through, an unknown ping is empty. */
export function pingText(ping) {
  return typeof ping === 'number' ? `${ping} ${PING_UNIT}` : ping ?? '';
}
