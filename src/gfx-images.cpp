/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-images.hpp"
#include "gfx-api.hpp"
#include "gfx-http.hpp"

#include <QUrl>

namespace gfx {

namespace {

constexpr int kMaxCached = 800;
constexpr int kParallel = 6;
constexpr int kMaxSide = 160; // emotes are ≤ 112 px; avatars are drawn small
constexpr qsizetype kMaxBytes = 2 * 1024 * 1024;

} // namespace

RemoteImages &RemoteImages::instance()
{
	static RemoteImages r;
	return r;
}

bool RemoteImages::allowed(const QString &url)
{
	if (url.size() > 600)
		return false;
	const QUrl u(url, QUrl::StrictMode);
	if (!u.isValid() || u.scheme() != QLatin1String("https") || !u.userInfo().isEmpty() || u.port() != -1)
		return false;
	const QString h = u.host().toLower();
	static const char *const exact[] = {
		"static-cdn.jtvnw.net",     // Twitch emotes + avatars
		"files.kick.com",           // Kick emotes + avatars
		"cdn.betterttv.net",        // BTTV
		"cdn.7tv.app",              // 7TV
		"cdn.frankerfacez.com",     // FFZ
		"yt3.ggpht.com",            // YouTube avatars
		"yt3.googleusercontent.com" // YouTube avatars
	};
	for (const char *e : exact)
		if (h == QLatin1String(e))
			return true;
	// TikTok avatars: p16-sign-va.tiktokcdn.com, p77-sign.tiktokcdn-us.com …
	return h.endsWith(QLatin1String(".tiktokcdn.com")) || h.endsWith(QLatin1String(".tiktokcdn-us.com"));
}

QImage RemoteImages::get(const QString &url)
{
	auto it = cache_.constFind(url);
	if (it != cache_.constEnd())
		return it.value();
	if (failed_.contains(url) || pending_.contains(url))
		return QImage();
	if (!allowed(url)) {
		failed_.insert(url);
		return QImage();
	}
	pending_.insert(url);
	queue_.append(url);
	pump();
	return QImage();
}

void RemoteImages::subscribe(QObject *ctx, std::function<void(const QString &url)> fn)
{
	subs_.append({QPointer<QObject>(ctx), std::move(fn)});
}

void RemoteImages::pump()
{
	if (!holder_)
		holder_ = new QObject(); // lives as long as OBS; never parented
	while (running_ < kParallel && !queue_.isEmpty()) {
		// Newest first: what's on screen now matters more than a backlog.
		const QString url = queue_.takeLast();
		++running_;
		runAsync(
			holder_,
			[url] {
				QImage img;
				const HttpResult r = httpGet(url);
				if (r.ok() && !r.body.isEmpty() && r.body.size() <= kMaxBytes) {
					img = QImage::fromData(r.body);
					if (!img.isNull() && (img.width() > kMaxSide || img.height() > kMaxSide))
						img = img.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio,
								 Qt::SmoothTransformation);
					if (!img.isNull() && img.format() != QImage::Format_ARGB32_Premultiplied)
						img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
				}
				return img;
			},
			[this, url](const QImage &img) { finished(url, img); });
	}
}

void RemoteImages::finished(const QString &url, const QImage &img)
{
	--running_;
	pending_.remove(url);
	if (img.isNull()) {
		failed_.insert(url);
	} else {
		cache_.insert(url, img);
		order_.append(url);
		while (order_.size() > kMaxCached)
			cache_.remove(order_.takeFirst());
	}
	// Failures notify too: a line showing a placeholder then falls back to the name.
	for (qsizetype i = subs_.size() - 1; i >= 0; --i) {
		if (!subs_.at(i).first) {
			subs_.removeAt(i);
			continue;
		}
		subs_.at(i).second(url);
	}
	pump();
}

} // namespace gfx
