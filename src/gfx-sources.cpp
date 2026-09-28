/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-sources.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/vec2.h>

#include <QUrl>
#include <QUrlQuery>

#include <cstring>
#include <string>

namespace gfx {
namespace sources {

// Private settings keys — invisible in the Properties dialog, saved with the
// scene collection, and how we recognise our own sources after a restart.
static const char *kWidgetKey = "gfx_widget";
static const char *kAccountKey = "gfx_account";
static const char *kBrowserId = "browser_source";

QString fullUrl(const WidgetInfo &w, const QString &overlayKey)
{
	QUrl u(w.url);
	QUrlQuery q(u);
	q.removeAllQueryItems(QStringLiteral("key"));
	q.addQueryItem(QStringLiteral("key"), overlayKey);
	u.setQuery(q);
	return u.toString(QUrl::FullyEncoded);
}

struct FindCtx {
	std::string account;
	std::string widget;
	obs_source_t *found = nullptr;
};

static bool isOurs(obs_source_t *src, const char *account, const char *widget)
{
	const char *id = obs_source_get_unversioned_id(src);
	if (!id || strcmp(id, kBrowserId) != 0)
		return false;
	obs_data_t *priv = obs_source_get_private_settings(src);
	if (!priv)
		return false;
	const bool match = strcmp(obs_data_get_string(priv, kAccountKey), account) == 0 &&
			   (!widget || strcmp(obs_data_get_string(priv, kWidgetKey), widget) == 0) &&
			   obs_data_get_string(priv, kWidgetKey)[0] != '\0';
	obs_data_release(priv);
	return match;
}

obs_source_t *find(const QString &account, const QString &widgetId)
{
	FindCtx ctx;
	ctx.account = account.toStdString();
	ctx.widget = widgetId.toStdString();
	obs_enum_sources(
		[](void *param, obs_source_t *src) {
			auto *c = static_cast<FindCtx *>(param);
			if (isOurs(src, c->account.c_str(), c->widget.c_str())) {
				c->found = obs_source_get_ref(src);
				return !c->found; // stop once we hold a reference
			}
			return true;
		},
		&ctx);
	return ctx.found;
}

obs_sceneitem_t *findItem(obs_source_t *sceneSource, obs_source_t *source)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene || !source)
		return nullptr;
	return obs_scene_find_source_recursive(scene, obs_source_get_name(source));
}

// "GoalForgeX · Goal Widget", or "… (2)" etc. if a source of that name exists.
static std::string uniqueName(const QString &base)
{
	const std::string b = base.toStdString();
	std::string name = b;
	for (int i = 2; i < 100; ++i) {
		obs_source_t *existing = obs_get_source_by_name(name.c_str());
		if (!existing)
			return name;
		obs_source_release(existing);
		name = b + " (" + std::to_string(i) + ")";
	}
	return name;
}

static void canvasSize(float *w, float *h)
{
	obs_video_info ovi = {};
	if (obs_get_video_info(&ovi)) {
		*w = static_cast<float>(ovi.base_width);
		*h = static_cast<float>(ovi.base_height);
	} else {
		*w = 1920.0f;
		*h = 1080.0f;
	}
}

static void itemExtent(obs_sceneitem_t *item, float *w, float *h)
{
	if (obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE) {
		vec2 b;
		obs_sceneitem_get_bounds(item, &b);
		*w = b.x;
		*h = b.y;
		return;
	}
	vec2 sc;
	obs_sceneitem_get_scale(item, &sc);
	obs_source_t *src = obs_sceneitem_get_source(item);
	*w = static_cast<float>(obs_source_get_width(src)) * sc.x;
	*h = static_cast<float>(obs_source_get_height(src)) * sc.y;
}

void place(obs_sceneitem_t *item, Place where)
{
	if (!item)
		return;
	float cw, ch, iw, ih;
	canvasSize(&cw, &ch);
	itemExtent(item, &iw, &ih);
	const float m = 24.0f; // margin from the canvas edge
	float x = 0.0f, y = 0.0f;
	switch (where) {
	case Place::TopLeft:
		x = m;
		y = m;
		break;
	case Place::TopCenter:
		x = (cw - iw) / 2.0f;
		y = m;
		break;
	case Place::TopRight:
		x = cw - iw - m;
		y = m;
		break;
	case Place::Center:
		x = (cw - iw) / 2.0f;
		y = (ch - ih) / 2.0f;
		break;
	case Place::BottomLeft:
		x = m;
		y = ch - ih - m;
		break;
	case Place::BottomCenter:
		x = (cw - iw) / 2.0f;
		y = ch - ih - m;
		break;
	case Place::BottomRight:
		x = cw - iw - m;
		y = ch - ih - m;
		break;
	}
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	vec2 pos;
	vec2_set(&pos, x, y);
	obs_sceneitem_set_pos(item, &pos);
}

void setScalePercent(obs_sceneitem_t *item, double percent)
{
	if (!item)
		return;
	const float s = static_cast<float>(percent / 100.0);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	vec2 sc;
	vec2_set(&sc, s, s);
	obs_sceneitem_set_scale(item, &sc);
}

