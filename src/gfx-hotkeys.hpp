/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QJsonValue>
#include <QString>

#include <functional>

namespace gfx {

// Registers the "GoalForgeX: …" entries in OBS Settings → Hotkeys. Presses
// are delivered to `dispatch` on the Qt UI thread as (actionId, argument) —
// the same action ids the Live tab uses. Bindings are saved with OBS's scene
// collection like built-in hotkeys.
void registerHotkeys(std::function<void(const QString &, const QJsonValue &)> dispatch);
void unregisterHotkeys();

} // namespace gfx
