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
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextBlockFormat>
#include <QWidget>

class QFrame;
class QLabel;
class QPoint;
class QPushButton;
class QTextBrowser;

// "Events": everything that happened on stream as a feed of cards, newest at
// the bottom like a chat — gift bombs (with who got a sub), channel-point
// redemptions, watch streaks, subs / bits / raids / follows, TikTok gifts and
// YouTube Super Chats / memberships. The cards are drawn exactly like
// MultiChat's event notices, so a raid looks the same in both places.
// Fed by the /api/obs/state poll (and nudged by the MultiChat long-poll).
class GfxEventsPanel : public QWidget {
public:
	explicit GfxEventsPanel(QWidget *parent = nullptr);

	// Events arrive oldest → newest; a gift bomb filling in (same bombId)
	// replaces its card. Returns how many NEW events the filter shows (unread count).
	int addEvents(const QList<gfx::FeedEvent> &events);
	void clearEvents();
	// eventsStatus.channelPoints from the state poll (see LiveState).
	void setChannelPointsStatus(const QString &status);
	// eventsStatus.shoutouts: needs_reconnect → a Reconnect bar (Twitch /shoutout needs a newer login).
	void setShoutoutStatus(const QString &status);
	// Settings changed (which kinds are listed, look, colours).
	void applySettings();

private:
	// What one card says: pill, platform, headline (HTML) and the extra lines.
	struct Card {
		QString type;     // notice type for the pill (giftbomb, points, raid…)
		QString pillIcon; // set for kinds MultiChat has no pill for (timer, wheel, Xive)
		QString pillLabel;
		QString pillColor;
		QString platform;  // twitch | kick | youtube | tiktok | "" (no icon)
		QString head;      // HTML
		QString message;   // plain text — what the viewer wrote
		QStringList names; // gift bomb recipients
		int missing = 0;   // gift bomb: recipients not (yet) reported
		bool bomb = false;
	};

	void buildUi();
	void setFilter(const QString &f);
	void rebuild();
	bool shown(const gfx::FeedEvent &e) const;
	QString group(const gfx::FeedEvent &e) const;
	Card describe(const gfx::FeedEvent &e) const;
	QString cardHtml(const gfx::FeedEvent &e) const;
	QTextBlockFormat cardFormat(const gfx::FeedEvent &e) const;
	void appendCard(const gfx::FeedEvent &e);
	void showPlaceholder();
	void renderTally();
	void renderNotice();
	void showMenu(const QPoint &pos);
	const gfx::FeedEvent *eventBySeq(qint64 seq) const;
	bool autoScrolling() const;
	void scrollToBottom();
	void updateJump();

	QList<gfx::FeedEvent> events_; // oldest → newest, capped
	QList<qint64> shownSeq_;       // block n of the feed shows the event with this seq
	QSet<qint64> toggled_;         // gift bombs whose recipient list was opened / closed by hand
	QString filter_ = QStringLiteral("all");
	QString cpStatus_;
	QString soStatus_;
	qint64 nextSeq_ = 1;        // panel-local ids — a server that restarts its feed can reuse seq numbers
	bool seenGiftBomb_ = false; // server sends gift-bomb cards → hide the duplicate "gift subs" alert card
	bool placeholder_ = true;
	bool follow_ = true;
	int programmatic_ = 0;
	int pendingNew_ = 0;

	QList<QPair<QString, QPushButton *>> chips_;
	QFrame *notice_ = nullptr;
	QLabel *noticeLbl_ = nullptr;
	QLabel *tileGifted_ = nullptr;
	QLabel *tileSupport_ = nullptr;
	QLabel *tilePoints_ = nullptr;
	QLabel *tileStreaks_ = nullptr;
	QTextBrowser *view_ = nullptr;
	QPushButton *jump_ = nullptr;
};
