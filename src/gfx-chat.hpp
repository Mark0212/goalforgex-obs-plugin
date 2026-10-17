/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextBlockFormat>
#include <QWidget>

#include <functional>

class QBoxLayout;
class QEvent;
class QFrame;
class QGridLayout;
class QResizeEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QTextBrowser;
class QTimer;

namespace gfx {

// A piece of a message: plain text, or an emote (name + image URL).
struct ChatPart {
	QString text;  // the text, or the emote's name
	QString image; // empty = plain text
};

// One chat line from GET /api/obs/chat (Twitch, Kick, YouTube or TikTok).
struct ChatMessage {
	QString id;
	QString platform; // twitch | kick | youtube | tiktok
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
	qint64 seq = 0;        // panel-local, increasing — ties a feed line to its message
	QList<ChatPart> parts; // set when the message has emotes
	QString channel;       // watched channel key ("twitch:xqc"); empty = the streamer's own chat
	QString channelName;   // how that channel is shown
	// Event notice (sub, gift bomb, raid, shoutout…) — empty for a normal
	// message. `text` / `parts` then hold the viewer's own message, if any.
	QString notice;
	QString noticeId; // a later copy with the same id (gift bomb filling in) replaces this one
	QString target;   // gift recipient / shoutout target
	QString tier;     // "Tier 1", "Prime"…
	QString value;    // reward name, Super Chat amount, TikTok gift name…
	QString tone;     // announcement colour
	QString sysText;  // the platform's own wording (Twitch system-msg)
	QStringList recipients;
	qint64 amount = 0; // viewers, bits, Kicks, diamonds, points…
	int months = 0;
	int streak = 0;
	int total = 0;
};

} // namespace gfx

// The "MultiChat" tab: Twitch, Kick, YouTube and TikTok chat in one feed (or
// filtered to one, to mentions, to one chatter or to a search), and a composer
// that sends as the streamer to Twitch, Kick and/or YouTube (TikTok is read-only). The dock owns the polling and the network
// calls; this panel only renders and collects input.
class GfxChatPanel : public QWidget {
public:
	using SendFn = std::function<void(const QStringList &platforms, const QString &text)>;
	// action: timeout | ban | unban | delete. displayName is for the confirmation line.
	using ModFn = std::function<void(const QString &platform, const QString &action, const QString &login,
					 int seconds, const QString &messageId, const QString &displayName)>;
	// Chatter card lookup (GET /api/obs/chat/user). done(info, error) on the UI thread.
	using InfoDone = std::function<void(const QJsonObject &info, const QString &error)>;
	using InfoFn = std::function<void(const QString &platform, const QString &login, InfoDone done)>;

	explicit GfxChatPanel(QWidget *parent = nullptr);

	// Platforms the dock asks the server for (Settings → MultiChat): Twitch and
	// Kick always, YouTube / TikTok when switched on.
	static QStringList wantedPlatforms();
	// Other channels to show, read-only (Settings → chatWatch), for ?watch=.
	static QStringList watchList();

	void setSendHandler(SendFn fn);
	void setModHandler(ModFn fn);
	void setInfoHandler(InfoFn fn);
	// Applies one poll response. Returns how many new messages arrived that the
	// current filter shows (for the unread badge on the tab).
	int applyPoll(const QJsonObject &response);
	// Non-empty = show the Pro lock instead of the chat.
	void setLocked(const QString &message);
	// The dock calls this when a send finishes.
	void sendFinished(bool anyDelivered);
	void clearChat();
	// Settings changed (⚙ dialog): re-read them and redraw the feed.
	void refresh();

protected:
	// Stats card clicks, feed hover / Ctrl+wheel, composer keys.
	bool eventFilter(QObject *watched, QEvent *event) override;
	// Re-arranges the header, stats and composer for the dock's size.
	void resizeEvent(QResizeEvent *event) override;

private:
	// One platform's card in the stats strip.
	struct StatCard {
		QString platform;
		QFrame *box = nullptr;
		QLabel *head = nullptr; // ● 1,234 viewers / Offline / Not connected
		QLabel *sub = nullptr;  // uptime · category (elided)
	};
	// One line in the feed: which message it shows and its alternating-row parity.
	struct Shown {
		qint64 seq = 0;
		bool odd = false;
	};
	void buildStatCard(StatCard &c, const QString &platform);
	// Viewers / uptime / title per platform + chatters and messages per minute.
	void renderStats();
	void renderStatCard(StatCard &c);
	// 1–4 cards per row (by width), only the platforms that are connected.
	void layoutCards(const QStringList &visible);
	// Picks the layout for this width / height (narrow ↔ wide); no-op when unchanged.
	void applyLayoutMode(bool force = false);
	void fitChips();
	qint64 serverNow() const;
	void buildUi();

	// ── Feed ──
	// Redraws every line (filter / search / settings changed, or a reset).
	void rebuild();
	// Adds one line at the bottom.
	void appendLine(const gfx::ChatMessage &m);
	// Redraws one line in place (deleted message).
	void updateLine(qint64 seq);
	// Drops lines whose messages fell out of the buffer, keeping the view still.
	void trimTop();
	void showPlaceholder();
	const gfx::ChatMessage *findMsg(qint64 seq) const;
	QString lineInnerHtml(const gfx::ChatMessage &m) const;
	// Event notices + cheers: their own look (Settings → MultiChat → Event notices).
	QString noticeHtml(const gfx::ChatMessage &m) const;
	bool noticeWanted(const gfx::ChatMessage &m) const;
	// The block format for any line (notice card, host, highlight, first-timer, shaded).
	QTextBlockFormat blockFormat(const gfx::ChatMessage &m, bool odd) const;
	QString textHtml(const QString &text) const;
	QString partsHtml(const gfx::ChatMessage &m, qreal basePt) const;
	int emoteSlot(const QString &url) const;
	bool watchStatusHas(const QString &key) const;
	bool isHighlighted(const gfx::ChatMessage &m) const;
	// Returns true when the pattern changed (names arrived, words edited).
	bool rebuildHighlightRe();
	qreal baseFontPt() const;
	void applyFontSize();
	void zoom(int steps);

