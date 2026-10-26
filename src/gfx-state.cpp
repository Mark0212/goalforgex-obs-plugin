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
	t.ended = o.value(QStringLiteral("ended")).toBool();
	t.remaining = qMax(0, o.value(QStringLiteral("remaining")).toInt());
	return t;
}

static qint64 i64(const QJsonValue &v)
{
	return static_cast<qint64>(v.toDouble());
}

// "amount" is a number (subs, points, streak) or text ("$5.00").
static QString textOf(const QJsonValue &v)
{
	if (v.isDouble())
		return QString::number(static_cast<qint64>(v.toDouble()));
	return v.toString();
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
	const QJsonObject at = o.value(QStringLiteral("actionTimer")).toObject();
	s.actionTimer = timerFrom(at);
	const QJsonObject io = at.value(QStringLiteral("intro")).toObject();
	if (!io.isEmpty()) {
		s.intro.known = true;
		s.intro.title = io.value(QStringLiteral("title")).toString();
		s.intro.subtitle = io.value(QStringLiteral("subtitle")).toString();
		s.intro.length = qMax(1, io.value(QStringLiteral("length")).toInt(300));
		s.intro.rotateEnabled = io.value(QStringLiteral("rotateEnabled")).toBool();
		const QJsonArray msgs = io.value(QStringLiteral("rotateMessages")).toArray();
		for (qsizetype i = 0; i < msgs.size(); ++i) {
			const QString m = msgs.at(i).toString();
			if (!m.isEmpty())
				s.intro.rotateMessages << m;
		}
		s.intro.endFlash = io.value(QStringLiteral("endFlash")).toBool(true);
		s.intro.endFlashColor = io.value(QStringLiteral("endFlashColor")).toString();
		s.intro.endFlashSpeed = io.value(QStringLiteral("endFlashSpeed")).toString();
		s.intro.autoStartOnLive = io.value(QStringLiteral("autoStartOnLive")).toBool();
	}

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

	const QJsonValue pv = o.value(QStringLiteral("presets"));
	s.presetsKnown = pv.isObject();
	s.presetsLocked = o.value(QStringLiteral("presetsLocked")).toBool(true);
	if (s.presetsKnown) {
		const QJsonObject po = pv.toObject();
		const auto readList = [&po](const QString &key, QList<PresetState> &out) {
			// Index loop, not range-for: Apple clang's -Wrange-loop-analysis
			// errors on range-for over a QJsonArray (it binds to temporaries).
			const QJsonArray arr = po.value(key).toArray();
			for (qsizetype i = 0; i < arr.size(); ++i) {
				const QJsonObject p = arr.at(i).toObject();
				PresetState st;
				st.id = p.value(QStringLiteral("id")).toString();
				st.name = p.value(QStringLiteral("name")).toString();
				st.active = p.value(QStringLiteral("active")).toBool();
				if (!st.id.isEmpty())
					out.push_back(st);
			}
		};
		readList(QStringLiteral("goal"), s.goalPresets);
		readList(QStringLiteral("follower"), s.followerPresets);
	}
	s.alertsLocked = o.value(QStringLiteral("alertsLocked")).toBool(true);
	s.hasLastAlert = o.value(QStringLiteral("hasLastAlert")).toBool();

	s.feed = o.value(QStringLiteral("feed")).toString();
	s.catalogueRev = o.value(QStringLiteral("catalogueRev")).toString();
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
		ev.sub = e.value(QStringLiteral("sub")).toString();
		ev.platform = e.value(QStringLiteral("platform")).toString();
		ev.user = e.value(QStringLiteral("user")).toString();
		ev.title = e.value(QStringLiteral("title")).toString();
		ev.amount = textOf(e.value(QStringLiteral("amount")));
		ev.message = e.value(QStringLiteral("message")).toString();
		const QJsonArray rs = e.value(QStringLiteral("recipients")).toArray();
		for (qsizetype j = 0; j < rs.size(); ++j) {
			const QString r = rs.at(j).toString();
			if (!r.isEmpty())
				ev.recipients << r;
		}
		ev.bombId = e.value(QStringLiteral("bombId")).toString().left(24);
		ev.partial = e.value(QStringLiteral("partial")).toBool();
		ev.update = e.value(QStringLiteral("update")).toBool() && !ev.bombId.isEmpty();
		ev.progress = i64(e.value(QStringLiteral("progress")));
		ev.goal = i64(e.value(QStringLiteral("goal")));
		ev.total = i64(e.value(QStringLiteral("total")));
		ev.trainType = e.value(QStringLiteral("trainType")).toString();
		ev.expiresAt = i64(e.value(QStringLiteral("expiresAt")));
		s.events.push_back(ev);
	}
	const QJsonObject es = o.value(QStringLiteral("eventsStatus")).toObject();
	s.channelPoints = es.value(QStringLiteral("channelPoints")).toString();
	s.shoutouts = es.value(QStringLiteral("shoutouts")).toString();
	s.hypeTrain = es.value(QStringLiteral("hypeTrain")).toString();
	return s;
}

} // namespace gfx
