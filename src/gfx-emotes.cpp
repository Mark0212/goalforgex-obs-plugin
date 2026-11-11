/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-emotes.hpp"
#include "gfx-images.hpp"
#include "gfx-settings.hpp"
#include "gfx-theme.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using gfx::RemoteImages;

namespace {

constexpr int kIcon = 30;
constexpr int kCell = 42;
constexpr int kMaxShown = 320;

QString shortTitle(const QString &id, const QString &title)
{
	if (id == QLatin1String("twitch-channel"))
		return QStringLiteral("🟣 Mine");
	if (id == QLatin1String("twitch-global"))
		return QStringLiteral("Twitch");
	if (id == QLatin1String("channel-3p"))
		return QStringLiteral("Channel 7TV·BTTV");
	if (id == QLatin1String("kick-7tv"))
		return QStringLiteral("Kick 7TV");
	if (id == QLatin1String("global-3p"))
		return QStringLiteral("Global 7TV·BTTV");
	return title;
}

QIcon blankIcon()
{
	static QIcon icon = [] {
		QPixmap pm(kIcon, kIcon);
		pm.fill(Qt::transparent);
		return QIcon(pm);
	}();
	return icon;
}

QString col(const char *hex)
{
	return gfx::theme::mapColor(QString::fromLatin1(hex));
}

} // namespace

