/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-chat.hpp"
#include "gfx-http.hpp"
#include "gfx-settings.hpp"
#include "gfx-theme.hpp"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSet>
#include <QSize>
#include <QSizePolicy>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QFont>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include <utility>

using namespace gfx;

namespace {

constexpr int kKeep = 500;   // messages kept in the panel
constexpr int kMaxLen = 500; // Twitch / Kick cap at 500 (YouTube 200 — the server reports a rejection)
constexpr int kHistory = 50; // sent messages ↑/↓ can recall
constexpr int kQuickTimeout = 600;

// Everything platform-specific in one place.
struct PlatformInfo {
	const char *key;
	const char *name;
	bool canSend;        // the streamer can post there from OBS
	bool canMod;         // delete / timeout / ban from a chat line
	const char *profile; // profile URL prefix (+ login)
	const char *channel; // the streamer's own channel URL must start with this
};
const PlatformInfo kInfo[] = {
	{"twitch", "Twitch", true, true, "https://www.twitch.tv/", "https://www.twitch.tv/"},
	{"kick", "Kick", true, true, "https://kick.com/", "https://kick.com/"},
	{"youtube", "YouTube", true, false, "https://www.youtube.com/channel/", "https://www.youtube.com/"},
	{"tiktok", "TikTok", false, false, "https://www.tiktok.com/@", "https://www.tiktok.com/"},
};

const PlatformInfo *info(const QString &p)
{
	for (const PlatformInfo &i : kInfo)
		if (p == QLatin1String(i.key))
			return &i;
	return nullptr;
}

QString platformName(const QString &p)
{
	const PlatformInfo *i = info(p);
	return i ? QString::fromLatin1(i->name) : p;
}

bool canMod(const QString &p)
{
	const PlatformInfo *i = info(p);
	return i && i->canMod;
}

// YouTube / TikTok have no bundled artwork — drawn at the requested size.
QImage drawnIcon(const QString &platform, int px)
{
	QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing, true);
	const qreal s = px;
	if (platform == QLatin1String("youtube")) {
		QPainterPath path;
		path.addRoundedRect(QRectF(0, s * 0.16, s, s * 0.68), s * 0.18, s * 0.18);
		p.fillPath(path, QColor(0xff, 0x00, 0x33));
		QPolygonF tri;
		tri << QPointF(s * 0.40, s * 0.33) << QPointF(s * 0.40, s * 0.67) << QPointF(s * 0.69, s * 0.50);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawPolygon(tri);
	} else { // tiktok
		QPainterPath path;
		path.addRoundedRect(QRectF(0, 0, s, s), s * 0.24, s * 0.24);
		p.fillPath(path, QColor(0x01, 0x01, 0x01));
		QFont f;
		f.setBold(true);
		f.setPixelSize(qMax(6, qRound(s * 0.78)));
		p.setFont(f);
		const QRectF box(0, -s * 0.04, s, s);
		const qreal o = qMax<qreal>(0.6, s * 0.05);
		p.setPen(QColor(0x25, 0xf4, 0xee));
		p.drawText(box.translated(-o, -o), Qt::AlignCenter, QStringLiteral("♪"));
		p.setPen(QColor(0xfe, 0x2c, 0x55));
		p.drawText(box.translated(o, o), Qt::AlignCenter, QStringLiteral("♪"));
		p.setPen(Qt::white);
		p.drawText(box, Qt::AlignCenter, QStringLiteral("♪"));
	}
	p.end();
	return img;
}

// Platform icon (Twitch / Kick: src/resources/*.png, 64 px; YouTube / TikTok:
// drawn) scaled once per size + screen scale with smooth filtering — the text
// renderer would otherwise shrink it on every repaint and blur it.
QImage platformIcon(const QString &platform, int logicalPx, qreal dpr)
{
	static QHash<QString, QImage> cache;
	const QString key = QStringLiteral("%1/%2/%3").arg(platform).arg(logicalPx).arg(dpr);
	auto it = cache.constFind(key);
	if (it != cache.constEnd())
		return it.value();
	const int px = qMax(1, qRound(logicalPx * dpr));
	QImage img;
	if (platform == QLatin1String("twitch") || platform == QLatin1String("kick")) {
		const QImage src(QStringLiteral(":/goalforgex/%1.png").arg(platform));
		if (!src.isNull())
			img = src.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	} else if (platform == QLatin1String("youtube") || platform == QLatin1String("tiktok")) {
		img = drawnIcon(platform, px);
	}
	if (!img.isNull())
		img.setDevicePixelRatio(dpr);
	cache.insert(key, img);
	return img;
}

// Profile links: only plain usernames / channel ids ever become a URL.
// (TikTok names can contain dots; YouTube logins are channel ids.)
const QRegularExpression &loginRe()
{
	static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,39}$"));
	return re;
}

// Message ids ride in the line links (user:… / mod:…), split on ':'.
const QRegularExpression &msgIdRe()
{
	static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9._=-]{1,160}$"));
	return re;
}

const QRegularExpression &urlRe()
{
	static const QRegularExpression re(QStringLiteral("https?://[^\\s<>\"']+"),
					   QRegularExpression::CaseInsensitiveOption);
	return re;
}

void openProfile(const QString &platform, const QString &login)
{
	const PlatformInfo *i = info(platform);
	if (!i || !loginRe().match(login).hasMatch())
		return;
	// Twitch / Kick / TikTok names are case-insensitive; YouTube channel ids are not.
	const QString l = platform == QLatin1String("youtube") ? login : login.toLower();
	QDesktopServices::openUrl(QUrl(QString::fromLatin1(i->profile) + l));
}

// The feed: serves the platform icons to the HTML as gfx:<platform>/<px>.
class ChatView : public QTextBrowser {
public:
	using QTextBrowser::QTextBrowser;

protected:
	QVariant loadResource(int type, const QUrl &name) override
	{
		if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("gfx")) {
			const QStringList parts = name.path().split(QLatin1Char('/'));
			const int px = parts.size() > 1 ? qBound(8, parts.at(1).toInt(), 48) : 15;
			return QVariant::fromValue(platformIcon(parts.value(0), px, devicePixelRatioF()));
		}
		return QTextBrowser::loadResource(type, name);
	}
};

// Chatter colours are picked for light AND dark chats — lift the dark ones so
// they stay readable on the dock's graphite background.
QString readableColor(const QString &hex)
{
	QColor c(hex);
	if (!c.isValid())
		return QStringLiteral("#c9ced8");
	if (c.lightness() < 115)
		c = c.lighter(c.lightness() < 50 ? 260 : 170);
	return c.name();
}

QString fmtCount(qint64 n)
{
	return QLocale(QLocale::English).toString(n);
}