	// ── Scrolling ──
	// The feed follows new lines unless the reader scrolled up or paused it.
	bool autoScrolling() const;
	void scrollToBottom();
	void setPaused(bool paused);
	void updateJump();

	bool matches(const gfx::ChatMessage &m) const;
	void renderStatus();
	void updateFilters();
	void updateTargets();
	QList<QPushButton *> targetButtons() const;
	void updateSendState();
	void setFilter(const QString &f);
	void setUserFilter(const QString &platform, const QString &login, const QString &shown);
	void doSend();
	bool platformConnected(const QString &p) const;
	// Click on a chatter's name: their card — what the platform says about them,
	// their recent messages here, and reply / profile / timeout / ban / unban.
	void showChatterCard(const QString &platform, const QString &login, const QString &messageId,
			     const QString &channel);
	// The same actions as a plain menu (right-click on a name).
	void showUserMenu(const QString &platform, const QString &login, const QString &messageId,
			  const QString &channel);
	void replyTo(const QString &platform, const QString &login, const QString &shown);
	void addModActions(QMenu *menu, const QString &platform, const QString &login, const QString &messageId,
			   const QString &shown, bool canDelete);
	void showWatchDialog();
	void updateWatchStatus(const QJsonArray &watch);
	void showOptionsMenu();
	// Composer: ↑/↓ recall sent messages, Tab completes chatter names.
	bool composerKey(QKeyEvent *e);
	void completeName(bool backwards);

	QList<gfx::ChatMessage> msgs_;
	QList<Shown> shown_;
	qint64 nextSeq_ = 1;
	bool placeholder_ = true;
	QJsonObject status_;
	QJsonObject streams_;                    // "streams" from the poll; empty = older server, strip hidden
	qint64 clockSkew_ = 0;                   // server clock − ours, so uptime matches the server's start times
	QString filter_ = QStringLiteral("all"); // all | twitch | kick | youtube | tiktok | watched | mentions
	QString search_;
	QString userFilterPlatform_, userFilterLogin_;
	QRegularExpression highlightRe_;
	SendFn send_;
	ModFn mod_;
	InfoFn info_;
	QJsonArray watchStatus_;
	bool sending_ = false;

	bool follow_ = true;       // the reader is at the bottom
	bool paused_ = false;      // ⏸ pressed
	bool hoverPaused_ = false; // mouse over the feed (setting)
	int programmatic_ = 0;     // > 0 while the panel itself moves the scroll bar
	int pendingNew_ = 0;

	QStringList history_; // sent messages, newest last
	int historyPos_ = -1;
	QString historyDraft_;
	QStringList tabMatches_;
	int tabIndex_ = -1;
	int tabStart_ = -1;

	// Layout mode: 0 narrow (≲ 380 px), 1 medium, 2 wide (≳ 640 px); short = little height.
	int mode_ = -1;
	bool short_ = false;
	int cardCols_ = 2;
	QBoxLayout *compLayout_ = nullptr;
	QWidget *targetsBox_ = nullptr;
	QWidget *chipsBox_ = nullptr;
	QPushButton *fWatch_ = nullptr;
	// Emote images: <img src='emote:N/px'> → urls_[N] (the HTML never carries a raw URL).
	mutable QStringList urls_;
	mutable QHash<QString, int> urlSlot_;

	QWidget *main_ = nullptr;
	QWidget *lockPanel_ = nullptr;
	QLabel *lockLbl_ = nullptr;
	QLabel *statusLbl_ = nullptr;
	QWidget *stats_ = nullptr;
	QGridLayout *cardsGrid_ = nullptr;
	QString cardsKey_; // which cards the grid holds now, e.g. "twitch,youtube"
	StatCard twCard_;
	StatCard kkCard_;
	StatCard ytCard_;
	StatCard ttCard_;
	QLabel *summary_ = nullptr;
	QTimer *tick_ = nullptr; // 1 s: uptime + chat pace
	QTimer *searchDelay_ = nullptr;
	QPushButton *fAll_ = nullptr;
	QPushButton *fTwitch_ = nullptr;
	QPushButton *fKick_ = nullptr;
	QPushButton *fYouTube_ = nullptr;
	QPushButton *fTikTok_ = nullptr;
	QPushButton *fMentions_ = nullptr;
	QPushButton *searchBtn_ = nullptr;
	QPushButton *pauseBtn_ = nullptr;
	QPushButton *moreBtn_ = nullptr;
	QWidget *searchRow_ = nullptr;
	QLineEdit *searchEdit_ = nullptr;
	QPushButton *userChip_ = nullptr;
	QTextBrowser *view_ = nullptr;
	QPushButton *jump_ = nullptr;
	QPushButton *toTwitch_ = nullptr;
	QPushButton *toKick_ = nullptr;
	QPushButton *toYouTube_ = nullptr;
	QLabel *readOnlyLbl_ = nullptr; // only TikTok connected → can't send
	QLineEdit *input_ = nullptr;
	QPushButton *sendBtn_ = nullptr;
	QLabel *countLbl_ = nullptr;
};
