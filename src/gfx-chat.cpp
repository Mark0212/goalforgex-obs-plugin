/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-chat.hpp"
#include "gfx-flow.hpp"
#include "gfx-http.hpp"
#include "gfx-images.hpp"
#include "gfx-notice.hpp"
#include "gfx-settings.hpp"
#include "gfx-theme.hpp"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QBoxLayout>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QListWidget>
#include <QList>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QPointer>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QSize>
#include <QSizePolicy>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextOption>
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

#include <iterator>
#include <utility>

using namespace gfx;

namespace {

constexpr int kKeep = 500;   // messages kept in the panel
constexpr int kMaxLen = 500; // Twitch / Kick cap at 500 (YouTube 200 — the server reports a rejection)
constexpr int kHistory = 50; // sent messages ↑/↓ can recall
constexpr int kQuickTimeout = 600;
// Block property: the colour of the accent bar ChatView paints down a notice card's left edge.
constexpr int kBarProp = QTextFormat::UserProperty + 7;

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
	{"youtube", "YouTube", true, true, "https://www.youtube.com/channel/", "https://www.youtube.com/"},
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

// Message ids the server may be asked to delete (YouTube's run ~100 chars).
const QRegularExpression &msgIdRe()
{
	static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9._=%+/-]{1,200}$"));
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

// An image scaled once to `px` logical pixels tall for this screen — the text
// renderer would otherwise rescale it on every repaint and blur it.
QImage scaledTo(const QImage &src, int px, qreal dpr)
{
	if (src.isNull() || src.height() <= 0)
		return src;
	const int h = qMax(1, qRound(px * dpr));
	const int w = qMax(1, qRound(src.width() * (double(h) / src.height())));
	QImage img = src.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	img.setDevicePixelRatio(dpr);
	return img;
}

// The feed: serves the platform icons to the HTML as gfx:<platform>/<px>,
// and emotes as emote:<slot>/<px> — a transparent square until the image has
// downloaded, then the image itself (the lines re-flow once, batched).
class ChatView : public QTextBrowser {
public:
	explicit ChatView(QWidget *parent) : QTextBrowser(parent)
	{
		relayout_.setSingleShot(true);
		relayout_.setInterval(120);
		connect(&relayout_, &QTimer::timeout, this, [this] { relayoutNow(); });
		RemoteImages::instance().subscribe(this, [this](const QString &url) { imageArrived(url); });
	}
	std::function<QString(int)> urlFor;  // emote slot → image URL
	std::function<void()> onImageFailed; // an emote on screen couldn't load → redraw with its name
	std::function<bool()> following;     // the panel is pinned to the newest line

	// Lines are about to be cleared and redrawn: forget what was waiting.
	void forgetWaiting() { waiting_.clear(); }

protected:
	void paintEvent(QPaintEvent *e) override
	{
		QTextBrowser::paintEvent(e);
		// Event notice cards: a full-height accent bar down the left edge
		// (rich text can't draw a border on one block, so it's painted here).
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
			// gfx:brand — the GoalForgeX mark on the STAFF badge.
			if (parts.value(0) == QLatin1String("brand"))
				return QVariant::fromValue(theme::brandMark(px, devicePixelRatioF()).toImage());
			return QVariant::fromValue(platformIcon(parts.value(0), px, devicePixelRatioF()));
		}
		if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("emote")) {
			const QStringList parts = name.path().split(QLatin1Char('/'));
			const int px = parts.size() > 1 ? qBound(10, parts.at(1).toInt(), 96) : 24;
			const QString url = urlFor ? urlFor(parts.value(0).toInt()) : QString();
			const QImage img = url.isEmpty() ? QImage() : RemoteImages::instance().get(url);
			if (img.isNull()) {
				if (!url.isEmpty() && !RemoteImages::instance().failed(url))
					waiting_[url].insert(name.toString());
				return QVariant::fromValue(placeholder(px));
			}
			return QVariant::fromValue(scaled(url, img, px));
		}
		return QTextBrowser::loadResource(type, name);
	}

private:
	QImage placeholder(int px) const
	{
		const qreal dpr = devicePixelRatioF();
		QImage img(qMax(1, qRound(px * dpr)), qMax(1, qRound(px * dpr)), QImage::Format_ARGB32_Premultiplied);
		img.fill(Qt::transparent);
		img.setDevicePixelRatio(dpr);
		return img;
	}
	QImage scaled(const QString &url, const QImage &img, int px)
	{
		const QString key = QStringLiteral("%1|%2|%3").arg(url).arg(px).arg(devicePixelRatioF());
		auto it = scaled_.constFind(key);
		if (it != scaled_.constEnd())
			return it.value();
		if (scaled_.size() > 1500)
			scaled_.clear();
		const QImage out = scaledTo(img, px, devicePixelRatioF());
		scaled_.insert(key, out);
		return out;
	}
	void imageArrived(const QString &url)
	{
		auto it = waiting_.find(url);
		if (it == waiting_.end())
			return;
		const QSet<QString> names = it.value();
		waiting_.erase(it);
		const QImage img = RemoteImages::instance().get(url);
		if (img.isNull()) {
			if (onImageFailed)
				onImageFailed();
			return;
		}
		for (const QString &n : names) {
			const QUrl u(n);
			const int px = qBound(10, u.path().section(QLatin1Char('/'), 1, 1).toInt(), 96);
			document()->addResource(QTextDocument::ImageResource, u,
						QVariant::fromValue(scaled(url, img, px)));
		}
		relayout_.start();
	}
	void relayoutNow()
	{
		// The images may be wider than their placeholders: lay the text out again,
		// keeping the line at the top of the view where it is if the reader scrolled up.
		QScrollBar *sb = verticalScrollBar();
		const bool atBottom = following ? following() : sb->value() >= sb->maximum() - 4;
		QTextBlock anchor;
		qreal offset = 0;
		QAbstractTextDocumentLayout *lay = document()->documentLayout();
		if (!atBottom) {
			anchor = cursorForPosition(QPoint(0, 0)).block();
			if (anchor.isValid())
				offset = sb->value() - lay->blockBoundingRect(anchor).top();
		}
		document()->markContentsDirty(0, document()->characterCount());
		if (atBottom)
			sb->setValue(sb->maximum());
		else if (anchor.isValid())
			sb->setValue(qMax(0, qRound(lay->blockBoundingRect(anchor).top() + offset)));
		viewport()->update();
	}

	QHash<QString, QSet<QString>> waiting_; // url → resource names showing its placeholder
	QHash<QString, QImage> scaled_;
	QTimer relayout_;
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

// ── Event notices ───────────────────────────────────────────────
struct NoticeMeta {
	const char *type;
	const char *icon;
	const char *label;
	const char *color; // the event's own colour (pill text, "event" tint)
	const char *group; // which Settings → Event notices switch it follows
};
const NoticeMeta kNotices[] = {
	{"sub", "⭐", "NEW SUB", "#ffb020", "subs"},
	{"resub", "⭐", "RESUB", "#ffb020", "subs"},
	{"prime", "👑", "PRIME", "#38bdf8", "subs"},
	{"upgrade", "⬆", "UPGRADE", "#ffb020", "subs"},
	{"member", "⭐", "NEW MEMBER", "#34d399", "subs"},
	{"milestone", "🏅", "MEMBER MILESTONE", "#34d399", "subs"},
	{"giftsub", "🎁", "GIFT SUB", "#f472b6", "gifts"},
	{"giftbomb", "🎁", "GIFT BOMB", "#f472b6", "gifts"},
	{"ytgift", "🎁", "GIFTED MEMBERSHIPS", "#f472b6", "gifts"},
	{"raid", "🚀", "RAID", "#fb7185", "raids"},
	{"cheer", "💎", "CHEER", "#c084fc", "cheers"},
	{"kicks", "💚", "KICKS", "#53fc18", "cheers"},
	{"superchat", "💬", "SUPER CHAT", "#facc15", "cheers"},
	{"supersticker", "🏷", "SUPER STICKER", "#facc15", "cheers"},
	{"tiktok_gift", "🎁", "GIFT", "#fe2c55", "tiktokgifts"},
	{"follow", "➕", "FOLLOW", "#a78bfa", "follows"},
	{"shoutout", "📣", "SHOUTOUT", "#22d3ee", "shoutouts"},
	{"shoutout_in", "📣", "SHOUTOUT", "#22d3ee", "shoutouts"},
	{"streak", "🔥", "WATCH STREAK", "#fb923c", "streaks"},
	{"hypetrain", "🚂", "HYPE TRAIN", "#a970ff", "hypetrain"},
	{"points", "🎯", "REDEEMED", "#c4b5fd", "points"},
	{"announcement", "📢", "ANNOUNCEMENT", "#9146ff", "announcements"},
	{"bitsbadge", "💎", "BITS BADGE", "#c084fc", "other"},
	{"payforward", "💞", "PAY IT FORWARD", "#f472b6", "other"},
	{"charity", "💗", "CHARITY", "#f472b6", "other"},
	{"system", "ℹ", "NOTICE", "#9aa1b0", "other"}, // last = fallback for anything new
};

const NoticeMeta &noticeMeta(const QString &type)
{
	for (const NoticeMeta &n : kNotices)
		if (type == QLatin1String(n.type))
			return n;
	return kNotices[std::size(kNotices) - 1];
}

// Each platform's own brand colour — always the same, so a glance says where it happened.
QString platformColor(const QString &p)
{
	if (p == QLatin1String("twitch"))
		return QStringLiteral("#9146ff");
	if (p == QLatin1String("kick"))
		return QStringLiteral("#53fc18");
	if (p == QLatin1String("youtube"))
		return QStringLiteral("#ff0033");
	if (p == QLatin1String("tiktok"))
		return QStringLiteral("#fe2c55");
	return QStringLiteral("#9aa1b0");
}

// Twitch /announce colours (PRIMARY = the channel's colour → Twitch purple here).
QString announceColor(const QString &tone)
{
	const QString t = tone.toLower();
	if (t == QLatin1String("blue"))
		return QStringLiteral("#3b82f6");
	if (t == QLatin1String("green"))
		return QStringLiteral("#22c55e");
	if (t == QLatin1String("orange"))
		return QStringLiteral("#f97316");
	if (t == QLatin1String("purple"))
		return QStringLiteral("#a855f7");
	return QStringLiteral("#9146ff");
}

QColor mixColor(const QColor &a, const QColor &b, qreal t)
{
	return QColor::fromRgbF(static_cast<float>(a.redF() * (1 - t) + b.redF() * t),
				static_cast<float>(a.greenF() * (1 - t) + b.greenF() * t),
				static_cast<float>(a.blueF() * (1 - t) + b.blueF() * t));
}

// A Twitch cheer is a real chat message — shown with the notice look when cheers are on.
bool isCheer(const ChatMessage &m)
{
	const Settings &s = settings();
	return m.notice.isEmpty() && m.bits > 0 && s.chatNotices && s.ncCheers;
}

QString eventColor(const ChatMessage &m)
{
	if (m.notice == QLatin1String("announcement"))
		return announceColor(m.tone);
	return QString::fromLatin1(noticeMeta(m.notice.isEmpty() ? QStringLiteral("cheer") : m.notice).color);
}

