/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-events.hpp"
#include "gfx-flow.hpp"
#include "gfx-http.hpp"
#include "gfx-notice.hpp"
#include "gfx-settings.hpp"
#include "gfx-theme.hpp"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QFontInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QPoint>
#include <QPushButton>
#include <QRectF>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStringList>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

using namespace gfx;

namespace {

constexpr int kKeep = 300; // events kept in the list
// Block property: accent bar colour, painted down a card's left edge (same as MultiChat).
constexpr int kBarProp = QTextFormat::UserProperty + 7;

const char *const kFilters[][2] = {
	{"all", "All"},       {"gifts", "Subs & gifts"}, {"support", "Support"},
	{"points", "Points"}, {"streaks", "Streaks"},    {"community", "Raids & follows"},
};

const QStringList &chatPlatforms()
{
	static const QStringList p = {QStringLiteral("twitch"), QStringLiteral("kick"), QStringLiteral("youtube"),
				      QStringLiteral("tiktok")};
	return p;
}

QString fmtTime(qint64 ms)
{
	return QDateTime::fromMSecsSinceEpoch(ms > 0 ? ms : QDateTime::currentMSecsSinceEpoch())
		.toString(QStringLiteral("HH:mm"));
}

QString fmtCount(qint64 n)
{
	return QLocale(QLocale::English).toString(n);
}

// "(…)" at the end of a one-line summary — the amount on alert lines.
QString trailingParen(const QString &text)
{
	static const QRegularExpression re(QStringLiteral("\\(([^()]*)\\)\\s*$"));
	const QRegularExpressionMatch m = re.match(text);
	return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

// The feed: platform icons as gfx:<platform>/<px>, plus each card's accent bar.
class EventsView : public QTextBrowser {
public:
	explicit EventsView(QWidget *parent) : QTextBrowser(parent) {}

protected:
	void paintEvent(QPaintEvent *e) override
	{
		QTextBrowser::paintEvent(e);
		QPainter p(viewport());
		const QAbstractTextDocumentLayout *lay = document()->documentLayout();
		const int dx = horizontalScrollBar()->value(), dy = verticalScrollBar()->value();
		const int h = viewport()->height();
		for (QTextBlock b = cursorForPosition(QPoint(0, 0)).block(); b.isValid(); b = b.next()) {
			const QRectF r = lay->blockBoundingRect(b).translated(-dx, -dy);
			if (r.top() > h)
				break;
			const QVariant bar = b.blockFormat().property(kBarProp);
			if (bar.isValid())
				p.fillRect(QRectF(r.left(), r.top(), 3, r.height()), bar.value<QColor>());
		}
	}
	QVariant loadResource(int type, const QUrl &name) override
	{
		if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("gfx")) {
			const QStringList parts = name.path().split(QLatin1Char('/'));
			const int px = parts.size() > 1 ? qBound(8, parts.at(1).toInt(), 48) : 15;
			return QVariant::fromValue(notice::platformIcon(parts.value(0), px, devicePixelRatioF()));
		}
		return QTextBrowser::loadResource(type, name);
	}
};

} // namespace

GfxEventsPanel::GfxEventsPanel(QWidget *parent) : QWidget(parent)
{
	buildUi();
	rebuild();
}

