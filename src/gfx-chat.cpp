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

#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMessageBox>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSize>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>

#include <utility>

using namespace gfx;

namespace {

constexpr int kKeep = 300;   // messages kept in the panel
constexpr int kMaxLen = 500; // Twitch and Kick both cap at 500
constexpr int kIconPx = 15;  // platform icon size in a chat line (logical px)

// Platform icon (src/resources/*.png, 64 px) scaled once per size + screen
// scale with smooth filtering — the text renderer would otherwise shrink it
// on every repaint and blur it.
QImage platformIcon(const QString &platform, int logicalPx, qreal dpr)
{
	static QHash<QString, QImage> cache;
	const bool kick = platform == QLatin1String("kick");
	const QString key = QStringLiteral("%1/%2/%3").arg(kick ? 1 : 0).arg(logicalPx).arg(dpr);
	auto it = cache.constFind(key);
	if (it != cache.constEnd())
		return it.value();
	const QImage src(kick ? QStringLiteral(":/goalforgex/kick.png") : QStringLiteral(":/goalforgex/twitch.png"));
	QImage img;
	if (!src.isNull()) {
		const int px = qMax(1, qRound(logicalPx * dpr));
		img = src.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		img.setDevicePixelRatio(dpr);
	}
	cache.insert(key, img);
	return img;
}

// Profile links: only plain usernames ever become a URL.
const QRegularExpression &loginRe()
{
	static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_-]{0,39}$"));
	return re;
}

const QRegularExpression &msgIdRe()
{
	static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9-]{1,80}$"));
	return re;
}

void openProfile(const QString &platform, const QString &login)
{
	const QString l = login.toLower();
	if (!loginRe().match(l).hasMatch())
		return;
	QDesktopServices::openUrl(QUrl((platform == QLatin1String("kick") ? QStringLiteral("https://kick.com/")
									  : QStringLiteral("https://www.twitch.tv/")) +
				       l));
}

// The feed: serves the platform icons to the HTML as gfx:twitch / gfx:kick.
class ChatView : public QTextBrowser {
public:
	using QTextBrowser::QTextBrowser;

protected:
	QVariant loadResource(int type, const QUrl &name) override
	{
		if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("gfx"))
			return QVariant::fromValue(platformIcon(name.path(), kIconPx, devicePixelRatioF()));
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

QString tag(const char *text, const char *color)
{
	return QStringLiteral("<span style='color:%1;font-size:7pt;font-weight:800;'>%2</span>&nbsp;")
		.arg(QLatin1String(color), QLatin1String(text));
}

} // namespace

