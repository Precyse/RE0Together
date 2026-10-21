import { h, setText, keyed, wasCreated } from './dom.js';
import { createAvatar } from './art.js';

const CAPTION = 'Friends playing';

/** FRIENDS PLAYING: 2-column cards keyed by lobby, staggered in on arrival, fading out when gone. */
export function createFriends(ctx) {
  const { send, motion } = ctx;
  const grid = h('div', { class: 'friends-grid' });
  const el = h('section', { class: 'friends', hidden: true }, h('div', { class: 'caption', text: CAPTION }), grid);

  function createCard(friend) {
    const avatar = createAvatar('friend-avatar');
    const name = h('span', { class: 'friend-name' });
    const game = h('span', { class: 'friend-game' });
    const count = h('span', { class: 'friend-count' });
    const join = h('button', { class: 'btn friend-join', type: 'button', on: { click: () => send('join.friend', { lobbyId: friend.key }) } });
    const card = h('div', { class: 'friend-card' }, avatar.el, h('div', { class: 'friend-text' }, name, h('div', { class: 'friend-sub' }, game, count)), join);
    card.parts = { avatar, name, game, count, join };
    return card;
  }

  function updateCard(card, friend) {
    const { avatar, name, game, count, join } = card.parts;
    avatar.set(friend.avatar);
    setText(name, friend.name);
    setText(game, friend.game);
    setText(count, friend.count);
    count.hidden = !friend.count;
    join.classList.toggle('btn-primary', friend.primary);
    join.classList.toggle('btn-ghost', !friend.primary);
    setText(join, friend.primary ? 'JOIN' : 'Join');
  }

  return {
    el,
    update(state) {
      if (!state.home) return;
      const { friends } = state.home;
      keyed(grid, friends, (friend) => friend.key, createCard, updateCard, (card, done) => motion.play(card, 'leave', { keep: true }).then(() => {
        done();
        el.hidden = grid.children.length === 0;
      }));
      const arrived = [...grid.children].filter((card) => !card.dataset.leaving && wasCreated(card));
      if (arrived.length) motion.stagger(arrived);
      el.hidden = grid.children.length === 0;
    },
  };
}
