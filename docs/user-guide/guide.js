// [icon, title, text]
const CARDS = {
  start: [
    ['🔌', 'Charge it', 'Plug in a USB-C cable. You can play while it charges.'],
    ['⏻', 'Wake it up', 'Press the PWR button.'],
    ['🗺️', 'Pick your partner', 'Choose a region, then one of its three starters.'],
    ['🥚', 'Hatch it', 'Tap the egg 3 times. Say hello!'],
  ],
  buttons: [
    ['🍎', 'Food', 'Berries & candy'],
    ['🌙', 'Light', 'Bedtime'],
    ['🫧', 'Bath', 'Clean up'],
    ['💪', 'Train', 'Mini-games'],
  ],
  touch: [
    ['👆', 'Tap its name', 'Opens the menu: Pokedex, settings & more'],
    ['🤚', 'Tap your creature', 'Give it a pet. It loves that!'],
    ['⏳', 'Hold it for 3 s', 'Let it go (it asks you first)'],
    ['⏻', 'PWR button', 'Press: screen on/off. Hold 4 s: power off'],
  ],
  needs: [
    ['🍎', 'FOOD', 'Gets hungry fastest. Each creature has a favourite berry. Find it for a bonus!'],
    ['😊', 'JOY', 'Pet it and play games. It gets grumpy when hungry or dirty.'],
    ['⚡', 'ENE', 'Energy. Tap Light to put it to bed. Too much candy tires it out.'],
    ['🫧', 'HYG', 'Hygiene. Poops make it messy. A bath cleans everything.'],
  ],
  growth: [
    ['🥚', 'Hatch', 'Your adventure begins.'],
    ['⭐', 'Every hour', 'It gains a level, even while the device is off.'],
    ['✨', 'Evolve', 'Most evolve for the first time around level 16, if you keep every bar above 40.'],
    ['💛', 'Day 3', 'A grown-up creature may be ready to say goodbye.'],
    ['💯', 'Day 4', 'Level 100, as strong as it gets.'],
  ],
  endings: [
    ['💛', 'Farewell', 'The happy ending. It joins your team, and your next egg is luckier.'],
    ['👋', 'Let go', 'Hold it for 3 s. It joins your team.'],
    ['💔', 'Runs away', 'If every bar stays empty for an hour, it leaves forever.'],
  ],
  train: [
    ['🥊', 'Strength', 'Punching bag'],
    ['🎯', 'Speed', 'Reaction test'],
    ['⚽', 'Defense', 'Ball game'],
  ],
  wild: [
    ['👈', 'Swipe left, tap WILD', 'It sits next to LAN BATTLE on the gym list.'],
    ['🗺️', 'Pick a region', 'It shows how many creatures you have seen there.'],
    ['🌳', 'Pick a place', 'Swipe for more. Locked places show the badge they need.'],
    ['⚔️', 'Pick your team & fight', 'Bring anyone from your team. Tap RUN to get away.'],
  ],
};

const LISTS = {
  home: [
    '<b>Name & level.</b> Tap it to open the menu.',
    '<b>Your creature.</b> Tap it to give it a pet.',
    '<b>How it feels.</b> Keep these bars green!',
    '<b>Care buttons.</b> Food, light, bath & training.',
  ],
  rules: [
    'Check in a few times a day.',
    'Don\'t let a bar run empty. That slows evolution.',
    'Candy is a treat, not a meal.',
  ],
  battle: [
    '<b>Swipe left</b>, pick a region and beat its 8 gym leaders, the Elite 4 and the champion.',
    'Bring your team. Wins earn <b>badges</b> and make your creature stronger.',
    'Too easy? Try <b>hard mode</b>.',
    '<b>Battle a friend:</b> on the gym list, tap <b>LAN BATTLE</b>. One of you taps HOST and the other taps JOIN.',
  ],
  wild: [
    'Every badge opens <b>new places</b> with stronger creatures.',
    'Some creatures only come out in the <b>morning</b> or at <b>night</b>.',
    'Each fight uses some <b>energy</b>. Your creature can\'t go while tired, asleep or still an egg.',
    'Win to <b>train</b> your creature. Foes 10+ levels weaker teach it nothing.',
  ],
  collect: [
    'Over <b>800</b> creatures to raise, plus rare <b>shiny</b> colours.',
    'Open the <b>Pokedex</b> from the menu, pick a region and see everyone you have raised.',
    'Every ending brings a new egg. Tap the <b>region</b> under it to choose where it comes from.',
    'Old friends wait in your <b>team</b> (6) and <b>box</b> (18).',
  ],
};

const FAQ = {
  help: [
    ['The screen is black.', 'Press PWR. Still dark? Hold PWR for 4 s, or charge it.'],
    ['My creature is a number, not a picture.', 'The memory card is missing or loose. Put it back in.'],
    ['A region says NEEDS PACK.', 'Its pictures aren\'t on the card yet. Add them from the website below.'],    ['It never goes to sleep.', 'Set the clock: menu, then Settings.'],
    ['Can I change the language?', 'Yes! Menu, then Settings. There are six to pick from.'],
  ],
};

const h = (tag, cls, html) => Object.assign(document.createElement(tag), { className: cls, innerHTML: html });
const fill = (sel, src, make) => document.querySelectorAll(`[${sel}]`).forEach(n =>
  (src[n.getAttribute(sel)] || []).forEach(d => n.appendChild(make(d, n))));

fill('data-cards', CARDS, ([icon, title, text], n) =>
  h(n.tagName === 'OL' ? 'li' : 'div', 'card',
    `<span class="ico">${icon}</span><span><b>${title}</b><small>${text}</small></span>`));
fill('data-list', LISTS, t => h('li', '', t));
fill('data-faq', FAQ, ([q, a]) => {
  const f = document.createDocumentFragment();
  f.append(h('dt', '', q), h('dd', '', a));
  return f;
});

const fw = document.querySelector('meta[name=fw]').content;
document.querySelectorAll('[data-fw]').forEach(n => n.textContent = fw);
document.getElementById('print').onclick = () => window.print();
