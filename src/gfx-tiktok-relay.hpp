/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

// TikTok LIVE through this PC (3.0.0).
//
// While OBS runs, the plugin does goalforgex.com's TikTok LIVE work from the
// streamer's own internet connection — the way TikFinity's desktop app does —
// instead of every TikTok request coming from the website's server (one
// data-centre address for every streamer, which TikTok blocks and drops):
//   · live checks: fetch the URL the server names (TikTok's live status JSON or
//     live page) and post the body back — the server reads it;
//   · the TikTok WebSocket: open it with the URL + headers the server hands over
//     (signed by the server), forward every frame it receives to the server, and
//     send the frames the server returns (enter-room, heartbeats, acks).
// All TikTok logic (signing, decoding, goals, alerts…) stays on the server; this
// is a pipe. One route: POST /api/obs/tiktok/relay. If this PC can't do it, or
// OBS closes, the server connects to TikTok itself again — nothing breaks.
// Only TikTok hosts are ever contacted (checked here, not trusted from the server).

namespace gfx {

void tiktokRelayStart(); // after the docks are created
void tiktokRelayStop();  // OBS exiting / module unload — joins the worker threads

} // namespace gfx
