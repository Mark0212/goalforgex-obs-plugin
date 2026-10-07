// Generates the Inno Setup branding for GoalForgeX for OBS (Broadcast Pro look):
//   wizard-*.bmp   side panel on the Welcome / Finished pages (164x314 @100% + HiDPI sizes)
//   small-*.bmp    header badge on the inner pages (55x55 @100% + HiDPI sizes)
//   goalforgex.ico setup + uninstall icon (16/24/32/48/64/256, 32-bit)
// Pure Node (no image libraries): shapes are rendered with 4x4 supersampling.
'use strict';
const fs = require('fs');
const path = require('path');
const out = process.argv[2];
fs.mkdirSync(out, { recursive: true });

const RED = [0xff, 0x3b, 0x4f], AMBER = [0xff, 0xb0, 0x20];
const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
const clamp01 = x => Math.max(0, Math.min(1, x));
const over = (dst, src, a) => dst.map((v, i) => v + (src[i] - v) * a); // src over dst, alpha a

// Signed distance to a rounded rectangle centred at (cx,cy)
function sdRoundRect(x, y, cx, cy, hw, hh, r) {
  const qx = Math.abs(x - cx) - (hw - r), qy = Math.abs(y - cy) - (hh - r);
  return Math.min(Math.max(qx, qy), 0) + Math.hypot(Math.max(qx, 0), Math.max(qy, 0)) - r;
}

// The mark at (mx,my) size s, composited over `bg` → returns [rgb, alpha] at a subpixel
function markSample(x, y, mx, my, s) {
  const cx = mx + s / 2, cy = my + s / 2;
  const d = sdRoundRect(x, y, cx, cy, s / 2, s / 2, s * 0.26);
  if (d > 0) return null;
  const t = clamp01(((x - mx) + (y - my)) / (2 * s)); // diagonal gradient
  let c = mix(RED, AMBER, t);
  // top-left shine
  const sh = Math.hypot(x - (mx + s * 0.3), y - (my + s * 0.15)) / (s * 0.75);
  c = over(c, [255, 255, 255], 0.27 * clamp01(1 - sh));
  const rr = Math.hypot(x - cx, y - cy);
  const ringW = Math.max(1.5, s * 0.09) / 2;
  if (Math.abs(rr - s * 0.26) <= ringW) c = over(c, [255, 255, 255], 0.94);
  if (rr <= s * 0.105) c = [255, 255, 255];
  return c;
}

function render(w, h, paintBg, mark) {
  const px = new Float64Array(w * h * 4);
  const SS = 4;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    let r = 0, g = 0, b = 0, a = 0;
    for (let sy = 0; sy < SS; sy++) for (let sx = 0; sx < SS; sx++) {
      const fx = x + (sx + 0.5) / SS, fy = y + (sy + 0.5) / SS;
      let c = paintBg ? paintBg(fx, fy) : null, al = paintBg ? 1 : 0;
      if (mark) {
        const m = markSample(fx, fy, mark.x, mark.y, mark.s);
        if (m) { c = m; al = 1; }
      }
      if (c) { r += c[0]; g += c[1]; b += c[2]; a += al; }
    }
    const n = SS * SS, i = (y * w + x) * 4;
    // colour averaged over covered samples (premultiplied-free for opaque bg)
    const cov = a / n;
    px[i] = a ? r / a : 0; px[i + 1] = a ? g / a : 0; px[i + 2] = a ? b / a : 0; px[i + 3] = cov;
  }
  return { w, h, px };
}

// Wizard side panel: graphite, warm broadcast glow, concentric "signal" rings, the mark.
function wizardBg(w, h) {
  const cx = w / 2, cy = h * 0.40, s = w * 0.42;
  return (x, y) => {
    let c = mix([0x1b, 0x1e, 0x25], [0x0e, 0x10, 0x14], y / h);
    const gd = Math.hypot(x - cx, (y - cy) * 0.9) / (w * 0.95);
    c = over(c, mix(RED, AMBER, clamp01(y / h + 0.2)), 0.28 * clamp01(1 - gd) ** 2);
    // signal rings
    const rr = Math.hypot(x - cx, y - cy);
    for (let k = 1; k <= 4; k++) {
      const R = s * (0.55 + k * 0.32);
      const a = 0.16 / k * clamp01(1 - Math.abs(rr - R) / (w * 0.006));
      if (a > 0) c = over(c, AMBER, a);
    }
    // ON-AIR bar near the bottom
    const by = h * 0.86, bh = Math.max(2, h * 0.009);
    if (Math.abs(y - by) < bh && x > w * 0.28 && x < w * 0.72) c = over(c, mix(RED, AMBER, (x - w * 0.28) / (w * 0.44)), 0.9);
    return c;
  };
}

