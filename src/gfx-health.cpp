/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-health.hpp"
#include "gfx-theme.hpp"
#include "gfx-auth.hpp"
#include "gfx-http.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QUrl>
#include <QVBoxLayout>

using namespace gfx;

GfxHealthPanel::GfxHealthPanel(QWidget *parent) : QWidget(parent)
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 4, 0, 4);
	outer->setSpacing(8);
	auto *top = new QHBoxLayout();
	auto *title = new QLabel(QStringLiteral("Checks your setup and fixes common problems in one click."), this);
	title->setWordWrap(true);
	title->setObjectName(QStringLiteral("gfxMuted"));
	auto *recheck = new QPushButton(QStringLiteral("↻ Re-check"), this);
	theme::setRole(recheck, "ghost");
	top->addWidget(title, 1);
	top->addWidget(recheck);
	outer->addLayout(top);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget(scroll);
	rows_ = new QVBoxLayout(content);
	rows_->setContentsMargins(0, 0, 0, 0);
	rows_->setSpacing(6);
	rows_->addStretch(1);
	scroll->setWidget(content);
	outer->addWidget(scroll, 1);
	connect(recheck, &QPushButton::clicked, this, [this] { refresh(); });
	timer_.setInterval(15000);
	connect(&timer_, &QTimer::timeout, this, [this] {
		if (isVisible())
			refresh();
	});
	timer_.start();
}

void GfxHealthPanel::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	refresh();
}

void GfxHealthPanel::addRow(bool ok, const QString &text, const QString &fixLabel, std::function<void()> fix)
{
	auto *row = new QFrame(this);
	row->setObjectName(QStringLiteral("gfxHealthRow"));
	theme::setState(row, ok ? "ok" : "warn");
	auto *h = new QHBoxLayout(row);
	h->setContentsMargins(10, 7, 8, 7);
	auto *lbl = new QLabel((ok ? QStringLiteral("✅  ") : QStringLiteral("⚠️  ")) + text, row);
	lbl->setWordWrap(true);
	h->addWidget(lbl, 1);
	if (fix) {
		auto *b = new QPushButton(fixLabel.isEmpty() ? QStringLiteral("Fix") : fixLabel, row);
		theme::setRole(b, "primary");
		h->addWidget(b);
		connect(b, &QPushButton::clicked, this, [this, fix] {
			fix();
			refresh();
		});
	}
	rows_->insertWidget(rows_->count() - 1, row); // keep the stretch last
}

static const WidgetInfo *widgetById(const QList<WidgetInfo> *list, const QString &id)
{
	if (!list)
		return nullptr;
	for (const WidgetInfo &w : *list)
		if (w.id == id)
			return &w;
	return nullptr;
}

