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

#include <QColor>
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
static bool g_fileExisted = false;

bool settingsFileExisted()
{
	return g_fileExisted;
}

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
	{"tabEvents", &Settings::tabEvents},
	{"chatToTwitch", &Settings::chatToTwitch},
	{"chatToKick", &Settings::chatToKick},
	{"chatToYouTube", &Settings::chatToYouTube},
	{"chatTimestamps", &Settings::chatTimestamps},
	{"chatStats", &Settings::chatStats},
	{"chatHighlightMentions", &Settings::chatHighlightMentions},
	{"chatQuickMod", &Settings::chatQuickMod},
	{"chatShowDeleted", &Settings::chatShowDeleted},
	{"chatAltRows", &Settings::chatAltRows},
	{"chatPauseOnHover", &Settings::chatPauseOnHover},
	{"chatYouTube", &Settings::chatYouTube},
	{"chatTikTok", &Settings::chatTikTok},
	{"chatIcons", &Settings::chatIcons},
	{"chatBadges", &Settings::chatBadges},
	{"chatNameColors", &Settings::chatNameColors},
	{"chatCompact", &Settings::chatCompact},
	{"chatEmotes", &Settings::chatEmotes},
	{"chatStaffBadge", &Settings::chatStaffBadge},
	{"chatNotices", &Settings::chatNotices},
	{"ncSubs", &Settings::ncSubs},
	{"ncGifts", &Settings::ncGifts},
	{"ncGiftNames", &Settings::ncGiftNames},
	{"ncRaids", &Settings::ncRaids},
	{"ncCheers", &Settings::ncCheers},
	{"ncTikTokGifts", &Settings::ncTikTokGifts},
	{"ncFollows", &Settings::ncFollows},
	{"ncTikTokFollows", &Settings::ncTikTokFollows},
	{"ncShoutouts", &Settings::ncShoutouts},
	{"ncHypeTrain", &Settings::ncHypeTrain},
	{"ncStreaks", &Settings::ncStreaks},
	{"ncPoints", &Settings::ncPoints},
	{"ncAnnouncements", &Settings::ncAnnouncements},
	{"ncOther", &Settings::ncOther},
	{"eventsOwnDock", &Settings::eventsOwnDock},
	{"eventsDockIntroduced", &Settings::eventsDockIntroduced},
	{"msDockIntroduced", &Settings::msDockIntroduced},
	{"evGiftBombs", &Settings::evGiftBombs},
	{"evPoints", &Settings::evPoints},
	{"evStreaks", &Settings::evStreaks},
	{"evAlerts", &Settings::evAlerts},
	{"evTikTok", &Settings::evTikTok},
	{"evYouTube", &Settings::evYouTube},
	{"evTimers", &Settings::evTimers},
	{"evShoutouts", &Settings::evShoutouts},
	{"evHypeTrain", &Settings::evHypeTrain},
	{"evAutoExpand", &Settings::evAutoExpand},
	{"compact", &Settings::compact},
	{"cardStats", &Settings::cardStats},
	{"cardTimer", &Settings::cardTimer},
	{"cardActionTimer", &Settings::cardActionTimer},
	{"cardGoal", &Settings::cardGoal},
	{"cardWheel", &Settings::cardWheel},
	{"cardCounters", &Settings::cardCounters},
	{"cardPresets", &Settings::cardPresets},
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
	g_fileExisted = f.exists();
	if (!f.exists() || !f.open(QIODevice::ReadOnly))
		return;
	const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	f.close();
	for (const BoolField &b : kBools) {
		const QJsonValue v = o.value(QLatin1String(b.key));
		if (v.isBool())
			g_settings.*(b.member) = v.toBool();
	}
	g_settings.chatFontSize = qBound(0, o.value(QStringLiteral("chatFontSize")).toInt(0), 24);
	g_settings.ncTikTokMinCoins = qBound(0, o.value(QStringLiteral("ncTikTokMinCoins")).toInt(0), 100000);
	static const QStringList kStyles = {QStringLiteral("card"), QStringLiteral("strip"), QStringLiteral("minimal")};
	static const QStringList kTints = {QStringLiteral("platform"), QStringLiteral("event"),
					   QStringLiteral("accent")};
	const QString ns = o.value(QStringLiteral("noticeStyle")).toString();
	if (kStyles.contains(ns))
		g_settings.noticeStyle = ns;
	const QString nt = o.value(QStringLiteral("noticeTint")).toString();
	if (kTints.contains(nt))
		g_settings.noticeTint = nt;
	const QJsonArray hw = o.value(QStringLiteral("chatHighlightWords")).toArray();
	for (qsizetype i = 0; i < hw.size() && i < 30; ++i) {
		const QString w = hw.at(i).toString().trimmed().left(40);
		if (!w.isEmpty() && !g_settings.chatHighlightWords.contains(w, Qt::CaseInsensitive))
			g_settings.chatHighlightWords << w;
	}
	const QJsonArray cw = o.value(QStringLiteral("chatWatch")).toArray();
	for (qsizetype i = 0; i < cw.size() && g_settings.chatWatch.size() < 6; ++i) {
		const QString w = cw.at(i).toString().trimmed().left(220);
		if (w.contains(QLatin1Char(':')) && !w.contains(QLatin1Char(',')) &&
		    !g_settings.chatWatch.contains(w, Qt::CaseInsensitive))
			g_settings.chatWatch << w;
	}
	// Appearance — anything unknown or out of range falls back to the default.
	static const QStringList kThemes = {QStringLiteral("graphite"), QStringLiteral("midnight"),
					    QStringLiteral("oled"), QStringLiteral("slate"), QStringLiteral("warm")};
	const QString th = o.value(QStringLiteral("theme")).toString();
	if (kThemes.contains(th))
		g_settings.theme = th;
	const QString ac = o.value(QStringLiteral("accent")).toString();
	if (ac.size() == 7 && ac.startsWith(QLatin1Char('#')) && QColor(ac).isValid())
		g_settings.accent = ac.toLower();
	g_settings.lastSeenVersion = o.value(QStringLiteral("lastSeenVersion")).toString().left(20);
	g_settings.updateDeclined = o.value(QStringLiteral("updateDeclined")).toString().left(20);
	const int ts = o.value(QStringLiteral("textScale")).toInt(100);
	g_settings.textScale = qBound(80, ts, 140);
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
	o.insert(QStringLiteral("chatFontSize"), g_settings.chatFontSize);
	o.insert(QStringLiteral("ncTikTokMinCoins"), g_settings.ncTikTokMinCoins);
	o.insert(QStringLiteral("noticeStyle"), g_settings.noticeStyle);
	o.insert(QStringLiteral("noticeTint"), g_settings.noticeTint);
	o.insert(QStringLiteral("chatHighlightWords"), QJsonArray::fromStringList(g_settings.chatHighlightWords));
	o.insert(QStringLiteral("chatWatch"), QJsonArray::fromStringList(g_settings.chatWatch));
	o.insert(QStringLiteral("theme"), g_settings.theme);
	o.insert(QStringLiteral("accent"), g_settings.accent);
	o.insert(QStringLiteral("textScale"), g_settings.textScale);
	o.insert(QStringLiteral("lastSeenVersion"), g_settings.lastSeenVersion);
	o.insert(QStringLiteral("updateDeclined"), g_settings.updateDeclined);
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
		{QStringLiteral("gift_bomb"), QStringLiteral("Gift bomb (5+ subs)")},
		{QStringLiteral("channel_points"), QStringLiteral("Channel points redeemed")},
		{QStringLiteral("watch_streak"), QStringLiteral("Watch streak shared")},
		{QStringLiteral("tiktok_gift"), QStringLiteral("TikTok gift")},
		{QStringLiteral("youtube_superchat"), QStringLiteral("YouTube Super Chat / Sticker")},
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
