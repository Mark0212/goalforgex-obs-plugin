/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-viewers.hpp"
#include "gfx-flow.hpp"
#include "gfx-notice.hpp"
#include "gfx-settings.hpp"
#include "gfx-theme.hpp"

#include <QAction>
#include <QColor>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QCursor>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

using namespace gfx;

namespace {

constexpr qint64 kChattingMs = 10 * 60 * 1000;
constexpr qint64 kArrivedMs = 2 * 60 * 1000;
constexpr qint64 kRecentMs = 10 * 60 * 1000;

constexpr int kHeaderRole = Qt::UserRole + 1; // section key; empty on a person's row
constexpr int kPlatformRole = Qt::UserRole + 2;
constexpr int kLoginRole = Qt::UserRole + 3;
constexpr int kColorRole = Qt::UserRole + 4;
constexpr int kBadgeRole = Qt::UserRole + 5;        // host | mod | vip | sub | empty
constexpr int kActivityRole = Qt::UserRole + 6;     // right-hand text
constexpr int kActivityKindRole = Qt::UserRole + 7; // chatting | arrived | empty
constexpr int kCollapsedRole = Qt::UserRole + 8;
constexpr int kRoleColorRole = Qt::UserRole + 9; // Settings → Chat style → Name colours for their role

QString readable(const QString &hex)
{
	QColor c(hex);
	if (!c.isValid())
		return QString();
	int h = 0, s = 0, l = 0;
	c.getHsl(&h, &s, &l);
	if (l < 130)
		c.setHsl(h, s, 130);
	return c.name();
}

QString shortAgo(qint64 ms)
{
	const qint64 sec = qMax<qint64>(0, ms / 1000);
	if (sec < 60)
		return QStringLiteral("%1s").arg(sec);
	if (sec < 3600)
		return QStringLiteral("%1 min").arg(sec / 60);
	return QStringLiteral("%1 h").arg(sec / 3600);
}

// Rows are painted by hand: a big channel can list thousands of people, far
// too many for one widget per row.
class RowDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		const int fh = QFontMetrics(option.font).height();
		if (!index.data(kHeaderRole).toString().isEmpty())
			return QSize(0, fh + 12);
		return QSize(0, fh + (settings().vwCompact ? 3 : 9));
	}

	void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		p->save();
		p->setRenderHint(QPainter::Antialiasing, true);
		p->setRenderHint(QPainter::SmoothPixmapTransform, true);
		if (!index.data(kHeaderRole).toString().isEmpty())
			paintHeader(p, option, index);
		else
			paintPerson(p, option, index);
		p->restore();
	}

