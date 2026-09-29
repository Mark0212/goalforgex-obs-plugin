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
#include <QJsonObject>
#include <QString>

namespace gfx {

// Result of one HTTPS call. status == 0 means the request never got an HTTP
// answer (DNS, TLS, timeout, offline, aborted) — see transportError.
struct HttpResult {
	long status = 0;
	QByteArray body;
	QString transportError;

	bool ok() const { return status >= 200 && status < 300; }
	QJsonObject json() const;
	// Server error code ("authorization_pending", "invalid_grant", ...) if any.
	QString errorCode() const;
	// Human-readable message from the server body, if any.
	QString serverMessage() const;
};

// Blocking calls — ONLY call these from a worker thread, never the Qt UI thread.
HttpResult httpGet(const QString &url, const QString &bearer = QString());
HttpResult httpPostJson(const QString &url, const QJsonObject &body, const QString &bearer = QString());

// Downloads a public file (no credentials sent) to `path`, following
// redirects over HTTPS only (GitHub release assets redirect to their CDN).
// Returns false and fills `error` on any failure. Worker thread only.
bool httpDownload(const QString &url, const QString &path, QString *error);

// Aborts in-flight transfers and makes new ones fail fast (OBS is exiting).
void httpBeginShutdown();
bool httpShuttingDown();
// Number of worker-thread requests still running (unload waits for 0).
int httpInFlight();

// https://goalforgex.com, or GOALFORGEX_BASE_URL for testing against a dev server.
QString baseUrl();

} // namespace gfx
