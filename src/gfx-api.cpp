/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-api.hpp"

#include <chrono>

namespace gfx {

// See uiTarget() in gfx-api.hpp. Atomic: worker / OBS threads read it while the UI
// thread creates and deletes it.
static std::atomic<QObject *> g_uiTarget{nullptr};
static std::atomic<int> g_asyncLive{0};

QObject *uiTarget()
{
	return g_uiTarget.load();
}
void uiTargetCreate()
{
	if (!g_uiTarget.load())
		g_uiTarget.store(new QObject());
}
void uiTargetDestroy()
{
	// Cleared first, so nothing new is posted; deleting it drops whatever is still queued.
	if (QObject *t = g_uiTarget.exchange(nullptr))
		delete t;
}
std::atomic<int> &asyncLive()
{
	return g_asyncLive;
}
void asyncWait(int maxMs)
{
	for (int waited = 0; g_asyncLive.load() > 0 && waited < maxMs; waited += 50)
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

ApiResult authedCall(const std::function<HttpResult(const QString &)> &call)
{
	ApiResult r;
	QString tok, err;
	r.token = Auth::instance().accessToken(&tok, &err);
	if (r.token != TokenStatus::Ok) {
		r.error = err;
		return r;
	}
	r.http = call(tok);
	if (r.http.status == 401) {
		if (r.http.errorCode() == QLatin1String("link_revoked") ||
		    r.http.errorCode() == QLatin1String("account_missing")) {
			r.token = TokenStatus::NeedsReconnect;
			r.error = r.http.serverMessage();
			return r;
		}
		Auth::instance().invalidateAccess();
		r.token = Auth::instance().accessToken(&tok, &err);
		if (r.token != TokenStatus::Ok) {
			r.error = err;
			return r;
		}
		r.http = call(tok);
		if (r.http.status == 401) {
			r.token = TokenStatus::NeedsReconnect;
			r.error = r.http.serverMessage();
		}
	}
	return r;
}

ApiResult apiGet(const QString &path)
{
	return authedCall([path](const QString &tok) { return httpGet(baseUrl() + path, tok); });
}

ApiResult apiPost(const QString &path, const QJsonObject &body)
{
	return authedCall([path, body](const QString &tok) { return httpPostJson(baseUrl() + path, body, tok); });
}

QString describe(const HttpResult &h)
{
	if (h.status == 0)
		return QStringLiteral("Can't reach GoalForgeX — check your internet connection.");
	if (h.status == 429)
		return QStringLiteral("Too many requests to GoalForgeX — try again in a moment.");
	if (isTransient(h))
		return QStringLiteral("GoalForgeX is restarting or busy — this usually clears in a few seconds.");
	if (h.status >= 500)
		return QStringLiteral("GoalForgeX is having a problem right now — try again shortly.");
	const QString m = h.serverMessage();
	return m.isEmpty() ? QStringLiteral("GoalForgeX returned an error (HTTP %1).").arg(h.status) : m;
}

bool isTransient(const HttpResult &h)
{
	// 502/503/504: the site is restarting (a few seconds after every update) or busy.
	// 520-530: Cloudflare couldn't get an answer from it in time.
	return h.status == 0 || h.status == 502 || h.status == 503 || h.status == 504 ||
	       (h.status >= 520 && h.status <= 530);
}

QString describe(const ApiResult &r)
{
	if (r.token != TokenStatus::Ok)
		return r.error.isEmpty() ? QStringLiteral("Not connected to GoalForgeX.") : r.error;
	return describe(r.http);
}

} // namespace gfx
