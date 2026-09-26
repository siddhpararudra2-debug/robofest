const menu = document.getElementById('menu');
const openButton = document.getElementById('menu-open');
const closeButton = document.getElementById('menu-close');
const backdrop = document.getElementById('menu-backdrop');
const links = menu.querySelectorAll('.menu__link');

function setMenu(open) {
  menu.classList.toggle('is-open', open);
  menu.setAttribute('aria-hidden', String(!open));
  openButton.setAttribute('aria-expanded', String(open));
  const target = open ? closeButton : openButton;
  target.focus({ preventScroll: true });
}

openButton.addEventListener('click', () => setMenu(true));
closeButton.addEventListener('click', () => setMenu(false));
backdrop.addEventListener('click', () => setMenu(false));
links.forEach((link) => link.addEventListener('click', () => setMenu(false)));
document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape' && menu.classList.contains('is-open')) setMenu(false);
});
