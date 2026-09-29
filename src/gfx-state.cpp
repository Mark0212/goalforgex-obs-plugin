/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-state.hpp"

#include <QDateTime>
#include <QJsonArray>

namespace gfx {

static TimerState timerFrom(const QJsonObject &o)
{
	TimerState t;
	t.active = o.value(QStringLiteral("active")).toBool();
	t.paused = o.value(QStringLiteral("paused")).toBool();
	t.locked = o.value(QStringLiteral("locked")).toBool();
	t.remaining = qMax(0, o.value(QStringLiteral("remaining")).toInt());
	return t;
}

static qint64 i64(const QJsonValue &v)
{
	return static_cast<qint64>(v.toDouble());
}

LiveState LiveState::fromJson(const QJsonObject &o)
{
	LiveState s;
	s.valid = true;
	s.receivedMs = QDateTime::currentMSecsSinceEpoch();
	s.pro = o.value(QStringLiteral("pro")).toBool();

	const QJsonObject live = o.value(QStringLiteral("live")).toObject();
	s.isLive = live.value(QStringLiteral("isLive")).toBool();
	s.liveSince = i64(live.value(QStringLiteral("since")));
	s.viewers = live.value(QStringLiteral("viewers")).isDouble() ? live.value(QStringLiteral("viewers")).toInt()
								     : -1;
	s.twitchConnected = live.value(QStringLiteral("twitchConnected")).toBool();
	s.twitchOk = live.value(QStringLiteral("twitchOk")).toBool(true);

	s.timer = timerFrom(o.value(QStringLiteral("timer")).toObject());
	s.actionTimer = timerFrom(o.value(QStringLiteral("actionTimer")).toObject());

	const QJsonObject goal = o.value(QStringLiteral("goal")).toObject();
	s.goalCurrent = goal.value(QStringLiteral("current")).toInt();
	s.goalTarget = goal.value(QStringLiteral("target")).toInt();
	const QJsonObject fol = o.value(QStringLiteral("follower")).toObject();
	s.followerCurrent = fol.value(QStringLiteral("current")).toInt();
	s.followerTarget = fol.value(QStringLiteral("target")).toInt();

	const QJsonObject wheel = o.value(QStringLiteral("wheel")).toObject();
	s.wheelLocked = wheel.value(QStringLiteral("locked")).toBool(true);
	s.wheelSegments = wheel.value(QStringLiteral("segments")).toInt();

	const QJsonArray cs = o.value(QStringLiteral("counters")).toArray();
	for (qsizetype i = 0; i < cs.size(); ++i) {
		const QJsonObject c = cs.at(i).toObject();
		CounterState st;
		st.id = c.value(QStringLiteral("id")).toString();
		st.name = c.value(QStringLiteral("name")).toString();
		st.value = c.value(QStringLiteral("value")).toInt();
		if (!st.id.isEmpty())
			s.counters.push_back(st);
	}
	s.countersLocked = o.value(QStringLiteral("countersLocked")).toBool(true);
	s.alertsLocked = o.value(QStringLiteral("alertsLocked")).toBool(true);
	s.hasLastAlert = o.value(QStringLiteral("hasLastAlert")).toBool();

	s.feed = o.value(QStringLiteral("feed")).toString();
	s.seq = i64(o.value(QStringLiteral("seq")));
	const QJsonArray evs = o.value(QStringLiteral("events")).toArray();
	for (qsizetype i = 0; i < evs.size(); ++i) {
		const QJsonObject e = evs.at(i).toObject();
		FeedEvent ev;
		ev.seq = i64(e.value(QStringLiteral("seq")));
		ev.at = i64(e.value(QStringLiteral("at")));
		ev.kind = e.value(QStringLiteral("kind")).toString();
		ev.type = e.value(QStringLiteral("type")).toString();
		ev.alertType = e.value(QStringLiteral("alertType")).toString();
		ev.event = e.value(QStringLiteral("event")).toString();
		ev.text = e.value(QStringLiteral("text")).toString();
		ev.by = e.value(QStringLiteral("by")).toString();
		s.events.push_back(ev);
	}
	return s;
}

} // namespace gfx