function bmp24(img) {
  const { w, h, px } = img, rowSize = Math.ceil((w * 3) / 4) * 4, size = 54 + rowSize * h;
  const b = Buffer.alloc(size);
  b.write('BM', 0); b.writeUInt32LE(size, 2); b.writeUInt32LE(54, 10);
  b.writeUInt32LE(40, 14); b.writeInt32LE(w, 18); b.writeInt32LE(h, 22); b.writeUInt16LE(1, 26); b.writeUInt16LE(24, 28);
  b.writeUInt32LE(rowSize * h, 34); b.writeInt32LE(3780, 38); b.writeInt32LE(3780, 42);
  for (let y = 0; y < h; y++) {
    const row = 54 + (h - 1 - y) * rowSize;
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4;
      b[row + x * 3] = Math.round(px[i + 2]); b[row + x * 3 + 1] = Math.round(px[i + 1]); b[row + x * 3 + 2] = Math.round(px[i]);
    }
  }
  return b;
}
function ico(images) {
  const dirs = [], datas = [];
  for (const img of images) {
    const { w, h, px } = img, maskRow = Math.ceil(w / 32) * 4;
    const d = Buffer.alloc(40 + w * h * 4 + maskRow * h);
    d.writeUInt32LE(40, 0); d.writeInt32LE(w, 4); d.writeInt32LE(h * 2, 8); d.writeUInt16LE(1, 12); d.writeUInt16LE(32, 14);
    d.writeUInt32LE(w * h * 4 + maskRow * h, 20);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4, o = 40 + ((h - 1 - y) * w + x) * 4;
      d[o] = Math.round(px[i + 2]); d[o + 1] = Math.round(px[i + 1]); d[o + 2] = Math.round(px[i]); d[o + 3] = Math.round(px[i + 3] * 255);
    }
    datas.push(d); dirs.push({ w, h });
  }
  const head = Buffer.alloc(6 + 16 * images.length);
  head.writeUInt16LE(0, 0); head.writeUInt16LE(1, 2); head.writeUInt16LE(images.length, 4);
  let off = head.length;
  dirs.forEach((dd, k) => {
    const e = 6 + k * 16;
    head[e] = dd.w >= 256 ? 0 : dd.w; head[e + 1] = dd.h >= 256 ? 0 : dd.h; head[e + 2] = 0; head[e + 3] = 0;
    head.writeUInt16LE(1, e + 4); head.writeUInt16LE(32, e + 6); head.writeUInt32LE(datas[k].length, e + 8); head.writeUInt32LE(off, e + 12);
    off += datas[k].length;
  });
  return Buffer.concat([head, ...datas]);
}

// Wizard side images (Inno Setup's recommended scale steps)
const wizardSizes = [[164, 314], [192, 386], [246, 459], [328, 628]];
for (const [w, h] of wizardSizes) {
  const s = Math.round(w * 0.42);
  const img = render(w, h, wizardBg(w, h), { x: (w - s) / 2, y: h * 0.40 - s / 2, s });
  fs.writeFileSync(path.join(out, `wizard-${w}.bmp`), bmp24(img));
}
// Small header images: the mark on the wizard page's white header background
for (const n of [55, 83, 110]) {
  const s = Math.round(n * 0.86);
  const img = render(n, n, () => [255, 255, 255], { x: (n - s) / 2, y: (n - s) / 2, s });
  fs.writeFileSync(path.join(out, `small-${n}.bmp`), bmp24(img));
}
// Icon (transparent corners)
const icons = [16, 24, 32, 48, 64, 256].map(n => render(n, n, null, { x: 0, y: 0, s: n }));
fs.writeFileSync(path.join(out, 'goalforgex.ico'), ico(icons));
// Preview PNG-free check: also dump the 328 wizard as BMP already written
console.log('wrote', fs.readdirSync(out).join(', '));
