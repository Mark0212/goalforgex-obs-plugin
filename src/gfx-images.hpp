/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

namespace gfx {

// Emote and avatar images for the MultiChat tab, downloaded in the background
// (no credentials sent) from the emote / avatar CDNs only, decoded once and
// kept in memory. PNG / JPEG / GIF (first frame) — whatever Qt can decode.
// Twitch chat GIFs (GIPHY) also keep their whole file for a while, so the chat
// can play them.
class RemoteImages {
public:
	static RemoteImages &instance();

	// Only https URLs on the known emote / avatar CDNs are ever fetched.
	static bool allowed(const QString &url);
	// An allowed URL on GIPHY — a Twitch chat GIF.
	static bool isGif(const QString &url);

	// The image if it has loaded; otherwise starts loading it (once) and
	// returns a null image. A URL that failed stays null.
	QImage get(const QString &url);
	bool failed(const QString &url) const { return failed_.contains(url); }
	// The whole GIF file, for playing it: only for the newest few GIFs that have
	// loaded — empty otherwise (the still image from get() is what's left).
	QByteArray animation(const QString &url) const;

	// fn(url) runs on the UI thread each time an image finishes loading (or
	// fails — check failed()), for as long as ctx lives.
	void subscribe(QObject *ctx, std::function<void(const QString &url)> fn);

private:
	RemoteImages() = default;
	void pump();
	void finished(const QString &url, const QImage &img, const QByteArray &raw);

	QObject *holder_ = nullptr; // context for the worker callbacks
	QHash<QString, QImage> cache_;
	QStringList order_; // oldest first, for eviction
	QHash<QString, QByteArray> gifBytes_;
	QStringList gifOrder_; // oldest first
	QSet<QString> failed_;
	QSet<QString> pending_;
	QStringList queue_;
	int running_ = 0;
	QList<QPair<QPointer<QObject>, std::function<void(const QString &)>>> subs_;
};

} // namespace gfx