GfxEmotePicker::GfxEmotePicker(QWidget *parent) : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
	setObjectName(QStringLiteral("gfxEmotePicker"));
	setAttribute(Qt::WA_StyledBackground, true);
	resize(372, 360);
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(10, 10, 10, 8);
	root->setSpacing(8);

	auto *top = new QHBoxLayout();
	top->setSpacing(6);
	search_ = new QLineEdit(this);
	search_->setObjectName(QStringLiteral("gfxEmoteSearch"));
	search_->setPlaceholderText(QStringLiteral("🔍  Search emotes…"));
	search_->setClearButtonEnabled(true);
	auto *refresh = new QToolButton(this);
	refresh->setObjectName(QStringLiteral("gfxEmoteMini"));
	refresh->setText(QStringLiteral("↻"));
	refresh->setToolTip(QStringLiteral("Load your emotes again"));
	refresh->setCursor(Qt::PointingHandCursor);
	top->addWidget(search_, 1);
	top->addWidget(refresh);
	root->addLayout(top);

	chips_ = new QWidget(this);
	auto *cl = new QHBoxLayout(chips_);
	cl->setContentsMargins(0, 0, 0, 0);
	cl->setSpacing(4);
	root->addWidget(chips_);

	grid_ = new QListWidget(this);
	grid_->setObjectName(QStringLiteral("gfxEmoteGrid"));
	grid_->setViewMode(QListView::IconMode);
	grid_->setIconSize(QSize(kIcon, kIcon));
	grid_->setGridSize(QSize(kCell, kCell));
	grid_->setUniformItemSizes(true);
	grid_->setMovement(QListView::Static);
	grid_->setResizeMode(QListView::Adjust);
	grid_->setWrapping(true);
	grid_->setSpacing(0);
	grid_->setMouseTracking(true);
	grid_->setSelectionMode(QAbstractItemView::SingleSelection);
	grid_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	grid_->setCursor(Qt::PointingHandCursor);
	root->addWidget(grid_, 1);

	empty_ = new QLabel(this);
	empty_->setObjectName(QStringLiteral("gfxEmoteEmpty"));
	empty_->setAlignment(Qt::AlignCenter);
	empty_->setWordWrap(true);
	empty_->hide();
	root->addWidget(empty_, 1);

	hover_ = new QLabel(QStringLiteral("Click to add · Shift + click to add several"), this);
	hover_->setObjectName(QStringLiteral("gfxEmoteHover"));
	hover_->setTextFormat(Qt::RichText);
	root->addWidget(hover_);

	const QString accent = gfx::theme::accentColor();
	setStyleSheet(
		QStringLiteral(
			"QFrame#gfxEmotePicker { background: %1; border: 1px solid %2; border-radius: 14px; }"
			"QLineEdit#gfxEmoteSearch { background: %3; color: %4; border: 1px solid %2; border-radius: 10px;"
			"  padding: 6px 9px; }"
			"QLineEdit#gfxEmoteSearch:focus { border: 1px solid %5; }"
			"QToolButton#gfxEmoteMini { color: %6; background: transparent; border: none; border-radius: 8px;"
			"  padding: 3px 7px; font-size: 12pt; }"
			"QToolButton#gfxEmoteMini:hover { color: %4; background: %3; }"
			"QPushButton#gfxEmoteChip { color: %6; background: %3; border: 1px solid %2; border-radius: 9px;"
			"  padding: 3px 8px; font-size: 8pt; font-weight: 600; }"
			"QPushButton#gfxEmoteChip:checked { color: %4; border: 1px solid %5; }"
			"QListWidget#gfxEmoteGrid { background: transparent; border: none; outline: 0; }"
			"QListWidget#gfxEmoteGrid::item { border-radius: 8px; }"
			"QListWidget#gfxEmoteGrid::item:hover { background: %3; }"
			"QListWidget#gfxEmoteGrid::item:selected { background: %3; border: 1px solid %5; }"
			"QLabel#gfxEmoteHover { color: %6; font-size: 8pt; background: transparent; }"
			"QLabel#gfxEmoteEmpty { color: %6; background: transparent; padding: 20px; }")
			.arg(col("#13161c"), col("#2a2f3a"), col("#1c2029"), col("#e8eaf0"), accent, col("#9aa1b0")));

	connect(search_, &QLineEdit::textChanged, this, [this] { populate(); });
	connect(refresh, &QToolButton::clicked, this, [this] {
		if (onRefresh)
			onRefresh();
	});
	connect(grid_, &QListWidget::itemEntered, this, [this](QListWidgetItem *item) {
		const QString note = item->data(Qt::UserRole + 2).toString();
		hover_->setText(QStringLiteral("<b style='color:%1'>%2</b>%3")
					.arg(col("#e8eaf0"), item->data(Qt::UserRole).toString().toHtmlEscaped(),
					     note.isEmpty() ? QString()
							    : QStringLiteral("&nbsp;&nbsp;<span>%1</span>")
								      .arg(note.toHtmlEscaped())));
	});
	connect(grid_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
		const QString name = item->data(Qt::UserRole).toString();
		noteUsed(name, item->data(Qt::UserRole + 1).toString());
		if (onPick)
			onPick(name);
		if (!(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier))
			hide();
	});
	connect(grid_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { loadVisibleIcons(); });
	RemoteImages::instance().subscribe(this, [this](const QString &url) {
		const QList<QListWidgetItem *> items = waiting_.values(url);
		if (items.isEmpty())
			return;
		const QImage img = RemoteImages::instance().get(url);
		if (img.isNull())
			return;
		const QIcon icon(QPixmap::fromImage(img));
		for (QListWidgetItem *it : items)
			it->setIcon(icon);
		waiting_.remove(url);
	});
}

void GfxEmotePicker::setData(const QJsonObject &response)
{
	groups_.clear();
	const QJsonArray arr = response.value(QStringLiteral("groups")).toArray();
	for (qsizetype i = 0; i < arr.size(); ++i) {
		const QJsonObject g = arr.at(i).toObject();
		Group group;
		group.id = g.value(QStringLiteral("id")).toString().left(40);
		group.title = g.value(QStringLiteral("title")).toString().left(60);
		const QJsonArray es = g.value(QStringLiteral("emotes")).toArray();
		for (qsizetype j = 0; j < es.size() && j < 2000; ++j) {
			const QJsonObject e = es.at(j).toObject();
			Emote em;
			em.name = e.value(QStringLiteral("name")).toString().left(60);
			em.url = e.value(QStringLiteral("url")).toString();
			em.note = e.value(QStringLiteral("note")).toString().left(30);
			em.source = e.value(QStringLiteral("source")).toString().left(10);
			if (!em.name.isEmpty() && !em.name.contains(QLatin1Char(' ')) && RemoteImages::allowed(em.url))
				group.emotes << em;
		}
		if (!group.id.isEmpty() && !group.emotes.isEmpty())
			groups_ << group;
	}
	buildChips();
	if (current_ != QLatin1String("recent")) {
		bool still = false;
		for (const Group &g : groups_)
			still = still || g.id == current_;
		if (!still)
			current_ = QStringLiteral("recent");
	}
	if (current_ == QLatin1String("recent") && recent().isEmpty() && !groups_.isEmpty())
		current_ = groups_.first().id;
	showGroup(current_);
}