double scalePercent(obs_sceneitem_t *item)
{
	if (!item || obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE)
		return 100.0;
	vec2 sc;
	obs_sceneitem_get_scale(item, &sc);
	return static_cast<double>(sc.x) * 100.0;
}

void fitToCanvas(obs_sceneitem_t *item)
{
	if (!item)
		return;
	float cw, ch;
	canvasSize(&cw, &ch);
	vec2 b, pos, one;
	vec2_set(&b, cw, ch);
	vec2_set(&pos, 0.0f, 0.0f);
	vec2_set(&one, 1.0f, 1.0f);
	obs_sceneitem_set_scale(item, &one);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &b);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_pos(item, &pos);
}

AddResult addToScene(obs_source_t *sceneSource, const WidgetInfo &w, const QString &overlayKey, const QString &account,
		     QString *error)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		if (error)
			*error = QStringLiteral("Pick a scene first.");
		return AddResult::Failed;
	}

	obs_source_t *src = find(account, w.id);
	if (src) {
		obs_sceneitem_t *existing = findItem(sceneSource, src);
		if (existing) {
			obs_sceneitem_select(existing, true);
			obs_source_release(src);
			return AddResult::AlreadyInScene;
		}
		obs_sceneitem_t *item = obs_scene_add(scene, src);
		obs_source_release(src);
		if (!item) {
			if (error)
				*error = QStringLiteral("OBS couldn't add the source to that scene.");
			return AddResult::Failed;
		}
		if (w.fullCanvas)
			fitToCanvas(item);
		else
			place(item, Place::Center);
		obs_sceneitem_select(item, true);
		return AddResult::Reused;
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "url", fullUrl(w, overlayKey).toUtf8().constData());
	obs_data_set_bool(settings, "is_local_file", false);
	obs_data_set_int(settings, "width", w.width);
	obs_data_set_int(settings, "height", w.height);
	// Keep running while hidden / off-scene so timers and alert queues stay
	// live — the overlays resync themselves anyway, but never shut down.
	obs_data_set_bool(settings, "shutdown", false);
	obs_data_set_bool(settings, "restart_when_active", false);
	// Alerts/wheel sounds show up in the OBS audio mixer instead of playing
	// straight to the desktop.
	obs_data_set_bool(settings, "reroute_audio", w.audio);
	// CSS left unset: the Browser Source default is already a transparent body.

	const std::string name = uniqueName(QStringLiteral("GoalForgeX · ") + w.name);
	src = obs_source_create(kBrowserId, name.c_str(), settings, nullptr);
	obs_data_release(settings);
	if (!src) {
		if (error)
			*error = QStringLiteral(
				"OBS couldn't create a Browser Source. Is the obs-browser plugin installed?");
		return AddResult::Failed;
	}
	obs_data_t *priv = obs_source_get_private_settings(src);
	obs_data_set_string(priv, kWidgetKey, w.id.toUtf8().constData());
	obs_data_set_string(priv, kAccountKey, account.toUtf8().constData());
	obs_data_release(priv);

	obs_sceneitem_t *item = obs_scene_add(scene, src);
	obs_source_release(src); // the scene item holds its own reference
	if (!item) {
		if (error)
			*error = QStringLiteral("OBS couldn't add the source to that scene.");
		return AddResult::Failed;
	}
	if (w.fullCanvas)
		fitToCanvas(item);
	else
		place(item, Place::Center);
	obs_sceneitem_select(item, true);
	return AddResult::Added;
}

struct SyncCtx {
	std::string account;
	const QList<WidgetInfo> *widgets;
	QString key;
	int updated = 0;
};

int syncUrls(const QList<WidgetInfo> &widgets, const QString &overlayKey, const QString &account)
{
	SyncCtx ctx;
	ctx.account = account.toStdString();
	ctx.widgets = &widgets;
	ctx.key = overlayKey;
	obs_enum_sources(
		[](void *param, obs_source_t *src) {
			auto *c = static_cast<SyncCtx *>(param);
			if (!isOurs(src, c->account.c_str(), nullptr))
				return true;
			obs_data_t *priv = obs_source_get_private_settings(src);
			const QString wid = QString::fromUtf8(obs_data_get_string(priv, kWidgetKey));
			obs_data_release(priv);
			for (const WidgetInfo &w : *c->widgets) {
				if (w.id != wid)
					continue;
				const std::string want = fullUrl(w, c->key).toStdString();
				obs_data_t *s = obs_source_get_settings(src);
				if (want != obs_data_get_string(s, "url")) {
					obs_data_t *upd = obs_data_create();
					obs_data_set_string(upd, "url", want.c_str());
					obs_source_update(src, upd);
					obs_data_release(upd);
					c->updated++;
				}
				obs_data_release(s);
				break;
			}
			return true;
		},
		&ctx);
	return ctx.updated;
}

bool reload(obs_source_t *source)
{
	if (!source)
		return false;
	obs_properties_t *props = obs_source_properties(source);
	if (!props)
		return false;
	// Same as clicking "Refresh cache of current page" in the source's Properties.
	// (The callback's bool return means "redraw the properties UI", not success.)
	obs_property_t *p = obs_properties_get(props, "refreshnocache");
	if (p)
		obs_property_button_clicked(p, source);
	obs_properties_destroy(props);
	return p != nullptr;
}

} // namespace sources
} // namespace gfx