void GfxEventsPanel::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 4, 0, 0);
	root->setSpacing(6);

	// ── Filters + Clear (wrap in a narrow dock) ──
	auto *chipsBox = new QWidget(this);
	auto *chips = new FlowLayout(chipsBox, 4, 4);
	chips->setContentsMargins(0, 0, 0, 0);
	for (const auto &f : kFilters) {
		const QString key = QString::fromLatin1(f[0]);
		auto *b = new QPushButton(QString::fromUtf8(f[1]), chipsBox);
		b->setObjectName(QStringLiteral("gfxSeg"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		b->setChecked(key == filter_);
		connect(b, &QPushButton::clicked, this, [this, key] { setFilter(key); });
		chips->addWidget(b);
		chips_.push_back({key, b});
	}
	auto *clearBtn = new QPushButton(QStringLiteral("Clear"), chipsBox);
	theme::setRole(clearBtn, "ghost");
	clearBtn->setCursor(Qt::PointingHandCursor);
	clearBtn->setToolTip(QStringLiteral("Clear this list (doesn't affect anything on stream)"));
	chips->addWidget(clearBtn);
	connect(clearBtn, &QPushButton::clicked, this, [this] { clearEvents(); });
	root->addWidget(chipsBox);

	// ── This session at a glance: four tiles ──
	auto *tiles = new QHBoxLayout();
	tiles->setSpacing(6);
	const auto tile = [this, tiles](const QString &tip) {
		auto *box = new QFrame(this);
		box->setObjectName(QStringLiteral("gfxStatCard"));
		auto *l = new QVBoxLayout(box);
		l->setContentsMargins(8, 5, 8, 5);
		auto *lbl = new QLabel(box);
		lbl->setTextFormat(Qt::RichText);
		lbl->setAlignment(Qt::AlignCenter);
		l->addWidget(lbl);
		box->setToolTip(tip);
		box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		tiles->addWidget(box, 1);
		return lbl;
	};
	tileGifted_ = tile(QStringLiteral("Subs gifted since OBS connected"));
	tileSupport_ = tile(QStringLiteral("Bits, Kicks, Super Chats / Stickers and TikTok gifts"));
	tilePoints_ = tile(QStringLiteral("Channel point redemptions"));
	tileStreaks_ = tile(QStringLiteral("Watch streaks shared"));
	root->addLayout(tiles);

	// ── Channel points need a newer Twitch login on some accounts ──
	notice_ = new QFrame(this);
	notice_->setObjectName(QStringLiteral("gfxUpdateBar"));
	auto *nl = new QHBoxLayout(notice_);
	nl->setContentsMargins(8, 6, 8, 6);
	nl->setSpacing(8);
	noticeLbl_ = new QLabel(notice_);
	noticeLbl_->setWordWrap(true);
	auto *noticeBtn = new QPushButton(QStringLiteral("Reconnect"), notice_);
	theme::setRole(noticeBtn, "soft");
	noticeBtn->setCursor(Qt::PointingHandCursor);
	nl->addWidget(noticeLbl_, 1);
	nl->addWidget(noticeBtn);
	notice_->hide();
	root->addWidget(notice_);
	// The dock asks the server for a one-time Twitch approve link for this
	// account: it only refreshes the saved Twitch permissions and never signs
	// the browser in or out of the dashboard.
	connect(noticeBtn, &QPushButton::clicked, this, [this] {
		if (onReconnect)
			onReconnect();
		else
			QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/account")));
	});

	// ── The feed: same frame and cards as MultiChat ──
	view_ = new EventsView(this);
	view_->setObjectName(QStringLiteral("gfxChatView"));
	view_->setOpenLinks(false);
	view_->setReadOnly(true);
	view_->setContextMenuPolicy(Qt::CustomContextMenu);
	view_->setMinimumHeight(160);
	view_->document()->setDocumentMargin(6);
	root->addWidget(view_, 1);
	connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint &p) { showMenu(p); });
	// toggle:<seq> — show / hide a gift bomb's recipients.
	connect(view_, &QTextBrowser::anchorClicked, this, [this](const QUrl &u) {
		if (u.scheme() != QLatin1String("toggle"))
			return;
		bool ok = false;
		const qint64 seq = u.path().toLongLong(&ok);
		if (!ok)
			return;
		if (toggled_.contains(seq))
			toggled_.remove(seq);
		else
			toggled_.insert(seq);
		rebuild();
	});
	QScrollBar *vsb = view_->verticalScrollBar();
	// Newest events are at the TOP. follow_ = pinned to the top; scrolled down
	// reading older ones, the view holds its distance from the bottom, so cards
	// landing above never move what the streamer is reading.
	connect(vsb, &QScrollBar::rangeChanged, this, [this](int min, int max) {
		++programmatic_;
		view_->verticalScrollBar()->setValue(follow_ ? min : qMax(min, max - fromBottom_));
		--programmatic_;
	});
	connect(vsb, &QScrollBar::valueChanged, this, [this](int v) {
		if (programmatic_)
			return;
		QScrollBar *sb = view_->verticalScrollBar();
		fromBottom_ = sb->maximum() - v;
		follow_ = v <= sb->minimum() + 4;
		if (follow_)
			pendingNew_ = 0;
		updateJump();
	});

	jump_ = new QPushButton(this);
	jump_->setObjectName(QStringLiteral("gfxJump"));
	theme::setRole(jump_, "soft");
	jump_->setCursor(Qt::PointingHandCursor);
	jump_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	jump_->hide();
	// Above the feed — new events land at the top, so that's where "↑ N new" points.
	root->insertWidget(root->indexOf(view_), jump_);
	connect(jump_, &QPushButton::clicked, this, [this] {
		follow_ = true;
		pendingNew_ = 0;
		scrollToNewest();
		updateJump();
	});
}

void GfxEventsPanel::setFilter(const QString &f)
{
	filter_ = f;
	for (const auto &c : chips_)
		c.second->setChecked(c.first == f);
	follow_ = true;
	rebuild();
}

// Which chip an event belongs to ("" = only under All).
QString GfxEventsPanel::group(const FeedEvent &e) const
{
	const QString &k = e.kind;
	if (k == QLatin1String("giftbomb"))
		return QStringLiteral("gifts");
	if (k == QLatin1String("points"))
		return QStringLiteral("points");
	if (k == QLatin1String("streak"))
		return QStringLiteral("streaks");
	if (k == QLatin1String("xive"))
		return QStringLiteral("support");
	if (k == QLatin1String("shoutout"))
		return QStringLiteral("community");
	if (k == QLatin1String("hypetrain"))
		return QStringLiteral("support");
	if (k == QLatin1String("alert")) {
		const QString &t = e.alertType;
		if (t == QLatin1String("subscriber") || t == QLatin1String("resub") || t == QLatin1String("gifted"))
			return QStringLiteral("gifts");
		if (t == QLatin1String("bits"))
			return QStringLiteral("support");
		if (t == QLatin1String("follower") || t == QLatin1String("raid"))
			return QStringLiteral("community");
		return QString();
	}
	if (k == QLatin1String("tiktok")) {
		if (e.sub == QLatin1String("gift"))
			return QStringLiteral("support");
		if (e.sub == QLatin1String("follow"))
			return QStringLiteral("community");
		return QStringLiteral("gifts");
	}
	if (k == QLatin1String("youtube")) {
		if (e.sub == QLatin1String("superchat") || e.sub == QLatin1String("supersticker"))
			return QStringLiteral("support");
		return QStringLiteral("gifts");
	}
	return QString();
}

