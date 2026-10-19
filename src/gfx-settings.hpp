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
	bool tabEvents = true;
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
	bool chatToYouTube = true;
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
	// Other people's channels shown in the feed, read-only ("twitch:name", "kick:name", …)
	QStringList chatWatch;
	// Which connected platforms MultiChat reads (Twitch + Kick are always on).
	// YouTube polls Google's API and TikTok opens a live connection — only
	// while these are on and the account has them connected on the website.
	bool chatYouTube = true;
	bool chatTikTok = true;
	// MultiChat look
	bool chatIcons = true;      // platform icon at the start of each line
	bool chatBadges = true;     // HOST / MOD / VIP / SUB tags
	bool chatNameColors = true; // chatters' own name colours (off = one neutral colour)
	bool chatCompact = false;   // tighter lines
	bool chatEmotes = true;     // Twitch / Kick / BTTV / FFZ / 7TV emotes as images
	bool chatStaffBadge = true; // GoalForgeX staff stand out (badge + tinted line)
	// MultiChat event notices: subs, gift bombs, raids, shoutouts, watch streaks…
	// as their own GoalForgeX-styled lines in the chat, per platform.
	bool chatNotices = true;
	bool ncSubs = true;      // subs, resubs, Prime, upgrades, YouTube members, TikTok subs
	bool ncGifts = true;     // gifted subs, gift bombs, gifted memberships
	bool ncGiftNames = true; // list who got a sub under a gift bomb
	bool ncRaids = true;
	bool ncCheers = true;         // bits (cheer messages), Kicks, Super Chats / Stickers
	bool ncTikTokGifts = true;    // TikTok gifts (see ncTikTokMinCoins)
	bool ncFollows = true;        // Twitch / Kick follows
	bool ncTikTokFollows = false; // TikTok follows — can be a lot
	bool ncShoutouts = true;      // ForgeBot !so and Twitch /shoutout, sent and received
	bool ncStreaks = true;        // watch streaks
	bool ncPoints = true;         // channel point redemptions
	bool ncAnnouncements = true;  // Twitch /announce
	bool ncOther = true;          // bits badges, charity, pay-it-forward, anything else Twitch posts
	int ncTikTokMinCoins = 0;     // hide TikTok gifts worth fewer diamonds than this (0 = all)
	QString noticeStyle = QStringLiteral("card");    // card | strip | minimal
	QString noticeTint = QStringLiteral("platform"); // platform | event | accent
	// Events: their own "GoalForgeX Events" dock (off = a tab in this dock)
	bool eventsOwnDock = true;
	bool eventsDockIntroduced = false; // shown once automatically after updating to 2.7
	bool msDockIntroduced = false;     // Multistream dock shown once after updating to 2.8
	// Events tab: which kinds are listed
	bool evGiftBombs = true;
	bool evPoints = true;
	bool evStreaks = true;
	bool evAlerts = true;
	bool evTikTok = true;
	bool evYouTube = true;
	bool evTimers = false;    // subathon / action timer / wheel lines
	bool evShoutouts = true;  // shoutouts sent (ForgeBot !so, Twitch /shoutout) and received
	bool evAutoExpand = true; // gift bombs open with their recipient list showing
	// Appearance (gfx-theme): palette preset, accent colour, text size, density
	QString theme = QStringLiteral("graphite"); // graphite | midnight | oled | slate | warm
	QString accent = QStringLiteral("#ffb020");
	int textScale = 100; // percent, 80–140
	bool compact = false;
	// Automation
	bool autoStartTimer = false;
	bool autoPauseTimer = false;
	// Updates
	bool checkUpdates = true;
	// Download updates in the background and install them when OBS closes.
	bool autoInstallUpdates = true;
	// The plugin version whose "What's new" popup was last shown (empty = never).
	QString lastSeenVersion;

	QList<Reaction> reactions;
};

Settings &settings();
void loadSettings();
// True when settings.json already existed at load — this OBS had the plugin before (an update, not a fresh install).
bool settingsFileExisted();
void saveSettings();

// id → label, in display order.
QList<QPair<QString, QString>> reactionTriggers();
QString reactionTriggerLabel(const QString &id);
QString reactionSummary(const Reaction &r);

} // namespace gfx