// Card background + accent bar colour, per Settings → Event notices → Colour.
QString noticeTint(const ChatMessage &m)
{
	const QString t = settings().noticeTint;
	if (t == QLatin1String("event"))
		return eventColor(m);
	if (t == QLatin1String("accent"))
		return theme::accentColor();
	return platformColor(m.platform);
}

} // namespace

// ── Shared with the Events dock (gfx-notice.hpp) ────────────────
namespace gfx::notice {

Look look(const QString &type)
{
	const NoticeMeta &m = ::noticeMeta(type);
	return {QString::fromUtf8(m.icon), QString::fromUtf8(m.label), QString::fromLatin1(m.color)};
}

QString platformColor(const QString &platform)
{
	return ::platformColor(platform);
}

QString tint(const QString &platform, const QString &eventColor)
{
	const QString t = settings().noticeTint;
	if (t == QLatin1String("event"))
		return eventColor;
	if (t == QLatin1String("accent"))
		return theme::accentColor();
	return ::platformColor(platform);
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
	return ::mixColor(a, b, t);
}

QImage platformIcon(const QString &platform, int logicalPx, qreal dpr)
{
	return ::platformIcon(platform, logicalPx, dpr);
}

} // namespace gfx::notice

QStringList GfxChatPanel::wantedPlatforms()
{
	QStringList p{QStringLiteral("twitch"), QStringLiteral("kick")};
	if (settings().chatYouTube)
		p << QStringLiteral("youtube");
	if (settings().chatTikTok)
		p << QStringLiteral("tiktok");
	return p;
}

QStringList GfxChatPanel::watchList()
{
	return settings().chatWatch;
}