bool GfxEventsPanel::shown(const FeedEvent &e) const
{
	const Settings &s = settings();
	const QString &k = e.kind;
	bool on = false;
	if (k == QLatin1String("giftbomb"))
		on = s.evGiftBombs;
	else if (k == QLatin1String("points"))
		on = s.evPoints;
	else if (k == QLatin1String("streak"))
		on = s.evStreaks;
	else if (k == QLatin1String("tiktok"))
		on = s.evTikTok;
	else if (k == QLatin1String("youtube"))
		on = s.evYouTube;
	else if (k == QLatin1String("shoutout"))
		on = s.evShoutouts;
	else if (k == QLatin1String("hypetrain"))
		on = s.evHypeTrain;
	else if (k == QLatin1String("alert") || k == QLatin1String("xive"))
		// The gift-bomb card (with its recipients) replaces the plain "gift subs" alert card.
		on = s.evAlerts && !(seenGiftBomb_ && s.evGiftBombs && e.alertType == QLatin1String("gifted"));
	else
		on = s.evTimers; // timer / actiontimer / wheel
	if (!on)
		return false;
	return filter_ == QLatin1String("all") || group(e) == filter_;
}

// ── Cards ───────────────────────────────────────────────────────

GfxEventsPanel::Card GfxEventsPanel::describe(const FeedEvent &e) const
{
	Card c;
	const QString strong = settings().noticeStyle == QLatin1String("minimal") ? QStringLiteral("#c9ced8")
										  : QStringLiteral("#ffffff");
	const QString soft = settings().noticeStyle == QLatin1String("minimal") ? QStringLiteral("#9aa1b0")
										: QStringLiteral("#d6d9e0");
	const auto B = [&strong](const QString &t) {
		return QStringLiteral("<b style='color:%1'>%2</b>").arg(strong, t.toHtmlEscaped());
	};
	const auto S = [&soft](const QString &t) {
		return QStringLiteral(" <span style='color:%1'>· %2</span>").arg(soft, t.toHtmlEscaped());
	};
	const QString N = B(e.user.isEmpty() ? QStringLiteral("Someone") : e.user);
	const QString &k = e.kind;
	c.platform = chatPlatforms().contains(e.platform) ? e.platform : QString();
	c.message = e.message;

	if (k == QLatin1String("giftbomb")) {
		const int total = qMax(e.amount.toInt(), static_cast<int>(e.recipients.size()));
		c.type = QStringLiteral("giftbomb");
		c.bomb = true;
		// Word for word what MultiChat's gift bomb card says.
		const QString subs = QStringLiteral("%1 %2").arg(fmtCount(total), total == 1 ? QStringLiteral("sub")
											     : QStringLiteral("subs"));
		c.head = e.platform == QLatin1String("kick")
				 ? N + QStringLiteral(" gifted ") + B(subs) + QLatin1Char('!')
				 : N + QStringLiteral(" is gifting ") + B(subs) + QStringLiteral(" to the community!");
		c.names = e.recipients;
		c.missing = qMax(0, total - static_cast<int>(e.recipients.size()));
		c.message.clear();
	} else if (k == QLatin1String("hypetrain")) {
		// One card per train, updated live: level, points to the next level,
		// when it runs out; at the end the final level and total. Top
		// contributors / record / cooldown come in as the message line.
		c.type = QStringLiteral("hypetrain");
		c.platform = QStringLiteral("twitch");
		if (e.trainType == QLatin1String("golden_kappa")) {
			c.pillLabel = QStringLiteral("GOLDEN KAPPA TRAIN");
			c.pillColor = QStringLiteral("#f5c542");
		} else if (e.trainType == QLatin1String("treasure")) {
			c.pillLabel = QStringLiteral("TREASURE TRAIN");
			c.pillColor = QStringLiteral("#22d3ee");
		}
		const int level = qMax(1, e.amount.toInt());
		const QString name = e.title.isEmpty() ? QStringLiteral("Hype Train") : e.title;
		const QString lvl = QStringLiteral("Level %1").arg(level);
		if (e.sub == QLatin1String("end")) {
			c.head = B(name) + QStringLiteral(" ended at ") + B(lvl) +
				 S(QStringLiteral("%1 points").arg(fmtCount(e.total)));
		} else {
			c.head = B(name) +
				 (e.sub == QLatin1String("begin") ? QStringLiteral(" started! ")
								  : QStringLiteral(" · ")) +
				 B(lvl);
			if (e.goal > 0)
				c.head += S(QStringLiteral("%1 / %2 to Level %3")
						    .arg(fmtCount(e.progress), fmtCount(e.goal))
						    .arg(level + 1));
			if (e.expiresAt > 0)
				c.head += S(QStringLiteral("runs until %1").arg(fmtTime(e.expiresAt)));
		}
	} else if (k == QLatin1String("points")) {
		c.type = QStringLiteral("points");
		c.platform = c.platform.isEmpty() && e.platform != QLatin1String("xive") ? QStringLiteral("twitch")
											 : c.platform;
		c.head = N + QStringLiteral(" redeemed ") + B(e.title.isEmpty() ? QStringLiteral("a reward") : e.title);
		if (e.amount.toLongLong() > 0)
			c.head += S(fmtCount(e.amount.toLongLong()));
	} else if (k == QLatin1String("streak")) {
		c.type = QStringLiteral("streak");
		c.platform = QStringLiteral("twitch");
		c.head = N + QStringLiteral(" is on a ") + B(e.amount + QStringLiteral("-stream")) +
			 QStringLiteral(" watch streak!");
	} else if (k == QLatin1String("tiktok")) {
		c.platform = QStringLiteral("tiktok");
		if (e.sub == QLatin1String("gift")) {
			c.type = QStringLiteral("tiktok_gift");
			const int n = e.amount.toInt();
			c.head = N + QStringLiteral(" sent ") +
				 B((n > 1 ? QStringLiteral("%1× ").arg(n) : QString()) +
				   (e.title.isEmpty() ? QStringLiteral("a gift") : e.title));
			static const QRegularExpression diamonds(QStringLiteral("\\((\\d[\\d,]*) 💎\\)"));
			const QRegularExpressionMatch m = diamonds.match(e.text);
			if (m.hasMatch())
				c.head += S(m.captured(1) + QStringLiteral(" 💎"));
		} else if (e.sub == QLatin1String("follow")) {
			c.type = QStringLiteral("follow");
			c.head = N + QStringLiteral(" followed");
		} else {
			c.type = QStringLiteral("sub");
			c.head = N + QStringLiteral(" subscribed!");
		}
	} else if (k == QLatin1String("youtube")) {
		c.platform = QStringLiteral("youtube");
		if (e.sub == QLatin1String("superchat") || e.sub == QLatin1String("supersticker")) {
			c.type = e.sub;
			c.head = N +
				 QStringLiteral(" sent a %1 · ")
					 .arg(e.sub == QLatin1String("superchat") ? QStringLiteral("Super Chat")
										  : QStringLiteral("Super Sticker")) +
				 B(e.amount);
		} else if (e.sub == QLatin1String("member")) {
			c.type = QStringLiteral("member");
			const QString level = trailingParen(e.text);
			c.head = N + QStringLiteral(" became a member") +
				 (level.isEmpty() ? QString() : QStringLiteral(" · ") + B(level)) + QLatin1Char('!');
		} else if (e.sub == QLatin1String("milestone")) {
			c.type = QStringLiteral("milestone");
			static const QRegularExpression months(QStringLiteral("for (\\d+) months"));
			const QRegularExpressionMatch m = months.match(e.text);
			c.head =
				N + QStringLiteral(" has been a member for ") +
				B((m.hasMatch() ? m.captured(1) : QStringLiteral("many")) + QStringLiteral(" months")) +
				QLatin1Char('!');
		} else {
			c.type = QStringLiteral("ytgift");
			const int n = qMax(1, e.amount.toInt());
			c.head = N + QStringLiteral(" gifted ") +
				 B(QStringLiteral("%1 %2").arg(fmtCount(n), n == 1 ? QStringLiteral("membership")
										   : QStringLiteral("memberships"))) +
				 QLatin1Char('!');
		}
	} else if (k == QLatin1String("alert")) {
		if (c.platform.isEmpty() && e.platform != QLatin1String("xive"))
			c.platform = QStringLiteral("twitch");
		const QString amt = trailingParen(e.text);
		const QString &t = e.alertType;
		if (t == QLatin1String("follower")) {
			c.type = QStringLiteral("follow");
			c.head = N + QStringLiteral(" followed");
		} else if (t == QLatin1String("subscriber")) {
			c.type = QStringLiteral("sub");
			c.head = N + QStringLiteral(" subscribed!");
		} else if (t == QLatin1String("resub")) {
			c.type = QStringLiteral("resub");
			c.head = N + QStringLiteral(" resubscribed") +
				 (amt.isEmpty() ? QString()
						: QStringLiteral(" for ") + B(amt + QStringLiteral(" months"))) +
				 QLatin1Char('!');
		} else if (t == QLatin1String("gifted")) {
			c.type = QStringLiteral("giftsub");
			c.head = N + QStringLiteral(" gifted ") +
				 B((amt.isEmpty() ? QStringLiteral("1") : amt) + QStringLiteral(" sub") +
				   (amt.toInt() > 1 ? QStringLiteral("s") : QString())) +
				 QLatin1Char('!');
		} else if (t == QLatin1String("bits")) {
			c.type = e.platform == QLatin1String("kick") ? QStringLiteral("kicks")
								     : QStringLiteral("cheer");
			c.head = N +
				 (e.platform == QLatin1String("kick") ? QStringLiteral(" sent ")
								      : QStringLiteral(" cheered ")) +
				 B(amt + (e.platform == QLatin1String("kick") ? QStringLiteral(" Kicks")
									      : QStringLiteral(" bits")));
		} else if (t == QLatin1String("raid")) {
			c.type = QStringLiteral("raid");
			c.head = N + QStringLiteral(" is raiding with ") + B(amt + QStringLiteral(" viewers")) +
				 QLatin1Char('!');
		} else {
			c.type = QStringLiteral("system");
			c.pillIcon = QStringLiteral("🏆");
			c.pillLabel = QStringLiteral("MILESTONE");
			c.pillColor = QStringLiteral("#ffb020");
			c.head = QString(e.text).remove(QStringLiteral("🔔 ")).toHtmlEscaped();
		}
	} else if (k == QLatin1String("shoutout")) {
		// Word for word what MultiChat's shoutout card says.
		const bool incoming = e.sub == QLatin1String("in");
		c.type = incoming ? QStringLiteral("shoutout_in") : QStringLiteral("shoutout");
		if (c.platform.isEmpty())
			c.platform = QStringLiteral("twitch");
		const qint64 viewers = e.amount.toLongLong();
		if (incoming) {
			c.head = N + QStringLiteral(" gave you a shoutout");
			if (viewers > 0)
				c.head += QStringLiteral(" in front of ") +
					  B(QStringLiteral("%1 %2").arg(fmtCount(viewers),
									viewers == 1 ? QStringLiteral("viewer")
										     : QStringLiteral("viewers")));
			c.head += QLatin1Char('!');
		} else {
			c.head = N + QStringLiteral(" gave a shoutout to ") +
				 B(e.title.isEmpty() ? QStringLiteral("someone") : e.title);
			if (e.message == QLatin1String("ForgeBot"))
				c.head += S(QStringLiteral("via ForgeBot"));
		}
		c.message.clear();
	} else if (k == QLatin1String("xive")) {
		c.type = QStringLiteral("system");
		c.pillIcon = QStringLiteral("🐝");
		c.pillLabel = QStringLiteral("XIVE");
		c.pillColor = QStringLiteral("#f5b400");
		c.head = QString(e.text).remove(QStringLiteral("🐝 Xive: ")).toHtmlEscaped();
		c.message.clear();
	} else {
		// Subathon / intro timer / wheel — GoalForgeX's own activity.
		c.type = QStringLiteral("system");
		QString text = e.text;
		if (k == QLatin1String("wheel")) {
			c.pillIcon = QStringLiteral("🎡");
			c.pillLabel = QStringLiteral("WHEEL");
			text.remove(QStringLiteral("🎡 "));
		} else if (k == QLatin1String("actiontimer")) {
			c.pillIcon = QStringLiteral("⏳");
			c.pillLabel = QStringLiteral("INTRO TIMER");
			text.remove(QStringLiteral("⏳ Intro timer "));
			text.remove(QStringLiteral("⚡ Action timer ")); // feed lines from an older server
		} else {
			c.pillIcon = QStringLiteral("⏱");
			c.pillLabel = QStringLiteral("TIMER");
			text.remove(QStringLiteral("⏱ Timer "));
		}
		c.pillColor = QStringLiteral("#9aa1b0");
		c.head = text.toHtmlEscaped();
		c.message.clear();
	}
	if (!e.by.isEmpty())
		c.head += S(QStringLiteral("by ") + e.by);
	return c;
}