QString fmtUptime(qint64 ms)
{
	const qint64 s = qMax<qint64>(0, ms / 1000);
	return QStringLiteral("%1:%2:%3")
		.arg(s / 3600)
		.arg((s / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString tag(const char *text, const char *color, qreal pt)
{
	return QStringLiteral("<span style='color:%1;font-size:%3pt;font-weight:800;'>%2</span>&nbsp;")
		.arg(QLatin1String(color), QLatin1String(text), QString::number(pt, 'f', 1));
}

// Each feed line is one text block; its background says what kind of line it is.
QTextBlockFormat lineFormat(bool host, bool highlighted, bool firstTimer, bool shaded)
{
	QTextBlockFormat f;
	f.setBottomMargin(settings().chatCompact ? 1 : 4);
	f.setLineHeight(112, QTextBlockFormat::ProportionalHeight);
	QColor bg;
	if (host)
		bg = QColor(QStringLiteral("#1f1a10"));
	else if (highlighted)
		bg = QColor(QStringLiteral("#3a2a0c"));
	else if (firstTimer)
		bg = QColor(QStringLiteral("#10231c"));
	else if (shaded)
		bg = QColor(QStringLiteral("#161920"));
	if (bg.isValid())
		f.setBackground(bg);
	return f;
}

} // namespace

QStringList GfxChatPanel::wantedPlatforms()
{
	QStringList p{QStringLiteral("twitch"), QStringLiteral("kick")};
	if (settings().chatYouTube)
		p << QStringLiteral("youtube");
	if (settings().chatTikTok)
		p << QStringLiteral("tiktok");
	return p;
}

GfxChatPanel::GfxChatPanel(QWidget *parent) : QWidget(parent)
{
	buildUi();
	updateFilters();
	updateTargets();
	rebuildHighlightRe();
	applyFontSize();
	rebuild();
	tick_ = new QTimer(this);
	tick_->setInterval(1000);
	connect(tick_, &QTimer::timeout, this, [this] {
		if (!isVisible())
			return;
		renderStats();
		// Safety net: following but short of the bottom for any reason → re-pin.
		QScrollBar *sb = view_->verticalScrollBar();
		if (autoScrolling() && sb->value() < sb->maximum()) {
			++programmatic_;
			sb->setValue(sb->maximum());
			--programmatic_;
		}
	});
	tick_->start();
}

void GfxChatPanel::buildUi()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 4, 0, 0);
	outer->setSpacing(0);

	// ── Pro lock ──
	lockPanel_ = new QWidget(this);
	auto *ll = new QVBoxLayout(lockPanel_);
	ll->setContentsMargins(8, 24, 8, 8);
	ll->setSpacing(10);
	lockLbl_ = new QLabel(lockPanel_);
	lockLbl_->setWordWrap(true);
	lockLbl_->setAlignment(Qt::AlignCenter);
	lockLbl_->setObjectName(QStringLiteral("gfxMuted"));
	auto *upgrade = new QPushButton(QStringLiteral("See GoalForgeX Pro"), lockPanel_);
	theme::setRole(upgrade, "primary");
	ll->addWidget(lockLbl_);
	ll->addWidget(upgrade, 0, Qt::AlignHCenter);
	ll->addStretch(1);
	lockPanel_->hide();
	outer->addWidget(lockPanel_, 1);
	connect(upgrade, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/subscribe"))); });

	// ── Chat ──
	main_ = new QWidget(this);
	auto *root = new QVBoxLayout(main_);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(6);
	outer->addWidget(main_, 1);

	// All | Twitch | Kick | YouTube | TikTok | @   ·   🔍 ⏸ ⋯ ●●  (platform chips only while 2+ are connected)
	auto *top = new QHBoxLayout();
	top->setSpacing(3);
	const auto seg = [this](const QString &text, const QString &key) {
		auto *b = new QPushButton(text, main_);
		b->setObjectName(QStringLiteral("gfxSeg"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		connect(b, &QPushButton::clicked, this, [this, key] { setFilter(key); });
		return b;
	};
	fAll_ = seg(QStringLiteral("All"), QStringLiteral("all"));
	fTwitch_ = seg(QStringLiteral("Twitch"), QStringLiteral("twitch"));
	fKick_ = seg(QStringLiteral("Kick"), QStringLiteral("kick"));
	fYouTube_ = seg(QStringLiteral("YouTube"), QStringLiteral("youtube"));
	fTikTok_ = seg(QStringLiteral("TikTok"), QStringLiteral("tiktok"));
	fMentions_ = seg(QStringLiteral("@"), QStringLiteral("mentions"));
	fMentions_->setToolTip(
		QStringLiteral("Only messages that mention you or one of your highlight words (⋯ → Highlight words)"));
	fAll_->setChecked(true);
	const auto iconBtn = [this](const QString &text, const QString &tip, bool checkable) {
		auto *b = new QPushButton(text, main_);
		b->setObjectName(QStringLiteral("gfxIconBtn"));
		b->setCheckable(checkable);
		b->setCursor(Qt::PointingHandCursor);
		b->setToolTip(tip);
		return b;
	};
	searchBtn_ = iconBtn(QStringLiteral("🔍"), QStringLiteral("Search chat by message or name"), true);
	pauseBtn_ = iconBtn(QStringLiteral("⏸"),
			    QStringLiteral("Pause the chat so it stops moving (new messages still come in)"), true);
	moreBtn_ = iconBtn(QStringLiteral("⋯"), QStringLiteral("Chat options"), false);
	statusLbl_ = new QLabel(main_);
	statusLbl_->setObjectName(QStringLiteral("gfxChatStatus"));
	statusLbl_->setTextFormat(Qt::RichText);
	top->addWidget(fAll_);
	top->addWidget(fTwitch_);
	top->addWidget(fKick_);
	top->addWidget(fYouTube_);
	top->addWidget(fTikTok_);
	top->addWidget(fMentions_);
	top->addStretch(1);
	top->addWidget(searchBtn_);
	top->addWidget(pauseBtn_);
	top->addWidget(moreBtn_);
	top->addSpacing(4);
	top->addWidget(statusLbl_);
	root->addLayout(top);
	connect(pauseBtn_, &QPushButton::clicked, this, [this](bool on) { setPaused(on); });
	connect(moreBtn_, &QPushButton::clicked, this, [this] { showOptionsMenu(); });

	// Search row: [ search…               ] [Only name ✕]
	searchRow_ = new QWidget(main_);
	auto *sr = new QHBoxLayout(searchRow_);
	sr->setContentsMargins(0, 0, 0, 0);
	sr->setSpacing(4);
	searchEdit_ = new QLineEdit(searchRow_);
	searchEdit_->setObjectName(QStringLiteral("gfxChatInput"));
	searchEdit_->setPlaceholderText(QStringLiteral("Search messages and names…"));
	searchEdit_->setClearButtonEnabled(true);
	userChip_ = new QPushButton(searchRow_);
	userChip_->setObjectName(QStringLiteral("gfxSeg"));
	userChip_->setCheckable(true);
	userChip_->setChecked(true);
	userChip_->setCursor(Qt::PointingHandCursor);
	userChip_->setToolTip(QStringLiteral("Click to show everyone again"));
	userChip_->hide();
	sr->addWidget(searchEdit_, 1);
	sr->addWidget(userChip_);
	searchRow_->hide();
	root->addWidget(searchRow_);
	searchDelay_ = new QTimer(this);
	searchDelay_->setSingleShot(true);
	searchDelay_->setInterval(180);
	connect(searchDelay_, &QTimer::timeout, this, [this] {
		const QString q = searchEdit_->text().trimmed();
		if (q == search_)
			return;
		search_ = q;
		follow_ = true;
		rebuild();
	});
	connect(searchEdit_, &QLineEdit::textChanged, this, [this] { searchDelay_->start(); });
	connect(searchBtn_, &QPushButton::toggled, this, [this](bool on) {
		if (on) {
			searchRow_->show();
			searchEdit_->setFocus();
			searchEdit_->selectAll();
			return;
		}
		searchEdit_->clear();
		searchDelay_->stop();
		if (!search_.isEmpty()) {
			search_.clear();
			follow_ = true;
			rebuild();
		}
		searchRow_->setVisible(!userFilterLogin_.isEmpty());
	});
	connect(userChip_, &QPushButton::clicked, this, [this] { setUserFilter(QString(), QString(), QString()); });
	searchEdit_->installEventFilter(this);

	// Stats: one card per connected platform, two per row + chatters / pace line
	stats_ = new QWidget(main_);
	auto *sl = new QVBoxLayout(stats_);
	sl->setContentsMargins(0, 0, 0, 0);
	sl->setSpacing(3);
	cardsGrid_ = new QGridLayout();
	cardsGrid_->setHorizontalSpacing(6);
	cardsGrid_->setVerticalSpacing(6);
	cardsGrid_->setColumnStretch(0, 1);
	cardsGrid_->setColumnStretch(1, 1);
	buildStatCard(twCard_, QStringLiteral("twitch"));
	buildStatCard(kkCard_, QStringLiteral("kick"));
	buildStatCard(ytCard_, QStringLiteral("youtube"));
	buildStatCard(ttCard_, QStringLiteral("tiktok"));
	for (StatCard *c : {&twCard_, &kkCard_, &ytCard_, &ttCard_})
		c->box->hide();
	sl->addLayout(cardsGrid_);
	summary_ = new QLabel(stats_);
	summary_->setObjectName(QStringLiteral("gfxStatSummary"));
	summary_->setTextFormat(Qt::RichText);
	summary_->setToolTip(QStringLiteral("Chatters: different people who chatted in the last 10 minutes.\n"
					    "Messages a minute: average over the last 5 minutes."));
	sl->addWidget(summary_);
	stats_->hide();
	root->addWidget(stats_);

	// Feed
	view_ = new ChatView(main_);
	view_->setObjectName(QStringLiteral("gfxChatView"));
	view_->setOpenLinks(false);
	view_->setOpenExternalLinks(false);
	view_->setUndoRedoEnabled(false);
	view_->setFrameShape(QFrame::NoFrame);
	view_->setMinimumHeight(160);
	view_->document()->setDocumentMargin(8);
	view_->viewport()->installEventFilter(this);
	view_->setMouseTracking(true);
	root->addWidget(view_, 1);
	// Links in the feed:
	//   user:<platform>:<login>:<messageId>          → the chatter menu
	//   mod:<delete|timeout>:<platform>:<login>:<id> → quick moderation
	//   http(s)://…                                  → the browser
	connect(view_, &QTextBrowser::anchorClicked, this, [this](const QUrl &u) {
		const QString scheme = u.scheme();
		if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
			if (u.isValid() && !u.host().isEmpty())
				QDesktopServices::openUrl(u);
			return;
		}
		const QStringList parts = u.path().split(QLatin1Char(':'));
		if (scheme == QLatin1String("user")) {
			if (parts.size() < 2)
				return;
			const QString platform = parts.at(0), login = parts.at(1);
			if (!info(platform) || !loginRe().match(login).hasMatch())
				return;
			showUserMenu(platform, login, parts.size() > 2 ? parts.at(2) : QString());
			return;
		}
		if (scheme == QLatin1String("mod") && parts.size() == 4 && mod_) {
			const QString action = parts.at(0), platform = parts.at(1), login = parts.at(2),
				      id = parts.at(3);
			if ((action != QLatin1String("delete") && action != QLatin1String("timeout")) ||
			    !canMod(platform) || !loginRe().match(login).hasMatch() || !msgIdRe().match(id).hasMatch())
				return;
			QString shown = login;
			for (const ChatMessage &m : msgs_)
				if (m.id == id && m.platform == platform) {
					if (m.host)
						return; // never the broadcaster
					shown = m.user;
				}
			if (action == QLatin1String("delete"))
				mod_(platform, action, login, 0, id, shown);
			else
				mod_(platform, action, login, kQuickTimeout, id, shown);
		}
	});

	// Follow the bottom. The text lays out in the background in chunks, so the
	// scroll range keeps growing after a line is added — re-pin on every growth
	// instead of scrolling once and hoping the layout was done.
	QScrollBar *vsb = view_->verticalScrollBar();
	connect(vsb, &QScrollBar::rangeChanged, this, [this](int, int max) {
		if (!autoScrolling())
			return;
		++programmatic_;
		view_->verticalScrollBar()->setValue(max);
		--programmatic_;
	});
	// Only the reader's own scrolling decides whether we follow.
	connect(vsb, &QScrollBar::valueChanged, this, [this](int v) {
		if (programmatic_)
			return;
		follow_ = v >= view_->verticalScrollBar()->maximum() - 4;
		if (follow_ && !paused_ && !hoverPaused_)
			pendingNew_ = 0;
		updateJump();
	});

	jump_ = new QPushButton(main_);
	jump_->setObjectName(QStringLiteral("gfxJump"));
	theme::setRole(jump_, "soft");
	jump_->setCursor(Qt::PointingHandCursor);
	jump_->hide();
	root->addWidget(jump_);
	connect(jump_, &QPushButton::clicked, this, [this] {
		if (paused_)
			setPaused(false);
		else
			scrollToBottom();
	});

	// Composer: [Twitch] [Kick] [YouTube] [ message…        ] [Send]   (TikTok is read-only)
	auto *comp = new QHBoxLayout();
	comp->setSpacing(4);
	const auto target = [this](const QString &text, const char *platform) {
		auto *b = new QPushButton(text, main_);
		b->setObjectName(QStringLiteral("gfxTarget"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		theme::setState(b, platform);
		return b;
	};
	toTwitch_ = target(QStringLiteral("Twitch"), "twitch");
	toKick_ = target(QStringLiteral("Kick"), "kick");
	toYouTube_ = target(QStringLiteral("YouTube"), "youtube");
	for (QPushButton *b : {toTwitch_, toKick_, toYouTube_}) {
		const QString p = b == toKick_
					  ? QStringLiteral("kick")
					  : (b == toYouTube_ ? QStringLiteral("youtube") : QStringLiteral("twitch"));
		b->setIcon(QIcon(QPixmap::fromImage(platformIcon(p, 16, devicePixelRatioF()))));
		b->setIconSize(QSize(16, 16));
	}
	toTwitch_->setChecked(settings().chatToTwitch);
	toKick_->setChecked(settings().chatToKick);
	toYouTube_->setChecked(settings().chatToYouTube);
	input_ = new QLineEdit(main_);
	input_->setObjectName(QStringLiteral("gfxChatInput"));
	input_->setMaxLength(kMaxLen);
	input_->setPlaceholderText(QStringLiteral("Send a message…"));
	input_->setClearButtonEnabled(false);
	input_->setToolTip(QStringLiteral("Tab completes chatter names · ↑ / ↓ bring back what you sent"));
	input_->installEventFilter(this);
	sendBtn_ = new QPushButton(QStringLiteral("Send"), main_);
	theme::setRole(sendBtn_, "primary");
	sendBtn_->setCursor(Qt::PointingHandCursor);
	comp->addWidget(toTwitch_);
	comp->addWidget(toKick_);
	comp->addWidget(toYouTube_);
	comp->addWidget(input_, 1);
	comp->addWidget(sendBtn_);
	root->addLayout(comp);

	readOnlyLbl_ =
		new QLabel(QStringLiteral("TikTok chat is read-only — TikTok doesn't let apps post as you."), main_);
	readOnlyLbl_->setObjectName(QStringLiteral("gfxMuted"));
	readOnlyLbl_->setWordWrap(true);
	readOnlyLbl_->hide();
	root->addWidget(readOnlyLbl_);

	countLbl_ = new QLabel(main_);
	countLbl_->setObjectName(QStringLiteral("gfxMuted"));
	countLbl_->setAlignment(Qt::AlignRight);
	countLbl_->hide();
	root->addWidget(countLbl_);

	// Targets: always keep at least one selected, remember the choice.
	for (QPushButton *b : targetButtons())
		connect(b, &QPushButton::clicked, this, [this, b] {
			if (!b->isChecked()) {
				bool other = false;
				for (QPushButton *o : targetButtons())
					if (o != b && o->isChecked() && o->isEnabled())
						other = true;
				if (!other)
					b->setChecked(true);
			}
			settings().chatToTwitch = toTwitch_->isChecked();
			settings().chatToKick = toKick_->isChecked();
			settings().chatToYouTube = toYouTube_->isChecked();
			saveSettings();
			updateSendState();
		});
	connect(input_, &QLineEdit::textChanged, this, [this](const QString &t) {
		countLbl_->setVisible(t.size() > kMaxLen - 100);
		countLbl_->setText(QStringLiteral("%1 / %2").arg(t.size()).arg(kMaxLen));
		updateSendState();
	});
	// Typing ends name completion and history browsing.
	connect(input_, &QLineEdit::textEdited, this, [this] {
		tabIndex_ = -1;
		historyPos_ = -1;
	});
	connect(input_, &QLineEdit::returnPressed, this, [this] { doSend(); });
	connect(sendBtn_, &QPushButton::clicked, this, [this] { doSend(); });
}

void GfxChatPanel::buildStatCard(StatCard &c, const QString &platform)
{
	c.platform = platform;
	c.box = new QFrame(stats_);
	c.box->setObjectName(QStringLiteral("gfxStatCard"));
	c.box->installEventFilter(this);
	auto *l = new QHBoxLayout(c.box);
	l->setContentsMargins(8, 5, 8, 5);
	l->setSpacing(7);
	auto *icon = new QLabel(c.box);
	icon->setPixmap(QPixmap::fromImage(platformIcon(platform, 18, devicePixelRatioF())));
	icon->setFixedSize(18, 18);
	auto *text = new QVBoxLayout();
	text->setContentsMargins(0, 0, 0, 0);
	text->setSpacing(0);
	c.head = new QLabel(c.box);
	c.head->setTextFormat(Qt::RichText);
	c.sub = new QLabel(c.box);
	c.sub->setObjectName(QStringLiteral("gfxStatSub"));
	c.sub->setTextFormat(Qt::PlainText);
	// Ignored width: long titles / categories clip instead of widening the dock.
	for (QLabel *lbl : {c.head, c.sub})
		lbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	// Clicks and tooltips belong to the card.
	for (QLabel *lbl : {icon, c.head, c.sub})
		lbl->setAttribute(Qt::WA_TransparentForMouseEvents);
	text->addWidget(c.head);
	text->addWidget(c.sub);
	l->addWidget(icon, 0, Qt::AlignVCenter);
	l->addLayout(text, 1);
}

qint64 GfxChatPanel::serverNow() const
{
	return QDateTime::currentMSecsSinceEpoch() + clockSkew_;
}

void GfxChatPanel::renderStatCard(StatCard &c)
{
	const QJsonObject s = streams_.value(c.platform).toObject();
	const QString name = platformName(c.platform);
	const bool connected = s.value(QStringLiteral("connected")).toBool();
	const bool live = connected && s.value(QStringLiteral("live")).toBool();
	const QJsonValue viewers = s.value(QStringLiteral("viewers"));
	const qint64 since = static_cast<qint64>(s.value(QStringLiteral("since")).toDouble());
	const QString title = s.value(QStringLiteral("title")).toString();
	const QString category = s.value(QStringLiteral("category")).toString();
	const QJsonValue followers = s.value(QStringLiteral("followers"));
	const QJsonValue subs = s.value(QStringLiteral("subscribers"));

	QString head, sub;
	QStringList tip;
	if (!connected) {
		head = QStringLiteral("<span style='color:#5b6270'>Not connected</span>");
		sub = QStringLiteral("Connect on goalforgex.com");
		tip << QStringLiteral(
			       "%1 isn't connected — connect it on goalforgex.com to see its chat and stats here.")
				.arg(name);
	} else if (s.value(QStringLiteral("pending")).toBool()) {
		head = QStringLiteral("<span style='color:#6b7280'>Checking…</span>");
		tip << name;
	} else if (live) {
		const bool hasViewers = viewers.isDouble();
		const qint64 v = static_cast<qint64>(viewers.toDouble());
		head = QStringLiteral(
			       "<span style='color:#ff3b4f'>●</span>&nbsp;<b style='color:#ffffff;font-size:11pt'>%1</b>"
			       "&nbsp;<span style='color:#9aa1b0;font-size:8pt'>%2</span>")
			       .arg(hasViewers ? fmtCount(v) : QStringLiteral("LIVE"),
				    hasViewers ? (v == 1 ? QStringLiteral("viewer") : QStringLiteral("viewers"))
					       : QString());
		QStringList bits;
		if (since > 0)
			bits << QStringLiteral("⏱ ") + fmtUptime(serverNow() - since);
		if (!category.isEmpty())
			bits << category;
		sub = bits.join(QStringLiteral(" · "));
		tip << QStringLiteral("%1 — LIVE").arg(name);
		if (hasViewers)
			tip << QStringLiteral("Viewers: ") + fmtCount(v);
		if (since > 0)
			tip << QStringLiteral("Uptime: ") + fmtUptime(serverNow() - since);
		if (!title.isEmpty())
			tip << QStringLiteral("Title: ") + title;
		if (!category.isEmpty())
			tip << QStringLiteral("Category: ") + category;
		if (c.platform == QLatin1String("youtube"))
			tip << QStringLiteral("(YouTube doesn't share viewer counts with chat apps)");
	} else {
		head = QStringLiteral("<span style='color:#9aa1b0;font-weight:600'>Offline</span>");
		tip << QStringLiteral("%1 — offline").arg(name);
		if (c.platform == QLatin1String("youtube") || c.platform == QLatin1String("tiktok"))
			tip << QStringLiteral("Shows as live a few seconds after you go live there.");
	}
	// Channel totals: in the sub line while offline, always in the tooltip.
	QStringList totals;
	if (connected && followers.isDouble()) {
		totals << fmtCount(static_cast<qint64>(followers.toDouble())) + QStringLiteral(" followers");
		tip << QStringLiteral("Followers: ") + fmtCount(static_cast<qint64>(followers.toDouble()));
	}
	if (connected && subs.isDouble()) {
		totals << fmtCount(static_cast<qint64>(subs.toDouble())) + QStringLiteral(" subs");
		tip << QStringLiteral("Active subscribers: ") + fmtCount(static_cast<qint64>(subs.toDouble()));
	}
	if (connected && !live)
		sub = totals.join(QStringLiteral(" · "));
	if (connected)
		tip << QStringLiteral("\nClick to open your %1 channel").arg(name);

	c.head->setText(head);
	c.sub->setText(c.sub->fontMetrics().elidedText(sub.isEmpty() ? QStringLiteral(" ") : sub, Qt::ElideRight,
						       qMax(40, c.sub->width())));
	c.box->setToolTip(tip.join(QLatin1Char('\n')));
	c.box->setCursor(connected ? Qt::PointingHandCursor : Qt::ArrowCursor);
	const QByteArray state = live ? c.platform.toLatin1() : QByteArray();
	theme::setState(c.box, state.constData());
}

void GfxChatPanel::layoutCards(const QStringList &visible)
{
	const QString key = visible.join(QLatin1Char(','));
	if (key == cardsKey_)
		return;
	cardsKey_ = key;
	const int n = static_cast<int>(visible.size());
	int slot = 0;
	for (StatCard *c : {&twCard_, &kkCard_, &ytCard_, &ttCard_}) {
		cardsGrid_->removeWidget(c->box);
		if (!visible.contains(c->platform)) {
			c->box->hide();
			continue;
		}
		const int row = slot / 2, col = slot % 2;
		const bool lone = slot == n - 1 && col == 0; // a last odd card spans the row
		cardsGrid_->addWidget(c->box, row, col, 1, lone ? 2 : 1);
		c->box->show();
		++slot;
	}
}

void GfxChatPanel::renderStats()
{
	// Only connected (and switched-on) platforms get a card — no "not connected" clutter.
	QStringList visible;
	for (const QString &p : wantedPlatforms())
		if (streams_.value(p).toObject().value(QStringLiteral("connected")).toBool())
			visible << p;
	const bool show = settings().chatStats && !visible.isEmpty();
	stats_->setVisible(show);
	if (!show)
		return;
	layoutCards(visible);
	for (StatCard *c : {&twCard_, &kkCard_, &ytCard_, &ttCard_})
		if (visible.contains(c->platform))
			renderStatCard(*c);

	QStringList parts;
	qint64 total = 0;
	int liveWithViewers = 0;
	bool anyLive = false;
	for (const QString &p : visible) {
		const QJsonObject s = streams_.value(p).toObject();
		if (!s.value(QStringLiteral("live")).toBool())
			continue;
		anyLive = true;
		if (s.value(QStringLiteral("viewers")).isDouble()) {
			total += static_cast<qint64>(s.value(QStringLiteral("viewers")).toDouble());
			++liveWithViewers;
		}
	}
	if (liveWithViewers >= 2)
		parts << QStringLiteral("<b style='color:#e8eaf0'>%1</b> watching in total").arg(fmtCount(total));

	// Chat pace from the messages the panel already has (host's own lines excluded).
	const qint64 now = serverNow();
	constexpr qint64 kChattersWindow = 10 * 60 * 1000, kPaceWindow = 5 * 60 * 1000;
	// A full buffer may not reach back 5 minutes — measure over what it holds.
	qint64 paceStart = now - kPaceWindow;
	if (msgs_.size() >= kKeep && msgs_.first().ts > paceStart)
		paceStart = msgs_.first().ts;
	QSet<QString> chatters;
	int paced = 0;
	for (const ChatMessage &m : msgs_) {
		if (m.host || m.ts <= 0)
			continue;
		if (now - m.ts <= kChattersWindow)
			chatters.insert(m.platform + QLatin1Char(':') + m.login.toLower());
		if (m.ts >= paceStart)
			++paced;
	}
	// Nothing live and nobody chatting → just the cards.
	summary_->setVisible(anyLive || !chatters.isEmpty());
	const double minutes = qMax(1.0, (now - paceStart) / 60000.0);
	const double pace = paced / minutes;
	parts << QStringLiteral("<b style='color:#e8eaf0'>%1</b> chatter%2")
			 .arg(chatters.size())
			 .arg(chatters.size() == 1 ? QString() : QStringLiteral("s"));
	parts << QStringLiteral("<b style='color:#e8eaf0'>%1</b> msgs/min")
			 .arg(pace < 10 ? QString::number(pace, 'f', 1) : QString::number(qRound(pace)));
	summary_->setText(parts.join(QStringLiteral("&nbsp;&nbsp;·&nbsp;&nbsp;")));
}

bool GfxChatPanel::eventFilter(QObject *watched, QEvent *event)
{
	const QEvent::Type t = event->type();

	// Stats card click → that channel.
	if (t == QEvent::MouseButtonRelease && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
		for (const StatCard *c : {&twCard_, &kkCard_, &ytCard_, &ttCard_}) {
			if (watched != c->box)
				continue;
			// Only ever the streamer's own channel page on that platform.
			const PlatformInfo *pi = info(c->platform);
			const QString url =
				streams_.value(c->platform).toObject().value(QStringLiteral("url")).toString();
			if (pi && url.startsWith(QLatin1String(pi->channel)))
				QDesktopServices::openUrl(QUrl(url));
			return true;
		}
	}

	if (view_ && watched == view_->viewport()) {
		if (t == QEvent::Enter && settings().chatPauseOnHover) {
			hoverPaused_ = true;
			updateJump();
		} else if (t == QEvent::Leave && hoverPaused_) {
			hoverPaused_ = false;
			if (follow_ && !paused_)
				scrollToBottom();
			updateJump();
		} else if (t == QEvent::Wheel) {
			auto *we = static_cast<QWheelEvent *>(event);
			if (we->modifiers() & Qt::ControlModifier) {
				const int dy = we->angleDelta().y();
				if (dy != 0)
					zoom(dy > 0 ? 1 : -1);
				return true;
			}
		}
	}

	if (t == QEvent::KeyPress) {
		auto *ke = static_cast<QKeyEvent *>(event);
		if (watched == input_ && composerKey(ke))
			return true;
		if (watched == searchEdit_ && ke->key() == Qt::Key_Escape) {
			searchBtn_->setChecked(false);
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void GfxChatPanel::setSendHandler(SendFn fn)
{
	send_ = std::move(fn);
}

void GfxChatPanel::setModHandler(ModFn fn)
{
	mod_ = std::move(fn);
}

void GfxChatPanel::showUserMenu(const QString &platform, const QString &login, const QString &messageId)
{
	const ChatMessage *msg = nullptr;
	if (!messageId.isEmpty())
		for (const ChatMessage &m : msgs_)
			if (m.id == messageId && m.platform == platform)
				msg = &m;
	const QString platName = platformName(platform);
	const PlatformInfo *pinfo = info(platform);
	const bool sendable = pinfo && pinfo->canSend;
	const QString shown = msg ? msg->user : login;
	const bool isHost = msg && msg->host;
	const QString msgText = msg ? msg->text : QString();

	// How much they've said in what the panel holds.
	int said = 0;
	for (const ChatMessage &m : msgs_)
		if (m.platform == platform && m.login.compare(login, Qt::CaseInsensitive) == 0)
			++said;

	QMenu menu(this);
	QAction *head = menu.addAction(QIcon(QPixmap::fromImage(platformIcon(platform, 16, devicePixelRatioF()))),
				       QStringLiteral("%1  ·  %2  ·  %3 message%4")
					       .arg(shown, platName)
					       .arg(said)
					       .arg(said == 1 ? QString() : QStringLiteral("s")));
	head->setEnabled(false);
	menu.addSeparator();

	if (!isHost && sendable && platformConnected(platform))
		connect(menu.addAction(QStringLiteral("💬   Reply to %1").arg(shown)), &QAction::triggered, this,
			[this, platform, login, shown] {
				// Reply goes to the chatter's own platform.
				toTwitch_->setChecked(platform == QLatin1String("twitch"));
				toKick_->setChecked(platform == QLatin1String("kick"));
				toYouTube_->setChecked(platform == QLatin1String("youtube"));
				updateSendState();
				// YouTube logins are channel ids — mention the name people see.
				const QString handle = platform == QLatin1String("youtube") ? shown : login;
				const QString at = QStringLiteral("@") + handle + QLatin1Char(' ');
				QString t = input_->text();
				if (!t.startsWith(at, Qt::CaseInsensitive))
					t = at + t;
				input_->setText(t);
				input_->setFocus();
				input_->setCursorPosition(t.size());
			});
	connect(menu.addAction(QStringLiteral("🔎   Show only %1's messages").arg(shown)), &QAction::triggered, this,
		[this, platform, login, shown] { setUserFilter(platform, login, shown); });
	connect(menu.addAction(QStringLiteral("👤   Open %1 profile").arg(platName)), &QAction::triggered, this,
		[platform, login] { openProfile(platform, login); });
	const QString copyName = platform == QLatin1String("youtube") ? shown : login;
	connect(menu.addAction(QStringLiteral("📋   Copy username")), &QAction::triggered, this,
		[copyName] { QGuiApplication::clipboard()->setText(copyName); });
	if (msg && !msgText.isEmpty())
		connect(menu.addAction(QStringLiteral("📄   Copy message")), &QAction::triggered, this,
			[msgText] { QGuiApplication::clipboard()->setText(msgText); });

	// YouTube / TikTok moderation happens on those sites.
	if (!isHost && !canMod(platform)) {
		menu.addSeparator();
		menu.addAction(QStringLiteral("Moderate %1 chat on %1 itself").arg(platName))->setEnabled(false);
	}
	// The broadcaster can't be moderated — the rest is for everyone else.
	if (!isHost && mod_ && canMod(platform)) {
		menu.addSeparator();
		if (msg && !msg->deleted && msgIdRe().match(messageId).hasMatch())
			connect(menu.addAction(QStringLiteral("🗑   Delete this message")), &QAction::triggered, this,
				[this, platform, login, messageId, shown] {
					mod_(platform, QStringLiteral("delete"), login, 0, messageId, shown);
				});
		QMenu *timeout = menu.addMenu(QStringLiteral("⏱   Timeout"));
		const QList<QPair<QString, int>> lengths = {
			{QStringLiteral("1 minute"), 60},   {QStringLiteral("10 minutes"), 600},
			{QStringLiteral("1 hour"), 3600},   {QStringLiteral("1 day"), 86400},
			{QStringLiteral("1 week"), 604800},
		};
		for (const QPair<QString, int> &len : lengths) {
			const int secs = len.second;
			connect(timeout->addAction(len.first), &QAction::triggered, this,
				[this, platform, login, messageId, shown, secs] {
					mod_(platform, QStringLiteral("timeout"), login, secs, messageId, shown);
				});
		}
		connect(menu.addAction(QStringLiteral("🚫   Ban")), &QAction::triggered, this,
			[this, platform, login, messageId, shown, platName] {
				if (QMessageBox::question(
					    this, QStringLiteral("Ban %1?").arg(shown),
					    QStringLiteral(
						    "Permanently ban %1 from your %2 chat? You can unban them later.")
						    .arg(shown, platName)) == QMessageBox::Yes)
					mod_(platform, QStringLiteral("ban"), login, 0, messageId, shown);
			});
		connect(menu.addAction(QStringLiteral("↩   Unban")), &QAction::triggered, this,
			[this, platform, login, messageId, shown] {
				mod_(platform, QStringLiteral("unban"), login, 0, messageId, shown);
			});
	}
	menu.exec(QCursor::pos());
}

void GfxChatPanel::showOptionsMenu()
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
			refresh();
		});
	};
	toggle(QStringLiteral("Highlight messages that mention me"), &s.chatHighlightMentions);
	toggle(QStringLiteral("Quick delete / timeout buttons"), &s.chatQuickMod);
	toggle(QStringLiteral("Keep the text of deleted messages"), &s.chatShowDeleted);
	toggle(QStringLiteral("Show the time on each message"), &s.chatTimestamps);
	toggle(QStringLiteral("Shade every other message"), &s.chatAltRows);
	toggle(QStringLiteral("Pause while my mouse is over the chat"), &s.chatPauseOnHover);
	toggle(QStringLiteral("Viewers, uptime and stream info"), &s.chatStats);
	toggle(QStringLiteral("Platform icon on each message"), &s.chatIcons);
	toggle(QStringLiteral("HOST / MOD / VIP / SUB badges"), &s.chatBadges);
	toggle(QStringLiteral("Chatters' own name colours"), &s.chatNameColors);
	toggle(QStringLiteral("Compact lines"), &s.chatCompact);
	menu.addSeparator();

	const QString words = s.chatHighlightWords.isEmpty() ? QStringLiteral("none")
							     : s.chatHighlightWords.join(QStringLiteral(", "));
	connect(menu.addAction(QStringLiteral("✨   Highlight words… (%1)")
				       .arg(QFontMetrics(menu.font()).elidedText(words, Qt::ElideRight, 160))),
		&QAction::triggered, this, [this] {
			bool ok = false;
			const QString text = QInputDialog::getText(
				this, QStringLiteral("Highlight words"),
				QStringLiteral(
					"Messages with any of these words get highlighted and show under @.\nYour own channel name is always included. Separate words with commas."),
				QLineEdit::Normal, settings().chatHighlightWords.join(QStringLiteral(", ")), &ok);
			if (!ok)
				return;
			QStringList list;
			for (const QString &w : text.split(QLatin1Char(','))) {
				const QString t = w.trimmed().left(40);
				if (!t.isEmpty() && !list.contains(t, Qt::CaseInsensitive) && list.size() < 30)
					list << t;
			}
			settings().chatHighlightWords = list;
			saveSettings();
			refresh();
		});

	QMenu *size = menu.addMenu(QStringLiteral("🔠   Text size"));
	connect(size->addAction(QStringLiteral("Bigger  (Ctrl + scroll up)")), &QAction::triggered, this,
		[this] { zoom(1); });
	connect(size->addAction(QStringLiteral("Smaller  (Ctrl + scroll down)")), &QAction::triggered, this,
		[this] { zoom(-1); });
	connect(size->addAction(QStringLiteral("Normal")), &QAction::triggered, this, [this] {
		settings().chatFontSize = 0;
		saveSettings();
		applyFontSize();
		rebuild();
	});
	menu.addSeparator();
	connect(menu.addAction(QStringLiteral("🧹   Clear this feed (only here)")), &QAction::triggered, this, [this] {
		msgs_.clear();
		follow_ = true;
		pendingNew_ = 0;
		renderStats();
		rebuild();
	});
	menu.exec(moreBtn_->mapToGlobal(QPoint(0, moreBtn_->height())));
}

void GfxChatPanel::setLocked(const QString &message)
{
	const bool locked = !message.isEmpty();
	lockLbl_->setText(
		QStringLiteral(
			"<b style='color:#fff'>MultiChat — Twitch, Kick, YouTube &amp; TikTok in OBS</b><br><br>%1<br>Read every chat in one feed and reply without leaving OBS.")
			.arg(message.toHtmlEscaped()));
	lockLbl_->setTextFormat(Qt::RichText);
	lockPanel_->setVisible(locked);
	main_->setVisible(!locked);
}

bool GfxChatPanel::platformConnected(const QString &p) const
{
	return wantedPlatforms().contains(p) && status_.value(p).toObject().value(QStringLiteral("connected")).toBool();
}

QList<QPushButton *> GfxChatPanel::targetButtons() const
{
	return {toTwitch_, toKick_, toYouTube_};
}

int GfxChatPanel::applyPoll(const QJsonObject &o)
{
	status_ = o.value(QStringLiteral("status")).toObject();
	streams_ = o.value(QStringLiteral("streams")).toObject();
	const double serverTime = o.value(QStringLiteral("serverTime")).toDouble();
	if (serverTime > 0)
		clockSkew_ = static_cast<qint64>(serverTime) - QDateTime::currentMSecsSinceEpoch();
	renderStatus();
	updateFilters();
	updateTargets();
	const bool namesChanged = rebuildHighlightRe();

	const bool reset = o.value(QStringLiteral("reset")).toBool();
	if (reset)
		msgs_.clear();
	int added = 0;
	QList<qint64> fresh;
	QSet<qint64> edited;
	const QJsonArray items = o.value(QStringLiteral("items")).toArray();
	// index loop: a range-for over QJsonArray binds to temporaries (-Wrange-loop-analysis on macOS)
	for (qsizetype i = 0; i < items.size(); ++i) {
		const QJsonObject it = items.at(i).toObject();
		if (it.value(QStringLiteral("kind")).toString() == QLatin1String("delete")) {
			const QString id = it.value(QStringLiteral("id")).toString();
			const QString login = it.value(QStringLiteral("login")).toString();
			const QString plat = it.value(QStringLiteral("platform")).toString();
			for (ChatMessage &m : msgs_) {
				const bool hit = !id.isEmpty() ? m.id == id
							       : (!login.isEmpty() &&
								  m.login.compare(login, Qt::CaseInsensitive) == 0 &&
								  (plat.isEmpty() || m.platform == plat));
				if (hit && !m.deleted) {
					m.deleted = true;
					edited.insert(m.seq);
				}
			}
			continue;
		}
		ChatMessage m;
		m.id = it.value(QStringLiteral("id")).toString();
		m.platform = it.value(QStringLiteral("platform")).toString();
		m.user = it.value(QStringLiteral("user")).toString();
		m.login = it.value(QStringLiteral("login")).toString();
		m.color = it.value(QStringLiteral("color")).toString();
		m.text = it.value(QStringLiteral("text")).toString();
		m.replyTo = it.value(QStringLiteral("replyTo")).toString();
		m.host = it.value(QStringLiteral("host")).toBool();
		m.mod = it.value(QStringLiteral("mod")).toBool();
		m.vip = it.value(QStringLiteral("vip")).toBool();
		m.sub = it.value(QStringLiteral("sub")).toBool();
		m.first = it.value(QStringLiteral("first")).toBool();
		m.bits = it.value(QStringLiteral("bits")).toInt();
		m.ts = static_cast<qint64>(it.value(QStringLiteral("ts")).toDouble());
		if (!info(m.platform))
			continue;
		m.seq = nextSeq_++;
		msgs_.push_back(m);
		fresh.push_back(m.seq);
		if (matches(m))
			++added;
	}
	while (msgs_.size() > kKeep)
		msgs_.removeFirst();
	renderStats();

	if (reset || (namesChanged && !msgs_.isEmpty())) {
		rebuild();
		return reset ? 0 : added;
	}

	// Only what changed touches the feed: old lines off the top, deleted lines
	// redrawn in place, new lines added at the bottom.
	trimTop();
	{
		QTextCursor batch(view_->document());
		batch.beginEditBlock();
		for (const qint64 seq : edited)
			updateLine(seq);
		const qint64 oldest = msgs_.isEmpty() ? nextSeq_ : msgs_.first().seq;
		for (const qint64 seq : fresh) {
			if (seq < oldest)
				continue; // already pushed out by this same batch
			const ChatMessage *m = findMsg(seq);
			if (!m || !matches(*m))
				continue;
			appendLine(*m);
			if (!autoScrolling())
				++pendingNew_;
		}
		batch.endEditBlock();
	}
	if (shown_.isEmpty())
		showPlaceholder(); // refresh its wording (connected / filters)
	updateJump();
	return added;
}

// ── Filtering + highlighting ────────────────────────────────────

bool GfxChatPanel::isHighlighted(const ChatMessage &m) const
{
	if (m.host || highlightRe_.pattern().isEmpty() || !highlightRe_.isValid())
		return false;
	return highlightRe_.match(m.text).hasMatch() ||
	       (!m.replyTo.isEmpty() && highlightRe_.match(m.replyTo).hasMatch());
}

bool GfxChatPanel::rebuildHighlightRe()
{
	QStringList terms;
	for (const QString &p : wantedPlatforms()) {
		const QJsonObject st = status_.value(p).toObject();
		if (!st.value(QStringLiteral("connected")).toBool())
			continue;
		const QString name = st.value(QStringLiteral("name")).toString().trimmed();
		if (name.size() >= 2 && !terms.contains(name, Qt::CaseInsensitive))
			terms << name;
	}
	for (const QString &w : settings().chatHighlightWords)
		if (!w.trimmed().isEmpty() && !terms.contains(w.trimmed(), Qt::CaseInsensitive))
			terms << w.trimmed();
	QString pattern;
	if (!terms.isEmpty()) {
		QStringList esc;
		for (const QString &t : terms)
			esc << QRegularExpression::escape(t);
		// Whole words, with or without @ in front.
		pattern = QStringLiteral("(?<![\\w])@?(?:%1)(?![\\w])").arg(esc.join(QLatin1Char('|')));
	}
	if (pattern == highlightRe_.pattern())
		return false;
	highlightRe_ = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption |
							   QRegularExpression::UseUnicodePropertiesOption);
	return true;
}

bool GfxChatPanel::matches(const ChatMessage &m) const
{
	if (!wantedPlatforms().contains(m.platform))
		return false;
	if (info(filter_) && m.platform != filter_)
		return false;
	if (filter_ == QLatin1String("mentions") && !isHighlighted(m))
		return false;
	if (!userFilterLogin_.isEmpty() &&
	    (m.platform != userFilterPlatform_ || m.login.compare(userFilterLogin_, Qt::CaseInsensitive) != 0))
		return false;
	if (!search_.isEmpty() && !m.text.contains(search_, Qt::CaseInsensitive) &&
	    !m.user.contains(search_, Qt::CaseInsensitive) && !m.login.contains(search_, Qt::CaseInsensitive))
		return false;
	return true;
}

// ── Line HTML ───────────────────────────────────────────────────

qreal GfxChatPanel::baseFontPt() const
{
	view_->ensurePolished();
	const qreal pt = QFontInfo(view_->font()).pointSizeF();
	return pt > 0 ? pt : 9.0;
}

QString GfxChatPanel::textHtml(const QString &raw) const
{
	// One message = one text block: no paragraph breaks inside a line.
	QString text = raw;
	text.replace(QChar::ParagraphSeparator, QLatin1Char(' '));
	text.replace(QChar::LineSeparator, QLatin1Char(' '));
	text.replace(QLatin1Char('\n'), QLatin1Char(' '));
	text.replace(QLatin1Char('\r'), QLatin1Char(' '));

	QString out;
	qsizetype last = 0;
	auto it = urlRe().globalMatch(text);
	while (it.hasNext()) {
		const QRegularExpressionMatch m = it.next();
		QString url = m.captured(0);
		// Sentence punctuation after a link isn't part of it.
		while (!url.isEmpty() && QStringLiteral(".,!?;:)]}").contains(url.back()))
			url.chop(1);
		if (url.size() < 9)
			continue;
		out += text.mid(last, m.capturedStart() - last).toHtmlEscaped();
		const QString shown = url.size() > 60 ? url.left(57) + QStringLiteral("…") : url;
		out += QStringLiteral("<a href=\"%1\" style='color:#7aa2ff;'>%2</a>")
			       .arg(url.toHtmlEscaped(), shown.toHtmlEscaped());
		last = m.capturedStart() + url.size();
	}
	out += text.mid(last).toHtmlEscaped();
	return out;
}

QString GfxChatPanel::lineInnerHtml(const ChatMessage &m) const
{
	const Settings &s = settings();
	const qreal base = baseFontPt();
	const qreal tagPt = qMax<qreal>(6.0, base * 0.78);
	const qreal smallPt = qMax<qreal>(6.5, base * 0.88);
	const int iconPx = qBound(12, qRound(base * 1.65), 32);
	const bool validLogin = loginRe().match(m.login).hasMatch();
	const bool validId = msgIdRe().match(m.id).hasMatch();

	QString h;
	// One-click moderation, for moderators who live in this tab.
	if (s.chatQuickMod && mod_ && canMod(m.platform) && !m.host && validLogin && validId && !m.deleted)
		h += QStringLiteral(
			     "<a href='mod:delete:%1:%2:%3' style='color:#6b7280;text-decoration:none;'>🗑</a>"
			     "&nbsp;<a href='mod:timeout:%1:%2:%3' style='color:#6b7280;text-decoration:none;'>⏱</a>"
			     "&nbsp;")
			     .arg(m.platform, m.login, m.id);
	// platform icon (served by ChatView::loadResource)
	if (s.chatIcons)
		h += QStringLiteral("<img src='gfx:%1/%2' width='%2' height='%2' style='vertical-align:middle'>&nbsp;")
			     .arg(m.platform)
			     .arg(iconPx);
	if (s.chatTimestamps && m.ts > 0)
		h += QStringLiteral("<span style='color:#5b6270;font-size:%2pt;'>%1</span> ")
			     .arg(QDateTime::fromMSecsSinceEpoch(m.ts).toString(QStringLiteral("HH:mm")),
				  QString::number(smallPt, 'f', 1));
	if (s.chatBadges) {
		if (m.host)
			h += tag("HOST", "#ff3b4f", tagPt);
		else if (m.mod)
			h += tag("MOD", "#34d399", tagPt);
		if (m.vip)
			h += tag("VIP", "#f472b6", tagPt);
		if (m.sub)
			h += tag(m.platform == QLatin1String("youtube") ? "MEMBER" : "SUB", "#ffb020", tagPt);
	}
	const QString nameColor = (!s.chatNameColors || m.color.isEmpty()) ? QStringLiteral("#c9ced8")
									   : readableColor(m.color);
	if (validLogin)
		h += QStringLiteral("<a href='user:%1:%2:%3' title='Click for options — reply, profile, timeout, ban' "
				    "style='color:%4;text-decoration:none;font-weight:bold;'>%5</a>")
			     .arg(m.platform, m.login, validId ? m.id : QString(), nameColor, m.user.toHtmlEscaped());
	else
		h += QStringLiteral("<b style='color:%1'>%2</b>").arg(nameColor, m.user.toHtmlEscaped());
	h += QStringLiteral("<span style='color:#5b6270'>:</span> ");
	if (m.deleted && !s.chatShowDeleted) {
		h += QStringLiteral("<i style='color:#5b6270'>message deleted</i>");
		return h;
	}
	if (!m.replyTo.isEmpty())
		h += QStringLiteral("<span style='color:#6b7280'>↪ @%1</span> ").arg(m.replyTo.toHtmlEscaped());
	if (m.first && !m.deleted)
		h += QStringLiteral("<span style='color:#34d399;font-weight:700'>✨ first message</span> ");
	if (m.bits > 0)
		h += QStringLiteral("<span style='color:#c084fc;font-weight:700'>💎 %1</span> ").arg(m.bits);
	if (m.deleted)
		h += QStringLiteral(
			     "<s style='color:#5b6270'>%1</s> <i style='color:#5b6270;font-size:%2pt'>(deleted)</i>")
			     .arg(m.text.toHtmlEscaped(), QString::number(smallPt, 'f', 1));
	else
		h += QStringLiteral("<span style='color:#e8eaf0'>%1</span>").arg(textHtml(m.text));
	return h;
}

// ── Feed ────────────────────────────────────────────────────────

const ChatMessage *GfxChatPanel::findMsg(qint64 seq) const
{
	// msgs_ is ordered by seq — binary search.
	qsizetype lo = 0, hi = msgs_.size() - 1;
	while (lo <= hi) {
		const qsizetype mid = (lo + hi) / 2;
		const qint64 s = msgs_.at(mid).seq;
		if (s == seq)
			return &msgs_.at(mid);
		if (s < seq)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return nullptr;
}

void GfxChatPanel::appendLine(const ChatMessage &m)
{
	++programmatic_;
	QTextDocument *doc = view_->document();
	if (placeholder_) {
		doc->clear();
		shown_.clear();
		placeholder_ = false;
	}
	const bool odd = !shown_.isEmpty() && !shown_.last().odd;
	const Settings &s = settings();
	const QTextBlockFormat fmt = lineFormat(m.host, s.chatHighlightMentions && isHighlighted(m),
						m.first && !m.deleted, s.chatAltRows && odd);
	QTextCursor c(doc);
	c.movePosition(QTextCursor::End);
	if (!shown_.isEmpty())
		c.insertBlock(fmt, QTextCharFormat());
	c.setCharFormat(QTextCharFormat());
	c.insertHtml(lineInnerHtml(m));
	// insertHtml into an empty block can bring its own block format — ours wins.
	c.setBlockFormat(fmt);
	shown_.push_back({m.seq, odd});
	--programmatic_;
}

void GfxChatPanel::updateLine(qint64 seq)
{
	if (placeholder_)
		return;
	int idx = -1;
	for (int i = shown_.size() - 1; i >= 0; --i)
		if (shown_.at(i).seq == seq) {
			idx = i;
			break;
		}
	const ChatMessage *m = findMsg(seq);
	if (idx < 0 || !m)
		return;
	++programmatic_;
	const Settings &s = settings();
	QTextBlock b = view_->document()->findBlockByNumber(idx);
	QTextCursor c(b);
	c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
	c.removeSelectedText();
	c.setCharFormat(QTextCharFormat());
	c.insertHtml(lineInnerHtml(*m));
	c.setBlockFormat(lineFormat(m->host, s.chatHighlightMentions && isHighlighted(*m), m->first && !m->deleted,
				    s.chatAltRows && shown_.at(idx).odd));
	--programmatic_;
}

void GfxChatPanel::trimTop()
{
	if (placeholder_ || shown_.isEmpty())
		return;
	const qint64 oldest = msgs_.isEmpty() ? nextSeq_ : msgs_.first().seq;
	int n = 0;
	while (n < shown_.size() && shown_.at(n).seq < oldest)
		++n;
	if (!n)
		return;
	++programmatic_;
	QTextDocument *doc = view_->document();
	QScrollBar *sb = view_->verticalScrollBar();
	if (n >= shown_.size()) {
		doc->clear();
		shown_.clear();
	} else {
		// A reader who scrolled up keeps the same line at the top of the view:
		// remember that line and how far into it they are, then put it back.
		QAbstractTextDocumentLayout *lay = doc->documentLayout();
		const bool keep = !autoScrolling();
		QTextBlock anchor;
		qreal offset = 0;
		if (keep) {
			anchor = view_->cursorForPosition(QPoint(0, 0)).block();
			if (!anchor.isValid() || anchor.blockNumber() < n)
				anchor = doc->findBlockByNumber(n);
			offset = sb->value() - lay->blockBoundingRect(anchor).top();
		}
		QTextCursor c(doc->firstBlock());
		c.setPosition(doc->findBlockByNumber(n).position(), QTextCursor::KeepAnchor);
		c.removeSelectedText();
		shown_.erase(shown_.begin(), shown_.begin() + n);
		// The merged first block may keep the removed line's format — restore its own.
		if (const ChatMessage *m = findMsg(shown_.first().seq)) {
			const Settings &s = settings();
			QTextCursor fc(doc->firstBlock());
			fc.setBlockFormat(lineFormat(m->host, s.chatHighlightMentions && isHighlighted(*m),
						     m->first && !m->deleted, s.chatAltRows && shown_.first().odd));
		}
		if (keep && anchor.isValid())
			sb->setValue(qMax(0, qRound(lay->blockBoundingRect(anchor).top() + offset)));
	}
	--programmatic_;
}

void GfxChatPanel::showPlaceholder()
{
	++programmatic_;
	QTextDocument *doc = view_->document();
	doc->clear();
	shown_.clear();
	placeholder_ = true;
	QString text;
	const bool filtered = !search_.isEmpty() || !userFilterLogin_.isEmpty() || filter_ != QLatin1String("all");
	bool anyConnected = false;
	for (const QString &p : wantedPlatforms())
		anyConnected = anyConnected || platformConnected(p);
	if (!anyConnected)
		text = QStringLiteral(
			"Connect Twitch, Kick, YouTube or TikTok on goalforgex.com to see your chat here.");
	else if (filter_ == QLatin1String("mentions") && search_.isEmpty() && userFilterLogin_.isEmpty())
		text = QStringLiteral(
			"No mentions yet. Messages with your name or a highlight word (⋯ → Highlight words) show up here.");
	else if (filtered)
		text = QStringLiteral("No messages match right now — new ones that do will show up here.");
	else
		text = QStringLiteral("No messages yet — chat shows up here as it comes in.");
	QTextBlockFormat f;
	f.setAlignment(Qt::AlignCenter);
	f.setTopMargin(24);
	QTextCursor c(doc);
	c.insertHtml(QStringLiteral("<span style='color:#6b7280'>%1</span>").arg(text.toHtmlEscaped()));
	c.setBlockFormat(f);
	--programmatic_;
}

void GfxChatPanel::rebuild()
{
	QScrollBar *sb = view_->verticalScrollBar();
	const bool toBottom = follow_;
	const double ratio = sb->maximum() > 0 ? double(sb->value()) / sb->maximum() : 1.0;

	++programmatic_;
	{
		// One edit block: the document lays out once at the end, not per line.
		QTextCursor batch(view_->document());
		batch.beginEditBlock();
		view_->document()->clear();
		shown_.clear();
		placeholder_ = false;
		for (const ChatMessage &m : msgs_)
			if (matches(m))
				appendLine(m);
		if (shown_.isEmpty())
			showPlaceholder();
		batch.endEditBlock();
	}
	--programmatic_;

	if (toBottom) {
		pendingNew_ = 0;
		if (!paused_ && !hoverPaused_)
			scrollToBottom();
	} else {
		++programmatic_;
		sb->setValue(qRound(ratio * sb->maximum()));
		--programmatic_;
	}
	updateJump();
}

// ── Scrolling ───────────────────────────────────────────────────

bool GfxChatPanel::autoScrolling() const
{
	return follow_ && !paused_ && !hoverPaused_;
}

void GfxChatPanel::scrollToBottom()
{
	follow_ = true;
	pendingNew_ = 0;
	++programmatic_;
	QScrollBar *sb = view_->verticalScrollBar();
	sb->setValue(sb->maximum());
	--programmatic_;
	updateJump();
}

void GfxChatPanel::setPaused(bool paused)
{
	paused_ = paused;
	pauseBtn_->setChecked(paused);
	pauseBtn_->setText(paused ? QStringLiteral("▶") : QStringLiteral("⏸"));
	pauseBtn_->setToolTip(
		paused ? QStringLiteral("Resume the chat")
		       : QStringLiteral("Pause the chat so it stops moving (new messages still come in)"));
	if (!paused && !hoverPaused_)
		scrollToBottom();
	updateJump();
}

void GfxChatPanel::updateJump()
{
	if (!jump_)
		return;
	const QString n = QStringLiteral("%1 new message%2")
				  .arg(pendingNew_)
				  .arg(pendingNew_ == 1 ? QString() : QStringLiteral("s"));
	QString text;
	if (paused_)
		text = pendingNew_ ? QStringLiteral("⏸  Paused · %1 — click to resume").arg(n)
				   : QStringLiteral("⏸  Chat paused — click to resume");
	else if (hoverPaused_)
		text = pendingNew_ ? QStringLiteral("⏸  Paused while your mouse is on the chat · %1").arg(n)
				   : QString();
	else if (!follow_)
		text = pendingNew_ ? QStringLiteral("↓  %1").arg(n) : QStringLiteral("↓  Back to live chat");
	jump_->setText(text);
	jump_->setVisible(!text.isEmpty());
}

// ── Header + composer ───────────────────────────────────────────

void GfxChatPanel::renderStatus()
{
	// grey = not connected, amber = connecting / needs reconnecting, green = connected
	QStringList tips;
	const auto dot = [this, &tips](const QString &p, const QString &label) {
		const QJsonObject s = status_.value(p).toObject();
		QString color = QStringLiteral("#4b5160");
		QString state = QStringLiteral("not connected");
		if (s.value(QStringLiteral("connected")).toBool()) {
			const bool trouble = !s.value(QStringLiteral("tokenValid")).toBool(true) ||
					     !s.value(QStringLiteral("ready")).toBool(true);
			color = trouble ? QStringLiteral("#ffb020") : QStringLiteral("#34d399");
			state = s.value(QStringLiteral("name")).toString() +
				(trouble ? QStringLiteral(" — connecting / reconnect needed")
					 : QStringLiteral(" — connected"));
		}
		tips << QStringLiteral("%1: %2").arg(label, state);
		return QStringLiteral("<span style='color:%1'>●</span>").arg(color);
	};
	QStringList dots;
	for (const QString &p : wantedPlatforms())
		dots << dot(p, platformName(p));
	statusLbl_->setText(dots.join(QStringLiteral("&thinsp;")));
	statusLbl_->setToolTip(tips.join(QLatin1Char('\n')));
}

// Platform chips: only connected ones, and only while there's more than one.
void GfxChatPanel::updateFilters()
{
	const QList<QPair<QString, QPushButton *>> chips = {{QStringLiteral("twitch"), fTwitch_},
							    {QStringLiteral("kick"), fKick_},
							    {QStringLiteral("youtube"), fYouTube_},
							    {QStringLiteral("tiktok"), fTikTok_}};
	int connected = 0;
	for (const auto &c : chips)
		if (platformConnected(c.first))
			++connected;
	for (const auto &c : chips)
		c.second->setVisible(connected > 1 && platformConnected(c.first));
	if (info(filter_) && (connected <= 1 || !platformConnected(filter_)))
		setFilter(QStringLiteral("all"));
}

// Send targets: Twitch / Kick / YouTube, each only while connected. TikTok has none.
void GfxChatPanel::updateTargets()
{
	int shown = 0;
	QPushButton *only = nullptr;
	for (QPushButton *b : targetButtons()) {
		const QString p = b == toKick_
					  ? QStringLiteral("kick")
					  : (b == toYouTube_ ? QStringLiteral("youtube") : QStringLiteral("twitch"));
		const bool on = platformConnected(p);
		b->setEnabled(on);
		b->setVisible(on || p != QLatin1String("youtube")); // YouTube's button only when it's there
		b->setToolTip(on ? QStringLiteral("Send to %1 chat").arg(platformName(p))
				 : QStringLiteral("Connect %1 on goalforgex.com").arg(platformName(p)));
		if (on) {
			++shown;
			only = b;
		}
	}
	// Only one place to send → that's where messages go.
	if (shown == 1 && only)
		only->setChecked(true);
	readOnlyLbl_->setVisible(shown == 0 && platformConnected(QStringLiteral("tiktok")));
	updateSendState();
}

void GfxChatPanel::updateSendState()
{
	QStringList where;
	for (QPushButton *b : targetButtons())
		if (b->isChecked() && b->isEnabled())
			where << b->text();
	sendBtn_->setEnabled(!sending_ && !where.isEmpty() && !input_->text().trimmed().isEmpty());
	input_->setEnabled(!sending_);
	QString label;
	if (where.size() == 1)
		label = where.first() + QStringLiteral(" chat");
	else if (where.size() == 2)
		label = where.join(QStringLiteral(" and ")) + QStringLiteral(" chat");
	else if (where.size() > 2)
		label = QStringLiteral("every chat");
	input_->setPlaceholderText(label.isEmpty() ? QStringLiteral("Connect Twitch, Kick or YouTube to chat")
						   : QStringLiteral("Message %1…").arg(label));
}

void GfxChatPanel::setFilter(const QString &f)
{
	filter_ = f;
	fAll_->setChecked(f == QLatin1String("all"));
	fTwitch_->setChecked(f == QLatin1String("twitch"));
	fKick_->setChecked(f == QLatin1String("kick"));
	fYouTube_->setChecked(f == QLatin1String("youtube"));
	fTikTok_->setChecked(f == QLatin1String("tiktok"));
	fMentions_->setChecked(f == QLatin1String("mentions"));
	follow_ = true;
	rebuild();
}

void GfxChatPanel::setUserFilter(const QString &platform, const QString &login, const QString &shown)
{
	userFilterPlatform_ = platform;
	userFilterLogin_ = login;
	userChip_->setText(QStringLiteral("Only %1  ✕").arg(shown));
	userChip_->setChecked(true);
	userChip_->setVisible(!login.isEmpty());
	searchRow_->setVisible(!login.isEmpty() || searchBtn_->isChecked());
	follow_ = true;
	rebuild();
}

void GfxChatPanel::doSend()
{
	const QString text = input_->text().trimmed();
	if (sending_ || text.isEmpty() || !send_)
		return;
	QStringList platforms;
	if (toTwitch_->isChecked() && toTwitch_->isEnabled())
		platforms << QStringLiteral("twitch");
	if (toKick_->isChecked() && toKick_->isEnabled())
		platforms << QStringLiteral("kick");
	if (toYouTube_->isChecked() && toYouTube_->isEnabled())
		platforms << QStringLiteral("youtube");
	if (platforms.isEmpty())
		return;
	if (history_.isEmpty() || history_.last() != text) {
		history_ << text;
		while (history_.size() > kHistory)
			history_.removeFirst();
	}
	historyPos_ = -1;
	tabIndex_ = -1;
	sending_ = true;
	sendBtn_->setText(QStringLiteral("…"));
	updateSendState();
	// Sending is "I'm back in the conversation" — show the latest.
	if (!paused_)
		scrollToBottom();
	send_(platforms, text);
}

void GfxChatPanel::sendFinished(bool anyDelivered)
{
	sending_ = false;
	sendBtn_->setText(QStringLiteral("Send"));
	if (anyDelivered)
		input_->clear();
	updateSendState();
	input_->setFocus();
}

void GfxChatPanel::clearChat()
{
	msgs_.clear();
	status_ = QJsonObject();
	streams_ = QJsonObject();
	pendingNew_ = 0;
	follow_ = true;
	renderStats();
	renderStatus();
	updateFilters();
	updateTargets();
	rebuildHighlightRe();
	rebuild();
}

void GfxChatPanel::refresh()
{
	if (!settings().chatPauseOnHover && hoverPaused_)
		hoverPaused_ = false;
	rebuildHighlightRe();
	applyFontSize();
	cardsKey_.clear();
	renderStatus();
	updateFilters();
	updateTargets();
	renderStats();
	rebuild();
}

void GfxChatPanel::applyFontSize()
{
	const int pt = settings().chatFontSize;
	// A stylesheet on the feed itself beats the theme's font, and only for the feed.
	view_->setStyleSheet(pt > 0 ? QStringLiteral("QTextBrowser#gfxChatView { font-size: %1pt; }").arg(pt)
				    : QString());
}

void GfxChatPanel::zoom(int steps)
{
	const int cur = settings().chatFontSize > 0 ? settings().chatFontSize : qRound(baseFontPt());
	const int next = qBound(7, cur + steps, 22);
	if (next == settings().chatFontSize)
		return;
	settings().chatFontSize = next;
	saveSettings();
	applyFontSize();
	rebuild();
}

bool GfxChatPanel::composerKey(QKeyEvent *e)
{
	const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
	switch (e->key()) {
	case Qt::Key_Tab:
	case Qt::Key_Backtab:
		if (mods)
			return false;
		completeName(e->key() == Qt::Key_Backtab || (e->modifiers() & Qt::ShiftModifier));
		return true; // never move focus out of the composer
	case Qt::Key_Up:
		if (history_.isEmpty())
			return false;
		if (historyPos_ < 0) {
			historyDraft_ = input_->text();
			historyPos_ = history_.size();
		}
		if (historyPos_ > 0) {
			--historyPos_;
			input_->setText(history_.at(historyPos_));
		}
		return true;
	case Qt::Key_Down:
		if (historyPos_ < 0)
			return false;
		++historyPos_;
		if (historyPos_ >= history_.size()) {
			historyPos_ = -1;
			input_->setText(historyDraft_);
		} else {
			input_->setText(history_.at(historyPos_));
		}
		return true;
	case Qt::Key_Escape:
		if (tabIndex_ >= 0 || historyPos_ >= 0) {
			tabIndex_ = -1;
			historyPos_ = -1;
			return true;
		}
		return false;
	default:
		return false;
	}
}

void GfxChatPanel::completeName(bool backwards)
{
	const QString text = input_->text();
	const int pos = input_->cursorPosition();
	if (tabIndex_ < 0) {
		int start = pos;
		while (start > 0 && !text.at(start - 1).isSpace())
			--start;
		const QString word = text.mid(start, pos - start);
		const bool at = word.startsWith(QLatin1Char('@'));
		const QString prefix = at ? word.mid(1) : word;
		// Most recent chatters first; the name people see in chat.
		tabMatches_.clear();
		QSet<QString> seen;
		for (qsizetype i = msgs_.size() - 1; i >= 0 && tabMatches_.size() < 40; --i) {
			const ChatMessage &m = msgs_.at(i);
			if (m.host || !loginRe().match(m.login).hasMatch())
				continue;
			const QString name = m.user.compare(m.login, Qt::CaseInsensitive) == 0 ? m.user : m.login;
			const QString key = name.toLower();
			if (seen.contains(key))
				continue;
			if (!prefix.isEmpty() && !name.startsWith(prefix, Qt::CaseInsensitive))
				continue;
			seen.insert(key);
			tabMatches_ << (at ? QStringLiteral("@") : QString()) + name;
		}
		if (tabMatches_.isEmpty())
			return;
		tabStart_ = start;
		tabIndex_ = backwards ? tabMatches_.size() - 1 : 0;
	} else {
		const int n = tabMatches_.size();
		tabIndex_ = (tabIndex_ + (backwards ? -1 : 1) + n) % n;
	}
	const QString rep = tabMatches_.at(tabIndex_) + QLatin1Char(' ');
	const int from = qMin(tabStart_, pos);
	QString t = text;
	t.replace(from, pos - from, rep);
	input_->setText(t);
	input_->setCursorPosition(qMin<int>(t.size(), from + rep.size()));
}
