const SVG_NS = 'http://www.w3.org/2000/svg';
const PROP_CLASS = 'class';
const PROP_TEXT = 'text';
const PROP_STYLE = 'style';
const PROP_DATA = 'data';
const PROP_ON = 'on';

function applyProps(el, props) {
  for (const [name, value] of Object.entries(props)) {
    if (value === null || value === undefined || value === false) continue;
    if (name === PROP_CLASS) el.setAttribute('class', value);
    else if (name === PROP_TEXT) el.textContent = value;
    else if (name === PROP_STYLE) for (const [key, v] of Object.entries(value)) el.style.setProperty(key, v);
    else if (name === PROP_DATA) for (const [key, v] of Object.entries(value)) el.setAttribute(`data-${key}`, v);
    else if (name === PROP_ON) for (const [type, handler] of Object.entries(value)) el.addEventListener(type, handler);
    else el.setAttribute(name, value === true ? '' : value);
  }
}

function append(el, children) {
  for (const child of children.flat()) {
    if (child === null || child === undefined || child === false) continue;
    el.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
}

/** Element builder: props `class`, `text`, `style` (CSS custom properties or properties), `data`, `on` (event map), anything else is an attribute. */
export function h(tag, props, ...children) {
  const el = document.createElement(tag);
  if (props) applyProps(el, props);
  append(el, children);
  return el;
}

export function svg(tag, props, ...children) {
  const el = document.createElementNS(SVG_NS, tag);
  if (props) applyProps(el, props);
  append(el, children);
  return el;
}

/** Writes the text only when it changed. */
export function setText(el, text) {
  const next = text === null || text === undefined ? '' : String(text);
  if (el.textContent !== next) el.textContent = next;
}

export function toggle(el, cls, on) {
  el.classList.toggle(cls, Boolean(on));
}

/** Sets an attribute only when it changed; null removes it. */
export function setAttr(el, name, value) {
  if (value === null || value === undefined || value === false) {
    if (el.hasAttribute(name)) el.removeAttribute(name);
    return;
  }
  const next = value === true ? '' : String(value);
  if (el.getAttribute(name) !== next) el.setAttribute(name, next);
}

export function setVar(el, name, value) {
  if (el.style.getPropertyValue(name) !== value) el.style.setProperty(name, value);
}

const entries = new WeakMap();

/**
 * Keyed list: keeps one element per item key, in item order. `create(item)` builds, `update(el, item)` refreshes (also right after create),
 * `exit(el, done)` (optional) plays a leave motion and calls done to remove; a key that returns while leaving gets a fresh element.
 */
export function keyed(parent, items, keyOf, create, update, exit) {
  let map = entries.get(parent);
  if (!map) entries.set(parent, (map = new Map()));
  const seen = new Set();
  let cursor = parent.firstChild;
  for (const item of items) {
    const key = keyOf(item);
    seen.add(key);
    let el = map.get(key);
    if (el?.dataset.leaving) {
      map.delete(key);
      el = undefined;
    }
    if (!el) {
      el = create(item);
      map.set(key, el);
      el.dataset.isNew = '1';
    }
    update(el, item);
    while (cursor && cursor.dataset?.leaving) cursor = cursor.nextSibling;
    if (el !== cursor) parent.insertBefore(el, cursor);
    else cursor = cursor.nextSibling;
  }
  for (const [key, el] of [...map]) {
    if (seen.has(key) || el.dataset.leaving) continue;
    const remove = () => {
      if (map.get(key) === el) map.delete(key);
      el.remove();
    };
    if (!exit) remove();
    else {
      el.dataset.leaving = '1';
      exit(el, remove);
    }
  }
}

/** True when the element was just created by `keyed` (clears the mark). */
export function wasCreated(el) {
  if (!el.dataset.isNew) return false;
  delete el.dataset.isNew;
  return true;
}
