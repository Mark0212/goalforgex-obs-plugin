# GoalForgeX for OBS

An OBS Studio plugin that adds a **GoalForgeX** dock: connect a GoalForgeX account, pick a scene, and add / show / hide / move / resize GoalForgeX widgets without copying URLs. Widgets are ordinary OBS **Browser Sources** pointing at goalforgex.com, so widget designs and live data update without a plugin update.

| | |
|---|---|
| OBS Studio | 31.1 or newer (built against 31.1.1) |
| Windows | 10 / 11, x64 — **first supported platform** |
| macOS / Linux | builds via the template, but not yet tested or released |
| Streamlabs Desktop | ❌ can't load OBS plugins — use goalforgex.com/obs → "Streamlabs links" instead |

## How it works

- **Connect** — RFC 8628 device-code flow. The dock shows a code and opens `goalforgex.com/obs/link`; the streamer approves while signed in to the site. The plugin receives a 1-hour access token, a refresh token (rotated on every use) and a view-only overlay key. Server side: `obs-link.js`.
- **Storage** — refresh token + overlay key in `%APPDATA%\obs-studio\plugin_config\goalforgex\credentials.dat`, encrypted with Windows DPAPI (per Windows user).
- **Widgets** — `GET /api/obs/widgets` returns the account's widgets (same list and Pro gating as the dashboard's Widget Links). The plugin appends `&key=<overlay key>` and only accepts URLs on goalforgex.com.
- **No duplicates** — each source the plugin creates is tagged (`gfx_widget` / `gfx_account` private settings). Adding a widget that already exists re-uses that source in the new scene; adding it to a scene it's already in just selects it.
- **Revocation** — removing a device at goalforgex.com/obs (or clicking Disconnect) kills that install's tokens and overlay key immediately and closes its overlays' live connections. Other installs, Streamlabs link sets, and the dashboard's own widget URLs are unaffected.
- **Reconnecting** re-points this account's existing GoalForgeX sources at the new overlay key automatically.

## Release test checklist

Run on **OBS 32.2.x (stable)** and the **OBS 33.0 beta** (new CEF + plugin loader), Windows 10 and 11:

- [ ] Fresh install → dock appears under Docks → GoalForgeX
- [ ] Connect → browser opens with code → approve → widgets load; decline → clear message; let code expire → clear message
- [ ] Add each widget type to a scene; alerts/chat box fill the canvas; alert/wheel sounds appear in the audio mixer
- [ ] Add the same widget to a second scene → same source reused, no "(2)" duplicate; add again to the same scene → "already in this scene"
- [ ] Show/Hide, Move to…, size %, Fit canvas, Reload, Properties, Customize
- [ ] Remove the device at goalforgex.com/obs → overlays go blank, dock shows Reconnect; Reconnect → same sources come back to life
- [ ] Start OBS offline → dock says it can't reach GoalForgeX and retries; sources already in scenes keep their last state
- [ ] Switch scene collections; Studio Mode; widgets inside groups; 1080p / 1440p / vertical canvases; 125–200% Windows display scaling
- [ ] Free account → Pro widgets show 🔒 and can't be added
- [ ] Portable OBS install via the zip
- [ ] Uninstall removes the plugin; OBS still starts cleanly

## License

GPL-2.0-or-later (inherited from the template; OBS plugins link libobs, which is GPL). This covers the plugin source only — the GoalForgeX website and server are separate programs talking over HTTPS and are not affected.
