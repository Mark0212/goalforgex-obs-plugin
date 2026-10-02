/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-sources.hpp"
#include "gfx-http.hpp"

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
// The Browser Source width/height this plugin last applied. A source whose
// current size no longer matches was resized by hand, so syncSizes() leaves
// it alone. -1 = never auto-size (adopted hand-made sources). Missing (0) =
// created by plugin ≤1.2, which always used the catalogue size → managed.
static const char *kSizeWKey = "gfx_w";
static const char *kSizeHKey = "gfx_h";

static void rememberSize(obs_source_t *src, int w, int h)
{
	obs_data_t *priv = obs_source_get_private_settings(src);
	if (!priv)
		return;
	obs_data_set_int(priv, kSizeWKey, w);
	obs_data_set_int(priv, kSizeHKey, h);
	obs_data_release(priv);
}

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
	rememberSize(src, w.width, w.height);

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

struct SizeTodo {
	obs_source_t *src; // own reference
	const WidgetInfo *w;
};

struct SizeCtx {
	std::string account;
	const QList<WidgetInfo> *widgets;
	QList<SizeTodo> todo;
};

static bool nearly(float a, float b)
{
	return a > b - 2.0f && a < b + 2.0f;
}

// A source switching between full-canvas and native size (e.g. Chat Box was
// full-canvas before) needs its scene items re-laid out too, or the old
// fit-to-canvas bounds would just stretch the new size back over the canvas.
static void relayoutItems(obs_source_t *src, const WidgetInfo &w)
{
	QList<obs_source_t *> scenes;
	obs_enum_scenes(
		[](void *param, obs_source_t *scene) {
			static_cast<QList<obs_source_t *> *>(param)->push_back(obs_source_get_ref(scene));
			return true;
		},
		&scenes);
	float cw, ch;
	canvasSize(&cw, &ch);
	const char *name = obs_source_get_name(src);
	for (obs_source_t *sceneSrc : scenes) {
		if (!sceneSrc)
			continue;
		obs_scene_t *scene = obs_scene_from_source(sceneSrc);
		if (!scene)
			scene = obs_group_from_source(sceneSrc);
		obs_sceneitem_t *item = scene ? obs_scene_find_source(scene, name) : nullptr;
		if (item) {
			const bool bounded = obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE;
			vec2 b = {};
			if (bounded)
				obs_sceneitem_get_bounds(item, &b);
			const bool canvasFit = bounded && nearly(b.x, cw) && nearly(b.y, ch);
			if (w.fullCanvas && !bounded) {
				fitToCanvas(item);
			} else if (!w.fullCanvas && canvasFit) {
				vec2 one;
				vec2_set(&one, 1.0f, 1.0f);
				obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
				obs_sceneitem_set_scale(item, &one);
				place(item, Place::Center);
			}
		}
		obs_source_release(sceneSrc);
	}
}

int syncSizes(const QList<WidgetInfo> &widgets, const QString &account)
{
	SizeCtx ctx;
	ctx.account = account.toStdString();
	ctx.widgets = &widgets;
	// Collect first, change after: resizing / touching scene items while
	// obs_enum_sources holds its lock isn't something to rely on.
	obs_enum_sources(
		[](void *param, obs_source_t *src) {
			auto *c = static_cast<SizeCtx *>(param);
			if (!isOurs(src, c->account.c_str(), nullptr))
				return true;
			obs_data_t *priv = obs_source_get_private_settings(src);
			const QString wid = QString::fromUtf8(obs_data_get_string(priv, kWidgetKey));
			const int rw = static_cast<int>(obs_data_get_int(priv, kSizeWKey));
			const int rh = static_cast<int>(obs_data_get_int(priv, kSizeHKey));
			obs_data_release(priv);
			for (const WidgetInfo &w : *c->widgets) {
				if (w.id != wid)
					continue;
				int cw = 0, ch = 0;
				sourceSize(src, &cw, &ch);
				const bool managed = (rw == 0 && rh == 0) || (rw == cw && rh == ch);
				if (managed && (cw != w.width || ch != w.height))
					c->todo.push_back({obs_source_get_ref(src), &w});
				break;
			}
			return true;
		},
		&ctx);
	int resized = 0;
	for (const SizeTodo &t : ctx.todo) {
		if (!t.src)
			continue;
		setSourceSize(t.src, t.w->width, t.w->height);
		relayoutItems(t.src, *t.w);
		obs_source_release(t.src);
		resized++;
	}
	return resized;
}

struct OwnedCtx {
	std::string account;
	QList<OwnedSource> out;
};

QList<OwnedSource> owned(const QString &account)
{
	OwnedCtx ctx;
	ctx.account = account.toStdString();
	obs_enum_sources(
		[](void *param, obs_source_t *src) {
			auto *c = static_cast<OwnedCtx *>(param);
			if (isOurs(src, c->account.c_str(), nullptr)) {
				obs_data_t *priv = obs_source_get_private_settings(src);
				OwnedSource o;
				o.name = QString::fromUtf8(obs_source_get_name(src));
				o.widgetId = QString::fromUtf8(obs_data_get_string(priv, kWidgetKey));
				obs_data_release(priv);
				c->out.push_back(o);
			}
			return true;
		},
		&ctx);
	return ctx.out;
}

