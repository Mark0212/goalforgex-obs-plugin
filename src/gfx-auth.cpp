/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-auth.hpp"
#include "gfx-http.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace gfx {

static const char *kCredFile = "credentials.dat";

static QString credPath()
{
	char *p = obs_module_config_path(kCredFile);
	const QString s = QString::fromUtf8(p ? p : "");
	bfree(p);
	return s;
}

#ifdef _WIN32
// DPAPI optional entropy — Windows-only, so declared here (an unused const
// elsewhere is a -Werror failure on the macOS/Linux builds).
static const char kEntropy[] = "GoalForgeX-OBS-credentials-v1";

static QByteArray protect(const QByteArray &plain)
{
	DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
	DATA_BLOB ent{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
	DATA_BLOB out{0, nullptr};
	if (!CryptProtectData(&in, L"GoalForgeX", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return QByteArray();
	QByteArray res(reinterpret_cast<const char *>(out.pbData), static_cast<qsizetype>(out.cbData));
	LocalFree(out.pbData);
	return res;
}

static QByteArray unprotect(const QByteArray &cipher)
{
	DATA_BLOB in{static_cast<DWORD>(cipher.size()), reinterpret_cast<BYTE *>(const_cast<char *>(cipher.constData()))};
	DATA_BLOB ent{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
	DATA_BLOB out{0, nullptr};
	if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return QByteArray();
	QByteArray res(reinterpret_cast<const char *>(out.pbData), static_cast<qsizetype>(out.cbData));
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return res;
}
#else
// macOS/Linux builds: owner-only file permissions (Keychain/libsecret later).
static QByteArray protect(const QByteArray &plain)
{
	return plain;
}
static QByteArray unprotect(const QByteArray &cipher)
{
	return cipher;
}
#endif

Auth &Auth::instance()
{
	static Auth a;
	return a;
}

void Auth::load()
{
	std::lock_guard<std::mutex> lk(m_);
	c_ = Credentials();
	QFile f(credPath());
	if (!f.exists() || !f.open(QIODevice::ReadOnly))
		return;
	const QByteArray plain = unprotect(f.readAll());
	f.close();
	const QJsonObject o = QJsonDocument::fromJson(plain).object();
	c_.refreshToken = o.value(QStringLiteral("refresh_token")).toString();
	c_.overlayKey = o.value(QStringLiteral("overlay_key")).toString();
	c_.linkId = o.value(QStringLiteral("link_id")).toString();
	c_.username = o.value(QStringLiteral("username")).toString();
	c_.displayName = o.value(QStringLiteral("display_name")).toString();
	if (!c_.valid()) {
		obs_log(LOG_WARNING, "Stored GoalForgeX credentials unreadable — you'll need to connect again");
		c_ = Credentials();
	}
}

void Auth::saveLocked()
{
	const QString path = credPath();
	if (path.isEmpty())
		return;
	QDir().mkpath(QFileInfo(path).absolutePath());
	if (!c_.valid()) {
		QFile::remove(path);
		return;
	}
	QJsonObject o;
	o.insert(QStringLiteral("refresh_token"), c_.refreshToken);
	o.insert(QStringLiteral("overlay_key"), c_.overlayKey);
	o.insert(QStringLiteral("link_id"), c_.linkId);
	o.insert(QStringLiteral("username"), c_.username);
	o.insert(QStringLiteral("display_name"), c_.displayName);
	const QByteArray data = protect(QJsonDocument(o).toJson(QJsonDocument::Compact));
	if (data.isEmpty()) {
		obs_log(LOG_ERROR, "Could not encrypt GoalForgeX credentials; not saving them");
		return;
	}
	// Atomic replace: a crash mid-write must never leave a half-written file,
	// because the refresh token inside it has already been rotated server-side.
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly)) {
		obs_log(LOG_ERROR, "Could not write GoalForgeX credentials file");
		return;
	}
	f.write(data);
	if (!f.commit()) {
		obs_log(LOG_ERROR, "Could not save GoalForgeX credentials file");
		return;
	}
	QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

Credentials Auth::credentials()
{
	std::lock_guard<std::mutex> lk(m_);
	return c_;
}

bool Auth::connected()
{
	std::lock_guard<std::mutex> lk(m_);
	return c_.valid();
}

bool Auth::acceptLink(const QJsonObject &r, QString *error)
{
	const QJsonObject acct = r.value(QStringLiteral("account")).toObject();
	Credentials c;
	c.refreshToken = r.value(QStringLiteral("refresh_token")).toString();
	c.overlayKey = r.value(QStringLiteral("overlay_key")).toString();
	c.linkId = r.value(QStringLiteral("link_id")).toString();
	c.username = acct.value(QStringLiteral("username")).toString();
	c.displayName = acct.value(QStringLiteral("displayName")).toString();
	if (!c.valid()) {
		if (error)
			*error = QStringLiteral("GoalForgeX sent an incomplete response — please try connecting again.");
		return false;
	}
	std::lock_guard<std::mutex> lk(m_);
	c_ = c;
	access_ = r.value(QStringLiteral("access_token")).toString();
	accessExpMs_ = QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(r.value(QStringLiteral("expires_in")).toInt(3600)) * 1000;
	saveLocked();
	return true;
}

void Auth::setOverlayKey(const QString &key)
{
	std::lock_guard<std::mutex> lk(m_);
	c_.overlayKey = key;
	saveLocked();
}

void Auth::clear()
{
	std::lock_guard<std::mutex> lk(m_);
	c_ = Credentials();
	access_.clear();
	accessExpMs_ = 0;
	saveLocked(); // removes the file
}

void Auth::invalidateAccess()
{
	std::lock_guard<std::mutex> lk(m_);
	access_.clear();
	accessExpMs_ = 0;
}

TokenStatus Auth::accessToken(QString *tokenOut, QString *errorOut)
{
	const auto fresh = [this](QString *out) {
		std::lock_guard<std::mutex> lk(m_);
		if (!access_.isEmpty() && QDateTime::currentMSecsSinceEpoch() < accessExpMs_ - 60 * 1000) {
			*out = access_;
			return true;
		}
		return false;
	};
	if (fresh(tokenOut))
		return TokenStatus::Ok;

	std::lock_guard<std::mutex> refreshLock(refreshM_);
	if (fresh(tokenOut)) // another thread refreshed while we waited
		return TokenStatus::Ok;

	QString refresh;
	{
		std::lock_guard<std::mutex> lk(m_);
		refresh = c_.refreshToken;
	}
	if (refresh.isEmpty()) {
		if (errorOut)
			*errorOut = QStringLiteral("Not connected to GoalForgeX.");
		return TokenStatus::NeedsReconnect;
	}

	QJsonObject body;
	body.insert(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
	body.insert(QStringLiteral("refresh_token"), refresh);
	const HttpResult r = httpPostJson(baseUrl() + QStringLiteral("/api/obs/token"), body);

	if (r.ok()) {
		const QJsonObject o = r.json();
		const QString at = o.value(QStringLiteral("access_token")).toString();
		const QString rt = o.value(QStringLiteral("refresh_token")).toString();
		if (at.isEmpty() || rt.isEmpty()) {
			if (errorOut)
				*errorOut = QStringLiteral("GoalForgeX sent an unexpected response.");
			return TokenStatus::Transient;
		}
		std::lock_guard<std::mutex> lk(m_);
		access_ = at;
		accessExpMs_ = QDateTime::currentMSecsSinceEpoch() + static_cast<qint64>(o.value(QStringLiteral("expires_in")).toInt(3600)) * 1000;
		c_.refreshToken = rt;
		const QJsonObject acct = o.value(QStringLiteral("account")).toObject();
		if (!acct.isEmpty())
			c_.displayName = acct.value(QStringLiteral("displayName")).toString(c_.displayName);
		saveLocked();
		*tokenOut = access_;
		return TokenStatus::Ok;
	}
	if (r.status == 400 && r.errorCode() == QLatin1String("invalid_grant")) {
		if (errorOut)
			*errorOut = r.serverMessage().isEmpty()
					    ? QStringLiteral("This OBS was disconnected from your GoalForgeX account. Click Connect to link it again.")
					    : r.serverMessage();
		return TokenStatus::NeedsReconnect;
	}
	if (errorOut)
		*errorOut = r.status == 0 ? QStringLiteral("Can't reach GoalForgeX (%1).").arg(r.transportError)
					  : QStringLiteral("GoalForgeX returned an error (HTTP %1).").arg(r.status);
	return TokenStatus::Transient;
}

} // namespace gfx