// Built exactly like MultiChat's noticeHtml() — same pill, sizes, spacing and
// Settings → Event notices look (card / strip / minimal) and colour.
QString GfxEventsPanel::cardHtml(const FeedEvent &e) const
{
	const Settings &s = settings();
	const Card c = describe(e);
	const notice::Look lk = notice::look(c.type);
	const QString icon = c.pillIcon.isEmpty() ? lk.icon : c.pillIcon;
	const QString label = c.pillLabel.isEmpty() ? lk.label : c.pillLabel;
	const QColor evColor(c.pillColor.isEmpty() ? lk.color : c.pillColor);
	const bool card = s.noticeStyle == QLatin1String("card");
	const bool minimal = s.noticeStyle == QLatin1String("minimal");
	view_->ensurePolished();
	const qreal fontPt = QFontInfo(view_->font()).pointSizeF();
	const qreal base = fontPt > 0 ? fontPt : 9.0;
	const qreal tagPt = qMax<qreal>(6.0, base * 0.78);
	const qreal smallPt = qMax<qreal>(6.5, base * 0.88);
	const int iconPx = qBound(12, qRound(base * 1.65), 32);
	const QString bg = theme::mapColor(QStringLiteral("#121419"));
	const QString soft = minimal ? QStringLiteral("#9aa1b0") : QStringLiteral("#d6d9e0");

	QString h;
	if (!minimal)
		h += QStringLiteral("&nbsp;&nbsp;"); // room for the accent bar
	if (s.chatIcons && !c.platform.isEmpty())
		h += QStringLiteral("<img src='gfx:%1/%2' width='%2' height='%2' style='vertical-align:middle'>&nbsp;")
			     .arg(c.platform)
			     .arg(iconPx);
	if (minimal)
		h += icon + QStringLiteral("&nbsp;");
	else
		h += QStringLiteral("<span style='background-color:%1;color:%2;font-size:%3pt;font-weight:800;'>"
				    "&nbsp;%4&nbsp;%5&nbsp;</span>&nbsp;")
			     .arg(notice::mix(QColor(bg), evColor, 0.28).name(), evColor.name(),
				  QString::number(tagPt, 'f', 1), icon, label);
	if (!minimal)
		h += QStringLiteral("<span style='color:#5b6270;font-size:%2pt;'>%1</span> ")
			     .arg(fmtTime(e.at), QString::number(smallPt, 'f', 1));
	const QString head = minimal ? QStringLiteral("<span style='color:%1'>%2</span>").arg(soft, c.head) : c.head;
	const QString indent = QStringLiteral("<br>&nbsp;&nbsp;");
	h += card ? indent + head : head;
	if (!c.message.trimmed().isEmpty()) {
		QString msg = c.message;
		msg.replace(QLatin1Char('\n'), QLatin1Char(' '));
		if (card)
			h += indent + QStringLiteral("<span style='color:#e8eaf0'>%1</span>").arg(msg.toHtmlEscaped());
		else
			h += QStringLiteral(" <span style='color:%1'>— %2</span>")
				     .arg(minimal ? soft : QStringLiteral("#e8eaf0"), msg.toHtmlEscaped());
	}
	// Gift bomb: who got one — open by default (Settings → Events), click to show / hide.
	if (c.bomb && card) {
		const bool open = s.evAutoExpand != toggled_.contains(e.seq);
		QString line;
		if (c.names.isEmpty()) {
			line = e.partial ? QStringLiteral("Handing out gifts…")
					 : QStringLiteral("Recipients weren't reported for this one");
		} else if (!open) {
			line = QStringLiteral("<a href='toggle:%1' style='color:#7aa2ff;text-decoration:none;'>▸ Show "
					      "who got one (%2)</a>")
				       .arg(e.seq)
				       .arg(c.names.size());
		} else {
			QStringList esc;
			for (const QString &n : c.names)
				esc << n.toHtmlEscaped();
			line = QStringLiteral("→ ") + esc.join(QStringLiteral(", "));
			if (c.missing > 0)
				line += e.partial ? QStringLiteral(" … +%1 on the way").arg(c.missing)
						  : QStringLiteral(" · +%1 not reported").arg(c.missing);
			line += QStringLiteral(
					"&nbsp;&nbsp;<a href='toggle:%1' style='color:#7aa2ff;text-decoration:none;'>"
					"hide</a>")
					.arg(e.seq);
		}
		h += indent + QStringLiteral("<span style='color:#9aa1b0;font-size:%1pt'>%2</span>")
				      .arg(QString::number(smallPt, 'f', 1), line);
	}
	return h;
}

