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
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;

namespace gfx {

// One Twitch / Kick chat line from GET /api/obs/chat.
struct ChatMessage {
	QString id;
	QString platform; // twitch | kick
	QString user;     // display name
	QString login;
	QString color;
	QString text;
	QString replyTo;
	bool host = false;
	bool mod = false;
	bool vip = false;
	bool sub = false;
	bool first = false;
	bool deleted = false;
	int bits = 0;
	qint64 ts = 0;
};

} // namespace gfx

// The "Chat" tab: Twitch + Kick chat in one feed (or filtered to one), and a
// composer that sends as the streamer to Twitch, Kick or both. The dock owns
// the polling and the network calls; this panel only renders and collects input.
class GfxChatPanel : public QWidget {
public:
	using SendFn = std::function<void(const QStringList &platforms, const QString &text)>;
	// action: timeout | ban | unban | delete. displayName is for the confirmation line.
	using ModFn = std::function<void(const QString &platform, const QString &action, const QString &login,
					 int seconds, const QString &messageId, const QString &displayName)>;

	explicit GfxChatPanel(QWidget *parent = nullptr);

	void setSendHandler(SendFn fn);
	void setModHandler(ModFn fn);
	// Applies one poll response. Returns how many new messages arrived that the
	// current filter shows (for the unread badge on the tab).
	int applyPoll(const QJsonObject &response);
	// Non-empty = show the Pro lock instead of the chat.
	void setLocked(const QString &message);
	// The dock calls this when a send finishes.
	void sendFinished(bool anyDelivered);
	void clearChat();

private:
	void buildUi();
	void render(int newlyAdded = 0, bool forceBottom = false);
	void scrollToBottom();
	bool matches(const gfx::ChatMessage &m) const;
	QString lineHtml(const gfx::ChatMessage &m) const;
	void renderStatus();
	void updateTargets();
	void updateSendState();
	void setFilter(const QString &f);
	void doSend();
	bool platformConnected(const QString &p) const;
	// Click on a chatter's name: profile / copy / delete / timeout / ban / unban.
	void showUserMenu(const QString &platform, const QString &login, const QString &messageId);

	QList<gfx::ChatMessage> msgs_;
	QJsonObject status_;
	QString filter_ = QStringLiteral("all");
	SendFn send_;
	ModFn mod_;
	bool sending_ = false;
	int pendingNew_ = 0;

	QWidget *main_ = nullptr;
	QWidget *lockPanel_ = nullptr;
	QLabel *lockLbl_ = nullptr;
	QLabel *statusLbl_ = nullptr;
	QPushButton *fAll_ = nullptr;
	QPushButton *fTwitch_ = nullptr;
	QPushButton *fKick_ = nullptr;
	QTextBrowser *view_ = nullptr;
	QPushButton *jump_ = nullptr;
	QPushButton *toTwitch_ = nullptr;
	QPushButton *toKick_ = nullptr;
	QLineEdit *input_ = nullptr;
	QPushButton *sendBtn_ = nullptr;
	QLabel *countLbl_ = nullptr;
};
