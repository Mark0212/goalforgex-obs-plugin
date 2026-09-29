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

// Runs work() on a worker thread and done(result) back on the Qt UI thread.
// The result is dropped if `ctx` has been destroyed or OBS is shutting down.
template<class Work, class Done> void runAsync(QObject *ctx, Work work, Done done)
{
	QPointer<QObject> guard(ctx);
	std::thread([guard, work, done]() mutable {
		auto result = work();
		QMetaObject::invokeMethod(
			qApp,
			[guard, done, result]() mutable {
				if (guard && !httpShuttingDown())
					done(result);
			},
			Qt::QueuedConnection);
	}).detach();
}

} // namespace gfx
