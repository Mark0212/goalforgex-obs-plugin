/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace gfx {

// "When <trigger> happens → <action> <target> for <seconds>".
struct Reaction {
	QString id;
	bool enabled = true;
	QString trigger;  // see reactionTriggers()
	QString action;   // "switch_scene" | "show_source"
	QString target;   // scene or source name
	int seconds = 10; // 0 = stay (don't switch back / don't hide again)
};

// Everything the streamer can turn on/off in the dock's ⚙ Settings.
// Saved per OBS install in plugin_config/goalforgex/settings.json.
struct Settings {
	// Tabs
	bool tabLive = true;
	bool tabWidgets = true;
	bool tabReactions = true;
	bool tabHealth = true;
	bool tabChat = true;
	// Live tab cards
	bool cardStats = true;
	bool cardTimer = true;
	bool cardActionTimer = true;
	bool cardGoal = true;
	bool cardWheel = true;
	bool cardCounters = true;
	bool cardAlerts = true;
	bool cardActivity = true;
	// Chat tab: where messages go (remembered between sessions) + show times
	bool chatToTwitch = true;
	bool chatToKick = true;
	bool chatTimestamps = true;
	// Viewers / uptime / title per platform above the chat
	bool chatStats = true;
	// Highlight lines that mention the streamer or a highlight word
	bool chatHighlightMentions = true;
	// One-click delete / 10-minute timeout links at the start of each line
	bool chatQuickMod = false;
	// Keep a deleted message's text (struck through) instead of hiding it
	bool chatShowDeleted = true;
	// Subtle alternating background on every other line
	bool chatAltRows = false;
	// Freeze the feed while the mouse is over it, so lines don't move under the cursor
	bool chatPauseOnHover = false;
	// Feed text size in points; 0 = the dock's normal size
	int chatFontSize = 0;
	// Extra words that highlight a line (besides the streamer's own name)
	QStringList chatHighlightWords;
	// Automation
	bool autoStartTimer = false;
	bool autoPauseTimer = false;
	// Updates
	bool checkUpdates = true;
	// Download updates in the background and install them when OBS closes.
	bool autoInstallUpdates = true;

	QList<Reaction> reactions;
};

Settings &settings();
void loadSettings();
void saveSettings();

// id → label, in display order.
QList<QPair<QString, QString>> reactionTriggers();
QString reactionTriggerLabel(const QString &id);
QString reactionSummary(const Reaction &r);

} // namespace gfx