void GfxEmotePicker::buildChips()
{
	for (QPushButton *b : chipBtns_)
		b->deleteLater();
	chipBtns_.clear();
	auto *l = static_cast<QHBoxLayout *>(chips_->layout());
	while (l->count() > 0)
		delete l->takeAt(0);
	const auto chip = [this, l](const QString &id, const QString &label, const QString &tip) {
		auto *b = new QPushButton(label, chips_);
		b->setObjectName(QStringLiteral("gfxEmoteChip"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		b->setToolTip(tip);
		b->setProperty("gfxGroup", id);
		connect(b, &QPushButton::clicked, this, [this, id] {
			search_->clear();
			showGroup(id);
		});
		l->addWidget(b);
		chipBtns_ << b;
	};
	chip(QStringLiteral("recent"), QStringLiteral("⭐"), QStringLiteral("Recently used"));
	for (const Group &g : groups_)
		chip(g.id, shortTitle(g.id, g.title), g.title);
	l->addStretch(1);
}

void GfxEmotePicker::showGroup(const QString &id)
{
	current_ = id;
	for (QPushButton *b : chipBtns_)
		b->setChecked(b->property("gfxGroup").toString() == id);
	populate();
}

QList<GfxEmotePicker::Emote> GfxEmotePicker::recent() const
{
	QList<Emote> out;
	for (const QString &r : gfx::settings().chatRecentEmotes) {
		const qsizetype tab = r.indexOf(QLatin1Char('\t'));
		if (tab <= 0)
			continue;
		Emote e;
		e.name = r.left(tab);
		e.url = r.mid(tab + 1);
		if (RemoteImages::allowed(e.url))
			out << e;
	}
	return out;
}

void GfxEmotePicker::noteUsed(const QString &name, const QString &url)
{
	if (name.isEmpty() || !RemoteImages::allowed(url))
		return;
	QStringList &list = gfx::settings().chatRecentEmotes;
	const QString entry = name + QLatin1Char('\t') + url;
	list.removeAll(entry);
	list.prepend(entry);
	while (list.size() > 30)
		list.removeLast();
	gfx::saveSettings();
}

void GfxEmotePicker::populate()
{
	grid_->clear();
	waiting_.clear();
	const QString q = search_->text().trimmed();
	QList<Emote> list;
	if (!q.isEmpty()) {
		// Every group; names that start with it first.
		QSet<QString> seen;
		QList<Emote> starts, has;
		for (const Group &g : groups_)
			for (const Emote &e : g.emotes) {
				if (seen.contains(e.name))
					continue;
				if (e.name.startsWith(q, Qt::CaseInsensitive)) {
					starts << e;
					seen.insert(e.name);
				} else if (e.name.contains(q, Qt::CaseInsensitive)) {
					has << e;
					seen.insert(e.name);
				}
			}
		list = starts + has;
	} else if (current_ == QLatin1String("recent")) {
		list = recent();
	} else {
		for (const Group &g : groups_)
			if (g.id == current_)
				list = g.emotes;
	}
	const qsizetype total = list.size();
	if (list.size() > kMaxShown)
		list = list.mid(0, kMaxShown);
	for (const Emote &e : list) {
		auto *item = new QListWidgetItem(blankIcon(), QString(), grid_);
		item->setData(Qt::UserRole, e.name);
		item->setData(Qt::UserRole + 1, e.url);
		item->setData(Qt::UserRole + 2, e.note);
		item->setToolTip(e.note.isEmpty() ? e.name : e.name + QStringLiteral("  ·  ") + e.note);
		item->setSizeHint(QSize(kCell, kCell));
	}
	const bool none = list.isEmpty();
	grid_->setVisible(!none);
	empty_->setVisible(none);
	if (none)
		empty_->setText(
			!q.isEmpty()                          ? QStringLiteral("No emote called “%1”.").arg(q)
			: current_ == QLatin1String("recent") ? QStringLiteral("Emotes you use show up here.")
			: groups_.isEmpty()
				? QStringLiteral("Loading your emotes…\nConnect Twitch or Kick on goalforgex.com "
						 "if this stays empty.")
				: QStringLiteral("Nothing here yet."));
	hover_->setText(total > kMaxShown
				? QStringLiteral("Showing %1 of %2 — search to find the rest").arg(kMaxShown).arg(total)
				: QStringLiteral("Click to add · Shift + click to add several"));
	grid_->scrollToTop();
	QTimer::singleShot(0, this, [this] { loadVisibleIcons(); });
}

void GfxEmotePicker::loadVisibleIcons()
{
	// Only what's on screen (plus a row below) loads, so a 1,500-emote set
	// doesn't download all at once.
	const QRect view = grid_->viewport()->rect().adjusted(0, 0, 0, kCell * 2);
	for (int i = 0; i < grid_->count(); ++i) {
		QListWidgetItem *item = grid_->item(i);
		if (!grid_->visualItemRect(item).intersects(view))
			continue;
		const QString url = item->data(Qt::UserRole + 1).toString();
		if (item->data(Qt::UserRole + 3).toBool())
			continue;
		const QImage img = RemoteImages::instance().get(url);
		if (!img.isNull()) {
			item->setIcon(QIcon(QPixmap::fromImage(img)));
			item->setData(Qt::UserRole + 3, true);
		} else if (!RemoteImages::instance().failed(url) && !waiting_.contains(url, item)) {
			waiting_.insert(url, item);
		}
	}
}

QStringList GfxEmotePicker::complete(const QString &prefix, int max) const
{
	QStringList out;
	if (prefix.size() < 2)
		return out;
	QSet<QString> seen;
	const auto take = [&](const QString &name) {
		if (out.size() >= max || seen.contains(name) || !name.startsWith(prefix, Qt::CaseInsensitive))
			return;
		seen.insert(name);
		out << name;
	};
	for (const Emote &e : recent())
		take(e.name);
	for (const Group &g : groups_)
		for (const Emote &e : g.emotes)
			take(e.name);
	return out;
}

void GfxEmotePicker::open(QWidget *anchor)
{
	if (!anchor)
		return;
	const QPoint br = anchor->mapToGlobal(QPoint(anchor->width(), 0));
	QPoint pos(br.x() - width(), br.y() - height() - 6);
	if (QScreen *screen = anchor->screen()) {
		const QRect area = screen->availableGeometry();
		if (pos.y() < area.top())
			pos.setY(anchor->mapToGlobal(QPoint(0, anchor->height())).y() + 6);
		pos.setX(qBound(area.left(), pos.x(), area.right() - width()));
	}
	move(pos);
	show();
	raise();
	search_->setFocus();
	search_->selectAll();
	populate();
}

void GfxEmotePicker::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
		// Enter in the search box picks the first match.
		QListWidgetItem *item = grid_->currentItem() ? grid_->currentItem() : grid_->item(0);
		if (item) {
			const QString name = item->data(Qt::UserRole).toString();
			noteUsed(name, item->data(Qt::UserRole + 1).toString());
			if (onPick)
				onPick(name);
			hide();
		}
		return;
	}
	QFrame::keyPressEvent(event);
}