GfxChatPanel::GfxChatPanel(QWidget *parent) : QWidget(parent)
{
	buildUi();
	updateTargets();
	render();
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

	// Filter: All | Twitch | Kick   ·   connection status
	auto *top = new QHBoxLayout();
	top->setSpacing(4);
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
	fAll_->setChecked(true);
	statusLbl_ = new QLabel(main_);
	statusLbl_->setObjectName(QStringLiteral("gfxChatStatus"));
	statusLbl_->setTextFormat(Qt::RichText);
	top->addWidget(fAll_);
	top->addWidget(fTwitch_);
	top->addWidget(fKick_);
	top->addStretch(1);
	top->addWidget(statusLbl_);
	root->addLayout(top);

	// Feed
	view_ = new ChatView(main_);
	view_->setObjectName(QStringLiteral("gfxChatView"));
	view_->setOpenLinks(false);
	// Names link to user:<platform>:<login>:<messageId> → the chatter menu.
	connect(view_, &QTextBrowser::anchorClicked, this, [this](const QUrl &u) {
		if (u.scheme() != QLatin1String("user"))
			return;
		const QStringList parts = u.path().split(QLatin1Char(':'));
		if (parts.size() < 2)
			return;
		const QString platform = parts.at(0), login = parts.at(1);
		if ((platform != QLatin1String("twitch") && platform != QLatin1String("kick")) ||
		    !loginRe().match(login).hasMatch())
			return;
		showUserMenu(platform, login, parts.size() > 2 ? parts.at(2) : QString());
	});
	view_->setOpenExternalLinks(false);
	view_->setFrameShape(QFrame::NoFrame);
	view_->setMinimumHeight(160);
	view_->document()->setDocumentMargin(8);
	root->addWidget(view_, 1);
	connect(view_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
		if (jump_ && v >= view_->verticalScrollBar()->maximum() - 24) {
			pendingNew_ = 0;
			jump_->hide();
		}
	});

	jump_ = new QPushButton(main_);
	jump_->setObjectName(QStringLiteral("gfxJump"));
	theme::setRole(jump_, "soft");
	jump_->setCursor(Qt::PointingHandCursor);
	jump_->hide();
	root->addWidget(jump_);
	connect(jump_, &QPushButton::clicked, this, [this] { scrollToBottom(); });

	// Composer: [Twitch] [Kick] [ message…        ] [Send]
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
	for (QPushButton *b : {toTwitch_, toKick_}) {
		const QString p = b == toKick_ ? QStringLiteral("kick") : QStringLiteral("twitch");
		b->setIcon(QIcon(QPixmap::fromImage(platformIcon(p, 16, devicePixelRatioF()))));
		b->setIconSize(QSize(16, 16));
	}
	toTwitch_->setChecked(settings().chatToTwitch);
	toKick_->setChecked(settings().chatToKick);
	input_ = new QLineEdit(main_);
	input_->setObjectName(QStringLiteral("gfxChatInput"));
	input_->setMaxLength(kMaxLen);
	input_->setPlaceholderText(QStringLiteral("Send a message…"));
	input_->setClearButtonEnabled(false);
	sendBtn_ = new QPushButton(QStringLiteral("Send"), main_);
	theme::setRole(sendBtn_, "primary");
	sendBtn_->setCursor(Qt::PointingHandCursor);
	comp->addWidget(toTwitch_);
	comp->addWidget(toKick_);
	comp->addWidget(input_, 1);
	comp->addWidget(sendBtn_);
	root->addLayout(comp);

	countLbl_ = new QLabel(main_);
	countLbl_->setObjectName(QStringLiteral("gfxMuted"));
	countLbl_->setAlignment(Qt::AlignRight);
	countLbl_->hide();
	root->addWidget(countLbl_);

	// Targets: always keep at least one selected, remember the choice.
	const auto onTarget = [this](QPushButton *self, QPushButton *other) {
		if (!self->isChecked() && !(other->isChecked() && other->isEnabled()))
			self->setChecked(true);
		settings().chatToTwitch = toTwitch_->isChecked();
		settings().chatToKick = toKick_->isChecked();
		saveSettings();
		updateSendState();
	};
	connect(toTwitch_, &QPushButton::clicked, this, [this, onTarget] { onTarget(toTwitch_, toKick_); });
	connect(toKick_, &QPushButton::clicked, this, [this, onTarget] { onTarget(toKick_, toTwitch_); });
	connect(input_, &QLineEdit::textChanged, this, [this](const QString &t) {
		countLbl_->setVisible(t.size() > kMaxLen - 100);
		countLbl_->setText(QStringLiteral("%1 / %2").arg(t.size()).arg(kMaxLen));
		updateSendState();
	});
	connect(input_, &QLineEdit::returnPressed, this, [this] { doSend(); });
	connect(sendBtn_, &QPushButton::clicked, this, [this] { doSend(); });
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
	const bool kick = platform == QLatin1String("kick");
	const QString shown = msg ? msg->user : login;
	const bool isHost = msg && msg->host;

	QMenu menu(this);
	QAction *head = menu.addAction(
		QIcon(QPixmap::fromImage(platformIcon(platform, 16, devicePixelRatioF()))),
		QStringLiteral("%1  ·  %2").arg(shown, kick ? QStringLiteral("Kick") : QStringLiteral("Twitch")));
	head->setEnabled(false);
	menu.addSeparator();
	connect(menu.addAction(QStringLiteral("👤   Open %1 profile")
				       .arg(kick ? QStringLiteral("Kick") : QStringLiteral("Twitch"))),
		&QAction::triggered, this, [platform, login] { openProfile(platform, login); });
	connect(menu.addAction(QStringLiteral("📋   Copy username")), &QAction::triggered, this,
		[login] { QGuiApplication::clipboard()->setText(login); });

	// The broadcaster can't be moderated — profile + copy only.
	if (!isHost && mod_) {
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
			[this, platform, login, messageId, shown, kick] {
				if (QMessageBox::question(
					    this, QStringLiteral("Ban %1?").arg(shown),
					    QStringLiteral(
						    "Permanently ban %1 from your %2 chat? You can unban them later.")
						    .arg(shown, kick ? QStringLiteral("Kick")
								     : QStringLiteral("Twitch"))) == QMessageBox::Yes)
					mod_(platform, QStringLiteral("ban"), login, 0, messageId, shown);
			});
		connect(menu.addAction(QStringLiteral("↩   Unban")), &QAction::triggered, this,
			[this, platform, login, messageId, shown] {
				mod_(platform, QStringLiteral("unban"), login, 0, messageId, shown);
			});
	}
	menu.exec(QCursor::pos());
}

void GfxChatPanel::setLocked(const QString &message)
{
	const bool locked = !message.isEmpty();
	lockLbl_->setText(
		QStringLiteral(
			"<b style='color:#fff'>Twitch + Kick chat in OBS</b><br><br>%1<br>Read both chats in one feed and reply to either — or both — without leaving OBS.")
			.arg(message.toHtmlEscaped()));
	lockLbl_->setTextFormat(Qt::RichText);
	lockPanel_->setVisible(locked);
	main_->setVisible(!locked);
}