// Which catalogue widget a GoalForgeX overlay URL points at ("" = not one).
static QString widgetIdForUrl(const QUrl &u)
{
	const QUrlQuery q(u);
	if (q.queryItemValue(QStringLiteral("src")) == QLatin1String("xive"))
		return QString(); // Xive overlays aren't in the plugin's catalogue
	QString f = u.path().section(QLatin1Char('/'), -1).toLower();
	if (f.endsWith(QLatin1String(".html")))
		f.chop(5);
	if (f == QLatin1String("widget")) {
		const QString t = q.queryItemValue(QStringLiteral("type"));
		if (t == QLatin1String("follower") || t == QLatin1String("timer") || t == QLatin1String("actiontimer"))
			return t;
		return QStringLiteral("goal");
	}
	if (f == QLatin1String("spinwheel") || f == QLatin1String("bitscup"))
		return f;
	if (f == QLatin1String("chat-overlay"))
		return QStringLiteral("chatbox");
	if (f == QLatin1String("alert-overlay"))
		return QStringLiteral("alerts");
	if (f == QLatin1String("counter")) {
		const QString c = q.queryItemValue(QStringLiteral("c"));
		return c.isEmpty() ? QString() : QStringLiteral("counter:") + c;
	}
	return QString();
}

static bool goalforgexHost(const QString &host)
{
	const QString h = host.toLower();
	return h == QUrl(baseUrl()).host().toLower() || h == QLatin1String("goalforgex.com") ||
	       h.endsWith(QLatin1String(".goalforgex.com"));
}

struct ManualCtx {
	QString account;
	QList<ManualSource> out;
};

QList<ManualSource> findManual(const QString &account)
{
	ManualCtx ctx;
	ctx.account = account.toLower();
	obs_enum_sources(
		[](void *param, obs_source_t *src) {
			auto *c = static_cast<ManualCtx *>(param);
			const char *id = obs_source_get_unversioned_id(src);
			if (!id || strcmp(id, kBrowserId) != 0)
				return true;
			obs_data_t *priv = obs_source_get_private_settings(src);
			const bool tagged = priv && obs_data_get_string(priv, kWidgetKey)[0] != '\0';
			obs_data_release(priv);
			if (tagged)
				return true;
			const QUrl u(sourceUrl(src));
			if (!u.isValid() || !goalforgexHost(u.host()))
				return true;
			if (QUrlQuery(u).queryItemValue(QStringLiteral("id")).toLower() != c->account)
				return true;
			const QString wid = widgetIdForUrl(u);
			if (wid.isEmpty())
				return true;
			ManualSource m;
			m.name = QString::fromUtf8(obs_source_get_name(src));
			m.widgetId = wid;
			c->out.push_back(m);
			return true;
		},
		&ctx);
	return ctx.out;
}

bool adopt(const QString &sourceName, const WidgetInfo &w, const QString &overlayKey, const QString &account)
{
	obs_source_t *src = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!src)
		return false;
	obs_data_t *priv = obs_source_get_private_settings(src);
	obs_data_set_string(priv, kWidgetKey, w.id.toUtf8().constData());
	obs_data_set_string(priv, kAccountKey, account.toUtf8().constData());
	obs_data_release(priv);
	rememberSize(src, -1, -1); // sized by hand before the plugin — keep it
	obs_data_t *upd = obs_data_create();
	obs_data_set_string(upd, "url", fullUrl(w, overlayKey).toUtf8().constData());
	obs_source_update(src, upd);
	obs_data_release(upd);
	obs_source_release(src);
	return true;
}

QString sourceUrl(obs_source_t *source)
{
	if (!source)
		return QString();
	obs_data_t *s = obs_source_get_settings(source);
	const QString url = QString::fromUtf8(obs_data_get_string(s, "url"));
	obs_data_release(s);
	return url;
}

void sourceSize(obs_source_t *source, int *w, int *h)
{
	*w = 0;
	*h = 0;
	if (!source)
		return;
	obs_data_t *s = obs_source_get_settings(source);
	*w = static_cast<int>(obs_data_get_int(s, "width"));
	*h = static_cast<int>(obs_data_get_int(s, "height"));
	obs_data_release(s);
}

void setSourceSize(obs_source_t *source, int w, int h)
{
	if (!source)
		return;
	obs_data_t *upd = obs_data_create();
	obs_data_set_int(upd, "width", w);
	obs_data_set_int(upd, "height", h);
	obs_source_update(source, upd);
	obs_data_release(upd);
	// Set to the catalogue size by the plugin → follow dashboard sizing again.
	rememberSize(source, w, h);
}

QString keyInUrl(const QString &url)
{
	return QUrlQuery(QUrl(url)).queryItemValue(QStringLiteral("key"));
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
