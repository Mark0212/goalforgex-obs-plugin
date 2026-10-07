# Installer branding

Broadcast Pro art for the Windows installer (`installer/goalforgex.iss`):

| File | Used for |
|---|---|
| `wizard-164/192/246/328.bmp` | Side panel on the Welcome and Finished pages (100 % / 125 % / 150 % / 200 % scaling) |
| `small-55/83/110.bmp` | Badge in the header of the inner wizard pages |
| `goalforgex.ico` | Setup and uninstall icon (16–256 px, 32-bit with alpha) |

They are generated, not hand-drawn. To change them, edit `tools/gen-installer-art.js` (it draws the same
mark the dock draws in `src/gfx-theme.cpp`) and re-run:

```
node tools/gen-installer-art.js installer/assets
```