GfxChatPanel::GfxChatPanel(QWidget *parent) : QWidget(parent)
{
	buildUi();
	updateFilters();
	updateTargets();
	rebuildHighlightRe();
	applyFontSize();
	rebuild();
	applyLayoutMode(true);
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
	lockLbl_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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
	main_->setMinimumWidth(0);
	auto *root = new QVBoxLayout(main_);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(6);
	outer->addWidget(main_, 1);

	// Header: filter chips that wrap onto a second line when the dock is narrow,
	// and the tools pinned top-right:
	//   [All] [▣ Twitch] [▣ Kick] [▣ YouTube] [▣ TikTok] [📺 Watching] [@]      🔍 ⏸ ⋯ ●●
	auto *top = new QHBoxLayout();
	top->setSpacing(4);
	chipsBox_ = new QWidget(main_);
	auto *flow = new gfx::FlowLayout(chipsBox_, 3, 3);
	chipsBox_->setLayout(flow);
	QSizePolicy chipsPolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	chipsPolicy.setHeightForWidth(true);
	chipsPolicy.setHorizontalStretch(1);
	chipsBox_->setSizePolicy(chipsPolicy);
	const auto seg = [this, flow](const QString &text, const QString &key) {
		auto *b = new QPushButton(text, chipsBox_);
		b->setObjectName(QStringLiteral("gfxSeg"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		b->setProperty("gfxLabel", text);
		connect(b, &QPushButton::clicked, this, [this, key] { setFilter(key); });
		flow->addWidget(b);
		return b;
	};
	fAll_ = seg(QStringLiteral("All"), QStringLiteral("all"));
	fTwitch_ = seg(QStringLiteral("Twitch"), QStringLiteral("twitch"));
	fKick_ = seg(QStringLiteral("Kick"), QStringLiteral("kick"));
	fYouTube_ = seg(QStringLiteral("YouTube"), QStringLiteral("youtube"));
	fTikTok_ = seg(QStringLiteral("TikTok"), QStringLiteral("tiktok"));
	fWatch_ = seg(QStringLiteral("📺 Watching"), QStringLiteral("watched"));
	fMentions_ = seg(QStringLiteral("@"), QStringLiteral("mentions"));
	for (QPushButton *b : {fTwitch_, fKick_, fYouTube_, fTikTok_}) {
		const QString p = b == fTwitch_    ? QStringLiteral("twitch")
				  : b == fKick_    ? QStringLiteral("kick")
				  : b == fYouTube_ ? QStringLiteral("youtube")
						   : QStringLiteral("tiktok");
		b->setIcon(QIcon(QPixmap::fromImage(platformIcon(p, 14, devicePixelRatioF()))));
		b->setIconSize(QSize(14, 14));
		b->setToolTip(QStringLiteral("Only your %1 chat").arg(platformName(p)));
	}
	fAll_->setToolTip(QStringLiteral("Every chat"));
	fWatch_->setToolTip(QStringLiteral("Only the other channels you're watching (⋯ → Watch other channels)"));
	fWatch_->hide();
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
	auto *tools = new QHBoxLayout();
	tools->setSpacing(1);
	tools->addWidget(searchBtn_);
	tools->addWidget(pauseBtn_);
	tools->addWidget(moreBtn_);
	tools->addSpacing(3);
	tools->addWidget(statusLbl_);
	top->addWidget(chipsBox_, 1, Qt::AlignTop);
	top->addLayout(tools);
	top->setAlignment(tools, Qt::AlignTop | Qt::AlignRight);
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
	searchEdit_->setMinimumWidth(60);
	userChip_ = new QPushButton(searchRow_);
	userChip_->setObjectName(QStringLiteral("gfxSeg"));
	userChip_->setCheckable(true);
	userChip_->setChecked(true);
	userChip_->setCursor(Qt::PointingHandCursor);
	userChip_->setToolTip(QStringLiteral("Click to show everyone again"));
	userChip_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
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

	// Stats: one card per connected platform (1–4 per row by width) + chatters / pace line
	stats_ = new QWidget(main_);
	auto *sl = new QVBoxLayout(stats_);
	sl->setContentsMargins(0, 0, 0, 0);
	sl->setSpacing(3);
	cardsGrid_ = new QGridLayout();
	cardsGrid_->setHorizontalSpacing(6);
	cardsGrid_->setVerticalSpacing(6);
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
	summary_->setWordWrap(true);
	summary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	summary_->setToolTip(QStringLiteral("Chatters: different people who chatted in the last 10 minutes.\n"
					    "Messages a minute: average over the last 5 minutes."));
	sl->addWidget(summary_);
	stats_->hide();
	// Its own height never forces the dock taller: a short dock hides it instead.
	stats_->setMinimumHeight(1);
	root->addWidget(stats_);

	// Feed
	auto *view = new ChatView(main_);
	view_ = view;
	view_->setObjectName(QStringLiteral("gfxChatView"));
	view_->setOpenLinks(false);
	view_->setOpenExternalLinks(false);
	view_->setUndoRedoEnabled(false);
	view_->setFrameShape(QFrame::NoFrame);
	view_->setMinimumHeight(70);
	view_->setMinimumWidth(0);
	// Long words / links wrap instead of pushing the dock wider.
	view_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	view_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
	view_->document()->setDocumentMargin(8);
	view_->viewport()->installEventFilter(this);
	view_->setMouseTracking(true);
	view_->setContextMenuPolicy(Qt::CustomContextMenu);
	view->urlFor = [this](int slot) {
		return slot >= 0 && slot < urls_.size() ? urls_.at(slot) : QString();
	};
	view->following = [this] {
		return autoScrolling();
	};
	auto *failRedraw = new QTimer(this);
	failRedraw->setSingleShot(true);
	failRedraw->setInterval(400);
	connect(failRedraw, &QTimer::timeout, this, [this] { rebuild(); });
	view->onImageFailed = [failRedraw] {
		failRedraw->start();
	};
	root->addWidget(view_, 1);
	// Links in the feed:
	//   user:<seq>               → the chatter's card
	//   mod:<delete|timeout>:<seq> → quick moderation
	//   http(s)://…              → the browser
	const auto lineMsg = [this](const QString &seqText) -> const ChatMessage * {
		bool ok = false;
		const qint64 seq = seqText.toLongLong(&ok);
		return ok ? findMsg(seq) : nullptr;
	};
	connect(view_, &QTextBrowser::anchorClicked, this, [this, lineMsg](const QUrl &u) {
		const QString scheme = u.scheme();
		if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
			if (u.isValid() && !u.host().isEmpty())
				QDesktopServices::openUrl(u);
			return;
		}
		const QStringList parts = u.path().split(QLatin1Char(':'));
		if (scheme == QLatin1String("user")) {
			const ChatMessage *m = lineMsg(parts.value(0));
			if (m && loginRe().match(m->login).hasMatch())
				showChatterCard(m->platform, m->login, m->id, m->channel);
			return;
		}
		if (scheme == QLatin1String("mod") && parts.size() == 2 && mod_) {
			const QString action = parts.at(0);
			const ChatMessage *m = lineMsg(parts.at(1));
			if (!m || m->host || !m->channel.isEmpty() || !canMod(m->platform) ||
			    !loginRe().match(m->login).hasMatch() || !msgIdRe().match(m->id).hasMatch())
				return;
			if (action == QLatin1String("delete"))
				mod_(m->platform, action, m->login, 0, m->id, m->user);
			else if (action == QLatin1String("timeout"))
				mod_(m->platform, action, m->login, kQuickTimeout, m->id, m->user);
		}
	});
	// Right-click on a name: the plain action menu; anywhere else: copy / select all.
	connect(view_, &QWidget::customContextMenuRequested, this, [this, lineMsg](const QPoint &pos) {
		const QUrl u(view_->anchorAt(pos));
		if (u.scheme() == QLatin1String("user")) {
			const ChatMessage *m = lineMsg(u.path());
			if (m && loginRe().match(m->login).hasMatch()) {
				showUserMenu(m->platform, m->login, m->id, m->channel);
				return;
			}
		}
		QMenu *menu = view_->createStandardContextMenu(pos);
		menu->exec(view_->viewport()->mapToGlobal(pos));
		delete menu;
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
	jump_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	jump_->hide();
	root->addWidget(jump_);
	connect(jump_, &QPushButton::clicked, this, [this] {
		if (paused_)
			setPaused(false);
		else
			scrollToBottom();
	});

	// Composer. Wide:   [Twitch] [Kick] [YouTube] [ message…        ] [Send]
	//           Narrow: [▣] [▣] [▣]
	//                   [ message…               ] [➤]
	// (TikTok is read-only.)
	compLayout_ = new QBoxLayout(QBoxLayout::LeftToRight);
	compLayout_->setSpacing(4);
	targetsBox_ = new QWidget(main_);
	auto *tl = new QHBoxLayout(targetsBox_);
	tl->setContentsMargins(0, 0, 0, 0);
	tl->setSpacing(4);
	const auto target = [this, tl](const QString &text, const char *platform) {
		auto *b = new QPushButton(text, targetsBox_);
		b->setObjectName(QStringLiteral("gfxTarget"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		b->setProperty("gfxLabel", text);
		theme::setState(b, platform);
		tl->addWidget(b);
		return b;
	};
	toTwitch_ = target(QStringLiteral("Twitch"), "twitch");
	toKick_ = target(QStringLiteral("Kick"), "kick");
	toYouTube_ = target(QStringLiteral("YouTube"), "youtube");
	tl->addStretch(1);
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
	auto *inputBox = new QWidget(main_);
	auto *il = new QHBoxLayout(inputBox);
	il->setContentsMargins(0, 0, 0, 0);
	il->setSpacing(4);
	input_ = new QLineEdit(inputBox);
	input_->setObjectName(QStringLiteral("gfxChatInput"));
	input_->setMaxLength(kMaxLen);
	input_->setPlaceholderText(QStringLiteral("Send a message…"));
	input_->setClearButtonEnabled(false);
	input_->setMinimumWidth(60);
	input_->setToolTip(QStringLiteral("Tab completes chatter names · ↑ / ↓ bring back what you sent"));
	input_->installEventFilter(this);
	sendBtn_ = new QPushButton(QStringLiteral("Send"), inputBox);
	theme::setRole(sendBtn_, "primary");
	sendBtn_->setCursor(Qt::PointingHandCursor);
	sendBtn_->setToolTip(QStringLiteral("Send (Enter)"));
	il->addWidget(input_, 1);
	il->addWidget(sendBtn_);
	compLayout_->addWidget(targetsBox_);
	compLayout_->addWidget(inputBox, 1);
	root->addLayout(compLayout_);

	readOnlyLbl_ =
		new QLabel(QStringLiteral("TikTok chat is read-only — TikTok doesn't let apps post as you."), main_);
	readOnlyLbl_->setObjectName(QStringLiteral("gfxMuted"));
	readOnlyLbl_->setWordWrap(true);
	readOnlyLbl_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
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

// ── Layout for the dock's size ──────────────────────────────────

void GfxChatPanel::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	applyLayoutMode();
	fitChips();
}

// Platform chips show their names when they all fit on one line next to the
// tools; otherwise just their icons (the name is in the tooltip).
void GfxChatPanel::fitChips()
{
	const QList<QPushButton *> named = {fTwitch_, fKick_, fYouTube_, fTikTok_, fWatch_};
	const auto setNames = [this, &named](bool on) {
		for (QPushButton *b : named) {
			const QString label = b->property("gfxLabel").toString();
			const QString t = on ? label : (b == fWatch_ ? QStringLiteral("📺") : QString());
			if (b->text() != t)
				b->setText(t);
			// Icon-only chips get tighter padding so more of them fit per line.
			b->setStyleSheet(on ? QString() : QStringLiteral("padding: 3px 6px;"));
		}
	};
	// Room for the chips = the panel minus the tools on the right.
	int tools = 0;
	for (QWidget *w : {static_cast<QWidget *>(searchBtn_), static_cast<QWidget *>(pauseBtn_),
			   static_cast<QWidget *>(moreBtn_), static_cast<QWidget *>(statusLbl_)})
		if (w->isVisibleTo(this))
			tools += w->sizeHint().width() + 2;
	const int room = width() - tools - 8;
	setNames(true);
	int need = 0, n = 0;
	for (QPushButton *b : {fAll_, fTwitch_, fKick_, fYouTube_, fTikTok_, fWatch_, fMentions_})
		if (b->isVisibleTo(this))
			need += b->sizeHint().width() + (n++ ? 3 : 0);
	if (need > room)
		setNames(false);
}

void GfxChatPanel::applyLayoutMode(bool force)
{
	const int w = width(), h = height();
	// A little hysteresis so a dock resized right at a breakpoint doesn't flicker.
	int mode = mode_;
	if (mode < 0 || force)
		mode = w < 380 ? 0 : (w < 640 ? 1 : 2);
	else if (mode == 0 && w >= 396)
		mode = w < 640 ? 1 : 2;
	else if (mode == 1 && w < 364)
		mode = 0;
	else if (mode == 1 && w >= 656)
		mode = 2;
	else if (mode == 2 && w < 624)
		mode = w < 380 ? 0 : 1;
	// Short = not enough height for the stats strip AND a comfortable feed: the
	// strip steps aside (it never sets the dock's minimum height — see buildUi).
	const int statsNeed = stats_->sizeHint().height() + 250;
	const bool shortH = h > 0 && h < statsNeed;
	const int cols = w < 250 ? 1 : (w < 640 ? 2 : 4);
	if (!force && mode == mode_ && shortH == short_ && cols == cardCols_)
		return;
	const bool modeChanged = mode != mode_ || force;
	mode_ = mode;
	short_ = shortH;
	if (cols != cardCols_) {
		cardCols_ = cols;
		cardsKey_.clear();
	}

	if (modeChanged) {
		// Send targets: names or icons, and the composer goes to two rows when narrow.
		updateTargets();
		compLayout_->setDirection(mode == 0 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
		sendBtn_->setText(sending_ ? QStringLiteral("…")
					   : (mode == 0 ? QStringLiteral("➤") : QStringLiteral("Send")));
		statusLbl_->setVisible(mode != 0 || w >= 330);
	}
	renderStats();
	updateJump();
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
	const int n = static_cast<int>(visible.size());
	// Never more columns than cards; 3 cards in a 4-wide row look better as 3.
	const int cols = qMax(1, qMin(cardCols_, n));
	const QString key = visible.join(QLatin1Char(',')) + QLatin1Char('/') + QString::number(cols);
	if (key == cardsKey_)
		return;
	cardsKey_ = key;
	for (int i = 0; i < 4; ++i)
		cardsGrid_->setColumnStretch(i, i < cols ? 1 : 0);
	int slot = 0;
	for (StatCard *c : {&twCard_, &kkCard_, &ytCard_, &ttCard_}) {
		cardsGrid_->removeWidget(c->box);
		if (!visible.contains(c->platform)) {
			c->box->hide();
			continue;
		}
		const int row = slot / cols, col = slot % cols;
		// The last card of an unfinished row stretches over what's left of it.
		const bool last = slot == n - 1;
		cardsGrid_->addWidget(c->box, row, col, 1, last ? cols - col : 1);
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
	// A short dock keeps its height for the chat itself.
	const bool show = settings().chatStats && !visible.isEmpty() && !short_;
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
		paceStart = msgs_.first().ts; // (watched channels share the buffer, so this stays an estimate)
	QSet<QString> chatters;
	int paced = 0;
	for (const ChatMessage &m : msgs_) {
		if (m.host || m.ts <= 0 || !m.channel.isEmpty() || !m.notice.isEmpty())
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

void GfxChatPanel::setInfoHandler(InfoFn fn)
{
	info_ = std::move(fn);
}

namespace {

// "3 years", "5 months", "12 days", "today"
QString ageText(qint64 sinceMs, qint64 nowMs)
{
	const qint64 days = qMax<qint64>(0, (nowMs - sinceMs) / 86400000);
	if (days >= 365) {
		const qint64 y = days / 365;
		return y == 1 ? QStringLiteral("1 year") : QStringLiteral("%1 years").arg(y);
	}
	if (days >= 31) {
		const qint64 m = days / 30;
		return m == 1 ? QStringLiteral("1 month") : QStringLiteral("%1 months").arg(m);
	}
	if (days >= 1)
		return days == 1 ? QStringLiteral("1 day") : QStringLiteral("%1 days").arg(days);
	return QStringLiteral("less than a day");
}

QString shortCount(qint64 n)
{
	if (n >= 1000000)
		return QString::number(n / 1000000.0, 'f', n >= 10000000 ? 0 : 1) + QStringLiteral("M");
	if (n >= 10000)
		return QString::number(n / 1000.0, 'f', n >= 100000 ? 0 : 1) + QStringLiteral("K");
	return fmtCount(n);
}

QPushButton *cardButton(const QString &text, QWidget *parent, const char *role = nullptr)
{
	auto *b = new QPushButton(text, parent);
	b->setObjectName(QStringLiteral("gfxCardBtn"));
	b->setCursor(Qt::PointingHandCursor);
	if (role)
		theme::setRole(b, role);
	return b;
}

} // namespace

void GfxChatPanel::replyTo(const QString &platform, const QString &login, const QString &shown)
{
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
}

void GfxChatPanel::addModActions(QMenu *menu, const QString &platform, const QString &login, const QString &messageId,
				 const QString &shown, bool canDelete)
{
	const QString platName = platformName(platform);
	if (canDelete && msgIdRe().match(messageId).hasMatch())
		connect(menu->addAction(QStringLiteral("🗑   Delete this message")), &QAction::triggered, this,
			[this, platform, login, messageId, shown] {
				mod_(platform, QStringLiteral("delete"), login, 0, messageId, shown);
			});
	QMenu *timeout = menu->addMenu(QStringLiteral("⏱   Timeout"));
	QList<QPair<QString, int>> lengths = {
		{QStringLiteral("1 minute"), 60}, {QStringLiteral("10 minutes"), 600}, {QStringLiteral("1 hour"), 3600},
		{QStringLiteral("1 day"), 86400}, {QStringLiteral("1 week"), 604800},
	};
	for (const QPair<QString, int> &len : lengths) {
		const int secs = len.second;
		connect(timeout->addAction(len.first), &QAction::triggered, this,
			[this, platform, login, messageId, shown, secs] {
				mod_(platform, QStringLiteral("timeout"), login, secs, messageId, shown);
			});
	}
	connect(menu->addAction(QStringLiteral("🚫   Ban")), &QAction::triggered, this,
		[this, platform, login, messageId, shown, platName] {
			if (QMessageBox::question(
				    this, QStringLiteral("Ban %1?").arg(shown),
				    QStringLiteral("Permanently ban %1 from your %2 chat? You can unban them later.")
					    .arg(shown, platName)) == QMessageBox::Yes)
				mod_(platform, QStringLiteral("ban"), login, 0, messageId, shown);
		});
	connect(menu->addAction(QStringLiteral("↩   Unban")), &QAction::triggered, this,
		[this, platform, login, messageId, shown] {
			mod_(platform, QStringLiteral("unban"), login, 0, messageId, shown);
		});
}

void GfxChatPanel::showUserMenu(const QString &platform, const QString &login, const QString &messageId,
				const QString &channel)
{
	const ChatMessage *msg = nullptr;
	if (!messageId.isEmpty())
		for (const ChatMessage &m : msgs_)
			if (m.id == messageId && m.platform == platform && m.channel == channel)
				msg = &m;
	const QString platName = platformName(platform);
	const PlatformInfo *pinfo = info(platform);
	const bool own = channel.isEmpty();
	const bool sendable = own && pinfo && pinfo->canSend;
	const QString shown = msg ? msg->user : login;
	const bool isHost = msg && msg->host;
	const QString msgText = msg ? msg->text : QString();

	// How much they've said in what the panel holds.
	int said = 0;
	for (const ChatMessage &m : msgs_)
		if (m.notice.isEmpty() && m.platform == platform && m.channel == channel &&
		    m.login.compare(login, Qt::CaseInsensitive) == 0)
			++said;

	QMenu menu(this);
	QAction *head = menu.addAction(QIcon(QPixmap::fromImage(platformIcon(platform, 16, devicePixelRatioF()))),
				       QStringLiteral("%1  ·  %2  ·  %3 message%4")
					       .arg(shown, own   ? platName
							   : msg ? msg->channelName
								 : platName)
					       .arg(said)
					       .arg(said == 1 ? QString() : QStringLiteral("s")));
	head->setEnabled(false);
	menu.addSeparator();
	connect(menu.addAction(QStringLiteral("🪪   Chatter card")), &QAction::triggered, this,
		[this, platform, login, messageId, channel] { showChatterCard(platform, login, messageId, channel); });
	if (!isHost && sendable && platformConnected(platform))
		connect(menu.addAction(QStringLiteral("💬   Reply to %1").arg(shown)), &QAction::triggered, this,
			[this, platform, login, shown] { replyTo(platform, login, shown); });
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

	if (!own) {
		menu.addSeparator();
		menu.addAction(QStringLiteral("Watching %1 — read-only").arg(msg ? msg->channelName : platName))
			->setEnabled(false);
	} else if (!isHost && !canMod(platform)) {
		menu.addSeparator();
		menu.addAction(QStringLiteral("Moderate %1 chat on %1 itself").arg(platName))->setEnabled(false);
	} else if (!isHost && mod_ && canMod(platform)) {
		// The broadcaster can't be moderated — the rest is for everyone else.
		menu.addSeparator();
		addModActions(&menu, platform, login, messageId, shown, msg && !msg->deleted);
	}
	menu.exec(QCursor::pos());
}

void GfxChatPanel::showChatterCard(const QString &platform, const QString &login, const QString &messageId,
				   const QString &channel)
{
	const ChatMessage *msg = nullptr;
	QList<const ChatMessage *> theirs;
	for (const ChatMessage &m : msgs_) {
		if (!m.notice.isEmpty() || m.platform != platform || m.channel != channel ||
		    m.login.compare(login, Qt::CaseInsensitive) != 0)
			continue;
		theirs << &m;
		if (!messageId.isEmpty() && m.id == messageId)
			msg = &m;
	}
	if (!msg && !theirs.isEmpty())
		msg = theirs.last();
	const QString platName = platformName(platform);
	const bool own = channel.isEmpty();
	const PlatformInfo *pinfo = info(platform);
	const QString shown = msg ? msg->user : login;
	const bool isHost = msg && msg->host;
	const qreal dpr = devicePixelRatioF();

	auto *card = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
	card->setObjectName(QStringLiteral("gfxCard"));
	card->setAttribute(Qt::WA_DeleteOnClose);
	card->setAttribute(Qt::WA_StyledBackground, true);
	auto *v = new QVBoxLayout(card);
	v->setContentsMargins(12, 12, 12, 10);
	v->setSpacing(8);

	// ── who ──
	auto *head = new QHBoxLayout();
	head->setSpacing(10);
	auto *avatar = new QLabel(card);
	avatar->setFixedSize(44, 44);
	avatar->setAlignment(Qt::AlignCenter);
	avatar->setObjectName(QStringLiteral("gfxCardAvatar"));
	avatar->setPixmap(QPixmap::fromImage(platformIcon(platform, 26, dpr)));
	auto *who = new QVBoxLayout();
	who->setSpacing(1);
	auto *nameLbl = new QLabel(card);
	nameLbl->setTextFormat(Qt::RichText);
	const QString nameColor = (msg && !msg->color.isEmpty()) ? readableColor(msg->color)
								 : QStringLiteral("#ffffff");
	QString badges;
	if (msg) {
		if (msg->host)
			badges += tag("HOST", "#ff3b4f", 7.0);
		else if (msg->mod)
			badges += tag("MOD", "#34d399", 7.0);
		if (msg->vip)
			badges += tag("VIP", "#f472b6", 7.0);
		if (msg->sub)
			badges += tag(platform == QLatin1String("youtube") ? "MEMBER" : "SUB", "#ffb020", 7.0);
	}
	nameLbl->setText(QStringLiteral("<b style='color:%1;font-size:12pt'>%2</b>&nbsp;&nbsp;%3")
				 .arg(nameColor, shown.toHtmlEscaped(), badges));
	nameLbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	auto *subLbl = new QLabel(card);
	subLbl->setObjectName(QStringLiteral("gfxMuted"));
	const QString handle = platform == QLatin1String("youtube")
				       ? QString()
				       : QStringLiteral("@") + login + QStringLiteral("  ·  ");
	subLbl->setText(
		own ? handle + platName
		    : handle + QStringLiteral("in %1's %2 chat").arg(msg ? msg->channelName : QString(), platName));
	subLbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	who->addWidget(nameLbl);
	who->addWidget(subLbl);
	head->addWidget(avatar, 0, Qt::AlignTop);
	head->addLayout(who, 1);
	v->addLayout(head);

	// ── what the platform says (filled in when the lookup answers) ──
	auto *facts = new QLabel(card);
	facts->setObjectName(QStringLiteral("gfxCardFacts"));
	facts->setTextFormat(Qt::RichText);
	facts->setWordWrap(true);
	facts->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	const auto line = [](const QString &icon, const QString &text) {
		return QStringLiteral("<tr><td style='padding-right:6px'>%1</td><td>%2</td></tr>").arg(icon, text);
	};
	// This session, from what the panel holds.
	QString session;
	if (!theirs.isEmpty()) {
		session =
			line(QStringLiteral("💬"), QStringLiteral("<b>%1</b> message%2 here this session · first at %3")
							   .arg(theirs.size())
							   .arg(theirs.size() == 1 ? QString() : QStringLiteral("s"))
							   .arg(QDateTime::fromMSecsSinceEpoch(theirs.first()->ts)
									.toString(QStringLiteral("HH:mm"))));
		if (theirs.first()->first)
			session += line(QStringLiteral("✨"), QStringLiteral("First time chatting in this channel"));
	}
	const auto setFacts = [facts, session](const QString &rows) {
		facts->setText(
			QStringLiteral("<table cellspacing='0' cellpadding='1'>%1%2</table>").arg(rows, session));
	};
	const bool lookup = info_ && platform != QLatin1String("tiktok");
	setFacts(lookup ? QStringLiteral("<tr><td></td><td style='color:#6b7280'>Looking up %1…</td></tr>")
				  .arg(shown.toHtmlEscaped())
			: QString());
	v->addWidget(facts);

	// ── their recent messages ──
	if (!theirs.isEmpty()) {
		auto *recent = new QTextBrowser(card);
		recent->setObjectName(QStringLiteral("gfxCardRecent"));
		recent->setFrameShape(QFrame::NoFrame);
		recent->setOpenLinks(false);
		recent->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		recent->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
		QString html;
		const qsizetype from = qMax<qsizetype>(0, theirs.size() - 10);
		for (qsizetype k = from; k < theirs.size(); ++k) {
			const ChatMessage *m = theirs.at(k);
			html += QStringLiteral(
					"<p style='margin:0 0 3px 0'><span style='color:#5b6270'>%1</span>&nbsp; %2</p>")
					.arg(QDateTime::fromMSecsSinceEpoch(m->ts).toString(QStringLiteral("HH:mm")),
					     m->deleted ? QStringLiteral("<s style='color:#5b6270'>%1</s>")
								  .arg(m->text.toHtmlEscaped())
							: QStringLiteral("<span style='color:#e8eaf0'>%1</span>")
								  .arg(textHtml(m->text)));
		}
		recent->setHtml(html);
		recent->setFixedHeight(qMin(132, 26 + 20 * static_cast<int>(theirs.size() - from)));
		connect(recent, &QTextBrowser::anchorClicked, this, [](const QUrl &u) {
			if ((u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https")) &&
			    !u.host().isEmpty())
				QDesktopServices::openUrl(u);
		});
		QTimer::singleShot(0, recent, [recent] {
			recent->verticalScrollBar()->setValue(recent->verticalScrollBar()->maximum());
		});
		v->addWidget(recent);
	}

	// ── actions ──
	auto *actions = new QWidget(card);
	auto *af = new gfx::FlowLayout(actions, 4, 4);
	actions->setLayout(af);
	QSizePolicy ap(QSizePolicy::Preferred, QSizePolicy::Preferred);
	ap.setHeightForWidth(true);
	actions->setSizePolicy(ap);
	const bool canReply = own && !isHost && pinfo && pinfo->canSend && platformConnected(platform);
	if (canReply) {
		QPushButton *b = cardButton(QStringLiteral("💬 Reply"), actions, "primary");
		connect(b, &QPushButton::clicked, card, [this, card, platform, login, shown] {
			card->close();
			replyTo(platform, login, shown);
		});
		af->addWidget(b);
	}
	{
		QPushButton *b = cardButton(QStringLiteral("🔎 Only theirs"), actions);
		b->setToolTip(QStringLiteral("Show only %1's messages").arg(shown));
		connect(b, &QPushButton::clicked, card, [this, card, platform, login, shown] {
			card->close();
			setUserFilter(platform, login, shown);
		});
		af->addWidget(b);
	}
	QPushButton *profileBtn = cardButton(QStringLiteral("👤 Profile"), actions);
	profileBtn->setToolTip(QStringLiteral("Open their %1 page").arg(platName));
	connect(profileBtn, &QPushButton::clicked, card, [card, platform, login] {
		card->close();
		openProfile(platform, login);
	});
	af->addWidget(profileBtn);
	{
		const QString copyName = platform == QLatin1String("youtube") ? shown : login;
		QPushButton *b = cardButton(QStringLiteral("📋 Copy name"), actions);
		connect(b, &QPushButton::clicked, card, [card, copyName] {
			QGuiApplication::clipboard()->setText(copyName);
			card->close();
		});
		af->addWidget(b);
	}
	if (own && !isHost && mod_ && canMod(platform)) {
		if (msg && !msg->deleted && msgIdRe().match(msg->id).hasMatch()) {
			QPushButton *b = cardButton(QStringLiteral("🗑 Delete"), actions);
			b->setToolTip(QStringLiteral("Delete their latest message shown here"));
			const QString id = msg->id;
			connect(b, &QPushButton::clicked, card, [this, card, platform, login, id, shown] {
				card->close();
				mod_(platform, QStringLiteral("delete"), login, 0, id, shown);
			});
			af->addWidget(b);
		}
		QPushButton *tb = cardButton(QStringLiteral("⏱ Timeout ▾"), actions);
		connect(tb, &QPushButton::clicked, card, [this, card, tb, platform, login, shown] {
			QMenu m(card);
			const QList<QPair<QString, int>> lengths = {{QStringLiteral("1 minute"), 60},
								    {QStringLiteral("10 minutes"), 600},
								    {QStringLiteral("1 hour"), 3600},
								    {QStringLiteral("1 day"), 86400},
								    {QStringLiteral("1 week"), 604800}};
			for (const QPair<QString, int> &len : lengths) {
				const int secs = len.second;
				connect(m.addAction(len.first), &QAction::triggered, this,
					[this, platform, login, shown, secs] {
						mod_(platform, QStringLiteral("timeout"), login, secs, QString(),
						     shown);
					});
			}
			if (m.exec(tb->mapToGlobal(QPoint(0, tb->height()))))
				card->close();
		});
		af->addWidget(tb);
		QPushButton *ban = cardButton(QStringLiteral("🚫 Ban"), actions, "danger");
		connect(ban, &QPushButton::clicked, card, [this, card, platform, login, shown, platName] {
			card->close();
			if (QMessageBox::question(
				    this, QStringLiteral("Ban %1?").arg(shown),
				    QStringLiteral("Permanently ban %1 from your %2 chat? You can unban them later.")
					    .arg(shown, platName)) == QMessageBox::Yes)
				mod_(platform, QStringLiteral("ban"), login, 0, QString(), shown);
		});
		af->addWidget(ban);
		QPushButton *unban = cardButton(QStringLiteral("↩ Unban"), actions);
		connect(unban, &QPushButton::clicked, card, [this, card, platform, login, shown] {
			card->close();
			mod_(platform, QStringLiteral("unban"), login, 0, QString(), shown);
		});
		af->addWidget(unban);
	}
	v->addWidget(actions);
	if (!own) {
		auto *ro = new QLabel(
			QStringLiteral("You're watching this chat — replies and mod actions are for your own channel."),
			card);
		ro->setObjectName(QStringLiteral("gfxMuted"));
		ro->setWordWrap(true);
		ro->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		v->addWidget(ro);
	}

	// Size: a comfortable card, never wider than the screen.
	const QPoint at = QCursor::pos();
	QScreen *scr = QGuiApplication::screenAt(at);
	const QRect avail = scr ? scr->availableGeometry() : QRect(0, 0, 1920, 1080);
	const int w = qBound(250, qMax(300, width() - 24), qMin(360, avail.width() - 16));
	const auto place = [card, w, at, avail] {
		card->setFixedWidth(w);
		const int h = card->layout()->heightForWidth(w) > 0 ? card->layout()->heightForWidth(w)
								    : card->sizeHint().height();
		card->resize(w, h);
		int x = at.x() + 8, y = at.y() + 8;
		if (x + w > avail.right())
			x = qMax(avail.left() + 4, at.x() - w - 8);
		if (y + h > avail.bottom())
			y = qMax(avail.top() + 4, avail.bottom() - h - 4);
		card->move(x, y);
	};
	place();
	card->show();

	// Fill in the facts + avatar once the platform answers.
	if (lookup) {
		QPointer<QFrame> guard(card);
		info_(platform, login,
		      [this, guard, setFacts, line, avatar, nameLbl, nameColor, badges, place, profileBtn,
		       platform](const QJsonObject &o, const QString &error) {
			      if (!guard)
				      return;
			      QString rows;
			      const qint64 now = QDateTime::currentMSecsSinceEpoch();
			      const auto num = [&o](const char *k) {
				      return static_cast<qint64>(o.value(QLatin1String(k)).toDouble());
			      };
			      if (!error.isEmpty()) {
				      rows = line(QStringLiteral("⚠"),
						  QStringLiteral("<span style='color:#9aa1b0'>%1</span>")
							  .arg(error.toHtmlEscaped()));
			      } else {
				      if (o.value(QStringLiteral("createdAt")).isDouble()) {
					      const qint64 c = num("createdAt");
					      rows += line(
						      QStringLiteral("🎂"),
						      QStringLiteral("%1 made <b>%2</b> ago · %3")
							      .arg(platform == QLatin1String("youtube")
									   ? QStringLiteral("Channel")
									   : QStringLiteral("Account"),
								   ageText(c, now),
								   QLocale(QLocale::English)
									   .toString(QDateTime::fromMSecsSinceEpoch(c)
											     .date(),
										     QStringLiteral("MMM yyyy"))));
				      }
				      if (o.value(QStringLiteral("followedAt")).isDouble()) {
					      const qint64 f = num("followedAt");
					      rows += line(
						      QStringLiteral("💜"),
						      QStringLiteral("Following you for <b>%1</b> · since %2")
							      .arg(ageText(f, now),
								   QLocale(QLocale::English)
									   .toString(QDateTime::fromMSecsSinceEpoch(f)
											     .date(),
										     QStringLiteral("d MMM yyyy"))));
				      } else if (o.value(QStringLiteral("following")).isBool() &&
						 !o.value(QStringLiteral("following")).toBool()) {
					      rows += line(QStringLiteral("🤍"), QStringLiteral("Not following you"));
				      }
				      if (o.value(QStringLiteral("followers")).isDouble())
					      rows += line(QStringLiteral("👥"),
							   QStringLiteral("<b>%1</b> followers")
								   .arg(shortCount(num("followers"))));
				      if (o.value(QStringLiteral("subscribers")).isDouble())
					      rows += line(QStringLiteral("👥"),
							   QStringLiteral("<b>%1</b> subscribers")
								   .arg(shortCount(num("subscribers"))));
				      const QString desc =
					      o.value(QStringLiteral("description")).toString().simplified();
				      if (!desc.isEmpty())
					      rows += line(QStringLiteral("📝"),
							   QStringLiteral("<span style='color:#9aa1b0'>%1</span>")
								   .arg((desc.size() > 140
										 ? desc.left(137) + QStringLiteral("…")
										 : desc)
										.toHtmlEscaped()));
				      const QString name = o.value(QStringLiteral("name")).toString();
				      if (!name.isEmpty() && name.size() <= 60)
					      nameLbl->setText(
						      QStringLiteral(
							      "<b style='color:%1;font-size:12pt'>%2</b>&nbsp;&nbsp;%3")
							      .arg(nameColor, name.toHtmlEscaped(), badges));
				      const QString av = o.value(QStringLiteral("avatar")).toString();
				      if (RemoteImages::allowed(av)) {
					      QPointer<QLabel> al(avatar);
					      const auto apply = [al](const QString &url) {
						      const QImage img = RemoteImages::instance().get(url);
						      if (!al || img.isNull())
							      return;
						      const qreal d = al->devicePixelRatioF();
						      QImage sq = scaledTo(img, 44, d);
						      // round it
						      QImage out(qRound(44 * d), qRound(44 * d),
								 QImage::Format_ARGB32_Premultiplied);
						      out.fill(Qt::transparent);
						      QPainter p(&out);
						      p.setRenderHint(QPainter::Antialiasing, true);
						      QPainterPath clip;
						      clip.addEllipse(QRectF(0, 0, out.width(), out.height()));
						      p.setClipPath(clip);
						      sq.setDevicePixelRatio(1.0);
						      p.drawImage(QRectF(0, 0, out.width(), out.height()), sq);
						      p.end();
						      out.setDevicePixelRatio(d);
						      al->setPixmap(QPixmap::fromImage(out));
					      };
					      if (!RemoteImages::instance().get(av).isNull())
						      apply(av);
					      else
						      RemoteImages::instance().subscribe(
							      avatar, [apply, av](const QString &url) {
								      if (url == av)
									      apply(url);
							      });
				      }
				      if (rows.isEmpty())
					      rows = line(
						      QStringLiteral("ℹ"),
						      QStringLiteral(
							      "<span style='color:#9aa1b0'>%1 doesn't share more about this viewer.</span>")
							      .arg(platformName(platform)));
			      }
			      Q_UNUSED(profileBtn);
			      setFacts(rows);
			      place();
		      });
	}
}

void GfxChatPanel::updateWatchStatus(const QJsonArray &watch)
{
	watchStatus_ = watch;
}

void GfxChatPanel::showWatchDialog()
{
	QDialog dlg(this);
	dlg.setWindowTitle(QStringLiteral("Watch other channels"));
	dlg.setMinimumWidth(qMin(460, qMax(300, width())));
	auto *v = new QVBoxLayout(&dlg);
	auto *intro = new QLabel(
		QStringLiteral("Add other streamers' chats to your feed — a co-streamer, a raid target, a friend. "
			       "They're read-only and marked with the channel's name. Up to 6."),
		&dlg);
	intro->setWordWrap(true);
	intro->setObjectName(QStringLiteral("gfxMuted"));
	v->addWidget(intro);

	auto *list = new QListWidget(&dlg);
	list->setMinimumHeight(120);
	v->addWidget(list, 1);
	QStringList items = settings().chatWatch;
	const auto stateOf = [this](const QString &raw) -> QString {
		for (qsizetype i = 0; i < watchStatus_.size(); ++i) {
			const QJsonObject o = watchStatus_.at(i).toObject();
			if (o.value(QStringLiteral("raw")).toString().compare(raw, Qt::CaseInsensitive) != 0)
				continue;
			const QString st = o.value(QStringLiteral("state")).toString();
			const QString err = o.value(QStringLiteral("error")).toString();
			if (st == QLatin1String("live"))
				return QStringLiteral("●  connected");
			if (st == QLatin1String("offline"))
				return QStringLiteral("○  ") + (err.isEmpty() ? QStringLiteral("not live") : err);
			if (st == QLatin1String("error"))
				return QStringLiteral("⚠  ") +
				       (err.isEmpty() ? QStringLiteral("couldn't connect") : err);
			return QStringLiteral("…  connecting");
		}
		return QStringLiteral("new — connects when you click OK");
	};
	const auto refill = [list, &items, stateOf, this] {
		list->clear();
		for (const QString &raw : items) {
			const QString plat = raw.section(QLatin1Char(':'), 0, 0);
			auto *it = new QListWidgetItem(
				QIcon(QPixmap::fromImage(platformIcon(plat, 16, devicePixelRatioF()))),
				QStringLiteral("%1     %2").arg(raw.section(QLatin1Char(':'), 1), stateOf(raw)), list);
			it->setData(Qt::UserRole, raw);
		}
	};
	refill();

	auto *addRow = new QHBoxLayout();
	auto *plat = new QComboBox(&dlg);
	for (const char *p : {"twitch", "kick", "youtube", "tiktok"})
		plat->addItem(QIcon(QPixmap::fromImage(platformIcon(QString::fromLatin1(p), 16, devicePixelRatioF()))),
			      platformName(QString::fromLatin1(p)), QString::fromLatin1(p));
	auto *name = new QLineEdit(&dlg);
	name->setObjectName(QStringLiteral("gfxChatInput"));
	const auto hint = [plat, name] {
		const QString p = plat->currentData().toString();
		name->setPlaceholderText(p == QLatin1String("youtube")
						 ? QStringLiteral("Live video link, @handle or channel link")
					 : p == QLatin1String("tiktok") ? QStringLiteral("@username")
									: QStringLiteral("Channel name or link"));
	};
	hint();
	connect(plat, &QComboBox::currentIndexChanged, &dlg, [hint] { hint(); });
	auto *add = new QPushButton(QStringLiteral("Add"), &dlg);
	theme::setRole(add, "primary");
	addRow->addWidget(plat);
	addRow->addWidget(name, 1);
	addRow->addWidget(add);
	v->addLayout(addRow);
	auto *note = new QLabel(&dlg);
	note->setObjectName(QStringLiteral("gfxMuted"));
	note->setWordWrap(true);
	note->setText(QStringLiteral(
		"YouTube needs your YouTube connected on goalforgex.com. "
		"Kick watching uses Kick's own website chat, so it can stop working if Kick changes it."));
	v->addWidget(note);
	auto *bottom = new QHBoxLayout();
	auto *remove = new QPushButton(QStringLiteral("Remove"), &dlg);
	remove->setEnabled(false);
	auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	bottom->addWidget(remove);
	bottom->addStretch(1);
	bottom->addWidget(box);
	v->addLayout(bottom);
	connect(list, &QListWidget::currentRowChanged, &dlg, [remove](int r) { remove->setEnabled(r >= 0); });
	connect(remove, &QPushButton::clicked, &dlg, [list, &items, refill] {
		const int r = list->currentRow();
		if (r >= 0 && r < items.size()) {
			items.removeAt(r);
			refill();
		}
	});
	const auto doAdd = [plat, name, note, &items, refill] {
		QString val = name->text().trimmed();
		const QString p = plat->currentData().toString();
		if (val.isEmpty())
			return;
		val.remove(QLatin1Char(','));
		const QString raw = p + QLatin1Char(':') + val.left(200);
		bool ok = false;
		static const QRegularExpression simple(QStringLiteral("^@?[A-Za-z0-9_.-]{2,40}$"));
		static const QRegularExpression link(QStringLiteral("^https?://\\S+$"));
		ok = simple.match(val).hasMatch() || link.match(val).hasMatch();
		if (!ok) {
			note->setText(QStringLiteral("⚠  That doesn't look like a channel name or link."));
			return;
		}
		if (items.size() >= 6) {
			note->setText(QStringLiteral("⚠  You can watch up to 6 channels — remove one first."));
			return;
		}
		if (items.contains(raw, Qt::CaseInsensitive))
			return;
		items << raw;
		name->clear();
		refill();
	};
	connect(add, &QPushButton::clicked, &dlg, doAdd);
	connect(name, &QLineEdit::returnPressed, &dlg, doAdd);
	connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	if (dlg.exec() != QDialog::Accepted)
		return;
	// A name still in the box counts as added.
	if (!name->text().trimmed().isEmpty())
		doAdd();
	if (items == settings().chatWatch)
		return;
	settings().chatWatch = items;
	saveSettings();
	refresh();
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
	toggle(QStringLiteral("Show emotes as pictures"), &s.chatEmotes);
	menu.addSeparator();

	// Event notices: on/off, look and colour right from the chat (the rest is in ⚙ Settings → MultiChat).
	QMenu *nm = menu.addMenu(QStringLiteral("🔔   Event notices"));
	{
		QAction *on = nm->addAction(QStringLiteral("Show subs, gifts, raids… in the chat"));
		on->setCheckable(true);
		on->setChecked(s.chatNotices);
		connect(on, &QAction::toggled, this, [this](bool v) {
			settings().chatNotices = v;
			saveSettings();
			refresh();
		});
		const auto choice = [this, nm](const QString &title, QString *value,
					       const QList<QPair<QString, QString>> &options) {
			nm->addSection(title);
			for (const auto &o : options) {
				QAction *a = nm->addAction(o.second);
				a->setCheckable(true);
				a->setChecked(*value == o.first);
				const QString key = o.first;
				connect(a, &QAction::triggered, this, [this, value, key] {
					*value = key;
					saveSettings();
					refresh();
				});
			}
		};
		choice(QStringLiteral("Look"), &s.noticeStyle,
		       {{QStringLiteral("card"), QStringLiteral("Card — pill, headline, message")},
			{QStringLiteral("strip"), QStringLiteral("Strip — one tinted line")},
			{QStringLiteral("minimal"), QStringLiteral("Minimal — quiet, like Twitch's own")}});
		choice(QStringLiteral("Colour"), &s.noticeTint,
		       {{QStringLiteral("platform"), QStringLiteral("Platform colour (Twitch purple, Kick green…)")},
			{QStringLiteral("event"), QStringLiteral("Event colour (gifts pink, raids red…)")},
			{QStringLiteral("accent"), QStringLiteral("My GoalForgeX accent")}});
	}
	menu.addSeparator();
	connect(menu.addAction(QStringLiteral("📺   Watch other channels… (%1)").arg(s.chatWatch.size())),
		&QAction::triggered, this, [this] { showWatchDialog(); });

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
	updateWatchStatus(o.value(QStringLiteral("watch")).toArray());
	const double serverTime = o.value(QStringLiteral("serverTime")).toDouble();
	if (serverTime > 0)
		clockSkew_ = static_cast<qint64>(serverTime) - QDateTime::currentMSecsSinceEpoch();
	renderStatus();
	updateFilters();
	updateTargets();
	const bool namesChanged = rebuildHighlightRe();

	// reset = the server started a new feed (first connect, or it restarted
	// without its history). What's already on screen stays (2.7.2) — only
	// messages we don't have yet are added, so a server update never wipes chat.
	const bool reset = o.value(QStringLiteral("reset")).toBool();
	const auto lineKey = [](const ChatMessage &m) {
		const QString id = m.notice.isEmpty() ? m.id : QStringLiteral("n:") + m.noticeId;
		return id.isEmpty() ? QString() : m.channel + QLatin1Char('|') + id;
	};
	QSet<QString> have;
	if (reset)
		for (const ChatMessage &m : msgs_)
			if (const QString k = lineKey(m); !k.isEmpty())
				have.insert(k);
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
			const QString chan = it.value(QStringLiteral("channel")).toString();
			for (ChatMessage &m : msgs_) {
				if (m.channel != chan || !m.notice.isEmpty())
					continue; // a watched channel's removals only touch that channel; notices aren't messages
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
		const bool isNotice = it.value(QStringLiteral("kind")).toString() == QLatin1String("notice");
		if (isNotice) {
			m.notice = it.value(QStringLiteral("type")).toString().left(24);
			m.noticeId = it.value(QStringLiteral("id")).toString().left(120);
			m.target = it.value(QStringLiteral("target")).toString().left(40);
			m.tier = it.value(QStringLiteral("tier")).toString().left(12);
			m.value = it.value(QStringLiteral("value")).toString().left(60);
			m.tone = it.value(QStringLiteral("tone")).toString().left(12);
			m.sysText = it.value(QStringLiteral("text")).toString().left(300);
			m.amount = static_cast<qint64>(it.value(QStringLiteral("amount")).toDouble());
			m.months = it.value(QStringLiteral("months")).toInt();
			m.streak = it.value(QStringLiteral("streak")).toInt();
			m.total = it.value(QStringLiteral("total")).toInt();
			const QJsonArray rs = it.value(QStringLiteral("recipients")).toArray();
			for (qsizetype k = 0; k < rs.size() && k < 500; ++k) {
				const QString r = rs.at(k).toString().left(40);
				if (!r.isEmpty())
					m.recipients << r;
			}
		} else {
			m.id = it.value(QStringLiteral("id")).toString();
		}
		m.platform = it.value(QStringLiteral("platform")).toString();
		m.user = it.value(QStringLiteral("user")).toString();
		m.login = it.value(QStringLiteral("login")).toString();
		m.color = it.value(QStringLiteral("color")).toString();
		// A notice's `message` is what the viewer wrote (resub message, Super Chat…).
		m.text = it.value(isNotice ? QStringLiteral("message") : QStringLiteral("text")).toString();
		m.replyTo = it.value(QStringLiteral("replyTo")).toString();
		m.host = it.value(QStringLiteral("host")).toBool();
		m.mod = it.value(QStringLiteral("mod")).toBool();
		m.vip = it.value(QStringLiteral("vip")).toBool();
		m.sub = it.value(QStringLiteral("sub")).toBool();
		m.first = it.value(QStringLiteral("first")).toBool();
		m.staff = it.value(QStringLiteral("staff")).toBool();
		m.bits = it.value(QStringLiteral("bits")).toInt();
		m.ts = static_cast<qint64>(it.value(QStringLiteral("ts")).toDouble());
		m.channel = it.value(QStringLiteral("channel")).toString().left(240);
		m.channelName = it.value(QStringLiteral("channelName")).toString().left(60);
		if (!m.channel.isEmpty() && m.channelName.isEmpty())
			m.channelName = m.channel.section(QLatin1Char(':'), 1).section(QLatin1Char('/'), -1);
		const QJsonArray parts = it.value(QStringLiteral("parts")).toArray();
		for (qsizetype k = 0; k < parts.size() && k < 150; ++k) {
			const QJsonValue pv = parts.at(k);
			ChatPart part;
			if (pv.isString()) {
				part.text = pv.toString();
			} else {
				const QJsonObject po = pv.toObject();
				part.text = po.value(QStringLiteral("e")).toString().left(60);
				part.image = po.value(QStringLiteral("u")).toString();
				if (!RemoteImages::allowed(part.image))
					part.image.clear();
			}
			if (!part.text.isEmpty())
				m.parts << part;
		}
		if (!info(m.platform))
			continue;
		// Gift bomb recipients filling in: redraw the line already in the feed.
		if (isNotice && it.value(QStringLiteral("update")).toBool()) {
			bool replaced = false;
			for (ChatMessage &old : msgs_) {
				if (old.notice.isEmpty() || old.noticeId != m.noticeId)
					continue;
				m.seq = old.seq;
				old = m;
				edited.insert(m.seq);
				replaced = true;
			}
			if (replaced)
				continue;
		}
		if (reset) {
			const QString k = lineKey(m);
			if (!k.isEmpty() && have.contains(k))
				continue; // already showing it
		}
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
	if (m.host || !m.notice.isEmpty() || highlightRe_.pattern().isEmpty() || !highlightRe_.isValid())
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
	if (m.channel.isEmpty() ? !wantedPlatforms().contains(m.platform) : !watchStatusHas(m.channel))
		return false;
	if (!m.notice.isEmpty() && !noticeWanted(m))
		return false;
	// Platform chips are about your own chats; 📺 Watching is everyone else's.
	if (info(filter_) && (m.platform != filter_ || !m.channel.isEmpty()))
		return false;
	if (filter_ == QLatin1String("watched") && m.channel.isEmpty())
		return false;
	if (filter_ == QLatin1String("mentions") && !isHighlighted(m))
		return false;
	if (!userFilterLogin_.isEmpty() &&
	    (m.platform != userFilterPlatform_ || m.login.compare(userFilterLogin_, Qt::CaseInsensitive) != 0))
		return false;
	if (!search_.isEmpty() && !m.text.contains(search_, Qt::CaseInsensitive) &&
	    !m.user.contains(search_, Qt::CaseInsensitive) && !m.login.contains(search_, Qt::CaseInsensitive) &&
	    !m.target.contains(search_, Qt::CaseInsensitive) && !m.value.contains(search_, Qt::CaseInsensitive) &&
	    !m.recipients.contains(search_, Qt::CaseInsensitive))
		return false;
	return true;
}

bool GfxChatPanel::noticeWanted(const ChatMessage &m) const
{
	const Settings &s = settings();
	if (!s.chatNotices)
		return false;
	const QLatin1String g(noticeMeta(m.notice).group);
	if (g == QLatin1String("subs"))
		return s.ncSubs;
	if (g == QLatin1String("gifts"))
		return s.ncGifts;
	if (g == QLatin1String("raids"))
		return s.ncRaids;
	if (g == QLatin1String("cheers"))
		return s.ncCheers;
	if (g == QLatin1String("tiktokgifts"))
		return s.ncTikTokGifts && (s.ncTikTokMinCoins <= 0 || m.amount >= s.ncTikTokMinCoins);
	if (g == QLatin1String("follows"))
		return m.platform == QLatin1String("tiktok") ? s.ncTikTokFollows : s.ncFollows;
	if (g == QLatin1String("shoutouts"))
		return s.ncShoutouts;
	if (g == QLatin1String("hypetrain"))
		return s.ncHypeTrain;
	if (g == QLatin1String("streaks"))
		return s.ncStreaks;
	if (g == QLatin1String("points"))
		return s.ncPoints;
	if (g == QLatin1String("announcements"))
		return s.ncAnnouncements;
	return s.ncOther;
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

bool GfxChatPanel::watchStatusHas(const QString &key) const
{
	for (qsizetype i = 0; i < watchStatus_.size(); ++i)
		if (watchStatus_.at(i).toObject().value(QStringLiteral("key")).toString() == key)
			return true;
	return false;
}

int GfxChatPanel::emoteSlot(const QString &url) const
{
	auto it = urlSlot_.constFind(url);
	if (it != urlSlot_.constEnd())
		return it.value();
	const int slot = static_cast<int>(urls_.size());
	urls_ << url;
	urlSlot_.insert(url, slot);
	return slot;
}

QString GfxChatPanel::partsHtml(const ChatMessage &m, qreal basePt) const
{
	// Emotes about 2× the text height (Twitch's own chat ratio), sharp at any size.
	const int px = qBound(16, qRound(basePt * 2.6), 72);
	QString out;
	for (const ChatPart &p : m.parts) {
		if (p.image.isEmpty() || RemoteImages::instance().failed(p.image)) {
			out += textHtml(p.text);
			continue;
		}
		out += QStringLiteral("<img src='emote:%1/%2' height='%2' style='vertical-align:middle' title='%3'>")
			       .arg(emoteSlot(p.image))
			       .arg(px)
			       .arg(p.text.toHtmlEscaped());
	}
	return out;
}

QString GfxChatPanel::lineInnerHtml(const ChatMessage &m) const
{
	if (!m.notice.isEmpty())
		return noticeHtml(m);
	const Settings &s = settings();
	const qreal base = baseFontPt();
	const qreal tagPt = qMax<qreal>(6.0, base * 0.78);
	const qreal smallPt = qMax<qreal>(6.5, base * 0.88);
	const int iconPx = qBound(12, qRound(base * 1.65), 32);
	const bool validLogin = loginRe().match(m.login).hasMatch();
	const bool validId = msgIdRe().match(m.id).hasMatch();
	const bool own = m.channel.isEmpty();

	QString h;
	const bool cheer = isCheer(m);
	const bool staffLine = !cheer && m.staff && s.chatStaffBadge;
	if ((cheer && s.noticeStyle != QLatin1String("minimal")) || staffLine)
		h += QStringLiteral("&nbsp;&nbsp;"); // room for the accent bar
	// One-click moderation, for moderators who live in this tab (own chat only).
	if (own && s.chatQuickMod && mod_ && canMod(m.platform) && !m.host && validLogin && validId && !m.deleted)
		h += QStringLiteral("<a href='mod:delete:%1' style='color:#6b7280;text-decoration:none;'>🗑</a>"
				    "&nbsp;<a href='mod:timeout:%1' style='color:#6b7280;text-decoration:none;'>⏱</a>"
				    "&nbsp;")
			     .arg(m.seq);
	// platform icon (served by ChatView::loadResource)
	if (s.chatIcons)
		h += QStringLiteral("<img src='gfx:%1/%2' width='%2' height='%2' style='vertical-align:middle'>&nbsp;")
			     .arg(m.platform)
			     .arg(iconPx);
	// Watched channel: whose chat this came from.
	if (!own)
		h += QStringLiteral(
			     "<span style='color:#9aa1b0;background-color:#23262e;font-size:%2pt;font-weight:700;'>"
			     "&nbsp;%1&nbsp;</span>&nbsp;")
			     .arg(m.channelName.toHtmlEscaped(), QString::number(tagPt, 'f', 1));
	if (s.chatTimestamps && m.ts > 0)
		h += QStringLiteral("<span style='color:#5b6270;font-size:%2pt;'>%1</span> ")
			     .arg(QDateTime::fromMSecsSinceEpoch(m.ts).toString(QStringLiteral("HH:mm")),
				  QString::number(smallPt, 'f', 1));
	// GoalForgeX staff: the brand mark + STAFF, ahead of every other badge.
	if (m.staff && s.chatStaffBadge) {
		const int markPx = qBound(10, qRound(base * 1.3), 26);
		h += QStringLiteral(
			     "<span style='background-color:%1;color:#ffd27a;font-size:%2pt;font-weight:800;'>"
			     "&nbsp;<img src='gfx:brand/%3' width='%3' height='%3' style='vertical-align:middle'>"
			     "&nbsp;GOALFORGEX STAFF&nbsp;</span>&nbsp;")
			     .arg(mixColor(QColor(theme::mapColor(QStringLiteral("#121419"))),
					   QColor(QStringLiteral("#ff3b4f")), 0.35)
					  .name(),
				  QString::number(tagPt, 'f', 1))
			     .arg(markPx);
	}
	if (s.chatBadges) {
		if (m.host)
			h += tag(own ? "HOST" : "STREAMER", "#ff3b4f", tagPt);
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
		h += QStringLiteral("<a href='user:%1' title='Click for their card · right-click for actions' "
				    "style='color:%2;text-decoration:none;font-weight:bold;'>%3</a>")
			     .arg(QString::number(m.seq), nameColor, m.user.toHtmlEscaped());
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
	if (cheer) {
		const QColor c(eventColor(m));
		h += QStringLiteral("<span style='background-color:%1;color:%2;font-size:%3pt;font-weight:800;'>"
				    "&nbsp;💎 CHEER %4&nbsp;</span>&nbsp;")
			     .arg(mixColor(QColor(theme::mapColor(QStringLiteral("#121419"))), c, 0.28).name(),
				  c.name(), QString::number(tagPt, 'f', 1), fmtCount(m.bits));
	} else if (m.bits > 0) {
		h += QStringLiteral("<span style='color:#c084fc;font-weight:700'>💎 %1</span> ").arg(m.bits);
	}
	if (m.deleted)
		h += QStringLiteral(
			     "<s style='color:#5b6270'>%1</s> <i style='color:#5b6270;font-size:%2pt'>(deleted)</i>")
			     .arg(m.text.toHtmlEscaped(), QString::number(smallPt, 'f', 1));
	else if (s.chatEmotes && !m.parts.isEmpty())
		h += QStringLiteral("<span style='color:#e8eaf0'>%1</span>").arg(partsHtml(m, base));
	else
		h += QStringLiteral("<span style='color:#e8eaf0'>%1</span>").arg(textHtml(m.text));
	return h;
}

// ── Event notice lines ──────────────────────────────────────────
//
//   card     ▌ [icon] [🎁 GIFT BOMB] 21:04
//            ▌ Santa is gifting 20 Tier 1 subs to the community!
//            ▌ → Kid1, Kid2, Kid3 …
//   strip    ▌ [icon] [🎁 GIFT BOMB] Santa is gifting 20 Tier 1 subs — message
//   minimal  [icon] 🎁 Santa is gifting 20 Tier 1 subs   (muted, like Twitch's own system lines)

QString GfxChatPanel::noticeHtml(const ChatMessage &m) const
{
	const Settings &s = settings();
	const NoticeMeta &meta = noticeMeta(m.notice);
	const QString style = s.noticeStyle;
	const bool card = style == QLatin1String("card");
	const bool minimal = style == QLatin1String("minimal");
	const qreal base = baseFontPt();
	const qreal tagPt = qMax<qreal>(6.0, base * 0.78);
	const qreal smallPt = qMax<qreal>(6.5, base * 0.88);
	const int iconPx = qBound(12, qRound(base * 1.65), 32);
	const QColor evColor(eventColor(m));
	const QString bg = theme::mapColor(QStringLiteral("#121419"));
	const QString strong = minimal ? QStringLiteral("#c9ced8") : QStringLiteral("#ffffff");
	const QString soft = minimal ? QStringLiteral("#9aa1b0") : QStringLiteral("#d6d9e0");

	// Names: the chatter's own colour (when that's on), clickable for their card.
	const QString nameColor = (s.chatNameColors && !m.color.isEmpty()) ? readableColor(m.color) : strong;
	const QString who = m.user.isEmpty() ? QStringLiteral("Someone") : m.user;
	const QString N = loginRe().match(m.login).hasMatch()
				  ? QStringLiteral("<a href='user:%1' title='Click for their card' "
						   "style='color:%2;text-decoration:none;font-weight:bold;'>%3</a>")
					    .arg(QString::number(m.seq), nameColor, who.toHtmlEscaped())
				  : QStringLiteral("<b style='color:%1'>%2</b>").arg(nameColor, who.toHtmlEscaped());
	const auto B = [&strong](const QString &t) {
		return QStringLiteral("<b style='color:%1'>%2</b>").arg(strong, t.toHtmlEscaped());
	};
	const auto plural = [](qint64 n, const char *one, const char *many) {
		return QStringLiteral("%1 %2").arg(fmtCount(n), QString::fromUtf8(n == 1 ? one : many));
	};
	const QString tierSp = m.tier.isEmpty() || m.tier == QLatin1String("Prime") ? QString()
										    : m.tier + QLatin1Char(' ');

	// Headline, per event type and platform.
	const QString &t = m.notice;
	const bool kick = m.platform == QLatin1String("kick");
	const bool tiktok = m.platform == QLatin1String("tiktok");
	QString head;
	if (t == QLatin1String("sub")) {
		head = N + QStringLiteral(" subscribed");
		if (!m.tier.isEmpty() && !kick && !tiktok)
			head += QStringLiteral(" at ") + B(m.tier);
		head += QLatin1Char('!');
	} else if (t == QLatin1String("resub")) {
		head = N + QStringLiteral(" resubscribed");
		if (m.months > 0)
			head += QStringLiteral(" for ") + B(plural(m.months, "month", "months"));
		if (!m.tier.isEmpty() && !kick)
			head += QStringLiteral(" at ") + B(m.tier);
		head += QLatin1Char('!');
		if (m.streak > 1)
			head += QStringLiteral(" <span style='color:%1'>· %2-month streak 🔥</span>")
					.arg(soft)
					.arg(m.streak);
	} else if (t == QLatin1String("prime")) {
		head = N + QStringLiteral(" subscribed with ") + B(QStringLiteral("Prime"));
		if (m.months > 1)
			head += QStringLiteral(" · ") + B(plural(m.months, "month", "months"));
		head += QLatin1Char('!');
	} else if (t == QLatin1String("giftsub")) {
		head = N + QStringLiteral(" gifted a %1sub to ").arg(tierSp.toHtmlEscaped()) +
		       B(m.target.isEmpty() ? QStringLiteral("someone") : m.target) + QLatin1Char('!');
		if (m.total > 1)
			head += QStringLiteral(" <span style='color:%1'>· %2 gifted in the channel</span>")
					.arg(soft, fmtCount(m.total));
	} else if (t == QLatin1String("giftbomb")) {
		const int total = qMax(m.total, static_cast<int>(m.recipients.size()));
		head = kick ? N + QStringLiteral(" gifted ") + B(plural(total, "sub", "subs")) + QLatin1Char('!')
			    : N + QStringLiteral(" is gifting ") +
				       B(QStringLiteral("%1 %2%3").arg(fmtCount(total), tierSp,
								       total == 1 ? QStringLiteral("sub")
										  : QStringLiteral("subs"))) +
				       QStringLiteral(" to the community!");
		if (m.value.toInt() > total)
			head += QStringLiteral(" <span style='color:%1'>· %2 gifted in the channel</span>")
					.arg(soft, fmtCount(m.value.toInt()));
	} else if (t == QLatin1String("upgrade")) {
		head = m.sysText.isEmpty() ? N + QStringLiteral(" upgraded their sub!") : textHtml(m.sysText);
	} else if (t == QLatin1String("raid")) {
		head = N + QStringLiteral(" is raiding with ") + B(plural(m.amount, "viewer", "viewers")) +
		       QStringLiteral("!");
	} else if (t == QLatin1String("follow")) {
		head = N + QStringLiteral(" followed");
	} else if (t == QLatin1String("shoutout")) {
		head = N + QStringLiteral(" gave a shoutout to ") + B(m.target);
		if (m.value == QLatin1String("ForgeBot"))
			head += QStringLiteral(" <span style='color:%1'>· via ForgeBot</span>").arg(soft);
	} else if (t == QLatin1String("shoutout_in")) {
		head = N + QStringLiteral(" gave you a shoutout");
		if (m.amount > 0)
			head += QStringLiteral(" in front of ") + B(plural(m.amount, "viewer", "viewers"));
		head += QLatin1Char('!');
	} else if (t == QLatin1String("streak")) {
		head = N + QStringLiteral(" is on a ") + B(QStringLiteral("%1-stream").arg(m.streak)) +
		       QStringLiteral(" watch streak!");
		if (m.amount > 0)
			head += QStringLiteral(" <span style='color:%1'>· +%2 points</span>")
					.arg(soft, fmtCount(m.amount));
	} else if (t == QLatin1String("points")) {
		head = N + QStringLiteral(" redeemed ") + B(m.value.isEmpty() ? QStringLiteral("a reward") : m.value);
		if (m.amount > 0)
			head += QStringLiteral(" <span style='color:%1'>· %2</span>").arg(soft, fmtCount(m.amount));
	} else if (t == QLatin1String("announcement")) {
		head = N;
	} else if (t == QLatin1String("bitsbadge")) {
		head = N + QStringLiteral(" just earned the ") + B(QStringLiteral("%1 bits").arg(fmtCount(m.amount))) +
		       QStringLiteral(" badge!");
	} else if (t == QLatin1String("kicks")) {
		head = N + QStringLiteral(" sent ") + B(plural(m.amount, "Kick", "Kicks"));
		if (!m.value.isEmpty())
			head += QStringLiteral(" <span style='color:%1'>· %2</span>").arg(soft, m.value.toHtmlEscaped());
	} else if (t == QLatin1String("superchat") || t == QLatin1String("supersticker")) {
		head = N + QStringLiteral(" sent a %1")
				   .arg(t == QLatin1String("superchat") ? QStringLiteral("Super Chat")
									: QStringLiteral("Super Sticker"));
		if (!m.value.isEmpty())
			head += QStringLiteral(" · ") + B(m.value);
	} else if (t == QLatin1String("member")) {
		head = N + QStringLiteral(" became a member");
		if (!m.value.isEmpty())
			head += QStringLiteral(" · ") + B(m.value);
		head += QLatin1Char('!');
	} else if (t == QLatin1String("milestone")) {
		head = N + QStringLiteral(" has been a member for ") + B(plural(m.months, "month", "months")) +
		       QLatin1Char('!');
	} else if (t == QLatin1String("ytgift")) {
		head = N + QStringLiteral(" gifted ") + B(plural(qMax(1, m.total), "membership", "memberships")) +
		       QLatin1Char('!');
	} else if (t == QLatin1String("tiktok_gift")) {
		head = N + QStringLiteral(" sent ") +
		       B((m.total > 1 ? QStringLiteral("%1× ").arg(m.total) : QString()) +
			 (m.value.isEmpty() ? QStringLiteral("a gift") : m.value));
		if (m.amount > 0)
			head += QStringLiteral(" <span style='color:%1'>· %2 💎</span>").arg(soft, fmtCount(m.amount));
	} else {
		// pay-it-forward, charity, anything new: the platform's own wording.
		head = m.sysText.isEmpty() ? N : textHtml(m.sysText);
	}
	if (minimal)
		head = QStringLiteral("<span style='color:%1'>%2</span>").arg(soft, head);

	QString h;
	if (!minimal)
		h += QStringLiteral("&nbsp;&nbsp;"); // room for the accent bar
	if (s.chatIcons)
		h += QStringLiteral("<img src='gfx:%1/%2' width='%2' height='%2' style='vertical-align:middle'>&nbsp;")
			     .arg(m.platform)
			     .arg(iconPx);
	if (minimal) {
		h += QString::fromUtf8(meta.icon) + QStringLiteral("&nbsp;");
	} else {
		h += QStringLiteral("<span style='background-color:%1;color:%2;font-size:%3pt;font-weight:800;'>"
				    "&nbsp;%4&nbsp;%5&nbsp;</span>&nbsp;")
			     .arg(mixColor(QColor(bg), evColor, 0.28).name(), evColor.name(),
				  QString::number(tagPt, 'f', 1), QString::fromUtf8(meta.icon),
				  QString::fromUtf8(meta.label));
	}
	if (s.chatTimestamps && m.ts > 0 && !minimal)
		h += QStringLiteral("<span style='color:#5b6270;font-size:%2pt;'>%1</span> ")
			     .arg(QDateTime::fromMSecsSinceEpoch(m.ts).toString(QStringLiteral("HH:mm")),
				  QString::number(smallPt, 'f', 1));
	const QString indent = QStringLiteral("<br>&nbsp;&nbsp;");
	h += card ? indent + head : head;

	// What the viewer wrote (resub / Super Chat / announcement message…).
	if (!m.text.trimmed().isEmpty()) {
		const QString body = s.chatEmotes && !m.parts.isEmpty() ? partsHtml(m, base) : textHtml(m.text);
		if (card)
			h += indent + QStringLiteral("<span style='color:#e8eaf0;%1'>%2</span>")
					      .arg(t == QLatin1String("announcement")
							   ? QStringLiteral("font-weight:600;")
							   : QString(),
						   body);
		else
			h += QStringLiteral(" <span style='color:%1'>— %2</span>")
				     .arg(minimal ? soft : QStringLiteral("#e8eaf0"), body);
	}

	// Gift bomb: who got one (fills in as Twitch announces each recipient).
	if (t == QLatin1String("giftbomb") && s.ncGiftNames && card) {
		const int total = qMax(m.total, static_cast<int>(m.recipients.size()));
		QString line;
		if (m.recipients.isEmpty()) {
			line = QStringLiteral("Handing out gifts…");
		} else {
			constexpr int kShow = 40;
			QStringList names;
			for (qsizetype i = 0; i < m.recipients.size() && i < kShow; ++i)
				names << m.recipients.at(i).toHtmlEscaped();
			line = QStringLiteral("→ ") + names.join(QStringLiteral(", "));
			if (m.recipients.size() > kShow)
				line += QStringLiteral(" <b>+%1 more</b>").arg(m.recipients.size() - kShow);
			else if (m.recipients.size() < total)
				line += QStringLiteral(" …");
		}
		h += indent + QStringLiteral("<span style='color:#9aa1b0;font-size:%1pt'>%2</span>")
				      .arg(QString::number(smallPt, 'f', 1), line);
	}
	return h;
}

QTextBlockFormat GfxChatPanel::blockFormat(const ChatMessage &m, bool odd) const
{
	const Settings &s = settings();
	// GoalForgeX staff: a warm brand-tinted line with the red → amber accent bar.
	if (m.notice.isEmpty() && !isCheer(m) && m.staff && s.chatStaffBadge) {
		QTextBlockFormat f = lineFormat(false, false, false, false);
		f.setBackground(mixColor(QColor(theme::mapColor(QStringLiteral("#121419"))),
					 QColor(QStringLiteral("#ff5a36")), 0.13));
		f.setProperty(kBarProp, QColor(QStringLiteral("#ff6a3d")));
		return f;
	}
	if (m.notice.isEmpty() && !isCheer(m))
		return lineFormat(m.host, s.chatHighlightMentions && isHighlighted(m), m.first && !m.deleted,
				  s.chatAltRows && odd);
	QTextBlockFormat f;
	f.setLineHeight(112, QTextBlockFormat::ProportionalHeight);
	if (s.noticeStyle == QLatin1String("minimal")) {
		f.setBottomMargin(s.chatCompact ? 1 : 4);
		return f;
	}
	const bool card = s.noticeStyle == QLatin1String("card");
	const QColor tint(noticeTint(m));
	f.setTopMargin(card ? 3 : 1);
	f.setBottomMargin(s.chatCompact ? 3 : (card ? 7 : 4));
	f.setBackground(mixColor(QColor(theme::mapColor(QStringLiteral("#121419"))), tint, card ? 0.15 : 0.10));
	f.setProperty(kBarProp, tint);
	return f;
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
	const QTextBlockFormat fmt = blockFormat(m, odd);
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
	QTextBlock b = view_->document()->findBlockByNumber(idx);
	QTextCursor c(b);
	c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
	c.removeSelectedText();
	c.setCharFormat(QTextCharFormat());
	c.insertHtml(lineInnerHtml(*m));
	c.setBlockFormat(blockFormat(*m, shown_.at(idx).odd));
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
			QTextCursor fc(doc->firstBlock());
			fc.setBlockFormat(blockFormat(*m, shown_.first().odd));
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
	if (filter_ == QLatin1String("watched") && search_.isEmpty() && userFilterLogin_.isEmpty())
		text = QStringLiteral(
			"Nothing from the channels you're watching yet — their chat shows up here as it comes in.");
	else if (!anyConnected && watchList().isEmpty())
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
	const bool watching = !watchList().isEmpty();
	// With watched channels, one connected platform still gets its chip: it's
	// how you see only your own chat.
	for (const auto &c : chips)
		c.second->setVisible((connected > 1 || (watching && connected > 0)) && platformConnected(c.first));
	fWatch_->setVisible(watching);
	fitChips();
	if (info(filter_) && !platformConnected(filter_))
		setFilter(QStringLiteral("all"));
	else if (info(filter_) && connected <= 1 && !watching)
		setFilter(QStringLiteral("all"));
	else if (filter_ == QLatin1String("watched") && !watching)
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
	// Names on the buttons when there's room; otherwise icons (name in the tooltip).
	const bool names = mode_ == 2 || (mode_ == 1 && shown <= 2) || (mode_ == 0 && shown <= 1);
	for (QPushButton *b : targetButtons())
		b->setText(names ? b->property("gfxLabel").toString() : QString());
	readOnlyLbl_->setVisible(shown == 0 && platformConnected(QStringLiteral("tiktok")));
	updateSendState();
}

void GfxChatPanel::updateSendState()
{
	QStringList where;
	for (QPushButton *b : targetButtons())
		if (b->isChecked() && b->isEnabled())
			where << b->property("gfxLabel").toString();
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
	fWatch_->setChecked(f == QLatin1String("watched"));
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
	sendBtn_->setText(mode_ == 0 ? QStringLiteral("➤") : QStringLiteral("Send"));
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
	// Watched channels removed in the dialog disappear now, not at the next poll.
	QJsonArray still;
	for (qsizetype i = 0; i < watchStatus_.size(); ++i) {
		const QJsonObject o = watchStatus_.at(i).toObject();
		if (watchList().contains(o.value(QStringLiteral("raw")).toString(), Qt::CaseInsensitive))
			still << o;
	}
	watchStatus_ = still;
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
			if (m.host || !m.channel.isEmpty() || !loginRe().match(m.login).hasMatch())
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