QTextBlockFormat GfxEventsPanel::cardFormat(const FeedEvent &e) const
{
	const Settings &s = settings();
	QTextBlockFormat f;
	f.setLineHeight(112, QTextBlockFormat::ProportionalHeight);
	if (s.noticeStyle == QLatin1String("minimal")) {
		f.setBottomMargin(s.chatCompact ? 1 : 4);
		return f;
	}
	const Card c = describe(e);
	const QString evColor = c.pillColor.isEmpty() ? notice::look(c.type).color : c.pillColor;
	const QColor tint(c.platform.isEmpty() ? evColor : notice::tint(c.platform, evColor));
	const bool card = s.noticeStyle == QLatin1String("card");
	f.setTopMargin(card ? 3 : 1);
	f.setBottomMargin(s.chatCompact ? 3 : (card ? 7 : 4));
	f.setBackground(notice::mix(QColor(theme::mapColor(QStringLiteral("#121419"))), tint, card ? 0.15 : 0.10));
	f.setProperty(kBarProp, tint);
	return f;
}

// ── Feed ────────────────────────────────────────────────────────

void GfxEventsPanel::appendCard(const FeedEvent &e)
{
	++programmatic_;
	QTextDocument *doc = view_->document();
	if (placeholder_) {
		doc->clear();
		shownSeq_.clear();
		placeholder_ = false;
	}
	const QTextBlockFormat fmt = cardFormat(e);
	QTextCursor c(doc);
	c.movePosition(QTextCursor::End);
	if (!shownSeq_.isEmpty())
		c.insertBlock(fmt, QTextCharFormat());
	c.setCharFormat(QTextCharFormat());
	c.insertHtml(cardHtml(e));
	c.setBlockFormat(fmt);
	shownSeq_.push_back(e.seq);
	--programmatic_;
}