void GfxHealthPanel::refresh()
{
	while (rows_->count() > 1) {
		QLayoutItem *it = rows_->takeAt(0);
		if (it->widget())
			it->widget()->deleteLater();
		delete it;
	}
	int problems = 0;
	const auto warn = [this, &problems](const QString &t, const QString &l, std::function<void()> f) {
		++problems;
		addRow(false, t, l, std::move(f));
	};

	// ── Connection ──────────────────────────────────────────────────
	if (!ctx_.connected || ctx_.needsReconnect) {
		warn(QStringLiteral("This OBS isn't connected to GoalForgeX."), QStringLiteral("Connect"),
		     ctx_.reconnect);
		return;
	}
	const LiveState *st = ctx_.state;
	const Credentials cred = Auth::instance().credentials();
	const QString dash = baseUrl() + QStringLiteral("/dashboard");
	if (st && st->valid && !st->twitchConnected)
		warn(QStringLiteral(
			     "Twitch isn't connected to your GoalForgeX account, so alerts and goals won't update."),
		     QStringLiteral("Open"), [dash] { QDesktopServices::openUrl(QUrl(dash)); });
	else if (st && st->valid && !st->twitchOk)
		warn(QStringLiteral("Your Twitch connection on GoalForgeX needs to be renewed."),
		     QStringLiteral("Open"), [dash] { QDesktopServices::openUrl(QUrl(dash)); });

	// ── Widgets pasted in by hand ───────────────────────────────────
	const QList<sources::OwnedSource> mine = sources::owned(cred.username);
	QStringList ownedIds;
	for (const auto &o : mine)
		ownedIds << o.widgetId;
	for (const sources::ManualSource &m : sources::findManual(cred.username)) {
		const WidgetInfo *w = widgetById(ctx_.widgets, m.widgetId);
		if (!w || ownedIds.contains(m.widgetId))
			continue;
		const QString name = m.name, key = cred.overlayKey, account = cred.username;
		const WidgetInfo copy = *w;
		warn(QStringLiteral("“%1” was added by hand. Take it over to control it from this panel.").arg(name),
		     QStringLiteral("Take over"),
		     [name, copy, key, account] { sources::adopt(name, copy, key, account); });
		ownedIds << m.widgetId;
	}

	// ── Our sources ────────────────────────────────────────────────
	obs_source_t *live = obs_frontend_get_current_scene();
	const QString liveName = live ? QString::fromUtf8(obs_source_get_name(live)) : QString();
	bool keyStale = false;
	bool alertsLive = false;
	for (const sources::OwnedSource &o : mine) {
		obs_source_t *src = obs_get_source_by_name(o.name.toUtf8().constData());
		if (!src)
			continue;
		const WidgetInfo *w = widgetById(ctx_.widgets, o.widgetId);
		if (sources::keyInUrl(sources::sourceUrl(src)) != cred.overlayKey)
			keyStale = true;
		if (w && !w->fullCanvas) {
			int sw = 0, sh = 0;
			sources::sourceSize(src, &sw, &sh);
			if (sw > 0 && sh > 0 && (sw < w->width * 3 / 4 || sh < w->height * 3 / 4)) {
				const QString n = o.name;
				const int rw = w->width, rh = w->height;
				warn(QStringLiteral("“%1” is %2×%3 — it may be cut off (recommended %4×%5).")
					     .arg(n)
					     .arg(sw)
					     .arg(sh)
					     .arg(rw)
					     .arg(rh),
				     QStringLiteral("Resize"), [n, rw, rh] {
					     if (obs_source_t *s = obs_get_source_by_name(n.toUtf8().constData())) {
						     sources::setSourceSize(s, rw, rh);
						     obs_source_release(s);
					     }
				     });
			}
		}
		if (o.widgetId == QLatin1String("alerts") || o.widgetId == QLatin1String("spinwheel")) {
			if (obs_source_muted(src) || obs_source_get_volume(src) < 0.01f) {
				const QString n = o.name;
				warn(QStringLiteral(
					     "“%1” is muted in the audio mixer, so its sounds won't play on stream.")
					     .arg(n),
				     QStringLiteral("Unmute"), [n] {
					     if (obs_source_t *s = obs_get_source_by_name(n.toUtf8().constData())) {
						     obs_source_set_muted(s, false);
						     if (obs_source_get_volume(s) < 0.01f)
							     obs_source_set_volume(s, 1.0f);
						     obs_source_release(s);
					     }
				     });
			}
		}
		if (live) {
			if (obs_sceneitem_t *item = sources::findItem(live, src)) {
				if (o.widgetId == QLatin1String("alerts"))
					alertsLive = true;
				if (!obs_sceneitem_visible(item)) {
					const QString n = o.name, sc = liveName;
					warn(QStringLiteral("“%1” is hidden in the scene you're live on (“%2”).")
						     .arg(n, sc),
					     QStringLiteral("Show"), [n, sc] {
						     obs_source_t *scs =
							     obs_get_source_by_name(sc.toUtf8().constData());
						     obs_source_t *s = obs_get_source_by_name(n.toUtf8().constData());
						     if (scs && s)
							     if (obs_sceneitem_t *it = sources::findItem(scs, s))
								     obs_sceneitem_set_visible(it, true);
						     if (s)
							     obs_source_release(s);
						     if (scs)
							     obs_source_release(scs);
					     });
				}
			}
		}
		obs_source_release(src);
	}
	if (keyStale) {
		const QList<WidgetInfo> widgets = ctx_.widgets ? *ctx_.widgets : QList<WidgetInfo>();
		const QString key = cred.overlayKey, account = cred.username;
		warn(QStringLiteral("Some GoalForgeX sources are using an old link and won't show anything."),
		     QStringLiteral("Update links"),
		     [widgets, key, account] { sources::syncUrls(widgets, key, account); });
	}
	const WidgetInfo *alerts = widgetById(ctx_.widgets, QStringLiteral("alerts"));
	if (live && alerts && !alerts->locked && !alertsLive) {
		const WidgetInfo copy = *alerts;
		const QString key = cred.overlayKey, account = cred.username, sc = liveName;
		const auto msg = ctx_.message;
		warn(QStringLiteral("Your alerts aren't in the scene you're live on (“%1”).").arg(sc),
		     QStringLiteral("Add"), [copy, key, account, sc, msg] {
			     obs_source_t *scs = obs_get_source_by_name(sc.toUtf8().constData());
			     if (!scs)
				     return;
			     QString err;
			     const auto r = sources::addToScene(scs, copy, key, account, &err);
			     obs_source_release(scs);
			     if (msg)
				     msg(r == sources::AddResult::Failed
						 ? err
						 : QStringLiteral("Added alerts to “%1”.").arg(sc));
		     });
	}
	if (live)
		obs_source_release(live);

	// ── Plugin update ──────────────────────────────────────────────
	if (!ctx_.latestVersion.isEmpty()) {
		const QString url = baseUrl() + QStringLiteral("/obs");
		warn(QStringLiteral("GoalForgeX for OBS %1 is available.").arg(ctx_.latestVersion),
		     QStringLiteral("Download"), [url] { QDesktopServices::openUrl(QUrl(url)); });
	}

	if (!problems)
		addRow(true, QStringLiteral("Everything looks good."));
}
