/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QFrame>
#include <QJsonObject>
#include <QMultiHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QWidget;

// The 😀 emote picker above MultiChat's message box (3.0): the streamer's
// Twitch channel emotes, Twitch globals and BTTV / FFZ / 7TV, with search,
// one chip per group and ⭐ Recent. Click inserts the emote's name (Shift +
// click keeps the picker open). The list comes from GET /api/obs/chat/emotes.
class GfxEmotePicker : public QFrame {
public:
	struct Emote {
		QString name;
		QString url;
		QString note;   // "Tier 1", "Follower"…
		QString source; // twitch | bttv | ffz | 7tv
	};
	struct Group {
		QString id;
		QString title;
		QList<Emote> emotes;
	};

	explicit GfxEmotePicker(QWidget *parent);

	void setData(const QJsonObject &response);
	bool hasData() const { return !groups_.isEmpty(); }
	// Opens it just above `anchor` (the 😀 button), right-aligned.
	void open(QWidget *anchor);
	// Emote names for Tab completion, best first.
	QStringList complete(const QString &prefix, int max) const;
	// Remembers an emote for ⭐ Recent (also when typed via Tab).
	static void noteUsed(const QString &name, const QString &url);

	std::function<void(const QString &name)> onPick;
	std::function<void()> onRefresh; // ↻ pressed / empty list

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	void buildChips();
	void showGroup(const QString &id);
	void populate();
	void loadVisibleIcons();
	QList<Emote> recent() const;

	QList<Group> groups_;
	QString current_ = QStringLiteral("recent");
	QLineEdit *search_ = nullptr;
	QWidget *chips_ = nullptr;
	QList<QPushButton *> chipBtns_;
	QListWidget *grid_ = nullptr;
	QLabel *hover_ = nullptr;
	QLabel *empty_ = nullptr;
	QMultiHash<QString, QListWidgetItem *> waiting_; // url → items still showing the blank icon
};
