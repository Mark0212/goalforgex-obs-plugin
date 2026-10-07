/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-settings.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace gfx {

static Settings g_settings;

Settings &settings()
{
	return g_settings;
}

static QString settingsPath()
{
	char *p = obs_module_config_path("settings.json");
	const QString s = QString::fromUtf8(p ? p : "");
	bfree(p);
	return s;
}

// Bool fields, so load/save can't drift apart.
struct BoolField {
	const char *key;
	bool Settings::*member;
};
static const BoolField kBools[] = {
	{"tabLive", &Settings::tabLive},
	{"tabWidgets", &Settings::tabWidgets},
	{"tabReactions", &Settings::tabReactions},
	{"tabHealth", &Settings::tabHealth},
	{"tabChat", &Settings::tabChat},
	{"chatToTwitch", &Settings::chatToTwitch},
	{"chatToKick", &Settings::chatToKick},
	{"chatTimestamps", &Settings::chatTimestamps},
	{"cardStats", &Settings::cardStats},
	{"cardTimer", &Settings::cardTimer},
	{"cardActionTimer", &Settings::cardActionTimer},
	{"cardGoal", &Settings::cardGoal},
	{"cardWheel", &Settings::cardWheel},
	{"cardCounters", &Settings::cardCounters},
	{"cardAlerts", &Settings::cardAlerts},
	{"cardActivity", &Settings::cardActivity},
	{"autoStartTimer", &Settings::autoStartTimer},
	{"autoPauseTimer", &Settings::autoPauseTimer},
	{"checkUpdates", &Settings::checkUpdates},
	{"autoInstallUpdates", &Settings::autoInstallUpdates},
};

void loadSettings()
{
	g_settings = Settings();
	QFile f(settingsPath());
	if (!f.exists() || !f.open(QIODevice::ReadOnly))
		return;
	const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	f.close();
	for (const BoolField &b : kBools) {
		const QJsonValue v = o.value(QLatin1String(b.key));
		if (v.isBool())
			g_settings.*(b.member) = v.toBool();
	}
	const QJsonArray rs = o.value(QStringLiteral("reactions")).toArray();
	for (qsizetype i = 0; i < rs.size() && i < 50; ++i) {
		const QJsonObject ro = rs.at(i).toObject();
		Reaction r;
		r.id = ro.value(QStringLiteral("id")).toString();
		r.enabled = ro.value(QStringLiteral("enabled")).toBool(true);
		r.trigger = ro.value(QStringLiteral("trigger")).toString();
		r.action = ro.value(QStringLiteral("action")).toString();
		r.target = ro.value(QStringLiteral("target")).toString();
		r.seconds = qBound(0, ro.value(QStringLiteral("seconds")).toInt(10), 3600);
		if (r.id.isEmpty())
			r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		if (!r.trigger.isEmpty() && !r.action.isEmpty() && !r.target.isEmpty())
			g_settings.reactions.push_back(r);
	}
}

void saveSettings()
{
	const QString path = settingsPath();
	if (path.isEmpty())
		return;
	QDir().mkpath(QFileInfo(path).absolutePath());
	QJsonObject o;
	for (const BoolField &b : kBools)
		o.insert(QLatin1String(b.key), g_settings.*(b.member));
	QJsonArray rs;
	for (const Reaction &r : g_settings.reactions) {
		QJsonObject ro;
		ro.insert(QStringLiteral("id"), r.id);
		ro.insert(QStringLiteral("enabled"), r.enabled);
		ro.insert(QStringLiteral("trigger"), r.trigger);
		ro.insert(QStringLiteral("action"), r.action);
		ro.insert(QStringLiteral("target"), r.target);
		ro.insert(QStringLiteral("seconds"), r.seconds);
		rs.append(ro);
	}
	o.insert(QStringLiteral("reactions"), rs);
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "Could not save GoalForgeX settings");
		return;
	}
	f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
	if (!f.commit())
		obs_log(LOG_WARNING, "Could not save GoalForgeX settings");
}

QList<QPair<QString, QString>> reactionTriggers()
{
	return {
		{QStringLiteral("goal_reached"), QStringLiteral("Sub goal reached")},
		{QStringLiteral("follower_goal_reached"), QStringLiteral("Follower goal reached")},
		{QStringLiteral("raid"), QStringLiteral("Raid alert")},
		{QStringLiteral("subscriber"), QStringLiteral("New sub alert")},
		{QStringLiteral("resub"), QStringLiteral("Resub alert")},
		{QStringLiteral("gifted"), QStringLiteral("Gift subs alert")},
		{QStringLiteral("bits"), QStringLiteral("Bits alert")},
		{QStringLiteral("follower"), QStringLiteral("Follow alert")},
		{QStringLiteral("timer_ended"), QStringLiteral("Subathon timer ended")},
	};
}

QString reactionTriggerLabel(const QString &id)
{
	for (const auto &t : reactionTriggers())
		if (t.first == id)
			return t.second;
	return id;
}

QString reactionSummary(const Reaction &r)
{
	const QString what = r.action == QLatin1String("switch_scene")
				     ? QStringLiteral("switch to scene “%1”").arg(r.target)
				     : QStringLiteral("show “%1”").arg(r.target);
	const QString dur = r.seconds > 0 ? QStringLiteral(" for %1s").arg(r.seconds) : QString();
	return QStringLiteral("%1 → %2%3").arg(reactionTriggerLabel(r.trigger), what, dur);
}

} // namespace gfx
