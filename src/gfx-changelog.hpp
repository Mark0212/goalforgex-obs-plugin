/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

class QWidget;

// "What's new" popup: shown once, the first time OBS starts after the plugin
// updated (never on a fresh install — the welcome card covers that), and any
// time from ⚙ → What's new. The notes ship inside the plugin, so it works
// offline and always matches the installed version.
namespace gfx::changelog {

// Call once OBS has finished loading. Shows the popup if this version is
// newer than the last one the streamer saw, then remembers it.
void showIfUpdated(QWidget *parent);
// Always shows it.
void show(QWidget *parent);

} // namespace gfx::changelog
