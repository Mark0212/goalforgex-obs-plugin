/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include "gfx-state.hpp"

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QFrame;
class QLabel;
class QPoint;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// "Events" tab: everything that happened on stream, newest first, filterable —
// gift bombs (expand to see every recipient), channel-point redemptions, watch
// streaks, sub/bits/raid alerts, TikTok gifts/follows and YouTube Super Chats /
// memberships. Fed by the same /api/obs/state poll as the Live tab.
class GfxEventsPanel : public QWidget {
public:
	explicit GfxEventsPanel(QWidget *parent = nullptr);

	// Events arrive oldest → newest. Returns how many are shown by the current
	// filter (for the unread count on the tab).
	int addEvents(const QList<gfx::FeedEvent> &events);
	void clearEvents();
	// eventsStatus.channelPoints from the state poll (see LiveState).
	void setChannelPointsStatus(const QString &status);
	// Settings → Events changed (which kinds are listed).
	void applySettings();

private:
	void buildUi();
	void setFilter(const QString &f);
	void rebuild();
	bool shown(const gfx::FeedEvent &e) const;
	QString group(const gfx::FeedEvent &e) const;
	QTreeWidgetItem *makeItem(const gfx::FeedEvent &e) const;
	void renderTally();
	void showMenu(const QPoint &pos);

	QList<gfx::FeedEvent> events_; // oldest → newest, capped
	QString filter_ = QStringLiteral("all");
	QString cpStatus_;
	bool seenGiftBomb_ = false; // server sends gift-bomb rows → hide the duplicate "gift subs" alert line

	QList<QPair<QString, QPushButton *>> chips_;
	QFrame *notice_ = nullptr;
	QLabel *noticeLbl_ = nullptr;
	QLabel *tally_ = nullptr;
	QTreeWidget *tree_ = nullptr;
	QLabel *empty_ = nullptr;
};
