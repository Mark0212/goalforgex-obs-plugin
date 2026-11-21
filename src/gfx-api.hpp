/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include "gfx-auth.hpp"
#include "gfx-http.hpp"

#include <QApplication>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <functional>
#include <thread>

namespace gfx {

// Result of a call that needs an access token.
struct ApiResult {
	TokenStatus token = TokenStatus::Ok;
	HttpResult http;
	QString error;

	bool ok() const { return token == TokenStatus::Ok && http.ok(); }
};

// Runs `call` with a fresh access token; on a 401 it refreshes once and
// retries. A revoked link comes back as TokenStatus::NeedsReconnect.
// Worker thread only.
ApiResult authedCall(const std::function<HttpResult(const QString &)> &call);
ApiResult apiGet(const QString &path);
ApiResult apiPost(const QString &path, const QJsonObject &body = QJsonObject());

// Plain-English description of a failed call.
QString describe(const HttpResult &h);
QString describe(const ApiResult &r);
// No answer, or a "try again" answer (the site restarting after an update, Cloudflare /
// nginx gateway errors): worth retrying quietly before telling the streamer anything.
bool isTransient(const HttpResult &h);

// ── Getting back onto the UI thread safely ──────────────────────────────────
// Everything that hops from another thread (workers, OBS signal / hotkey threads) to the
// UI thread is posted to uiTarget(): a QObject this plugin owns, created in
// obs_module_load and deleted in obs_module_unload. Deleting it makes Qt throw away any
// call still queued — so nothing of ours can run after OBS unloads this DLL. (Posting to
// qApp, as before, could leave a queued call behind that ran into unloaded code.)
// Returns nullptr before load / after unload: the caller then just drops the call.
QObject *uiTarget();
void uiTargetCreate();  // obs_module_load (UI thread)
void uiTargetDestroy(); // obs_module_unload, after asyncWait()
// Worker threads started by runAsync that haven't finished yet, and a bounded wait for them.
std::atomic<int> &asyncLive();
void asyncWait(int maxMs);
// Post fn to the UI thread (no-op once the plugin is unloading).
template<class Fn> void postToUi(Fn fn)
{
	if (QObject *t = uiTarget())
		QMetaObject::invokeMethod(t, std::move(fn), Qt::QueuedConnection);
}

// Runs work() on a worker thread and done(result) back on the Qt UI thread.
// The result is dropped if `ctx` has been destroyed or OBS is shutting down.
// The thread is counted (asyncLive) until its very last statement, so obs_module_unload
// can wait for every one of them before this DLL's code goes away.
template<class Work, class Done> void runAsync(QObject *ctx, Work work, Done done)
{
	QPointer<QObject> guard(ctx);
	++asyncLive();
	std::thread([guard, work, done]() mutable {
		struct Leave {
			~Leave() { --asyncLive(); }
		} leave;
		auto result = work();
		if (!httpShuttingDown())
			postToUi([guard, done, result]() mutable {
				if (guard && !httpShuttingDown())
					done(result);
			});
	}).detach();
}

} // namespace gfx