// A new event goes in at the TOP (newest first). Splits in front of the current
// first card: that card moves to block 2 keeping its own format, block 1 is the
// new one — so shownSeq_[n] still matches block n for the right-click menu.
void GfxEventsPanel::prependCard(const FeedEvent &e)
{
	++programmatic_;
	QTextDocument *doc = view_->document();
	if (placeholder_) {
		doc->clear();
		shownSeq_.clear();
		placeholder_ = false;
	}
	const QTextBlockFormat fmt = cardFormat(e);
	QTextCursor c(doc);
	c.movePosition(QTextCursor::Start);
	if (!shownSeq_.isEmpty()) {
		c.insertBlock(doc->firstBlock().blockFormat(), QTextCharFormat());
		c.movePosition(QTextCursor::Start);
	}
	c.setBlockFormat(fmt);
	c.setCharFormat(QTextCharFormat());
	c.insertHtml(cardHtml(e));
	c.setBlockFormat(fmt);
	shownSeq_.prepend(e.seq);
	--programmatic_;
}

void GfxEventsPanel::showPlaceholder()
{
	++programmatic_;
	QTextDocument *doc = view_->document();
	doc->clear();
	shownSeq_.clear();
	placeholder_ = true;
	const QString text =
		events_.isEmpty()
			? QStringLiteral(
				  "Nothing yet — gift bombs, channel-point redemptions, watch streaks, subs, raids "
				  "and TikTok / YouTube support show up here the moment they happen.")
			: QStringLiteral("Nothing matches this filter yet.");
	QTextBlockFormat f;
	f.setAlignment(Qt::AlignCenter);
	f.setTopMargin(24);
	QTextCursor c(doc);
	c.insertHtml(QStringLiteral("<span style='color:#6b7280'>%1</span>").arg(text.toHtmlEscaped()));
	c.setBlockFormat(f);
	--programmatic_;
}