private:
	void paintHeader(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
		QFont f = option.font;
		f.setBold(true);
		f.setPointSizeF(qMax(6.0, f.pointSizeF() - 1.0));
		p->setFont(f);
		p->setPen(QColor(theme::mapColor(QStringLiteral("#9aa1b0"))));
		const QString arrow = index.data(kCollapsedRole).toBool() ? QStringLiteral("▸  ")
									  : QStringLiteral("▾  ");
		p->drawText(option.rect.adjusted(4, 4, -4, 0), Qt::AlignLeft | Qt::AlignVCenter,
			    arrow + index.data(Qt::DisplayRole).toString());
	}

	void paintPerson(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
	{
		const Settings &s = settings();
		const QRect r = option.rect;
		const QString kind = index.data(kActivityKindRole).toString();
		if (kind == QLatin1String("arrived") && s.vwArrivals) {
			QColor tint(theme::accentColor());
			tint.setAlpha(36);
			QPainterPath path;
			path.addRoundedRect(QRectF(r.adjusted(1, 1, -1, -1)), 5, 5);
			p->fillPath(path, tint);
		} else if (option.state & QStyle::State_MouseOver) {
			QPainterPath path;
			path.addRoundedRect(QRectF(r.adjusted(1, 1, -1, -1)), 5, 5);
			p->fillPath(path, QColor(theme::mapColor(QStringLiteral("#1d2129"))));
		}

		p->setFont(option.font);
		const QFontMetrics fm(option.font);
		int x = r.left() + 5;
		int right = r.right() - 5;

		if (s.vwPlatformIcons) {
			const int px = qMin(16, r.height() - 3);
			p->drawImage(QRect(x, r.center().y() - px / 2, px, px),
				     icon(index.data(kPlatformRole).toString(), px, p->device()->devicePixelRatioF()));
			x += px + 6;
		}

		const QString badge = index.data(kBadgeRole).toString();
		if (s.vwRoleBadges && !badge.isEmpty()) {
			const bool host = badge == QLatin1String("host");
			const bool mod = badge == QLatin1String("mod");
			const bool vip = badge == QLatin1String("vip");
			const QString glyph = host  ? QStringLiteral("♛")
					      : mod ? QStringLiteral("◆")
					      : vip ? QStringLiteral("♦")
						    : QStringLiteral("★");
			p->setPen(QColor(host  ? QStringLiteral("#ffb020")
					 : mod ? QStringLiteral("#4ade80")
					 : vip ? QStringLiteral("#f472b6")
					       : QStringLiteral("#a78bfa")));
			const int w = fm.horizontalAdvance(glyph);
			p->drawText(QRect(x, r.top(), w + 2, r.height()), Qt::AlignLeft | Qt::AlignVCenter, glyph);
			x += w + 5;
		}

		if (s.vwActivity) {
			const QString activity = index.data(kActivityRole).toString();
			if (!activity.isEmpty()) {
				QFont tiny = option.font;
				tiny.setPointSizeF(qMax(6.0, tiny.pointSizeF() - 1.5));
				p->setFont(tiny);
				const QFontMetrics sm(tiny);
				const int w = sm.horizontalAdvance(activity);
				QColor c(theme::mapColor(QStringLiteral("#7a8190")));
				if (kind == QLatin1String("chatting"))
					c = QColor(QStringLiteral("#4ade80"));
				else if (kind == QLatin1String("arrived"))
					c = QColor(theme::accentColor());
				p->setPen(c);
				p->drawText(QRect(right - w, r.top(), w + 1, r.height()),
					    Qt::AlignRight | Qt::AlignVCenter, activity);
				right -= w + 8;
				p->setFont(option.font);
			}
		}

		const QString color = index.data(kColorRole).toString();
		const QString roleColor = index.data(kRoleColorRole).toString();
		p->setPen(s.vwNameColors && !roleColor.isEmpty() ? QColor(roleColor)
			  : s.vwNameColors && !color.isEmpty()   ? QColor(color)
								 : QColor(theme::mapColor(QStringLiteral("#e8eaf0"))));
		p->drawText(QRect(x, r.top(), qMax(0, right - x), r.height()), Qt::AlignLeft | Qt::AlignVCenter,
			    fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, qMax(0, right - x)));
	}

	QImage icon(const QString &platform, int px, qreal dpr) const
	{
		const QString key = QStringLiteral("%1/%2/%3").arg(platform).arg(px).arg(dpr);
		auto it = icons_.constFind(key);
		if (it != icons_.constEnd())
			return it.value();
		const QImage img = notice::platformIcon(platform, px, dpr);
		icons_.insert(key, img);
		return img;
	}

	mutable QHash<QString, QImage> icons_;
};

} // namespace

