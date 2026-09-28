/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QJsonObject>
#include <QString>

#include <mutex>

namespace gfx {

// What this OBS install keeps between sessions. The refresh token and overlay
// key are the only secrets; on Windows the file is encrypted with DPAPI so it
// is unreadable to other Windows accounts and useless if copied elsewhere.
struct Credentials {
	QString refreshToken;
	QString overlayKey; // view-only key appended to every Browser Source URL
	QString linkId;
	QString username;
	QString displayName;

	bool valid() const { return !refreshToken.isEmpty() && !overlayKey.isEmpty() && !username.isEmpty(); }
};

enum class TokenStatus {
	Ok,             // access token ready
	NeedsReconnect, // link revoked / refresh rejected — user must click Connect again
	Transient,      // offline / server hiccup — keep credentials, retry later
};

class Auth {
public:
	static Auth &instance();

	void load();
	Credentials credentials();
	bool connected();

	// Stores the result of an approved device-code grant (worker thread OK).
	bool acceptLink(const QJsonObject &tokenResponse, QString *error);
	void setOverlayKey(const QString &key);
	void clear(); // forget everything (disconnect)

	// Worker thread only. Returns a valid access token, refreshing if needed.
	// Refreshes are serialized: the server rotates the refresh token on every
	// use, so two concurrent refreshes would make one of them look revoked.
	TokenStatus accessToken(QString *tokenOut, QString *errorOut);
	// Drop the cached access token (e.g. after a 401) so the next call refreshes.
	void invalidateAccess();

private:
	Auth() = default;
	void saveLocked();

	std::mutex m_;
	std::mutex refreshM_;
	Credentials c_;
	QString access_;
	qint64 accessExpMs_ = 0;
};

} // namespace gfx