void GfxEventsPanel::rebuild()
{
	QScrollBar *sb = view_->verticalScrollBar();
	++programmatic_;
	{
		QTextCursor batch(view_->document());
		batch.beginEditBlock();
		view_->document()->clear();
		shownSeq_.clear();
		placeholder_ = false;
		// Newest first: walk the (oldest → newest) list backwards.
		for (qsizetype i = events_.size() - 1; i >= 0; --i)
			if (shown(events_.at(i)))
				appendCard(events_.at(i));
		if (shownSeq_.isEmpty())
			showPlaceholder();
		batch.endEditBlock();
	}
	--programmatic_;
	++programmatic_;
	sb->setValue(follow_ ? sb->minimum() : qMax(sb->minimum(), sb->maximum() - fromBottom_));
	--programmatic_;
	renderTally();
	updateJump();
}

int GfxEventsPanel::addEvents(const QList<FeedEvent> &events)
{
	int visible = 0;
	bool needRebuild = false;
	QList<qint64> fresh;
	for (const FeedEvent &e : events) {
		// A gift bomb filling in (2.7+ server): replace its card where it is.
		if (!e.bombId.isEmpty()) {
			qsizetype at = -1;
			for (qsizetype i = events_.size() - 1; i >= 0 && at < 0; --i)
				if (events_.at(i).bombId == e.bombId)
					at = i;
			if (at >= 0) {
				FeedEvent upd = e;
				upd.seq = events_.at(at).seq; // keep its place and its open / closed state
				upd.at = events_.at(at).at;
				events_[at] = upd;
				needRebuild = true;
				continue; // not a new event — no unread count
			}
		}
		if (e.kind == QLatin1String("giftbomb") && !seenGiftBomb_) {
			seenGiftBomb_ = true;
			needRebuild = true; // earlier "gift subs" alert cards now hide
		}
		FeedEvent ev = e;
		ev.seq = nextSeq_++; // panel-local: a restarted server feed can reuse seq numbers
		events_.push_back(ev);
		if (shown(ev)) {
			++visible;
			fresh.push_back(ev.seq);
		}
	}
	while (events_.size() > kKeep) {
		events_.removeFirst();
		needRebuild = true;
	}
	if (needRebuild) {
		if (!follow_)
			pendingNew_ += visible;
		rebuild();
		return visible;
	}
	{
		QTextCursor batch(view_->document());
		batch.beginEditBlock();
		for (const qint64 seq : fresh)
			if (const FeedEvent *e = eventBySeq(seq))
				prependCard(*e);
		batch.endEditBlock();
	}
	if (!follow_)
		pendingNew_ += visible;
	else
		scrollToNewest();
	if (shownSeq_.isEmpty())
		showPlaceholder();
	renderTally();
	updateJump();
	return visible;
}

const FeedEvent *GfxEventsPanel::eventBySeq(qint64 seq) const
{
	for (qsizetype i = events_.size() - 1; i >= 0; --i)
		if (events_.at(i).seq == seq)
			return &events_.at(i);
	return nullptr;
}

bool GfxEventsPanel::autoScrolling() const
{
	return follow_;
}

void GfxEventsPanel::scrollToNewest()
{
	++programmatic_;
	view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->minimum());
	--programmatic_;
}

void GfxEventsPanel::updateJump()
{
	const bool show = !autoScrolling() && pendingNew_ > 0;
	jump_->setText(show ? QStringLiteral("↑  %1 new event%2")
				       .arg(pendingNew_)
				       .arg(pendingNew_ == 1 ? QString() : QStringLiteral("s"))
			    : QString());
	jump_->setVisible(show);
}

// ── Tiles ───────────────────────────────────────────────────────

