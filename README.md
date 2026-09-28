# GoalForgeX for OBS

An OBS Studio plugin that adds a **GoalForgeX** dock: connect a GoalForgeX account, pick a scene, and add / show / hide / move / resize GoalForgeX widgets without copying URLs. Widgets are ordinary OBS **Browser Sources** pointing at goalforgex.com, so widget designs and live data update without a plugin update.

Built from the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) (C++17, Qt 6, `obs-frontend-api`, libcurl).

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

## Building

GitHub only runs workflows from a repository's root `.github/`, so CI works once this folder is its **own repository** (recommended — the plugin is GPL-2.0, see below):

1. Create a repo from this folder and push. `push.yaml` / `build-project.yaml` build Windows (and macOS/Ubuntu) on every push.
2. Windows artifacts: `goalforgex-<ver>-windows-x64.zip` and **`goalforgex-<ver>-windows-x64-installer.exe`** (Inno Setup, built by `.github/scripts/Package-Windows.ps1` from `installer/goalforgex.iss`).
3. Push a semver tag (e.g. `1.0.0`) to get a draft GitHub Release with the packages attached.

Local Windows build (Visual Studio 2022, CMake 3.30, PowerShell 7):

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
```

Version/name live in `buildspec.json`.

## Installing

The installer detects OBS (`HKLM\SOFTWARE\OBS Studio` → `bin\64bit\obs64.exe` version) and installs to `C:\ProgramData\obs-studio\plugins\goalforgex\`:

- **OBS 33+**: `goalforgex.dll` in that folder (new layout)
- **OBS 31.1–32.x**: `bin\64bit\goalforgex.dll` (legacy layout — OBS 34 stops loading it, so re-run the installer after upgrading OBS)

It only ever leaves one DLL location behind, refuses to install while OBS is running, and blocks OBS older than 31.1. Portable OBS: unzip the `.zip` into the portable install's `plugins\` folder.

## Publishing the download

Set `OBS_PLUGIN_DOWNLOAD_URL` on the GoalForgeX server to the installer's HTTPS URL (e.g. the GitHub Release asset). Until it's set, goalforgex.com/obs shows the download as "coming soon".

## Testing against a dev server

Set `GOALFORGEX_BASE_URL` before launching OBS (https, or `http://localhost:…`):

```powershell
$env:GOALFORGEX_BASE_URL = "http://localhost:3000"; & "C:\Program Files\obs-studio\bin\64bit\obs64.exe"
```

The OBS log (Help → Log Files) shows `GoalForgeX loaded (version …, server …)` and any request failures.

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
