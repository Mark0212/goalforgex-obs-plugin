/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-update.hpp"
#include "gfx-http.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#endif

namespace gfx {

static const QRegularExpression kVersionRe(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));
static const QRegularExpression kShaRe(QStringLiteral("^[a-f0-9]{64}$"));

bool versionNewer(const QString &a, const QString &b)
{
	const QStringList x = a.split(QLatin1Char('.')), y = b.split(QLatin1Char('.'));
	for (int i = 0; i < 3; ++i) {
		const int xa = i < x.size() ? x[i].toInt() : 0, yb = i < y.size() ? y[i].toInt() : 0;
		if (xa != yb)
			return xa > yb;
	}
	return false;
}

UpdateInfo UpdateInfo::fromJson(const QJsonObject &o)
{
	UpdateInfo u;
	u.version = o.value(QStringLiteral("version")).toString();
	u.installerUrl = o.value(QStringLiteral("installerUrl")).toString();
	u.sha256 = o.value(QStringLiteral("sha256")).toString().toLower();
	u.minVersion = o.value(QStringLiteral("minVersion")).toString();
	if (!kVersionRe.match(u.version).hasMatch())
		u.version.clear();
	if (!kVersionRe.match(u.minVersion).hasMatch())
		u.minVersion.clear();
	return u;
}

// Only ever download installers from a GitHub release of this project.
static bool trustedInstallerUrl(const QString &s)
{
	const QUrl u(s);
	return u.isValid() && u.scheme() == QLatin1String("https") && u.host() == QLatin1String("github.com") &&
	       u.path().contains(QLatin1String("/releases/download/")) &&
	       u.path().endsWith(QLatin1String("-windows-x64-installer.exe"));
}

bool UpdateInfo::installable() const
{
	return !version.isEmpty() && versionNewer(version, QString::fromUtf8(PLUGIN_VERSION)) &&
	       trustedInstallerUrl(installerUrl) && kShaRe.match(sha256).hasMatch();
}

bool UpdateInfo::required() const
{
	return !minVersion.isEmpty() && versionNewer(minVersion, QString::fromUtf8(PLUGIN_VERSION));
}

QString installerPath(const QString &version)
{
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	return QDir(dir).filePath(QStringLiteral("GoalForgeX-OBS-Setup-%1.exe").arg(version));
}

bool fileMatches(const QString &path, const QString &sha256)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return false;
	QCryptographicHash h(QCryptographicHash::Sha256);
	if (!h.addData(&f))
		return false;
	return QString::fromLatin1(h.result().toHex()) == sha256.toLower();
}

QString downloadAndVerify(const UpdateInfo &info, const QString &path)
{
	if (!info.installable())
		return QStringLiteral(
			"This update can't be installed automatically — download it from goalforgex.com/obs.");
	if (QFile::exists(path) && fileMatches(path, info.sha256))
		return QString(); // already downloaded earlier
	QString err;
	if (!httpDownload(info.installerUrl, path, &err))
		return err;
	if (!fileMatches(path, info.sha256)) {
		QFile::remove(path);
		obs_log(LOG_WARNING, "Update %s failed its checksum check — discarded",
			info.version.toUtf8().constData());
		return QStringLiteral("The downloaded update didn't pass its safety check, so it was discarded.");
	}
	return QString();
}

bool launchInstaller(const QString &path, bool relaunch, QString *error)
{
#ifdef _WIN32
	const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
	const std::wstring params = relaunch ? L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /WAITOBS=1 /RELAUNCH=1"
					     : L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /WAITOBS=1";
	SHELLEXECUTEINFOW sei = {};
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOASYNC;
	sei.lpVerb = L"runas"; // the plugin folder needs admin rights → one UAC prompt
	sei.lpFile = file.c_str();
	sei.lpParameters = params.c_str();
	sei.nShow = SW_HIDE;
	if (!ShellExecuteExW(&sei)) {
		const DWORD e = GetLastError();
		if (error)
			*error = e == ERROR_CANCELLED ? QStringLiteral("The update was cancelled.")
						      : QStringLiteral("Couldn't start the installer (error %1).")
								.arg(static_cast<qulonglong>(e));
		return false;
	}
	obs_log(LOG_INFO, "Launched GoalForgeX update installer (relaunch=%d)", relaunch ? 1 : 0);
	return true;
#else
	(void)path;
	(void)relaunch;
	QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/obs")));
	if (error)
		*error = QStringLiteral(
			"Automatic updates are Windows-only for now — the download page has been opened.");
	return false;
#endif
}

} // namespace gfx
