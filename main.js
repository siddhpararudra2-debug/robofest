(() => {
  const body = document.body;
  const menuButton = document.querySelector('.menu-button');
  const closeButton = document.querySelector('.close-button');
  const menu = document.querySelector('.mobile-menu');
  const overlay = document.querySelector('.mobile-overlay');
  const mobileLinks = document.querySelectorAll('.mobile-nav a, .mobile-cta');

  const setMenu = (open) => {
    if (!menuButton || !menu || !overlay) return;
    menu.hidden = !open;
    overlay.hidden = !open;
    body.style.overflow = open ? 'hidden' : '';
    menuButton.setAttribute('aria-expanded', String(open));
    menuButton.setAttribute('aria-label', open ? 'Close menu' : 'Open menu');
  };

  menuButton?.addEventListener('click', () => setMenu(true));
  closeButton?.addEventListener('click', () => setMenu(false));
  overlay?.addEventListener('click', () => setMenu(false));
  mobileLinks.forEach((link) => link.addEventListener('click', () => setMenu(false)));
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') setMenu(false);
  });
  window.addEventListener('resize', () => {
    if (window.innerWidth >= 768) setMenu(false);
  });

  const video = document.querySelector('.background-video');
  video?.play().catch(() => {
    // Autoplay can be blocked by a browser; muted inline video will retry on first interaction.
    document.addEventListener('pointerdown', () => video.play().catch(() => {}), { once: true });
  });
})();
