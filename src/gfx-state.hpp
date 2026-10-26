/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace gfx {

struct TimerState {
	bool active = false;
	bool paused = false;
	bool locked = false;
	bool ended = false; // Intro timer only: ran out and is holding 00:00
	int remaining = 0;  // seconds, as of LiveState::receivedMs
};

// Intro timer options the dock can change live (POST /api/actiontimer/live).
// known == false on servers that don't send them yet — the dock then hides
// its Options panel.
struct IntroOptions {
	bool known = false;
	QString title;
	QString subtitle;
	int length = 300; // seconds — what Start counts down from
	bool rotateEnabled = false;
	QStringList rotateMessages;
	bool endFlash = true;
	QString endFlashColor;
	QString endFlashSpeed; // slow | normal | fast
	bool autoStartOnLive = false;
};

struct CounterState {
	QString id;
	QString name;
	int value = 0;
};

// A saved Sub Goal / Follower Goal preset (server 2.10+), or an alert preset
// (2.11+, game = the game it's labelled with). active = its saved look is
// what's on screen right now (the server works that out).
struct PresetState {
	QString id;
	QString name;
	QString game;
	bool active = false;
};

struct FeedEvent {
	qint64 seq = 0;
	qint64 at = 0;
	// alert | timer | actiontimer | wheel | giftbomb | points | streak | tiktok | youtube | hypetrain
	QString kind;
	QString type;      // raw broadcast type
	QString alertType; // for kind == alert
	QString event;     // e.g. "ended" for timer kinds
	QString text;      // ready-made one-line summary (every kind has one)
	QString by;        // mod name, if a mod did it
	// Structured fields (server 2.4+; empty on older servers)
	QString sub;            // tiktok: gift|follow|sub · youtube: superchat|supersticker|member|milestone|gift
	QString platform;       // twitch | kick | youtube | tiktok
	QString user;           // who did it (gifter, redeemer, …)
	QString title;          // reward name / TikTok gift name
	QString amount;         // subs gifted, points cost, streak length, "$5.00", …
	QString message;        // redemption input / streak or Super Chat message
	QStringList recipients; // gift bomb: everyone who got a sub
	// Gift bomb shown live (server 2.7+, ?live=1): rows sharing a bombId replace
	// each other — partial while recipients are still arriving, update = not new.
	QString bombId;
	bool partial = false;
	bool update = false;
	// Hype Train (server 2.10+, kind == hypetrain; amount = level, sub = begin|progress|end)
	qint64 progress = 0;  // points at the current level
	qint64 goal = 0;      // points the level needs
	qint64 total = 0;     // points in the whole train
	QString trainType;    // regular | golden_kappa | treasure
	qint64 expiresAt = 0; // ms epoch, 0 = ended / unknown
};

// GET /api/obs/state
struct LiveState {
	bool valid = false;
	bool pro = false;
	qint64 receivedMs = 0;

	bool isLive = false;
	qint64 liveSince = 0; // ms epoch, 0 = unknown
	int viewers = -1;     // -1 = unknown
	bool twitchConnected = false;
	bool twitchOk = true;

	TimerState timer;
	TimerState actionTimer; // the intro timer ("actiontimer" server-side)
	IntroOptions intro;
	int goalCurrent = 0;
	int goalTarget = 0;
	int followerCurrent = 0;
	int followerTarget = 0;
	bool wheelLocked = true;
	int wheelSegments = 0;
	QList<CounterState> counters;
	bool countersLocked = true;
	// Presets card. presetsKnown == false on servers that don't send presets
	// yet — the dock then hides the card.
	bool presetsKnown = false;
	bool presetsLocked = true;
	QList<PresetState> goalPresets;
	QList<PresetState> followerPresets;
	// Alert presets: only sent for accounts that can use them (staff for now);
	// alertPresetsKnown == false keeps the Alerts row hidden.
	bool alertPresetsKnown = false;
	QList<PresetState> alertPresets;
	bool alertsLocked = true;
	bool hasLastAlert = false;

	QString feed;
	// Fingerprint of the widget catalogue (ids + sizes). Changes when the
	// streamer resizes a widget in the dashboard, adds a counter, etc.
	QString catalogueRev;
	qint64 seq = 0;
	QList<FeedEvent> events;
	// Channel points for the Events tab: ok | pending | needs_reconnect |
	// no_twitch | unavailable | error — empty on servers before 2.4.
	QString channelPoints;
	// Twitch's native /shoutout in MultiChat: ok | pending | needs_scope | … —
	// empty on servers before 2.7.
	QString shoutouts;
	// Hype Train cards + notices: ok | pending | needs_reconnect | … — empty on servers before 2.10.
	QString hypeTrain;

	static LiveState fromJson(const QJsonObject &o);
};

} // namespace gfx
