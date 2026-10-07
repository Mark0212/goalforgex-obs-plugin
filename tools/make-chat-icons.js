// Shrinks the full-size Twitch / Kick artwork into the small icons the dock's
// Chat tab uses (src/resources/). Pure Node: decodes the PNG, trims the
// transparent margin, area-averages down (premultiplied alpha, so edges don't
// get dark fringes) and re-encodes.
//   node tools/make-chat-icons.js <twitch.png> <kick.png> [size=64]
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

function decodePng(buf) {
  if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error('not a PNG');
  let off = 8, w = 0, h = 0, bitDepth = 0, colorType = 0, interlace = 0;
  const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off), type = buf.toString('ascii', off + 4, off + 8), data = buf.subarray(off + 8, off + 8 + len);
    if (type === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); bitDepth = data[8]; colorType = data[9]; interlace = data[12]; }
    else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    off += 12 + len;
  }
  if (bitDepth !== 8 || interlace !== 0 || (colorType !== 6 && colorType !== 2)) throw new Error(`unsupported PNG (depth ${bitDepth}, type ${colorType}, interlace ${interlace})`);
  const bpp = colorType === 6 ? 4 : 3, stride = w * bpp;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const out = Buffer.alloc(w * h * 4);
  let prev = Buffer.alloc(stride), cur = Buffer.alloc(stride);
  for (let y = 0; y < h; y++) {
    const ft = raw[y * (stride + 1)], line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let i = 0; i < stride; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let v = line[i];
      if (ft === 1) v += a; else if (ft === 2) v += b; else if (ft === 3) v += (a + b) >> 1;
      else if (ft === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
      cur[i] = v & 255;
    }
    for (let x = 0; x < w; x++) {
      const o = (y * w + x) * 4;
      out[o] = cur[x * bpp]; out[o + 1] = cur[x * bpp + 1]; out[o + 2] = cur[x * bpp + 2]; out[o + 3] = bpp === 4 ? cur[x * bpp + 3] : 255;
    }
    [prev, cur] = [cur, prev];
  }
  return { w, h, px: out };
}

const crcT = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc = b => { let c = -1; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ -1) >>> 0; };
const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
function encodePng({ w, h, px }) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) { raw[y * (w * 4 + 1)] = 0; px.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4); }
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 6;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ih), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

// Bounding box of pixels that are visibly there (alpha above a small threshold — drops faint glow haze).
function trim(img, thr = 24) {
  let x0 = img.w, y0 = img.h, x1 = -1, y1 = -1;
  for (let y = 0; y < img.h; y++) for (let x = 0; x < img.w; x++) if (img.px[(y * img.w + x) * 4 + 3] > thr) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
  // square it up around the centre so both icons sit the same in a line of text
  const side = Math.max(x1 - x0 + 1, y1 - y0 + 1), cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  return { x: Math.round(cx - side / 2), y: Math.round(cy - side / 2), side };
}

// Area-average resample of a square crop into n×n (premultiplied).
function resample(img, box, n) {
  const out = Buffer.alloc(n * n * 4), scale = box.side / n;
  for (let oy = 0; oy < n; oy++) for (let ox = 0; ox < n; ox++) {
    const sx0 = box.x + ox * scale, sy0 = box.y + oy * scale, sx1 = sx0 + scale, sy1 = sy0 + scale;
    let r = 0, g = 0, b = 0, a = 0, area = 0;
    for (let sy = Math.floor(sy0); sy < Math.ceil(sy1); sy++) {
      const wy = Math.min(sy + 1, sy1) - Math.max(sy, sy0);
      for (let sx = Math.floor(sx0); sx < Math.ceil(sx1); sx++) {
        const wx = Math.min(sx + 1, sx1) - Math.max(sx, sx0), wgt = wx * wy;
        area += wgt;
        if (sx < 0 || sy < 0 || sx >= img.w || sy >= img.h) continue; // outside the source = transparent
        const o = (sy * img.w + sx) * 4, al = img.px[o + 3] / 255;
        r += img.px[o] * al * wgt; g += img.px[o + 1] * al * wgt; b += img.px[o + 2] * al * wgt; a += al * wgt;
      }
    }
    const o = (oy * n + ox) * 4;
    if (a > 0) { out[o] = Math.round(r / a); out[o + 1] = Math.round(g / a); out[o + 2] = Math.round(b / a); }
    out[o + 3] = Math.round(255 * a / area);
  }
  return { w: n, h: n, px: out };
}

const [twitch, kick, sizeArg] = process.argv.slice(2);
const n = parseInt(sizeArg, 10) || 64;
const outDir = path.join(__dirname, '..', 'src', 'resources');
fs.mkdirSync(outDir, { recursive: true });
for (const [name, file] of [['twitch', twitch], ['kick', kick]]) {
  const img = decodePng(fs.readFileSync(file));
  const box = trim(img);
  const png = encodePng(resample(img, box, n));
  fs.writeFileSync(path.join(outDir, `${name}.png`), png);
  console.log(`${name}: ${img.w}x${img.h} → crop ${box.side}px → ${n}x${n} (${png.length} bytes)`);
}
