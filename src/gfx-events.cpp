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

#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
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
#include <QPixmap>
#include <QRectF>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTextDocumentFragment>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

using namespace gfx;

namespace {

constexpr int kKeep = 300; // events kept in the list

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

// The tile on the left of a card: the event's emoji on a wash of its colour,
// with the platform's icon in the corner.
QPixmap eventTile(const QString &emoji, const QColor &color, const QString &platform, int px, qreal dpr)
{
	QPixmap pm(qMax(1, qRound(px * dpr)), qMax(1, qRound(px * dpr)));
	pm.setDevicePixelRatio(dpr);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);
	const QRectF r(0, 0, px, px);
	const QColor base(theme::mapColor(QStringLiteral("#101217")));
	p.setPen(Qt::NoPen);
	p.setBrush(notice::mix(base, color, 0.26));
	p.drawRoundedRect(r, px * 0.3, px * 0.3);
	QFont f;
	f.setPixelSize(qMax(8, qRound(px * 0.48)));
	p.setFont(f);
	p.setPen(QColor(Qt::white));
	p.drawText(r, Qt::AlignCenter, emoji);
	if (!platform.isEmpty()) {
		const int ip = qMax(8, qRound(px * 0.4));
		const QRectF ir(px - ip - 1, px - ip - 1, ip, ip);
		p.setBrush(base);
		p.drawEllipse(ir.adjusted(-1.5, -1.5, 1.5, 1.5));
		p.drawImage(ir, notice::platformIcon(platform, ip, dpr));
	}
	p.end();
	return pm;
}

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

	// ── The feed: one card widget per event, newest at the top ──
	scroll_ = new QScrollArea(this);
	scroll_->setObjectName(QStringLiteral("gfxEvScroll"));
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll_->setMinimumHeight(160);
	list_ = new QWidget(scroll_);
	list_->setObjectName(QStringLiteral("gfxEvList"));
	listLay_ = new QVBoxLayout(list_);
	listLay_->setContentsMargins(0, 0, 4, 4);
	listLay_->setSpacing(6);
	placeholderLbl_ = new QLabel(list_);
	placeholderLbl_->setObjectName(QStringLiteral("gfxMuted"));
	placeholderLbl_->setWordWrap(true);
	placeholderLbl_->setAlignment(Qt::AlignCenter);
	placeholderLbl_->setContentsMargins(12, 24, 12, 12);
	listLay_->addWidget(placeholderLbl_);
	listLay_->addStretch(1);
	scroll_->setWidget(list_);
	root->addWidget(scroll_, 1);
	QScrollBar *vsb = scroll_->verticalScrollBar();
	// Newest events are at the TOP. follow_ = pinned to the top; scrolled down
	// reading older ones, the view holds its distance from the bottom, so cards
	// landing above never move what the streamer is reading.
	connect(vsb, &QScrollBar::rangeChanged, this, [this](int min, int max) {
		++programmatic_;
		scroll_->verticalScrollBar()->setValue(follow_ ? min : qMax(min, max - fromBottom_));
		--programmatic_;
	});
	connect(vsb, &QScrollBar::valueChanged, this, [this](int v) {
		if (programmatic_)
			return;
		QScrollBar *sb = scroll_->verticalScrollBar();
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
	root->insertWidget(root->indexOf(scroll_), jump_);
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

QWidget *GfxEventsPanel::makeCard(const FeedEvent &e)
{
	const Settings &s = settings();
	const Card c = describe(e);
	const notice::Look lk = notice::look(c.type);
	const QString icon = c.pillIcon.isEmpty() ? lk.icon : c.pillIcon;
	const QString label = c.pillLabel.isEmpty() ? lk.label : c.pillLabel;
	const QColor color(c.pillColor.isEmpty() ? lk.color : c.pillColor);
	const bool compact = s.chatCompact;
	const qint64 seq = e.seq;

	auto *card = new QFrame(list_);
	card->setObjectName(QStringLiteral("gfxEvCard"));
	card->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(card, &QWidget::customContextMenuRequested, this,
		[this, card, seq](const QPoint &p) { showMenu(seq, card->mapToGlobal(p)); });
	auto *h = new QHBoxLayout(card);
	h->setContentsMargins(10, compact ? 6 : 9, 10, compact ? 6 : 9);
	h->setSpacing(10);
	const int tilePx = compact ? 28 : 36;
	auto *tile = new QLabel(card);
	tile->setFixedSize(tilePx, tilePx);
	tile->setPixmap(eventTile(icon, color, s.chatIcons ? c.platform : QString(), tilePx, devicePixelRatioF()));
	h->addWidget(tile, 0, Qt::AlignTop);

	auto *col = new QVBoxLayout();
	col->setContentsMargins(0, 0, 0, 0);
	col->setSpacing(compact ? 1 : 3);
	auto *top = new QHBoxLayout();
	top->setSpacing(6);
	auto *what = new QLabel(
		QStringLiteral("<span style='color:%1'>%2</span>").arg(color.name(), label.toHtmlEscaped()), card);
	what->setObjectName(QStringLiteral("gfxEvLabel"));
	what->setTextFormat(Qt::RichText);
	auto *when = new QLabel(fmtTime(e.at), card);
	when->setObjectName(QStringLiteral("gfxEvTime"));
	top->addWidget(what);
	top->addStretch(1);
	top->addWidget(when);
	col->addLayout(top);
	const auto text = [card, col](const QString &html, const char *name) {
		auto *l = new QLabel(html, card);
		l->setObjectName(QString::fromLatin1(name));
		l->setTextFormat(Qt::RichText);
		l->setWordWrap(true);
		l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		col->addWidget(l);
		return l;
	};
	text(c.head, "gfxEvHead");
	if (!c.message.trimmed().isEmpty()) {
		QString msg = c.message;
		msg.replace(QLatin1Char('\n'), QLatin1Char(' '));
		text(QStringLiteral("“%1”").arg(msg.toHtmlEscaped()), "gfxEvMsg");
	}
	// Gift bomb: who got one — open by default (Settings → Events), click to show / hide.
	if (c.bomb) {
		const bool open = s.evAutoExpand != toggled_.contains(seq);
		QString line;
		if (c.names.isEmpty()) {
			line = e.partial ? QStringLiteral("Handing out gifts…")
					 : QStringLiteral("Recipients weren't reported for this one");
		} else if (!open) {
			line = QStringLiteral(
				       "<a href='toggle' style='color:#7aa2ff;text-decoration:none;'>▸ Show who got one "
				       "(%1)</a>")
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
				"&nbsp;&nbsp;<a href='toggle' style='color:#7aa2ff;text-decoration:none;'>hide</a>");
		}
		QLabel *names = text(line, "gfxEvNames");
		connect(names, &QLabel::linkActivated, this, [this, seq](const QString &) {
			if (toggled_.contains(seq))
				toggled_.remove(seq);
			else
				toggled_.insert(seq);
			rebuild();
		});
	}
	h->addLayout(col, 1);
	cards_.insert(seq, card);
	return card;
}

void GfxEventsPanel::clearCards()
{
	// Keep the placeholder (first) and the stretch (last).
	while (listLay_->count() > 2) {
		QLayoutItem *it = listLay_->takeAt(1);
		if (QWidget *w = it->widget()) {
			w->hide();
			w->deleteLater();
		}
		delete it;
	}
	shownCount_ = 0;
	cards_.clear();
}

void GfxEventsPanel::showPlaceholder()
{
	placeholderLbl_->setText(
		events_.isEmpty()
			? QStringLiteral(
				  "Nothing yet — gift bombs, channel-point redemptions, watch streaks, subs, raids "
				  "and TikTok / YouTube support show up here the moment they happen.")
			: QStringLiteral("Nothing matches this filter yet."));
	placeholderLbl_->setVisible(shownCount_ == 0);
}

void GfxEventsPanel::rebuild()
{
	QScrollBar *sb = scroll_->verticalScrollBar();
	++programmatic_;
	list_->setUpdatesEnabled(false);
	clearCards();
	// Newest first: walk the (oldest → newest) list backwards.
	for (qsizetype i = events_.size() - 1; i >= 0; --i)
		if (shown(events_.at(i))) {
			listLay_->insertWidget(listLay_->count() - 1, makeCard(events_.at(i)));
			++shownCount_;
		}
	showPlaceholder();
	list_->setUpdatesEnabled(true);
	sb->setValue(follow_ ? sb->minimum() : qMax(sb->minimum(), sb->maximum() - fromBottom_));
	--programmatic_;
	renderTally();
	updateJump();
}

int GfxEventsPanel::addEvents(const QList<FeedEvent> &events)
{
	int visible = 0;
	bool needRebuild = false;
	QList<qint64> fresh, updated;
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
				updated.push_back(upd.seq);
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
	// Over the cap: the oldest event (and its card, at the bottom) goes.
	while (events_.size() > kKeep) {
		if (QWidget *w = cards_.take(events_.first().seq)) {
			listLay_->removeWidget(w);
			w->hide();
			w->deleteLater();
			--shownCount_;
		}
		toggled_.remove(events_.first().seq);
		events_.removeFirst();
	}
	if (needRebuild) {
		if (!follow_)
			pendingNew_ += visible;
		rebuild();
		return visible;
	}
	// A gift bomb filling in: swap just its card, where it is.
	for (const qint64 seq : updated) {
		QWidget *old = cards_.value(seq);
		const FeedEvent *e = eventBySeq(seq);
		const int idx = old ? listLay_->indexOf(old) : -1;
		if (!e || idx < 0)
			continue;
		listLay_->insertWidget(idx, makeCard(*e));
		listLay_->removeWidget(old);
		old->hide();
		old->deleteLater();
	}
	// New events go in at the top, oldest of the batch first so the newest ends up on top.
	for (const qint64 seq : fresh)
		if (const FeedEvent *e = eventBySeq(seq)) {
			listLay_->insertWidget(1, makeCard(*e));
			++shownCount_;
		}
	if (!follow_)
		pendingNew_ += visible;
	else
		scrollToNewest();
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
	scroll_->verticalScrollBar()->setValue(scroll_->verticalScrollBar()->minimum());
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

void GfxEventsPanel::showMenu(qint64 seq, const QPoint &globalPos)
{
	const FeedEvent *e = eventBySeq(seq);
	if (!e)
		return;
	const QString who = e->user;
	const QStringList names = e->recipients;
	const Card card = describe(*e);
	QString line = QTextDocumentFragment::fromHtml(card.head).toPlainText().simplified();
	if (!card.message.trimmed().isEmpty())
		line += QStringLiteral(" — ") + card.message.simplified();
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
	menu.exec(globalPos);
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
