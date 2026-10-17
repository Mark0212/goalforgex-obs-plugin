/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QColor>
#include <QImage>
#include <QString>

// The event-card look shared by MultiChat's notices and the Events dock, so
// a gift bomb or a raid reads the same in both. Defined in gfx-chat.cpp.
namespace gfx::notice {

// Pill for one event type (sub, giftbomb, raid, points, streak, …).
struct Look {
	QString icon;  // emoji
	QString label; // "GIFT BOMB"
	QString color; // the event's own colour
};
Look look(const QString &type);
// Each platform's brand colour (Twitch purple, Kick green, YouTube red, TikTok pink).
QString platformColor(const QString &platform);
// Card tint for an event per Settings → Event notices → Colour (platform | event | accent).
QString tint(const QString &platform, const QString &eventColor);
QColor mix(const QColor &a, const QColor &b, qreal t);
// The platform icon MultiChat draws (bundled Twitch / Kick art, drawn YouTube / TikTok).
QImage platformIcon(const QString &platform, int logicalPx, qreal dpr);

} // namespace gfx::notice
