const video = document.getElementById('bg-video');
const card = document.querySelector('[data-glass-card]');
const container = document.getElementById('dup-video-container');
const canvas = document.getElementById('dup-image');
const ctx = canvas.getContext('2d');
const DUP_PIXEL_RATIO = 1;
let canvasWidth = 0;
let canvasHeight = 0;

function drawFrame() {
  const rect = card.getBoundingClientRect();
  const vw = document.documentElement.clientWidth;
  const vh = document.documentElement.clientHeight;
  if (!rect.width || !rect.height || !video.videoWidth || !video.videoHeight) {
    requestAnimationFrame(drawFrame);
    return;
  }
  container.style.left = `${-rect.left}px`;
  container.style.top = `${-rect.top}px`;
  container.style.width = `${vw}px`;
  container.style.height = `${vh}px`;
  const w = Math.max(1, Math.floor(vw * DUP_PIXEL_RATIO));
  const h = Math.max(1, Math.floor(vh * DUP_PIXEL_RATIO));
  if (canvasWidth !== w || canvasHeight !== h) {
    canvas.width = w;
    canvas.height = h;
    canvasWidth = w;
    canvasHeight = h;
  }
  const cover = Math.max(vw / video.videoWidth, vh / video.videoHeight);
  const sw = vw / cover;
  const sh = vh / cover;
  const sx = (video.videoWidth - sw) / 2;
  const sy = (video.videoHeight - sh) / 2;
  try { ctx.drawImage(video, sx, sy, sw, sh, 0, 0, w, h); } catch (_) { /* A frame may not be decodable yet. */ }
  requestAnimationFrame(drawFrame);
}

// Viewport sizing is deliberate: it keeps filtered edge bands outside the card and leaves only clean refraction visible.
// The duplicate stays at 1× even on retina: the SVG filter cost scales with pixel count while the result remains soft.
requestAnimationFrame(drawFrame);