GfxViewerPanel::GfxViewerPanel(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(170);
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 4, 2, 2);
	root->setSpacing(4);

	auto *head = new QHBoxLayout();
	head->setSpacing(4);
	auto *title = new QLabel(QStringLiteral("Who's here"), this);
	title->setObjectName(QStringLiteral("gfxFieldLabel"));
	QFont tf = title->font();
	tf.setBold(true);
	title->setFont(tf);
	count_ = new QLabel(this);
	count_->setObjectName(QStringLiteral("gfxMuted"));
	const auto iconBtn = [this](const QString &text, const QString &tip) {
		auto *b = new QPushButton(text, this);
		b->setObjectName(QStringLiteral("gfxIconBtn"));
		b->setCursor(Qt::PointingHandCursor);
		b->setToolTip(tip);
		return b;
	};
	auto *more = iconBtn(QStringLiteral("⋯"), QStringLiteral("Customize this list"));
	auto *close = iconBtn(QStringLiteral("✕"), QStringLiteral("Hide this list (the 👥 button brings it back)"));
	head->addWidget(title);
	head->addWidget(count_);
	head->addStretch(1);
	head->addWidget(more);
	head->addWidget(close);
	root->addLayout(head);
	connect(more, &QPushButton::clicked, this, [this] { showOptions(); });
	connect(close, &QPushButton::clicked, this, [this] {
		settings().chatViewers = false;
		saveSettings();
		if (onLayoutChanged)
			onLayoutChanged();
	});

	search_ = new QLineEdit(this);
	search_->setObjectName(QStringLiteral("gfxChatInput"));
	search_->setPlaceholderText(QStringLiteral("Search people here…"));
	search_->setClearButtonEnabled(true);
	root->addWidget(search_);
	connect(search_, &QLineEdit::textChanged, this, [this] { rebuild(); });

	auto *chips = new QWidget(this);
	auto *flow = new gfx::FlowLayout(chips, 3, 3);
	chips->setLayout(flow);
	QSizePolicy chipsPolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	chipsPolicy.setHeightForWidth(true);
	chips->setSizePolicy(chipsPolicy);
	const auto seg = [this, chips, flow](const QString &key) {
		auto *b = new QPushButton(chips);
		b->setObjectName(QStringLiteral("gfxSeg"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		connect(b, &QPushButton::clicked, this, [this, key] { setFilter(key); });
		flow->addWidget(b);
		return b;
	};
	fAll_ = seg(QStringLiteral("all"));
	fTeam_ = seg(QStringLiteral("team"));
	fChatting_ = seg(QStringLiteral("chatting"));
	fLurking_ = seg(QStringLiteral("lurking"));
	fTeam_->setToolTip(QStringLiteral("The broadcaster, moderators and VIPs"));
	root->addWidget(chips);

	status_ = new QLabel(this);
	status_->setObjectName(QStringLiteral("gfxMuted"));
	status_->setWordWrap(true);
	status_->hide();
	root->addWidget(status_);

	list_ = new QListWidget(this);
	list_->setObjectName(QStringLiteral("gfxViewerList"));
	list_->setItemDelegate(new RowDelegate(list_));
	list_->setSelectionMode(QAbstractItemView::NoSelection);
	list_->setFocusPolicy(Qt::NoFocus);
	list_->setMouseTracking(true);
	list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	list_->setContextMenuPolicy(Qt::CustomContextMenu);
	root->addWidget(list_, 1);
	connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
		const QString section = item->data(kHeaderRole).toString();
		if (!section.isEmpty()) {
			if (collapsed_.contains(section))
				collapsed_.remove(section);
			else
				collapsed_.insert(section);
			// Not from inside the click: rebuilding deletes the item that was clicked.
			QMetaObject::invokeMethod(this, [this] { rebuild(); }, Qt::QueuedConnection);
		} else if (open_) {
			open_(item->data(kPlatformRole).toString(), item->data(kLoginRole).toString());
		}
	});
	connect(list_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) { showPersonMenu(pos); });

	rebuild();
}

void GfxViewerPanel::setOpenHandler(PersonFn fn)
{
	open_ = std::move(fn);
}

void GfxViewerPanel::setModHandler(PersonFn fn)
{
	mod_ = std::move(fn);
}

void GfxViewerPanel::applyPoll(const QJsonObject &response)
{
	now_ = static_cast<qint64>(response.value(QStringLiteral("serverTime")).toDouble());
	twitchStatus_ = response.value(QStringLiteral("twitch")).toObject().value(QStringLiteral("status")).toString();
	total_ = response.value(QStringLiteral("total")).toInt();
	note_.clear();
	people_.clear();
	const QJsonArray arr = response.value(QStringLiteral("people")).toArray();
	people_.reserve(arr.size());
	for (qsizetype i = 0; i < arr.size(); ++i) {
		const QJsonObject o = arr.at(i).toObject();
		Person p;
		p.platform = o.value(QStringLiteral("platform")).toString();
		p.login = o.value(QStringLiteral("login")).toString();
		p.name = o.value(QStringLiteral("name")).toString();
		if (p.name.isEmpty())
			p.name = p.login;
		p.color = readable(o.value(QStringLiteral("color")).toString());
		p.role = o.value(QStringLiteral("role")).toString();
		p.sub = o.value(QStringLiteral("sub")).toBool();
		p.bot = o.value(QStringLiteral("bot")).toBool();
		p.chatAt = static_cast<qint64>(o.value(QStringLiteral("chatAt")).toDouble());
		p.joinedAt = static_cast<qint64>(o.value(QStringLiteral("joinedAt")).toDouble());
		people_ << p;
	}
	rebuild();
}

void GfxViewerPanel::setNote(const QString &note)
{
	if (note == note_)
		return;
	note_ = note;
	rebuild();
}

void GfxViewerPanel::refresh()
{
	list_->doItemsLayout();
	rebuild();
}

bool GfxViewerPanel::chatting(const Person &p) const
{
	return p.chatAt > 0 && now_ - p.chatAt < kChattingMs;
}

// Passes the streamer's own rules (bots, hidden names, lurkers on or off).
bool GfxViewerPanel::listed(const Person &p) const
{
	const Settings &s = settings();
	if (s.vwHideBots && p.bot)
		return false;
	if (s.vwHidden.contains(p.login, Qt::CaseInsensitive))
		return false;
	return s.vwLurkers || !p.role.isEmpty() || chatting(p);
}