bool GfxChatPanel::platformConnected(const QString &p) const
{
	return status_.value(p).toObject().value(QStringLiteral("connected")).toBool();
}

int GfxChatPanel::applyPoll(const QJsonObject &o)
{
	status_ = o.value(QStringLiteral("status")).toObject();
	renderStatus();
	updateTargets();

	bool changed = false;
	if (o.value(QStringLiteral("reset")).toBool()) {
		msgs_.clear();
		changed = true;
	}
	int added = 0;
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
					changed = true;
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
		if (m.platform != QLatin1String("twitch") && m.platform != QLatin1String("kick"))
			continue;
		msgs_.push_back(m);
		changed = true;
		if (matches(m))
			++added;
	}
	while (msgs_.size() > kKeep)
		msgs_.removeFirst();
	if (changed)
		render(o.value(QStringLiteral("reset")).toBool() ? 0 : added,
		       o.value(QStringLiteral("reset")).toBool());
	return o.value(QStringLiteral("reset")).toBool() ? 0 : added;
}

bool GfxChatPanel::matches(const ChatMessage &m) const
{
	return filter_ == QLatin1String("all") || m.platform == filter_;
}

QString GfxChatPanel::lineHtml(const ChatMessage &m) const
{
	const bool kick = m.platform == QLatin1String("kick");
	QString h = QStringLiteral("<p style='margin:0 0 5px 0;%1'>")
			    .arg(m.host ? QStringLiteral("background-color:#1f1a10;") : QString());
	// platform icon (served by ChatView::loadResource)
	h += QStringLiteral("<img src='gfx:%1' width='%2' height='%2' style='vertical-align:middle'>&nbsp;")
		     .arg(kick ? QStringLiteral("kick") : QStringLiteral("twitch"))
		     .arg(kIconPx);
	if (settings().chatTimestamps && m.ts > 0)
		h += QStringLiteral("<span style='color:#5b6270;font-size:8pt;'>%1</span> ")
			     .arg(QDateTime::fromMSecsSinceEpoch(m.ts).toString(QStringLiteral("HH:mm")));
	if (m.host)
		h += tag("HOST", "#ff3b4f");
	else if (m.mod)
		h += tag("MOD", "#34d399");
	if (m.vip)
		h += tag("VIP", "#f472b6");
	if (m.sub)
		h += tag("SUB", "#ffb020");
	const QString nameColor = m.color.isEmpty() ? QStringLiteral("#c9ced8") : readableColor(m.color);
	if (loginRe().match(m.login).hasMatch())
		h += QStringLiteral("<a href='user:%1:%2:%3' title='Click for options — profile, timeout, ban' "
				    "style='color:%4;text-decoration:none;font-weight:bold;'>%5</a>")
			     .arg(m.platform, m.login, msgIdRe().match(m.id).hasMatch() ? m.id : QString(), nameColor,
				  m.user.toHtmlEscaped());
	else
		h += QStringLiteral("<b style='color:%1'>%2</b>").arg(nameColor, m.user.toHtmlEscaped());
	h += QStringLiteral("<span style='color:#5b6270'>:</span> ");
	if (m.deleted) {
		h += QStringLiteral("<i style='color:#5b6270'>message deleted</i>");
	} else {
		if (!m.replyTo.isEmpty())
			h += QStringLiteral("<span style='color:#6b7280'>↪ @%1</span> ").arg(m.replyTo.toHtmlEscaped());
		if (m.first)
			h += QStringLiteral("<span style='color:#ffb020'>✨ first message</span> ");
		if (m.bits > 0)
			h += QStringLiteral("<span style='color:#c084fc;font-weight:700'>💎 %1</span> ").arg(m.bits);
		h += QStringLiteral("<span style='color:#e8eaf0'>%1</span>").arg(m.text.toHtmlEscaped());
	}
	return h + QStringLiteral("</p>");
}

void GfxChatPanel::render(int newlyAdded, bool forceBottom)
{
	QScrollBar *sb = view_->verticalScrollBar();
	const bool atBottom = forceBottom || sb->value() >= sb->maximum() - 24;
	const int keep = sb->value();

	QString html;
	html.reserve(msgs_.size() * 260);
	int shown = 0;
	for (const ChatMessage &m : msgs_) {
		if (!matches(m))
			continue;
		html += lineHtml(m);
		++shown;
	}
	if (!shown) {
		const bool any = platformConnected(QStringLiteral("twitch")) ||
				 platformConnected(QStringLiteral("kick"));
		html = any ? QStringLiteral(
				     "<p style='color:#6b7280;margin-top:24px;' align='center'>No messages yet — chat shows up here as it comes in.</p>")
			   : QStringLiteral(
				     "<p style='color:#6b7280;margin-top:24px;' align='center'>Connect Twitch or Kick on goalforgex.com to see your chat here.</p>");
	}
	view_->setHtml(html);

	if (atBottom) {
		scrollToBottom();
	} else {
		sb->setValue(keep);
		if (newlyAdded > 0) {
			pendingNew_ += newlyAdded;
			jump_->setText(QStringLiteral("↓  %1 new message%2")
					       .arg(pendingNew_)
					       .arg(pendingNew_ == 1 ? QString() : QStringLiteral("s")));
			jump_->show();
		}
	}
}

