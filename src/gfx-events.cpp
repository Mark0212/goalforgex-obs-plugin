/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-events.hpp"
#include "gfx-http.hpp"
#include "gfx-settings.hpp"
#include "gfx-theme.hpp"

#include <QAction>
#include <QBrush>
#include <QClipboard>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPoint>
#include <QPushButton>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

using namespace gfx;

namespace {

constexpr int kKeep = 300; // events kept in the tab
constexpr int kRoleSeq = Qt::UserRole + 1;
constexpr int kRoleUser = Qt::UserRole + 2;

const char *const kFilters[][2] = {
	{"all", "All"},         {"gifts", "Subs & gifts"}, {"points", "Points"},
	{"streaks", "Streaks"}, {"tiktok", "TikTok"},      {"youtube", "YouTube"},
};

QString fmtTime(qint64 ms)
{
	return QDateTime::fromMSecsSinceEpoch(ms > 0 ? ms : QDateTime::currentMSecsSinceEpoch())
		.toString(QStringLiteral("HH:mm"));
}

QString fmtCount(qint64 n)
{
	return QLocale(QLocale::English).toString(n);
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

	// Filter chips (wrap onto two rows in a narrow dock)
	auto *chipsTop = new QHBoxLayout();
	chipsTop->setSpacing(4);
	auto *chipsBottom = new QHBoxLayout();
	chipsBottom->setSpacing(4);
	int idx = 0;
	for (const auto &f : kFilters) {
		const QString key = QString::fromLatin1(f[0]);
		auto *b = new QPushButton(QString::fromUtf8(f[1]), this);
		b->setObjectName(QStringLiteral("gfxSeg"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		b->setChecked(key == filter_);
		connect(b, &QPushButton::clicked, this, [this, key] { setFilter(key); });
		(idx < 4 ? chipsTop : chipsBottom)->addWidget(b);
		chips_.push_back({key, b});
		++idx;
	}
	chipsTop->addStretch(1);
	auto *clearBtn = new QPushButton(QStringLiteral("Clear"), this);
	theme::setRole(clearBtn, "ghost");
	clearBtn->setToolTip(QStringLiteral("Clear this list (doesn't affect anything on stream)"));
	chipsBottom->addStretch(1);
	chipsBottom->addWidget(clearBtn);
	connect(clearBtn, &QPushButton::clicked, this, [this] { clearEvents(); });
	root->addLayout(chipsTop);
	root->addLayout(chipsBottom);

	// Channel points need a newer Twitch login on some accounts.
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
	connect(noticeBtn, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/account"))); });

	tally_ = new QLabel(this);
	tally_->setObjectName(QStringLiteral("gfxStatSummary"));
	tally_->setTextFormat(Qt::RichText);
	tally_->setToolTip(QStringLiteral("Counted since OBS connected to GoalForgeX"));
	root->addWidget(tally_);

	tree_ = new QTreeWidget(this);
	tree_->setObjectName(QStringLiteral("gfxEvents"));
	tree_->setColumnCount(2);
	tree_->setHeaderHidden(true);
	tree_->setRootIsDecorated(true);
	tree_->setIndentation(14);
	tree_->setUniformRowHeights(false);
	tree_->setWordWrap(true);
	tree_->setSelectionMode(QAbstractItemView::SingleSelection);
	tree_->setContextMenuPolicy(Qt::CustomContextMenu);
	tree_->header()->setStretchLastSection(false);
	tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	tree_->setMinimumHeight(160);
	root->addWidget(tree_, 1);
	connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &p) { showMenu(p); });

	empty_ = new QLabel(this);
	empty_->setObjectName(QStringLiteral("gfxMuted"));
	empty_->setAlignment(Qt::AlignCenter);
	empty_->setWordWrap(true);
	root->addWidget(empty_);
}

void GfxEventsPanel::setFilter(const QString &f)
{
	filter_ = f;
	for (const auto &c : chips_)
		c.second->setChecked(c.first == f);
	rebuild();
}

// Which chip an event belongs to ("" = only under All).
QString GfxEventsPanel::group(const FeedEvent &e) const
{
	if (e.kind == QLatin1String("giftbomb"))
		return QStringLiteral("gifts");
	if (e.kind == QLatin1String("alert")) {
		const QString t = e.alertType;
		if (t == QLatin1String("subscriber") || t == QLatin1String("resub") || t == QLatin1String("gifted"))
			return QStringLiteral("gifts");
		return QString();
	}
	if (e.kind == QLatin1String("points"))
		return QStringLiteral("points");
	if (e.kind == QLatin1String("streak"))
		return QStringLiteral("streaks");
	if (e.kind == QLatin1String("tiktok"))
		return QStringLiteral("tiktok");
	if (e.kind == QLatin1String("youtube"))
		return QStringLiteral("youtube");
	return QString();
}

bool GfxEventsPanel::shown(const FeedEvent &e) const
{
	const Settings &s = settings();
	const QString k = e.kind;
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
	else if (k == QLatin1String("alert"))
		// The gift-bomb row (with its recipients) replaces the plain "gift subs" alert line.
		on = s.evAlerts && !(seenGiftBomb_ && s.evGiftBombs && e.alertType == QLatin1String("gifted"));
	else
		on = s.evTimers; // timer / actiontimer / wheel
	if (!on)
		return false;
	return filter_ == QLatin1String("all") || group(e) == filter_;
}

QTreeWidgetItem *GfxEventsPanel::makeItem(const FeedEvent &e) const
{
	auto *item = new QTreeWidgetItem();
	item->setData(0, kRoleSeq, e.seq);
	item->setData(0, kRoleUser, e.user);
	item->setText(1, fmtTime(e.at));
	item->setForeground(1, QBrush(QColor(theme::mapColor(QStringLiteral("#6b7280")))));
	item->setTextAlignment(1, Qt::AlignRight | Qt::AlignTop);

	QString line = e.text;
	if (!e.by.isEmpty())
		line += QStringLiteral("  · ") + e.by;

	if (e.kind == QLatin1String("giftbomb")) {
		const int total = qMax(e.amount.toInt(), static_cast<int>(e.recipients.size()));
		line = QStringLiteral("🎁 %1 gifted %2 sub%3")
			       .arg(e.user.isEmpty() ? QStringLiteral("Anonymous") : e.user)
			       .arg(total)
			       .arg(total == 1 ? QString() : QStringLiteral("s"));
		if (e.platform == QLatin1String("kick"))
			line += QStringLiteral(" · Kick");
		item->setText(0, line);
		item->setToolTip(0, e.recipients.isEmpty()
					    ? line
					    : line + QStringLiteral("\n\n") + e.recipients.join(QStringLiteral(", ")));
		QFont f = item->font(0);
		f.setBold(true);
		item->setFont(0, f);
		item->setForeground(0, QBrush(QColor(theme::mapColor(QStringLiteral("#ffd27a")))));
		const QColor muted(theme::mapColor(QStringLiteral("#c9ced8")));
		for (const QString &r : e.recipients) {
			auto *child = new QTreeWidgetItem(item);
			child->setText(0, QStringLiteral("→ ") + r);
			child->setData(0, kRoleUser, r);
			child->setForeground(0, QBrush(muted));
		}
		const int missing = total - static_cast<int>(e.recipients.size());
		if (missing > 0) {
			auto *child = new QTreeWidgetItem(item);
			child->setText(0, e.recipients.isEmpty()
						  ? QStringLiteral("Recipients weren't reported for this one")
						  : QStringLiteral("+%1 more not reported").arg(missing));
			child->setForeground(0, QBrush(QColor(theme::mapColor(QStringLiteral("#6b7280")))));
		}
		return item;
	}

	QString accent;
	if (e.kind == QLatin1String("points"))
		accent = QStringLiteral("#c4b5fd");
	else if (e.kind == QLatin1String("streak"))
		accent = QStringLiteral("#fdba74");
	else if (e.kind == QLatin1String("tiktok"))
		accent = QStringLiteral("#67e8f9");
	else if (e.kind == QLatin1String("youtube"))
		accent = QStringLiteral("#fca5a5");
	item->setText(0, line);
	item->setToolTip(0, line);
	if (!accent.isEmpty())
		item->setForeground(0, QBrush(QColor(accent)));
	return item;
}

int GfxEventsPanel::addEvents(const QList<FeedEvent> &events)
{
	int visible = 0;
	bool needRebuild = false;
	for (const FeedEvent &e : events) {
		if (e.kind == QLatin1String("giftbomb") && !seenGiftBomb_) {
			seenGiftBomb_ = true;
			needRebuild = true; // earlier "gift subs" alert lines now hide
		}
		events_.push_back(e);
		if (shown(e)) {
			++visible;
			if (!needRebuild) {
				QTreeWidgetItem *item = makeItem(e);
				tree_->insertTopLevelItem(0, item);
				if (settings().evAutoExpand && item->childCount() > 0)
					item->setExpanded(true);
			}
		}
	}
	while (events_.size() > kKeep)
		events_.removeFirst();
	if (needRebuild) {
		rebuild();
		return visible;
	}
	while (tree_->topLevelItemCount() > kKeep)
		delete tree_->takeTopLevelItem(tree_->topLevelItemCount() - 1);
	renderTally();
	empty_->setVisible(tree_->topLevelItemCount() == 0);
	tree_->setVisible(tree_->topLevelItemCount() > 0);
	return visible;
}

void GfxEventsPanel::rebuild()
{
	tree_->clear();
	for (const FeedEvent &e : events_) {
		if (!shown(e))
			continue;
		QTreeWidgetItem *item = makeItem(e);
		tree_->insertTopLevelItem(0, item);
		if (settings().evAutoExpand && item->childCount() > 0)
			item->setExpanded(true);
	}
	const bool any = tree_->topLevelItemCount() > 0;
	tree_->setVisible(any);
	empty_->setVisible(!any);
	empty_->setText(events_.isEmpty()
				? QStringLiteral("Nothing yet — gift bombs, channel-point redemptions, watch streaks, "
						 "subs and TikTok / YouTube support show up here as they happen.")
				: QStringLiteral("Nothing matches this filter yet."));
	renderTally();
}

void GfxEventsPanel::renderTally()
{
	qint64 gifted = 0, redemptions = 0, streaks = 0, tiktokGifts = 0, superChats = 0;
	for (const FeedEvent &e : events_) {
		if (e.kind == QLatin1String("giftbomb"))
			gifted += qMax<qint64>(e.amount.toLongLong(), e.recipients.size());
		else if (e.kind == QLatin1String("points"))
			++redemptions;
		else if (e.kind == QLatin1String("streak"))
			++streaks;
		else if (e.kind == QLatin1String("tiktok") && e.sub == QLatin1String("gift"))
			++tiktokGifts;
		else if (e.kind == QLatin1String("youtube") &&
			 (e.sub == QLatin1String("superchat") || e.sub == QLatin1String("supersticker")))
			++superChats;
	}
	const QString b = theme::mapColor(QStringLiteral("#e8eaf0"));
	QStringList parts;
	const auto add = [&parts, &b](qint64 n, const char *icon, const char *one, const char *many) {
		if (n > 0)
			parts << QStringLiteral("%1 <b style='color:%2'>%3</b> %4")
					 .arg(QString::fromUtf8(icon), b, fmtCount(n),
					      QString::fromUtf8(n == 1 ? one : many));
	};
	add(gifted, "🎁", "sub gifted", "subs gifted");
	add(redemptions, "🎯", "redemption", "redemptions");
	add(streaks, "🔥", "streak", "streaks");
	add(tiktokGifts, "🎵", "TikTok gift", "TikTok gifts");
	add(superChats, "▶️", "Super Chat", "Super Chats");
	tally_->setVisible(!parts.isEmpty());
	tally_->setText(parts.join(QStringLiteral("&nbsp;&nbsp;·&nbsp;&nbsp;")));
}

void GfxEventsPanel::showMenu(const QPoint &pos)
{
	QTreeWidgetItem *item = tree_->itemAt(pos);
	if (!item)
		return;
	QTreeWidgetItem *top = item->parent() ? item->parent() : item;
	QMenu menu(this);
	const QString who = item->data(0, kRoleUser).toString();
	if (!who.isEmpty())
		connect(menu.addAction(QStringLiteral("📋   Copy “%1”").arg(who)), &QAction::triggered, this,
			[who] { QGuiApplication::clipboard()->setText(who); });
	if (top->childCount() > 0) {
		QStringList names;
		for (int i = 0; i < top->childCount(); ++i) {
			const QString n = top->child(i)->data(0, kRoleUser).toString();
			if (!n.isEmpty())
				names << n;
		}
		if (!names.isEmpty()) {
			connect(menu.addAction(QStringLiteral("📋   Copy all %1 names").arg(names.size())),
				&QAction::triggered, this,
				[names] { QGuiApplication::clipboard()->setText(names.join(QStringLiteral(", "))); });
			const QString gifter = top->data(0, kRoleUser).toString();
			connect(menu.addAction(QStringLiteral("💬   Copy a thank-you message")), &QAction::triggered,
				this, [names, gifter] {
					QString t = gifter.isEmpty() || gifter == QLatin1String("Anonymous")
							    ? QStringLiteral("Thank you for the gifted subs! ")
							    : QStringLiteral("Thank you @%1 for the gifted subs! ")
								      .arg(gifter);
					t += QStringLiteral("Welcome ");
					QStringList at;
					for (const QString &n : names)
						at << QStringLiteral("@") + n;
					t += at.join(QStringLiteral(" "));
					QGuiApplication::clipboard()->setText(t.left(500));
				});
			menu.addSeparator();
			connect(menu.addAction(top->isExpanded() ? QStringLiteral("Hide recipients")
								 : QStringLiteral("Show recipients")),
				&QAction::triggered, this, [top] { top->setExpanded(!top->isExpanded()); });
		}
	}
	connect(menu.addAction(QStringLiteral("📋   Copy line")), &QAction::triggered, this,
		[top] { QGuiApplication::clipboard()->setText(top->text(0)); });
	menu.exec(tree_->viewport()->mapToGlobal(pos));
}

void GfxEventsPanel::clearEvents()
{
	events_.clear();
	rebuild();
}

void GfxEventsPanel::setChannelPointsStatus(const QString &status)
{
	if (status == cpStatus_)
		return;
	cpStatus_ = status;
	if (status == QLatin1String("needs_reconnect")) {
		noticeLbl_->setText(QStringLiteral(
			"Channel point redemptions need a fresh Twitch login — reconnect Twitch on your GoalForgeX account page, then they'll show here."));
		notice_->show();
	} else {
		notice_->hide();
	}
}

void GfxEventsPanel::applySettings()
{
	rebuild();
}
