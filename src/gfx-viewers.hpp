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
#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// "Who's here": the list beside MultiChat. Twitch reports everyone in chat,
// lurkers too; the other platforms have no viewer list, so people show there
// once they chat. The main dock polls GET /api/obs/chat/viewers and hands the
// answer to applyPoll; this panel only filters, sorts and draws it.
class GfxViewerPanel : public QWidget {
public:
	using PersonFn = std::function<void(const QString &platform, const QString &login)>;

	explicit GfxViewerPanel(QWidget *parent = nullptr);

	void setOpenHandler(PersonFn fn);
	void setModHandler(PersonFn fn);
	std::function<void()> onLayoutChanged; // switched off, or moved to the other side

	void applyPoll(const QJsonObject &response);
	// A message in place of the status line (Pro lock, older site…). Empty clears it.
	void setNote(const QString &note);
	// Settings changed: re-read them and redraw.
	void refresh();

private:
	struct Person {
		QString platform;
		QString login;
		QString name;
		QString color;
		QString role; // host | mod | vip | empty
		bool sub = false;
		bool bot = false;
		qint64 chatAt = 0;
		qint64 joinedAt = 0;
	};

	bool chatting(const Person &p) const;
	bool listed(const Person &p) const;
	bool matchesFilter(const Person &p) const;
	void setFilter(const QString &filter);
	void rebuild();
	void showOptions();
	void showPersonMenu(const QPoint &pos);

	QList<Person> people_;
	QSet<QString> collapsed_;
	QString filter_ = QStringLiteral("all"); // all | team | chatting | lurking
	QString twitchStatus_;
	QString note_;
	qint64 now_ = 0; // the server's clock at the last poll
	int total_ = 0;
	PersonFn open_;
	PersonFn mod_;

	QLabel *count_ = nullptr;
	QLabel *status_ = nullptr;
	QLineEdit *search_ = nullptr;
	QPushButton *fAll_ = nullptr;
	QPushButton *fTeam_ = nullptr;
	QPushButton *fChatting_ = nullptr;
	QPushButton *fLurking_ = nullptr;
	QListWidget *list_ = nullptr;
};