bool GfxViewerPanel::matchesFilter(const Person &p) const
{
	if (filter_ == QLatin1String("team"))
		return !p.role.isEmpty();
	if (filter_ == QLatin1String("chatting"))
		return chatting(p);
	if (filter_ == QLatin1String("lurking"))
		return !chatting(p);
	return true;
}

void GfxViewerPanel::setFilter(const QString &filter)
{
	filter_ = filter;
	rebuild();
}

void GfxViewerPanel::rebuild()
{
	const Settings &s = settings();
	if (!s.vwLurkers && filter_ == QLatin1String("lurking"))
		filter_ = QStringLiteral("all");

	QList<const Person *> base;
	base.reserve(people_.size());
	int team = 0, talking = 0;
	for (const Person &p : people_) {
		if (!listed(p))
			continue;
		base << &p;
		team += p.role.isEmpty() ? 0 : 1;
		talking += chatting(p) ? 1 : 0;
	}
	const int everyone = static_cast<int>(base.size());
	count_->setText(everyone ? QString::number(everyone) : QString());
	fAll_->setText(QStringLiteral("All %1").arg(everyone));
	fTeam_->setText(QStringLiteral("Team %1").arg(team));
	fChatting_->setText(QStringLiteral("Chatting %1").arg(talking));
	fLurking_->setText(QStringLiteral("Lurking %1").arg(everyone - talking));
	fLurking_->setVisible(s.vwLurkers);
	fAll_->setChecked(filter_ == QLatin1String("all"));
	fTeam_->setChecked(filter_ == QLatin1String("team"));
	fChatting_->setChecked(filter_ == QLatin1String("chatting"));
	fLurking_->setChecked(filter_ == QLatin1String("lurking"));

	QString status = note_;
	if (status.isEmpty() && twitchStatus_ == QLatin1String("needs_scope"))
		status = QStringLiteral("Reconnect Twitch on goalforgex.com to see lurkers. For now only people who "
					"chatted are listed.");
	else if (status.isEmpty() && twitchStatus_ == QLatin1String("error"))
		status = QStringLiteral("Twitch's viewer list didn't load. Trying again shortly.");
	else if (status.isEmpty() && total_ > people_.size())
		status = QStringLiteral("Showing %1 of %2 people.").arg(people_.size()).arg(total_);
	status_->setText(status);
	status_->setVisible(!status.isEmpty());

	const QString q = search_->text().trimmed();
	QList<const Person *> rows;
	rows.reserve(base.size());
	for (const Person *p : base)
		if (matchesFilter(*p) && (q.isEmpty() || p->name.contains(q, Qt::CaseInsensitive) ||
					  p->login.contains(q, Qt::CaseInsensitive)))
			rows << p;
	std::stable_sort(rows.begin(), rows.end(), [&s](const Person *a, const Person *b) {
		if (s.vwNewestFirst) {
			const qint64 ta = qMax(a->joinedAt, a->chatAt), tb = qMax(b->joinedAt, b->chatAt);
			if (ta != tb)
				return ta > tb;
		}
		return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
	});

	struct Section {
		QString key;
		QString title;
	};
	const QList<Section> sections = {{QStringLiteral("host"), QStringLiteral("BROADCASTER")},
					 {QStringLiteral("mod"), QStringLiteral("MODERATORS")},
					 {QStringLiteral("vip"), QStringLiteral("VIPS")},
					 {QStringLiteral("chatting"), QStringLiteral("CHATTING")},
					 {QStringLiteral("lurking"), QStringLiteral("LURKING")}};
	const auto sectionOf = [this](const Person &p) {
		if (!p.role.isEmpty())
			return p.role;
		return chatting(p) ? QStringLiteral("chatting") : QStringLiteral("lurking");
	};

	QScrollBar *bar = list_->verticalScrollBar();
	const int keep = bar->value();
	list_->setUpdatesEnabled(false);
	list_->clear();
	for (const Section &sec : sections) {
		QList<const Person *> inSection;
		for (const Person *p : rows)
			if (sectionOf(*p) == sec.key)
				inSection << p;
		if (inSection.isEmpty())
			continue;
		const bool folded = collapsed_.contains(sec.key);
		auto *header =
			new QListWidgetItem(QStringLiteral("%1  %2").arg(sec.title).arg(inSection.size()), list_);
		header->setData(kHeaderRole, sec.key);
		header->setData(kCollapsedRole, folded);
		if (folded)
			continue;
		for (const Person *p : inSection) {
			auto *item = new QListWidgetItem(p->name, list_);
			item->setData(kPlatformRole, p->platform);
			item->setData(kLoginRole, p->login);
			item->setData(kColorRole, p->color);
			item->setData(kRoleColorRole, gfx::roleNameColor(p->role == QLatin1String("host"),
									 p->role == QLatin1String("mod"),
									 p->role == QLatin1String("vip"), p->sub));
			item->setData(kBadgeRole, !p->role.isEmpty() ? p->role
						  : p->sub           ? QStringLiteral("sub")
								     : QString());
			QString activity, kind;
			if (chatting(*p)) {
				activity = QStringLiteral("chatting");
				kind = QStringLiteral("chatting");
			} else if (p->joinedAt > 0 && now_ - p->joinedAt < kArrivedMs) {
				activity = QStringLiteral("just arrived");
				kind = QStringLiteral("arrived");
			} else if (p->joinedAt > 0 && now_ - p->joinedAt < kRecentMs) {
				activity = shortAgo(now_ - p->joinedAt);
			} else if (p->platform != QLatin1String("twitch") && p->chatAt > 0) {
				activity = shortAgo(now_ - p->chatAt);
			} else {
				activity = QStringLiteral("lurking");
			}
			item->setData(kActivityRole, activity);
			item->setData(kActivityKindRole, kind);
			item->setToolTip(QStringLiteral("%1 on %2").arg(p->login, p->platform));
		}
	}
	list_->setUpdatesEnabled(true);
	bar->setValue(keep);
}

