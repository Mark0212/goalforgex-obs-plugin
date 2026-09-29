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

namespace gfx {

// GET /api/obs/latest
struct UpdateInfo {
	QString version;      // newest published plugin version
	QString installerUrl; // GitHub release asset
	QString sha256;       // expected SHA-256 of the installer (lowercase hex)
	QString minVersion;   // oldest version still allowed to use the dock

	static UpdateInfo fromJson(const QJsonObject &o);
	// Newer than this build, with a GitHub installer link and a checksum —
	// i.e. something we're willing to download and run automatically.
	bool installable() const;
	// This build is below the minimum → the dock must be updated to be used.
	bool required() const;
};

bool versionNewer(const QString &a, const QString &b); // a > b ?

// Where the verified installer for `version` is saved (the temp folder).
QString installerPath(const QString &version);

// Worker thread: downloads the installer and checks its SHA-256. Anything
// that doesn't match exactly is deleted. Returns "" on success, else a
// plain-English error.
QString downloadAndVerify(const UpdateInfo &info, const QString &path);

// True if a file already on disk matches the expected SHA-256.
bool fileMatches(const QString &path, const QString &sha256);

// Starts the installer through Windows' normal "allow changes" (UAC) prompt.
// It waits for OBS to close before installing; with `relaunch` it reopens OBS
// afterwards. UI thread. Windows only — elsewhere it opens the download page.
bool launchInstaller(const QString &path, bool relaunch, QString *error);

} // namespace gfx
