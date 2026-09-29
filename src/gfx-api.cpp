/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-api.hpp"

namespace gfx {

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
	if (h.status >= 500)
		return QStringLiteral("GoalForgeX is having a problem right now — try again shortly.");
	const QString m = h.serverMessage();
	return m.isEmpty() ? QStringLiteral("GoalForgeX returned an error (HTTP %1).").arg(h.status) : m;
}

QString describe(const ApiResult &r)
{
	if (r.token != TokenStatus::Ok)
		return r.error.isEmpty() ? QStringLiteral("Not connected to GoalForgeX.") : r.error;
	return describe(r.http);
}

} // namespace gfx
