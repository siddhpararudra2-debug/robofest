(() => {
  const body = document.body;
  const toggle = document.querySelector('.menu-toggle');
  const menu = document.querySelector('.mobile-menu');
  const overlay = document.querySelector('.mobile-overlay');
  const mobileLinks = document.querySelectorAll('.mobile-link, .mobile-sign-in');

  const setMenu = (open) => {
    toggle.classList.toggle('open', open);
    toggle.setAttribute('aria-expanded', String(open));
    toggle.setAttribute('aria-label', open ? 'Close menu' : 'Open menu');
    body.classList.toggle('menu-open', open);
    menu.hidden = !open;
    overlay.hidden = !open;
  };

  toggle?.addEventListener('click', () => setMenu(toggle.getAttribute('aria-expanded') !== 'true'));
  overlay?.addEventListener('click', () => setMenu(false));
  mobileLinks.forEach((link) => link.addEventListener('click', () => setMenu(false)));
  document.addEventListener('keydown', (event) => { if (event.key === 'Escape') setMenu(false); });
  window.addEventListener('resize', () => { if (window.innerWidth > 720) setMenu(false); });

  const easeOutCubic = (t) => 1 - Math.pow(1 - t, 3);
  const counters = document.querySelectorAll('.count');
  const animateCount = (element, index) => {
    const target = Number(element.dataset.target);
    const decimals = Number(element.dataset.decimals || 0);
    const duration = 1500 + index * 80;
    const start = performance.now();
    const tick = (now) => {
      const progress = Math.min((now - start) / duration, 1);
      element.textContent = (target * easeOutCubic(progress)).toFixed(decimals);
      if (progress < 1) requestAnimationFrame(tick);
    };
    setTimeout(() => requestAnimationFrame(tick), 480 + index * 90);
  };

  const stats = document.querySelector('.stats');
  if ('IntersectionObserver' in window && stats) {
    const observer = new IntersectionObserver((entries, instance) => {
      if (entries.some((entry) => entry.isIntersecting)) {
        counters.forEach(animateCount);
        instance.disconnect();
      }
    }, { threshold: .25 });
    observer.observe(stats);
  } else counters.forEach(animateCount);
})();