void GfxChatPanel::scrollToBottom()
{
	pendingNew_ = 0;
	jump_->hide();
	QScrollBar *sb = view_->verticalScrollBar();
	sb->setValue(sb->maximum());
	// The document lays out lazily — scroll again once it has.
	QTimer::singleShot(0, this,
			   [this] { view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum()); });
}

void GfxChatPanel::renderStatus()
{
	const auto dot = [this](const QString &p, const QString &label) {
		// grey = not connected, amber = connecting / needs reconnecting, green = live
		const QJsonObject s = status_.value(p).toObject();
		QString color = QStringLiteral("#4b5160");
		if (s.value(QStringLiteral("connected")).toBool())
			color = (!s.value(QStringLiteral("tokenValid")).toBool(true) ||
				 !s.value(QStringLiteral("ready")).toBool(true))
					? QStringLiteral("#ffb020")
					: QStringLiteral("#34d399");
		return QStringLiteral("<span style='color:%1'>●</span> <span style='color:#9aa1b0'>%2</span>")
			.arg(color, label);
	};
	statusLbl_->setText(dot(QStringLiteral("twitch"), QStringLiteral("Twitch")) + QStringLiteral("&nbsp;&nbsp;") +
			    dot(QStringLiteral("kick"), QStringLiteral("Kick")));
	const QJsonObject tw = status_.value(QStringLiteral("twitch")).toObject();
	const QJsonObject kk = status_.value(QStringLiteral("kick")).toObject();
	statusLbl_->setToolTip(QStringLiteral("Twitch: %1\nKick: %2")
				       .arg(tw.value(QStringLiteral("connected")).toBool()
						    ? tw.value(QStringLiteral("name")).toString()
						    : QStringLiteral("not connected"),
					    kk.value(QStringLiteral("connected")).toBool()
						    ? kk.value(QStringLiteral("name")).toString()
						    : QStringLiteral("not connected")));
}

void GfxChatPanel::updateTargets()
{
	const bool tw = platformConnected(QStringLiteral("twitch"));
	const bool kk = platformConnected(QStringLiteral("kick"));
	toTwitch_->setEnabled(tw);
	toKick_->setEnabled(kk);
	toTwitch_->setToolTip(tw ? QStringLiteral("Send to Twitch chat")
				 : QStringLiteral("Connect Twitch on goalforgex.com"));
	toKick_->setToolTip(kk ? QStringLiteral("Send to Kick chat")
			       : QStringLiteral("Connect Kick on goalforgex.com"));
	// Only one platform connected → that's where messages go.
	if (tw && !kk)
		toTwitch_->setChecked(true);
	if (kk && !tw)
		toKick_->setChecked(true);
	updateSendState();
}

void GfxChatPanel::updateSendState()
{
	const bool anyTarget = (toTwitch_->isChecked() && toTwitch_->isEnabled()) ||
			       (toKick_->isChecked() && toKick_->isEnabled());
	sendBtn_->setEnabled(!sending_ && anyTarget && !input_->text().trimmed().isEmpty());
	input_->setEnabled(!sending_);
	QString where;
	if (toTwitch_->isChecked() && toTwitch_->isEnabled() && toKick_->isChecked() && toKick_->isEnabled())
		where = QStringLiteral("Twitch and Kick");
	else if (toTwitch_->isChecked() && toTwitch_->isEnabled())
		where = QStringLiteral("Twitch");
	else if (toKick_->isChecked() && toKick_->isEnabled())
		where = QStringLiteral("Kick");
	input_->setPlaceholderText(where.isEmpty() ? QStringLiteral("Connect Twitch or Kick to chat")
						   : QStringLiteral("Message %1 chat…").arg(where));
}

void GfxChatPanel::setFilter(const QString &f)
{
	filter_ = f;
	fAll_->setChecked(f == QLatin1String("all"));
	fTwitch_->setChecked(f == QLatin1String("twitch"));
	fKick_->setChecked(f == QLatin1String("kick"));
	render(0, true);
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
	if (platforms.isEmpty())
		return;
	sending_ = true;
	sendBtn_->setText(QStringLiteral("…"));
	updateSendState();
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
	pendingNew_ = 0;
	renderStatus();
	updateTargets();
	render(0, true);
}
