// Small UI glyphs for the dock style sheet (src/resources/): the tab bar's
// scroll chevrons, normal + disabled. Rendered with 4x4 supersampling.
//   node tools/make-ui-icons.js
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const crcT = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc = b => { let c = -1; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
function png(w, h, px) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; px.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4); }
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}
// distance from point to segment
function segDist(px, py, ax, ay, bx, by) {
  const dx = bx - ax, dy = by - ay, t = Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)));
  return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}
// Chevron ">" (dir 1) or "<" (dir -1) in an n×n box, round caps.
function chevron(n, dir, rgb) {
  const px = Buffer.alloc(n * n * 4), SS = 4, sw = n * 0.13, c = n / 2;
  const tip = [c + dir * n * 0.14, c], a = [c - dir * n * 0.12, c - n * 0.26], b = [c - dir * n * 0.12, c + n * 0.26];
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    let hit = 0;
    for (let sy = 0; sy < SS; sy++) for (let sx = 0; sx < SS; sx++) {
      const fx = x + (sx + 0.5) / SS, fy = y + (sy + 0.5) / SS;
      const d = Math.min(segDist(fx, fy, a[0], a[1], tip[0], tip[1]), segDist(fx, fy, b[0], b[1], tip[0], tip[1]));
      if (d <= sw) hit++;
    }
    const o = (y * n + x) * 4;
    px[o] = rgb[0]; px[o + 1] = rgb[1]; px[o + 2] = rgb[2]; px[o + 3] = Math.round(255 * hit / (SS * SS));
  }
  return png(n, n, px);
}
const out = path.join(__dirname, '..', 'src', 'resources');
fs.mkdirSync(out, { recursive: true });
const N = 32; // shown at 10–12 px, so this is sharp up to ~300 % scaling
const ON = [0xd3, 0xd7, 0xdf], OFF = [0x4b, 0x51, 0x60];
fs.writeFileSync(path.join(out, 'chevron-right.png'), chevron(N, 1, ON));
fs.writeFileSync(path.join(out, 'chevron-left.png'), chevron(N, -1, ON));
fs.writeFileSync(path.join(out, 'chevron-right-off.png'), chevron(N, 1, OFF));
fs.writeFileSync(path.join(out, 'chevron-left-off.png'), chevron(N, -1, OFF));
console.log('wrote chevrons to', out);