void GfxViewerPanel::showOptions()
{
	Settings &s = settings();
	QMenu menu(this);
	const auto toggle = [this, &menu](const QString &text, bool *value) {
		QAction *a = menu.addAction(text);
		a->setCheckable(true);
		a->setChecked(*value);
		connect(a, &QAction::toggled, this, [this, value](bool on) {
			*value = on;
			saveSettings();
			rebuild();
			list_->doItemsLayout();
		});
	};
	toggle(QStringLiteral("Include lurkers"), &s.vwLurkers);
	toggle(QStringLiteral("Hide bots"), &s.vwHideBots);
	toggle(QStringLiteral("Newest arrivals first (off = A to Z)"), &s.vwNewestFirst);
	menu.addSeparator();
	toggle(QStringLiteral("Highlight people who just arrived"), &s.vwArrivals);
	toggle(QStringLiteral("Platform icons"), &s.vwPlatformIcons);
	toggle(QStringLiteral("Role badges"), &s.vwRoleBadges);
	toggle(QStringLiteral("Chatting / lurking labels"), &s.vwActivity);
	toggle(QStringLiteral("Chatters' own name colours"), &s.vwNameColors);
	toggle(QStringLiteral("Compact rows"), &s.vwCompact);
	menu.addSeparator();
	QAction *side = menu.addAction(s.vwLeft ? QStringLiteral("Move to the right of the chat")
						: QStringLiteral("Move to the left of the chat"));
	connect(side, &QAction::triggered, this, [this] {
		settings().vwLeft = !settings().vwLeft;
		saveSettings();
		if (onLayoutChanged)
			onLayoutChanged();
	});
	if (!s.vwHidden.isEmpty()) {
		QAction *clear = menu.addAction(QStringLiteral("Show hidden people again (%1)").arg(s.vwHidden.size()));
		connect(clear, &QAction::triggered, this, [this] {
			settings().vwHidden.clear();
			saveSettings();
			rebuild();
		});
	}
	menu.exec(QCursor::pos());
}

void GfxViewerPanel::showPersonMenu(const QPoint &pos)
{
	QListWidgetItem *item = list_->itemAt(pos);
	if (!item || !item->data(kHeaderRole).toString().isEmpty())
		return;
	const QString platform = item->data(kPlatformRole).toString();
	const QString login = item->data(kLoginRole).toString();
	QMenu menu(this);
	QAction *open = menu.addAction(QStringLiteral("Open their card"));
	QAction *mod = menu.addAction(QStringLiteral("Moderate…"));
	menu.addSeparator();
	QAction *hide = menu.addAction(QStringLiteral("Hide from this list"));
	QAction *picked = menu.exec(list_->viewport()->mapToGlobal(pos));
	if (picked == open && open_) {
		open_(platform, login);
	} else if (picked == mod && mod_) {
		mod_(platform, login);
	} else if (picked == hide) {
		Settings &s = settings();
		if (s.vwHidden.size() < 200 && !s.vwHidden.contains(login, Qt::CaseInsensitive))
			s.vwHidden << login;
		saveSettings();
		rebuild();
	}
}
