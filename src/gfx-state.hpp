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
	QString kind;      // alert | timer | actiontimer | wheel
	QString type;      // raw broadcast type
	QString alertType; // for kind == alert
	QString event;     // e.g. "ended" for timer kinds
	QString text;
	QString by; // mod name, if a mod did it
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
	qint64 seq = 0;
	QList<FeedEvent> events;

	static LiveState fromJson(const QJsonObject &o);
};

} // namespace gfx
