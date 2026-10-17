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
	int remaining = 0; // seconds, as of LiveState::receivedMs
};

struct CounterState {
	QString id;
	QString name;
	int value = 0;
};

struct FeedEvent {
	qint64 seq = 0;
	qint64 at = 0;
	// alert | timer | actiontimer | wheel | giftbomb | points | streak | tiktok | youtube
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
	TimerState actionTimer;
	int goalCurrent = 0;
	int goalTarget = 0;
	int followerCurrent = 0;
	int followerTarget = 0;
	bool wheelLocked = true;
	int wheelSegments = 0;
	QList<CounterState> counters;
	bool countersLocked = true;
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

	static LiveState fromJson(const QJsonObject &o);
};

} // namespace gfx
