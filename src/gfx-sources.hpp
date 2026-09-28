/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <obs.h>

#include <QList>
#include <QString>

namespace gfx {

// One entry of GET /api/obs/widgets.
struct WidgetInfo {
	QString id;
	QString name;
	QString icon;
	QString url; // without key
	QString configureUrl;
	QString lockReason;
	int width = 800;
	int height = 600;
	bool fullCanvas = false;
	bool audio = false;
	bool locked = false;
};

enum class Place { TopLeft, TopCenter, TopRight, Center, BottomLeft, BottomCenter, BottomRight };

// Every function here touches libobs scene state — call from the Qt UI thread.
namespace sources {

// Full Browser Source URL: catalogue URL + this install's overlay key.
QString fullUrl(const WidgetInfo &w, const QString &overlayKey);

// The Browser Source this plugin created for `widgetId` on `account`, or
// nullptr. Returns a NEW reference (caller must obs_source_release).
obs_source_t *find(const QString &account, const QString &widgetId);

// The scene item showing `source` inside `scene` (groups included), or
// nullptr. No new reference — valid while the scene is.
obs_sceneitem_t *findItem(obs_source_t *scene, obs_source_t *source);

enum class AddResult { Added, Reused, AlreadyInScene, Failed };
// Adds the widget to `scene`. If this widget already has a GoalForgeX source,
// that same source is placed in the scene (so it stays one widget everywhere,
// never a duplicate). If it's already in this scene, nothing is added.
AddResult addToScene(obs_source_t *scene, const WidgetInfo &w, const QString &overlayKey, const QString &account,
		     QString *error);

// Point every GoalForgeX source for `account` at the current overlay key
// (after re-connecting or a key rotation). Leaves size/position/CSS alone.
// Returns how many sources were updated.
int syncUrls(const QList<WidgetInfo> &widgets, const QString &overlayKey, const QString &account);

void place(obs_sceneitem_t *item, Place where);
void setScalePercent(obs_sceneitem_t *item, double percent);
double scalePercent(obs_sceneitem_t *item);
void fitToCanvas(obs_sceneitem_t *item);
bool reload(obs_source_t *source);

} // namespace sources
} // namespace gfx