void GfxEventsPanel::renderTally()
{
	qint64 gifted = 0, support = 0, redemptions = 0, streaks = 0;
	for (const FeedEvent &e : events_) {
		if (e.kind == QLatin1String("giftbomb"))
			gifted += qMax<qint64>(e.amount.toLongLong(), e.recipients.size());
		else if (e.kind == QLatin1String("points"))
			++redemptions;
		else if (e.kind == QLatin1String("streak"))
			++streaks;
		else if (group(e) == QLatin1String("support"))
			++support;
	}
	const QString num = theme::mapColor(QStringLiteral("#ffffff"));
	const auto set = [&num](QLabel *l, const char *icon, qint64 n, const char *what) {
		l->setText(QStringLiteral("<span style='font-size:11pt;font-weight:800;color:%1'>%2 %3</span><br>"
					  "<span style='color:#8b93a3;font-size:7.5pt'>%4</span>")
				   .arg(num, QString::fromUtf8(icon), fmtCount(n), QString::fromUtf8(what)));
	};
	set(tileGifted_, "🎁", gifted, "gifted");
	set(tileSupport_, "💎", support, "support");
	set(tilePoints_, "🎯", redemptions, "redeemed");
	set(tileStreaks_, "🔥", streaks, "streaks");
}

// ── Right-click ─────────────────────────────────────────────────

void GfxEventsPanel::showMenu(const QPoint &pos)
{
	if (placeholder_)
		return;
	const int block = view_->cursorForPosition(pos).block().blockNumber();
	if (block < 0 || block >= shownSeq_.size())
		return;
	const qint64 seq = shownSeq_.at(block);
	const FeedEvent *e = eventBySeq(seq);
	if (!e)
		return;
	const QString who = e->user;
	const QStringList names = e->recipients;
	const QString line = view_->document()->findBlockByNumber(block).text().simplified();
	QMenu menu(this);
	if (!who.isEmpty())
		connect(menu.addAction(QStringLiteral("📋   Copy “%1”").arg(who)), &QAction::triggered, this,
			[who] { QGuiApplication::clipboard()->setText(who); });
	if (!names.isEmpty()) {
		connect(menu.addAction(QStringLiteral("📋   Copy all %1 names").arg(names.size())), &QAction::triggered,
			this, [names] { QGuiApplication::clipboard()->setText(names.join(QStringLiteral(", "))); });
		connect(menu.addAction(QStringLiteral("💬   Copy a thank-you message")), &QAction::triggered, this,
			[names, who] {
				QString t = who.isEmpty() || who == QLatin1String("Anonymous")
						    ? QStringLiteral("Thank you for the gifted subs! ")
						    : QStringLiteral("Thank you @%1 for the gifted subs! ").arg(who);
				t += QStringLiteral("Welcome ");
				QStringList at;
				for (const QString &n : names)
					at << QStringLiteral("@") + n;
				t += at.join(QStringLiteral(" "));
				QGuiApplication::clipboard()->setText(t.left(500));
			});
		const bool open = settings().evAutoExpand != toggled_.contains(seq);
		connect(menu.addAction(open ? QStringLiteral("Hide recipients") : QStringLiteral("Show recipients")),
			&QAction::triggered, this, [this, seq] {
				if (toggled_.contains(seq))
					toggled_.remove(seq);
				else
					toggled_.insert(seq);
				rebuild();
			});
	}
	menu.addSeparator();
	connect(menu.addAction(QStringLiteral("📋   Copy this event")), &QAction::triggered, this,
		[line] { QGuiApplication::clipboard()->setText(line); });
	menu.exec(view_->viewport()->mapToGlobal(pos));
}

void GfxEventsPanel::clearEvents()
{
	events_.clear();
	toggled_.clear();
	pendingNew_ = 0;
	follow_ = true;
	rebuild();
}

void GfxEventsPanel::setChannelPointsStatus(const QString &status)
{
	if (status == cpStatus_)
		return;
	cpStatus_ = status;
	renderNotice();
}

void GfxEventsPanel::setShoutoutStatus(const QString &status)
{
	if (status == soStatus_)
		return;
	soStatus_ = status;
	renderNotice();
}

void GfxEventsPanel::setHypeTrainStatus(const QString &status)
{
	if (status == htStatus_)
		return;
	htStatus_ = status;
	renderNotice();
}

// One Reconnect bar for whatever needs a fresher Twitch login.
void GfxEventsPanel::renderNotice()
{
	QStringList parts;
	if (cpStatus_ == QLatin1String("needs_reconnect"))
		parts << QStringLiteral("Channel point redemptions");
	if (soStatus_ == QLatin1String("needs_reconnect"))
		parts << QStringLiteral("Twitch /shoutout");
	if (htStatus_ == QLatin1String("needs_reconnect"))
		parts << QStringLiteral("Hype Trains");
	QString what;
	if (parts.size() == 1)
		what = parts.first();
	else if (parts.size() > 1)
		what = parts.mid(0, parts.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" and ") +
		       parts.last();
	if (what.isEmpty()) {
		notice_->hide();
		return;
	}
	noticeLbl_->setText(what + QStringLiteral(" need one more Twitch permission. Click Reconnect and approve it "
						  "on Twitch, then they'll show here and in MultiChat."));
	notice_->show();
}

void GfxEventsPanel::applySettings()
{
	rebuild();
}
