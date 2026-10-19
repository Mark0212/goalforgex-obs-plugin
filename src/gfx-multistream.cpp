/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-multistream.hpp"
#include "gfx-api.hpp"
#include "gfx-auth.hpp"
#include "gfx-http.hpp"
#include "gfx-notice.hpp"
#include "gfx-theme.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <iterator>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

using namespace gfx;

namespace {

// ── Platforms ───────────────────────────────────────────────────
struct PlatformPreset {
	const char *key;
	const char *name;
	const char *server; // default ingest ("" = paste it from the platform)
	const char *hint;
};
const PlatformPreset kPlatforms[] = {
	{"twitch", "Twitch", "rtmp://live.twitch.tv/app",
	 "Twitch → Creator Dashboard → Settings → Stream → Primary Stream key."},
	{"kick", "Kick", "rtmps://fa723fc1b171.global-contribute.live-video.net/app",
	 "Kick → Creator Dashboard → Settings → Stream URL & Key. Copy both."},
	{"youtube", "YouTube", "rtmp://a.rtmp.youtube.com/live2", "YouTube Studio → Go live → Stream → Stream key."},
	{"tiktok", "TikTok", "",
	 "TikTok LIVE Producer (live.tiktok.com) → Server URL and Stream key. TikTok only gives these to some accounts, "
	 "and its keys change every stream."},
	{"custom", "Custom RTMP", "", "Any RTMP / RTMPS server: paste its server URL and stream key."},
};
const PlatformPreset *preset(const QString &k)
{
	for (const PlatformPreset &p : kPlatforms)
		if (k == QLatin1String(p.key))
			return &p;
	return &kPlatforms[std::size(kPlatforms) - 1];
}

QString fmtClock(qint64 secs)
{
	secs = qMax<qint64>(0, secs);
	return QStringLiteral("%1:%2:%3")
		.arg(secs / 3600)
		.arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(secs % 60, 2, 10, QLatin1Char('0'));
}

// ── Saved stream keys: DPAPI on Windows ─────────────────────────
#ifdef _WIN32
const char kEntropy[] = "GoalForgeX-OBS-multistream-v1";
QByteArray protect(const QByteArray &plain)
{
	DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
	DATA_BLOB ent{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
	DATA_BLOB out{0, nullptr};
	if (!CryptProtectData(&in, L"GoalForgeX", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return QByteArray();
	QByteArray res(reinterpret_cast<const char *>(out.pbData), static_cast<qsizetype>(out.cbData));
	LocalFree(out.pbData);
	return res;
}
QByteArray unprotect(const QByteArray &cipher)
{
	DATA_BLOB in{static_cast<DWORD>(cipher.size()),
		     reinterpret_cast<BYTE *>(const_cast<char *>(cipher.constData()))};
	DATA_BLOB ent{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy))};
	DATA_BLOB out{0, nullptr};
	if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return QByteArray();
	QByteArray res(reinterpret_cast<const char *>(out.pbData), static_cast<qsizetype>(out.cbData));
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return res;
}
#else
QByteArray protect(const QByteArray &plain)
{
	return plain;
}
QByteArray unprotect(const QByteArray &cipher)
{
	return cipher;
}
#endif

QString storePath()
{
	char *p = obs_module_config_path("multistream.dat");
	const QString s = QString::fromUtf8(p ? p : "");
	bfree(p);
	return s;
}

QString stopReason(int code)
{
	switch (code) {
	case 0:
		return QString();
	case -1:
		return QStringLiteral("The server address isn't valid.");
	case -2:
		return QStringLiteral("Couldn't connect — check the server address and your internet.");
	case -3:
		return QStringLiteral("The platform rejected the stream key.");
	case -5:
		return QStringLiteral("Disconnected, and reconnecting gave up.");
	case -6:
		return QStringLiteral("This server needs a setting OBS doesn't support.");
	case -8:
		return QStringLiteral("The encoder failed.");
	default:
		return QStringLiteral("The stream stopped with an error.");
	}
}

// What's set in OBS → Settings → Stream (OBS's own, "main" stream).
struct MainStreamInfo {
	bool configured = false;
	QString platform; // twitch | kick | youtube | tiktok | custom
	QString service;  // OBS's service name ("Twitch", "YouTube - RTMPS"…) or "Custom server"
	QString server;
	QString key;
};
MainStreamInfo readMainStream()
{
	MainStreamInfo m;
	obs_service_t *svc = obs_frontend_get_streaming_service(); // borrowed, no reference added
	if (!svc)
		return m;
	obs_data_t *s = obs_service_get_settings(svc);
	m.service = QString::fromUtf8(obs_data_get_string(s, "service"));
	m.server = QString::fromUtf8(obs_data_get_string(s, "server"));
	m.key = QString::fromUtf8(obs_data_get_string(s, "key"));
	obs_data_release(s);
	const QString hay = (m.service + QLatin1Char(' ') + m.server).toLower();
	if (hay.contains(QLatin1String("twitch")))
		m.platform = QStringLiteral("twitch");
	else if (hay.contains(QLatin1String("kick")) || hay.contains(QLatin1String("live-video.net")))
		m.platform = QStringLiteral("kick");
	else if (hay.contains(QLatin1String("youtube")))
		m.platform = QStringLiteral("youtube");
	else if (hay.contains(QLatin1String("tiktok")))
		m.platform = QStringLiteral("tiktok");
	else
		m.platform = QStringLiteral("custom");
	if (m.service.isEmpty())
		m.service = QStringLiteral("Custom server");
	m.configured = !m.key.isEmpty();
	return m;
}

// live_123456789abc → live_••••9abc
QString maskKey(const QString &key)
{
	if (key.size() <= 8)
		return QStringLiteral("••••••");
	return key.left(5) + QStringLiteral("••••") + key.right(4);
}

QLabel *platformIconLabel(const QString &platform, QWidget *parent)
{
	auto *l = new QLabel(parent);
	l->setFixedSize(20, 20);
	const QImage img = notice::platformIcon(platform, 18, parent->devicePixelRatioF());
	if (!img.isNull())
		l->setPixmap(QPixmap::fromImage(img));
	else
		l->setText(QStringLiteral("📡"));
	return l;
}

} // namespace

// Handed to an output's signal callbacks (they run on OBS threads). Named namespace,
// not anonymous: Runner (external linkage) holds a pointer to it, which GCC
// flags with -Wsubobject-linkage otherwise.
namespace gfx::multistream {
struct SigCtx {
	QPointer<GfxMultistreamDock> dock;
	QString id;
};
} // namespace gfx::multistream
using MsSigCtx = gfx::multistream::SigCtx;

// ── One live output ──────────────────────────────────────────────
struct GfxMultistreamDock::Runner {
	obs_output_t *output = nullptr;
	obs_service_t *service = nullptr;
	obs_encoder_t *ownVideo = nullptr; // only when this destination encodes on its own
	obs_encoder_t *ownAudio = nullptr;
	MsSigCtx *ctx = nullptr;
	QString state = QStringLiteral("connecting"); // connecting | live | reconnecting
	qint64 startedMs = 0;
	uint64_t lastBytes = 0;
	qint64 lastBytesMs = 0;
	int kbps = 0;
};

namespace {

void post(void *data, const char *what, long long code)
{
	auto *c = static_cast<MsSigCtx *>(data);
	if (!c)
		return;
	QPointer<GfxMultistreamDock> dock = c->dock;
	const QString id = c->id;
	const QString w = QString::fromLatin1(what);
	QMetaObject::invokeMethod(
		qApp,
		[dock, id, w, code] {
			if (dock)
				dock->onOutputSignal(id, w, static_cast<int>(code));
		},
		Qt::QueuedConnection);
}
void sigStart(void *data, calldata_t *)
{
	post(data, "start", 0);
}
void sigStop(void *data, calldata_t *cd)
{
	post(data, "stop", calldata_int(cd, "code"));
}
void sigReconnect(void *data, calldata_t *)
{
	post(data, "reconnect", 0);
}
void sigReconnected(void *data, calldata_t *)
{
	post(data, "reconnect_success", 0);
}

} // namespace

// ── Dock ─────────────────────────────────────────────────────────

GfxMultistreamDock::GfxMultistreamDock(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(280);
	setObjectName(QStringLiteral("GfxDock")); // the shared style sheet keys its background off it
	setAttribute(Qt::WA_StyledBackground, true);
	load();
	buildUi();
	theme::applyTo(this);
	applyAppearance();
	tick_.setInterval(1000);
	connect(&tick_, &QTimer::timeout, this, [this] { tick(); });
	// OBS → Settings → Stream can change at any time (and isn't loaded yet when this dock is
	// created), so the Main stream row re-reads it every few seconds while visible.
	mainRefresh_.setInterval(3000);
	connect(&mainRefresh_, &QTimer::timeout, this, [this] {
		if (isVisible() && !tick_.isActive())
			renderMain();
	});
	mainRefresh_.start();
	msgClear_.setSingleShot(true);
	connect(&msgClear_, &QTimer::timeout, this, [this] { msg_->hide(); });
	mainLive_ = obs_frontend_streaming_active();
	if (mainLive_) {
		mainStartMs_ = QDateTime::currentMSecsSinceEpoch(); // already live when the dock loaded
		tick_.start();
	}
	renderList();
}

GfxMultistreamDock::~GfxMultistreamDock()
{
	shutdown();
}

void GfxMultistreamDock::buildUi()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *inner = new QWidget(scroll);
	auto *root = new QVBoxLayout(inner);
	root->setContentsMargins(10, 8, 10, 10);
	root->setSpacing(8);
	scroll->setWidget(inner);
	outer->addWidget(scroll);

	// Header: mark · Multistream · "2 of 3 live"
	auto *head = new QHBoxLayout();
	head->setSpacing(8);
	mark_ = new QLabel(inner);
	mark_->setFixedSize(22, 22);
	auto *title = new QLabel(QStringLiteral("Multistream"), inner);
	title->setObjectName(QStringLiteral("gfxBrandName"));
	summary_ = new QLabel(inner);
	summary_->setObjectName(QStringLiteral("gfxMuted"));
	head->addWidget(mark_);
	head->addWidget(title);
	head->addStretch(1);
	head->addWidget(summary_);
	root->addLayout(head);

	bandwidth_ = new QLabel(inner);
	bandwidth_->setObjectName(QStringLiteral("gfxStatSummary"));
	bandwidth_->setWordWrap(true);
	bandwidth_->setTextFormat(Qt::RichText);
	root->addWidget(bandwidth_);

	// ── Title & category everywhere (Pro) ──
	infoCard_ = new QFrame(inner);
	infoCard_->setObjectName(QStringLiteral("gfxStatCard"));
	auto *ic = new QVBoxLayout(infoCard_);
	ic->setContentsMargins(10, 8, 10, 10);
	ic->setSpacing(6);
	auto *icHead = new QLabel(QStringLiteral("<b>Title &amp; category everywhere</b>"), infoCard_);
	icHead->setTextFormat(Qt::RichText);
	ic->addWidget(icHead);
	title_ = new QLineEdit(infoCard_);
	title_->setPlaceholderText(QStringLiteral("Stream title"));
	title_->setMaxLength(140);
	category_ = new QLineEdit(infoCard_);
	category_->setPlaceholderText(QStringLiteral("Category or game, e.g. Just Chatting"));
	category_->setMaxLength(100);
	ic->addWidget(title_);
	ic->addWidget(category_);
	auto *icRow = new QHBoxLayout();
	toTwitch_ = new QCheckBox(QStringLiteral("Twitch"), infoCard_);
	toKick_ = new QCheckBox(QStringLiteral("Kick"), infoCard_);
	toTwitch_->setChecked(true);
	toKick_->setChecked(true);
	pushBtn_ = new QPushButton(QStringLiteral("Update everywhere"), infoCard_);
	theme::setRole(pushBtn_, "primary");
	pushBtn_->setCursor(Qt::PointingHandCursor);
	icRow->addWidget(toTwitch_);
	icRow->addWidget(toKick_);
	icRow->addStretch(1);
	icRow->addWidget(pushBtn_);
	ic->addLayout(icRow);
	infoNote_ = new QLabel(infoCard_);
	infoNote_->setObjectName(QStringLiteral("gfxMuted"));
	infoNote_->setWordWrap(true);
	infoNote_->hide();
	ic->addWidget(infoNote_);
	root->addWidget(infoCard_);
	connect(pushBtn_, &QPushButton::clicked, this, [this] { pushStreamInfo(); });

	// ── Main stream: whatever is set in OBS → Settings → Stream ──
	auto *mh = new QLabel(QStringLiteral("<b>Main stream</b>"), inner);
	mh->setTextFormat(Qt::RichText);
	root->addWidget(mh);
	auto *mainBox = new QFrame(inner);
	mainBox->setObjectName(QStringLiteral("gfxStatCard"));
	auto *ml = new QHBoxLayout(mainBox);
	ml->setContentsMargins(8, 6, 6, 6);
	ml->setSpacing(8);
	mainIcon_ = new QLabel(mainBox);
	mainIcon_->setFixedSize(20, 20);
	auto *mtexts = new QVBoxLayout();
	mtexts->setSpacing(1);
	mainName_ = new QLabel(mainBox);
	mainName_->setTextFormat(Qt::RichText);
	mainState_ = new QLabel(mainBox);
	mainState_->setTextFormat(Qt::RichText);
	mainStats_ = new QLabel(mainBox);
	mainStats_->setObjectName(QStringLiteral("gfxStatSub"));
	mainKey_ = new QLabel(mainBox);
	mainKey_->setObjectName(QStringLiteral("gfxStatSub"));
	mainKey_->setTextFormat(Qt::RichText);
	mainKey_->setWordWrap(true);
	mtexts->addWidget(mainName_);
	mtexts->addWidget(mainState_);
	mtexts->addWidget(mainStats_);
	mtexts->addWidget(mainKey_);
	ml->addWidget(mainIcon_, 0, Qt::AlignTop);
	ml->addLayout(mtexts, 1);
	mainBtn_ = new QPushButton(mainBox);
	mainBtn_->setCursor(Qt::PointingHandCursor);
	ml->addWidget(mainBtn_, 0, Qt::AlignTop);
	root->addWidget(mainBox);
	// Starts / stops OBS's own stream, exactly like OBS's Start Streaming button.
	connect(mainBtn_, &QPushButton::clicked, this, [] {
		if (obs_frontend_streaming_active())
			obs_frontend_streaming_stop();
		else
			obs_frontend_streaming_start();
	});

	// ── Destinations ──
	auto *dh = new QHBoxLayout();
	auto *dhl = new QLabel(QStringLiteral("<b>Destinations</b>"), inner);
	dhl->setTextFormat(Qt::RichText);
	auto *addBtn = new QPushButton(QStringLiteral("+ Add"), inner);
	theme::setRole(addBtn, "soft");
	addBtn->setCursor(Qt::PointingHandCursor);
	dh->addWidget(dhl);
	dh->addStretch(1);
	dh->addWidget(addBtn);
	root->addLayout(dh);
	connect(addBtn, &QPushButton::clicked, this, [this] { editDestination(QString()); });

	list_ = new QVBoxLayout();
	list_->setSpacing(6);
	root->addLayout(list_);
	empty_ = new QLabel(QStringLiteral("Add Twitch, Kick, TikTok or any RTMP server and go live on all of them at "
					   "once. Your main OBS stream counts as one already."),
			    inner);
	empty_->setObjectName(QStringLiteral("gfxMuted"));
	empty_->setWordWrap(true);
	empty_->setAlignment(Qt::AlignCenter);
	empty_->setMargin(10);
	root->addWidget(empty_);

	allBtn_ = new QPushButton(inner);
	theme::setRole(allBtn_, "primary");
	allBtn_->setCursor(Qt::PointingHandCursor);
	root->addWidget(allBtn_);
	connect(allBtn_, &QPushButton::clicked, this, [this] {
		bool anyLive = false;
		for (const Destination &d : dests_)
			anyLive = anyLive || runners_.contains(d.id);
		if (anyLive)
			stopAll();
		else
			startAll();
	});

	msg_ = new QLabel(inner);
	msg_->setObjectName(QStringLiteral("gfxMsg"));
	msg_->setWordWrap(true);
	msg_->hide();
	root->addWidget(msg_);

	auto *note = new QLabel(
		QStringLiteral(
			"Each extra platform needs its own upload bandwidth. Check each platform's rules on streaming "
			"elsewhere at the same time. TikTok prefers vertical video: use a vertical canvas plugin for it, "
			"or it shows letterboxed."),
		inner);
	note->setObjectName(QStringLiteral("gfxMuted"));
	note->setWordWrap(true);
	root->addWidget(note);
	root->addStretch(1);
}

void GfxMultistreamDock::applyAppearance()
{
	theme::applyTo(this);
	mark_->setPixmap(theme::brandMark(22, devicePixelRatioF()));
}

GfxMultistreamDock::Destination *GfxMultistreamDock::find(const QString &id)
{
	for (Destination &d : dests_)
		if (d.id == id)
			return &d;
	return nullptr;
}

// ── Storage ─────────────────────────────────────────────────────

void GfxMultistreamDock::load()
{
	QFile f(storePath());
	if (!f.exists() || !f.open(QIODevice::ReadOnly))
		return;
	const QByteArray plain = unprotect(f.readAll());
	f.close();
	const QJsonArray arr = QJsonDocument::fromJson(plain).object().value(QStringLiteral("destinations")).toArray();
	for (qsizetype i = 0; i < arr.size() && i < 12; ++i) {
		const QJsonObject o = arr.at(i).toObject();
		Destination d;
		d.id = o.value(QStringLiteral("id")).toString();
		d.name = o.value(QStringLiteral("name")).toString().left(40);
		d.platform = QString::fromLatin1(preset(o.value(QStringLiteral("platform")).toString())->key);
		d.server = o.value(QStringLiteral("server")).toString().left(300);
		d.key = o.value(QStringLiteral("key")).toString().left(300);
		d.withMain = o.value(QStringLiteral("withMain")).toBool(true);
		d.ownEncoder = o.value(QStringLiteral("ownEncoder")).toBool(false);
		d.bitrate = qBound(500, o.value(QStringLiteral("bitrate")).toInt(6000), 50000);
		d.keyFromAccount = o.value(QStringLiteral("keyFromAccount")).toBool(false);
		if (d.id.isEmpty())
			d.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		dests_.push_back(d);
	}
}

void GfxMultistreamDock::save()
{
	const QString path = storePath();
	if (path.isEmpty())
		return;
	QDir().mkpath(QFileInfo(path).absolutePath());
	QJsonArray arr;
	for (const Destination &d : dests_) {
		QJsonObject o;
		o.insert(QStringLiteral("id"), d.id);
		o.insert(QStringLiteral("name"), d.name);
		o.insert(QStringLiteral("platform"), d.platform);
		o.insert(QStringLiteral("server"), d.server);
		o.insert(QStringLiteral("key"), d.key);
		o.insert(QStringLiteral("withMain"), d.withMain);
		o.insert(QStringLiteral("ownEncoder"), d.ownEncoder);
		o.insert(QStringLiteral("bitrate"), d.bitrate);
		o.insert(QStringLiteral("keyFromAccount"), d.keyFromAccount);
		arr.append(o);
	}
	QJsonObject root;
	root.insert(QStringLiteral("destinations"), arr);
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly))
		return;
	f.write(protect(QJsonDocument(root).toJson(QJsonDocument::Compact)));
	if (!f.commit())
		obs_log(LOG_WARNING, "Could not save GoalForgeX multistream destinations");
}

// ── List ────────────────────────────────────────────────────────

void GfxMultistreamDock::renderList()
{
	for (const Row &r : rows_)
		delete r.box;
	rows_.clear();
	for (const Destination &d : dests_) {
		Row row;
		row.box = new QFrame(this);
		row.box->setObjectName(QStringLiteral("gfxStatCard"));
		auto *l = new QHBoxLayout(row.box);
		l->setContentsMargins(8, 6, 6, 6);
		l->setSpacing(8);
		l->addWidget(platformIconLabel(d.platform, row.box));
		auto *texts = new QVBoxLayout();
		texts->setSpacing(1);
		auto *name = new QLabel(QStringLiteral("<b>%1</b>").arg(d.name.toHtmlEscaped()), row.box);
		name->setTextFormat(Qt::RichText);
		row.state = new QLabel(row.box);
		row.state->setTextFormat(Qt::RichText);
		row.stats = new QLabel(row.box);
		row.stats->setObjectName(QStringLiteral("gfxStatSub"));
		texts->addWidget(name);
		texts->addWidget(row.state);
		texts->addWidget(row.stats);
		l->addLayout(texts, 1);
		row.toggle = new QPushButton(row.box);
		row.toggle->setCursor(Qt::PointingHandCursor);
		const QString id = d.id;
		connect(row.toggle, &QPushButton::clicked, this, [this, id] {
			if (runners_.contains(id))
				stopDest(id);
			else
				startDest(id, false);
		});
		auto *more = new QToolButton(row.box);
		more->setText(QStringLiteral("⋯"));
		more->setPopupMode(QToolButton::InstantPopup);
		theme::setRole(more, "ghost");
		auto *menu = new QMenu(more);
		connect(menu->addAction(QStringLiteral("Edit…")), &QAction::triggered, this,
			[this, id] { editDestination(id); });
		connect(menu->addAction(QStringLiteral("Remove")), &QAction::triggered, this, [this, id] {
			if (runners_.contains(id))
				stopDest(id);
			for (qsizetype i = 0; i < dests_.size(); ++i)
				if (dests_.at(i).id == id) {
					dests_.removeAt(i);
					break;
				}
			save();
			QTimer::singleShot(0, this, [this] { renderList(); });
		});
		more->setMenu(menu);
		l->addWidget(row.toggle);
		l->addWidget(more);
		list_->addWidget(row.box);
		rows_.insert(d.id, row);
	}
	empty_->setVisible(dests_.isEmpty());
	allBtn_->setVisible(!dests_.isEmpty());
	renderStatus();
}

void GfxMultistreamDock::renderStatus()
{
	renderMain();
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	int live = 0, totalKbps = 0;
	for (const Destination &d : dests_) {
		auto it = rows_.find(d.id);
		if (it == rows_.end())
			continue;
		Runner *r = runners_.value(d.id, nullptr);
		QString state, stats;
		if (!r) {
			state = QStringLiteral("<span style='color:#6b7280'>● Off</span>");
			stats = d.ownEncoder ? QStringLiteral("Own encoder · %1 kbps").arg(d.bitrate)
					     : QStringLiteral("Same as main stream");
			if (d.withMain)
				stats += QStringLiteral(" · goes live with OBS");
			it->toggle->setText(QStringLiteral("Start"));
			if (it->toggle->property("gfxRole").toString() != QLatin1String("soft"))
				theme::setRole(it->toggle, "soft");
		} else {
			if (r->state == QLatin1String("live")) {
				++live;
				totalKbps += r->kbps;
				state = QStringLiteral("<span style='color:#ff3b4f;font-weight:700'>● LIVE</span> "
						       "<span style='color:#9aa1b0'>%1</span>")
						.arg(fmtClock((now - r->startedMs) / 1000));
			} else if (r->state == QLatin1String("reconnecting")) {
				state = QStringLiteral(
					"<span style='color:#ffb020;font-weight:700'>● Reconnecting…</span>");
			} else {
				state = QStringLiteral("<span style='color:#ffb020'>● Connecting…</span>");
			}
			const int dropped = r->output ? obs_output_get_frames_dropped(r->output) : 0;
			const int total = r->output ? obs_output_get_total_frames(r->output) : 0;
			stats = QStringLiteral("%1 kbps").arg(r->kbps);
			if (total > 0)
				stats += QStringLiteral(" · %1 dropped (%2%)")
						 .arg(dropped)
						 .arg(QString::number(100.0 * dropped / total, 'f',
								      dropped * 1000 < total ? 1 : 0));
			it->toggle->setText(QStringLiteral("Stop"));
			if (it->toggle->property("gfxRole").toString() != QLatin1String("danger"))
				theme::setRole(it->toggle, "danger");
		}
		it->state->setText(state);
		it->stats->setText(stats);
	}
	summary_->setText(dests_.isEmpty() ? QString() : QStringLiteral("%1 of %2 live").arg(live).arg(dests_.size()));
	bool anyRunning = false;
	for (const Destination &d : dests_)
		anyRunning = anyRunning || runners_.contains(d.id);
	allBtn_->setText(anyRunning ? QStringLiteral("■  Stop all destinations")
				    : QStringLiteral("●  Go live on all destinations"));
	if (allBtn_->property("gfxRole").toString() != QLatin1String(anyRunning ? "danger" : "primary"))
		theme::setRole(allBtn_, anyRunning ? "danger" : "primary");
	if (totalKbps > 0) {
		const double mbps = totalKbps / 1000.0;
		bandwidth_->setText(QStringLiteral("⬆ Extra upload right now: <b>%1 Mbps</b>%2")
					    .arg(QString::number(mbps, 'f', 1),
						 mbps > 15
							 ? QStringLiteral(" — that's a lot; watch for dropped frames.")
							 : QString()));
		bandwidth_->show();
	} else {
		bandwidth_->setText(mainLive_ ? QStringLiteral("Your main OBS stream is live.")
					      : QStringLiteral("Your main OBS stream is offline."));
		bandwidth_->setVisible(!dests_.isEmpty());
	}
	// Title & category: Pro extra, needs the main dock connected.
	const bool connected = Auth::instance().connected();
	infoCard_->setEnabled(connected);
	if (!connected) {
		infoNote_->setText(QStringLiteral("Connect GoalForgeX in the GoalForgeX dock to change your title and "
						  "category everywhere at once."));
		infoNote_->show();
	} else if (!infoLoaded_) {
		infoLoaded_ = true;
		loadStreamInfo();
	}
}

void GfxMultistreamDock::setMsg(const QString &text, bool error)
{
	msg_->setText(text);
	theme::setState(msg_, error ? "error" : "");
	msg_->setVisible(!text.isEmpty());
	msgClear_.start(error ? 12000 : 6000);
}

void GfxMultistreamDock::tick()
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	for (Runner *r : runners_) {
		if (!r->output)
			continue;
		const uint64_t bytes = obs_output_get_total_bytes(r->output);
		if (r->lastBytesMs > 0 && now > r->lastBytesMs && bytes >= r->lastBytes)
			r->kbps = static_cast<int>((bytes - r->lastBytes) * 8 /
						   static_cast<uint64_t>(now - r->lastBytesMs));
		r->lastBytes = bytes;
		r->lastBytesMs = now;
	}
	// OBS's own stream: same bitrate calculation from its output.
	if (obs_output_t *mainOut = obs_frontend_get_streaming_output()) {
		const uint64_t bytes = obs_output_active(mainOut) ? obs_output_get_total_bytes(mainOut) : 0;
		if (mainLastMs_ > 0 && now > mainLastMs_ && bytes >= mainLastBytes_)
			mainKbps_ = static_cast<int>((bytes - mainLastBytes_) * 8 /
						     static_cast<uint64_t>(now - mainLastMs_));
		mainLastBytes_ = bytes;
		mainLastMs_ = now;
		obs_output_release(mainOut);
	}
	renderStatus();
	if (runners_.isEmpty() && !mainLive_)
		tick_.stop();
}

void GfxMultistreamDock::renderMain()
{
	const MainStreamInfo m = readMainStream();
	const bool live = obs_frontend_streaming_active();
	if (m.platform != mainPlatform_) {
		mainPlatform_ = m.platform;
		const QImage img = notice::platformIcon(m.platform, 18, devicePixelRatioF());
		if (!img.isNull())
			mainIcon_->setPixmap(QPixmap::fromImage(img));
		else
			mainIcon_->setText(QStringLiteral("📡"));
	}
	const QString label = m.platform == QLatin1String("custom") ? m.service
								    : QString::fromLatin1(preset(m.platform)->name);
	mainName_->setText(m.configured ? QStringLiteral("<b>%1</b> <span style='color:#8b93a3'>· OBS stream</span>")
						  .arg(label.toHtmlEscaped())
					: QStringLiteral("<b>OBS stream</b>"));
	if (live) {
		const qint64 secs = mainStartMs_ > 0 ? (QDateTime::currentMSecsSinceEpoch() - mainStartMs_) / 1000 : 0;
		mainState_->setText(QStringLiteral("<span style='color:#ff3b4f;font-weight:700'>● LIVE</span> "
						   "<span style='color:#9aa1b0'>%1</span>")
					    .arg(mainStartMs_ > 0 ? fmtClock(secs) : QString()));
		QString stats = QStringLiteral("%1 kbps").arg(mainKbps_);
		if (obs_output_t *mainOut = obs_frontend_get_streaming_output()) {
			const int dropped = obs_output_get_frames_dropped(mainOut);
			const int total = obs_output_get_total_frames(mainOut);
			if (total > 0)
				stats += QStringLiteral(" · %1 dropped (%2%)")
						 .arg(dropped)
						 .arg(QString::number(100.0 * dropped / total, 'f',
								      dropped * 1000 < total ? 1 : 0));
			obs_output_release(mainOut);
		}
		mainStats_->setText(stats);
	} else {
		mainState_->setText(QStringLiteral("<span style='color:#6b7280'>● Off</span>"));
		mainStats_->setText(m.configured
					    ? QStringLiteral("Set in OBS → Settings → Stream")
					    : QStringLiteral("No stream key yet: add one in OBS → Settings → Stream."));
	}
	// Key: masked, plus (Pro, Twitch) whether it's the key for the connected Twitch channel.
	if (!m.configured) {
		mainKey_->hide();
	} else {
		if (m.platform == QLatin1String("twitch") && m.key != mainKeyChecked_ && Auth::instance().connected()) {
			mainKeyChecked_ = m.key;
			mainKeyCheck_.clear();
			const QString checking = m.key;
			fetchKeys([this, checking](const QJsonObject &keys, const QString &error) {
				if (checking != mainKeyChecked_ || !error.isEmpty())
					return; // changed meanwhile, or not available (not Pro / older site)
				const QString acct = keys.value(QStringLiteral("twitch"))
							     .toObject()
							     .value(QStringLiteral("key"))
							     .toString();
				if (!acct.isEmpty())
					mainKeyCheck_ = acct == checking ? QStringLiteral("match")
									 : QStringLiteral("mismatch");
				renderStatus();
			});
		}
		QString k = QStringLiteral("Key %1").arg(maskKey(m.key).toHtmlEscaped());
		if (m.platform == QLatin1String("twitch") && mainKeyCheck_ == QLatin1String("match"))
			k += QStringLiteral(" · <span style='color:#34d399'>✓ your connected Twitch channel</span>");
		else if (m.platform == QLatin1String("twitch") && mainKeyCheck_ == QLatin1String("mismatch"))
			k += QStringLiteral(" · <span style='color:#ffb020'>⚠ not the key for your connected Twitch "
					    "channel</span>");
		mainKey_->setText(k);
		mainKey_->show();
	}
	mainBtn_->setText(live ? QStringLiteral("Stop") : QStringLiteral("Start"));
	mainBtn_->setEnabled(m.configured || live);
	const char *role = live ? "danger" : "soft";
	if (mainBtn_->property("gfxRole").toString() != QLatin1String(role))
		theme::setRole(mainBtn_, role);
}

// ── Start / stop ────────────────────────────────────────────────

void GfxMultistreamDock::startDest(const QString &id, bool quiet)
{
	Destination *d = find(id);
	if (!d || runners_.contains(id))
		return;
	// Account-filled keys are refreshed first (Twitch keys can be reset any time).
	if (d->keyFromAccount && Auth::instance().connected()) {
		fetchKeys([this, id, quiet](const QJsonObject &keys, const QString &) {
			Destination *dd = find(id);
			if (!dd)
				return;
			const QJsonObject k = keys.value(dd->platform).toObject();
			const QString key = k.value(QStringLiteral("key")).toString();
			if (!key.isEmpty() && key != dd->key) {
				dd->key = key;
				const QString server = k.value(QStringLiteral("server")).toString();
				if (server.startsWith(QLatin1String("rtmp")))
					dd->server = server;
				save();
			}
			dd->keyFromAccount = false; // start once, without fetching again
			startDest(id, quiet);
			if (Destination *again = find(id))
				again->keyFromAccount = true;
		});
		return;
	}
	if (d->server.trimmed().isEmpty() || d->key.trimmed().isEmpty()) {
		setMsg(QStringLiteral("%1 needs a server address and a stream key — ⋯ → Edit.").arg(d->name), true);
		return;
	}
	// Same key as OBS's own stream: it's already going there, and a second connection would kick the first.
	const MainStreamInfo mainInfo = readMainStream();
	if (mainInfo.configured && d->key.trimmed() == mainInfo.key.trimmed()) {
		setMsg(QStringLiteral(
			       "“%1” uses the same stream key as your main OBS stream, which already goes there.")
			       .arg(d->name),
		       true);
		return;
	}
	obs_encoder_t *video = nullptr;
	obs_encoder_t *audio = nullptr;
	obs_output_t *mainOut = obs_frontend_get_streaming_output();
	obs_encoder_t *mainVideo = mainOut ? obs_output_get_video_encoder(mainOut) : nullptr;
	obs_encoder_t *mainAudio = mainOut ? obs_output_get_audio_encoder(mainOut, 0) : nullptr;
	const bool mainActive = mainOut && obs_output_active(mainOut);

	auto *r = new Runner();
	if (!d->ownEncoder) {
		if (!mainActive || !mainVideo || !mainAudio) {
			obs_output_release(mainOut);
			delete r;
			if (!quiet)
				setMsg(QStringLiteral(
					       "“%1” shares your main stream's encoder, so start your OBS stream "
					       "first — or ⋯ → Edit → Own encoder to run it on its own.")
					       .arg(d->name),
				       true);
			return;
		}
		video = mainVideo;
		audio = mainAudio;
	} else {
		// Own encoder: same encoder type as the main stream when there is one.
		const char *vid = mainVideo ? obs_encoder_get_id(mainVideo) : "obs_x264";
		obs_data_t *vs = obs_data_create();
		obs_data_set_int(vs, "bitrate", d->bitrate);
		obs_data_set_string(vs, "rate_control", "CBR");
		obs_data_set_int(vs, "keyint_sec", 2);
		const QByteArray vname = (QStringLiteral("GoalForgeX multistream video ") + d->id).toUtf8();
		r->ownVideo = obs_video_encoder_create(vid, vname.constData(), vs, nullptr);
		obs_data_release(vs);
		obs_data_t *as = obs_data_create();
		obs_data_set_int(as, "bitrate", 160);
		const QByteArray aname = (QStringLiteral("GoalForgeX multistream audio ") + d->id).toUtf8();
		r->ownAudio = obs_audio_encoder_create("ffmpeg_aac", aname.constData(), as, 0, nullptr);
		obs_data_release(as);
		if (!r->ownVideo || !r->ownAudio) {
			obs_encoder_release(r->ownVideo);
			obs_encoder_release(r->ownAudio);
			obs_output_release(mainOut);
			delete r;
			setMsg(QStringLiteral("Couldn't create an encoder for “%1”.").arg(d->name), true);
			return;
		}
		obs_encoder_set_video(r->ownVideo, obs_get_video());
		obs_encoder_set_audio(r->ownAudio, obs_get_audio());
		video = r->ownVideo;
		audio = r->ownAudio;
	}
	obs_output_release(mainOut);

	obs_data_t *ss = obs_data_create();
	obs_data_set_string(ss, "server", d->server.trimmed().toUtf8().constData());
	obs_data_set_string(ss, "key", d->key.trimmed().toUtf8().constData());
	const QByteArray sname = (QStringLiteral("GoalForgeX multistream service ") + d->id).toUtf8();
	r->service = obs_service_create("rtmp_custom", sname.constData(), ss, nullptr);
	obs_data_release(ss);
	const QByteArray oname = (QStringLiteral("GoalForgeX multistream ") + d->id).toUtf8();
	r->output = obs_output_create("rtmp_output", oname.constData(), nullptr, nullptr);
	if (!r->service || !r->output) {
		obs_service_release(r->service);
		obs_output_release(r->output);
		obs_encoder_release(r->ownVideo);
		obs_encoder_release(r->ownAudio);
		delete r;
		setMsg(QStringLiteral("OBS couldn't create a stream output for “%1”.").arg(d->name), true);
		return;
	}
	obs_output_set_service(r->output, r->service);
	obs_output_set_video_encoder(r->output, video);
	obs_output_set_audio_encoder(r->output, audio, 0);
	obs_output_set_reconnect_settings(r->output, 20, 2);

	r->ctx = new MsSigCtx{QPointer<GfxMultistreamDock>(this), id};
	signal_handler_t *sh = obs_output_get_signal_handler(r->output);
	signal_handler_connect(sh, "start", sigStart, r->ctx);
	signal_handler_connect(sh, "stop", sigStop, r->ctx);
	signal_handler_connect(sh, "reconnect", sigReconnect, r->ctx);
	signal_handler_connect(sh, "reconnect_success", sigReconnected, r->ctx);
	runners_.insert(id, r);

	if (!obs_output_start(r->output)) {
		const char *err = obs_output_get_last_error(r->output);
		const QString why = err && *err ? QString::fromUtf8(err) : QStringLiteral("check the server and key");
		releaseRunner(id, true);
		setMsg(QStringLiteral("“%1” couldn't start: %2").arg(d->name, why), true);
		renderStatus();
		return;
	}
	tick_.start();
	renderStatus();
}

void GfxMultistreamDock::stopDest(const QString &id)
{
	Runner *r = runners_.value(id, nullptr);
	if (!r)
		return;
	if (r->output && obs_output_active(r->output))
		obs_output_stop(r->output); // the "stop" signal releases it
	else
		releaseRunner(id, true);
	renderStatus();
}

void GfxMultistreamDock::releaseRunner(const QString &id, bool force)
{
	Runner *r = runners_.take(id);
	if (!r)
		return;
	if (r->output) {
		signal_handler_t *sh = obs_output_get_signal_handler(r->output);
		signal_handler_disconnect(sh, "start", sigStart, r->ctx);
		signal_handler_disconnect(sh, "stop", sigStop, r->ctx);
		signal_handler_disconnect(sh, "reconnect", sigReconnect, r->ctx);
		signal_handler_disconnect(sh, "reconnect_success", sigReconnected, r->ctx);
		if (force && obs_output_active(r->output))
			obs_output_force_stop(r->output);
		obs_output_release(r->output);
	}
	obs_service_release(r->service);
	obs_encoder_release(r->ownVideo);
	obs_encoder_release(r->ownAudio);
	delete r->ctx;
	delete r;
}

void GfxMultistreamDock::onOutputSignal(const QString &id, const QString &what, int code)
{
	Runner *r = runners_.value(id, nullptr);
	Destination *d = find(id);
	if (!r)
		return;
	if (what == QLatin1String("start")) {
		r->state = QStringLiteral("live");
		r->startedMs = QDateTime::currentMSecsSinceEpoch();
	} else if (what == QLatin1String("reconnect")) {
		r->state = QStringLiteral("reconnecting");
	} else if (what == QLatin1String("reconnect_success")) {
		r->state = QStringLiteral("live");
	} else if (what == QLatin1String("stop")) {
		releaseRunner(id, false);
		const QString why = stopReason(code);
		if (!why.isEmpty() && d)
			setMsg(QStringLiteral("“%1” stopped: %2").arg(d->name, why), true);
	}
	renderStatus();
}

void GfxMultistreamDock::startAll()
{
	bool needMain = false;
	for (const Destination &d : dests_) {
		if (!d.ownEncoder && !mainLive_) {
			needMain = true;
			continue;
		}
		startDest(d.id, true);
	}
	if (needMain)
		setMsg(QStringLiteral("Destinations that share your main stream's encoder start when your OBS stream "
				      "does."));
}

void GfxMultistreamDock::stopAll()
{
	const QList<QString> ids = runners_.keys();
	for (const QString &id : ids)
		stopDest(id);
}

void GfxMultistreamDock::mainStreamStarted()
{
	mainLive_ = true;
	mainStartMs_ = QDateTime::currentMSecsSinceEpoch();
	mainLastBytes_ = 0;
	mainLastMs_ = 0;
	mainKbps_ = 0;
	tick_.start();
	for (const Destination &d : dests_)
		if (d.withMain)
			startDest(d.id, true);
	renderStatus();
}

void GfxMultistreamDock::mainStreamStopping()
{
	mainLive_ = false;
	// Shared-encoder outputs can't outlive the main stream; "with main" ones stop with it too.
	for (const Destination &d : dests_)
		if ((!d.ownEncoder || d.withMain) && runners_.contains(d.id))
			stopDest(d.id);
	renderStatus();
}

void GfxMultistreamDock::shutdown()
{
	tick_.stop();
	mainRefresh_.stop();
	const QList<QString> ids = runners_.keys();
	for (const QString &id : ids)
		releaseRunner(id, true);
}

// ── Add / edit ──────────────────────────────────────────────────

void GfxMultistreamDock::editDestination(const QString &id)
{
	Destination *existing = find(id);
	Destination d = existing ? *existing : Destination();
	if (!existing) {
		d.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		d.platform = QStringLiteral("kick");
	}
	QDialog dlg(this);
	dlg.setWindowTitle(existing ? QStringLiteral("Edit destination") : QStringLiteral("Add a destination"));
	dlg.setMinimumWidth(420);
	auto *root = new QVBoxLayout(&dlg);
	root->setContentsMargins(14, 12, 14, 12);
	auto *form = new QFormLayout();
	form->setLabelAlignment(Qt::AlignLeft);
	auto *platform = new QComboBox(&dlg);
	for (const PlatformPreset &p : kPlatforms)
		platform->addItem(QString::fromLatin1(p.name), QString::fromLatin1(p.key));
	platform->setCurrentIndex(qMax(0, platform->findData(d.platform)));
	auto *name = new QLineEdit(d.name, &dlg);
	name->setMaxLength(40);
	auto *server = new QLineEdit(d.server, &dlg);
	server->setPlaceholderText(QStringLiteral("rtmp:// or rtmps:// server address"));
	auto *keyRow = new QHBoxLayout();
	auto *key = new QLineEdit(d.key, &dlg);
	key->setEchoMode(QLineEdit::Password);
	key->setPlaceholderText(QStringLiteral("Stream key"));
	auto *show = new QPushButton(QStringLiteral("Show"), &dlg);
	theme::setRole(show, "ghost");
	show->setCheckable(true);
	keyRow->addWidget(key, 1);
	keyRow->addWidget(show);
	connect(show, &QPushButton::toggled, &dlg, [key, show](bool on) {
		key->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
		show->setText(on ? QStringLiteral("Hide") : QStringLiteral("Show"));
	});
	auto *fill = new QPushButton(QStringLiteral("Fill in from my GoalForgeX account"), &dlg);
	theme::setRole(fill, "soft");
	auto *hint = new QLabel(&dlg);
	hint->setObjectName(QStringLiteral("gfxMuted"));
	hint->setWordWrap(true);
	auto *withMain = new QCheckBox(QStringLiteral("Go live and stop together with my OBS stream"), &dlg);
	withMain->setChecked(d.withMain);
	auto *encoder = new QComboBox(&dlg);
	encoder->addItem(QStringLiteral("Same as main stream — no extra CPU/GPU (recommended)"), false);
	encoder->addItem(QStringLiteral("Own encoder — runs on its own, uses more CPU/GPU"), true);
	encoder->setCurrentIndex(d.ownEncoder ? 1 : 0);
	auto *bitrate = new QSpinBox(&dlg);
	bitrate->setRange(500, 50000);
	bitrate->setSingleStep(500);
	bitrate->setSuffix(QStringLiteral(" kbps"));
	bitrate->setValue(d.bitrate);
	form->addRow(QStringLiteral("Platform"), platform);
	form->addRow(QStringLiteral("Name"), name);
	form->addRow(QStringLiteral("Server"), server);
	form->addRow(QStringLiteral("Stream key"), keyRow);
	form->addRow(QString(), fill);
	form->addRow(QString(), hint);
	form->addRow(QString(), withMain);
	form->addRow(QStringLiteral("Encoding"), encoder);
	form->addRow(QStringLiteral("Bitrate"), bitrate);
	root->addLayout(form);
	bool fromAccount = d.keyFromAccount;
	// The platform OBS itself already streams to (Settings → Stream), to warn about doubling it.
	const MainStreamInfo mainInfo = readMainStream();
	const QString mainPlat = mainInfo.configured ? mainInfo.platform : QString();

	const auto refresh = [platform, name, server, fill, hint, bitrate, encoder, form,
			      mainPlat](bool platformChanged) {
		const PlatformPreset *p = preset(platform->currentData().toString());
		const QString k = QString::fromLatin1(p->key);
		if (platformChanged) {
			// Swap the default server / name when they're still the previous defaults.
			bool serverIsPreset = server->text().trimmed().isEmpty();
			bool nameIsPreset = name->text().trimmed().isEmpty();
			for (const PlatformPreset &q : kPlatforms) {
				serverIsPreset = serverIsPreset || server->text() == QLatin1String(q.server);
				nameIsPreset = nameIsPreset || name->text() == QLatin1String(q.name);
			}
			if (serverIsPreset)
				server->setText(QString::fromLatin1(p->server));
			if (nameIsPreset)
				name->setText(QString::fromLatin1(p->name));
		}
		fill->setVisible(k == QLatin1String("twitch") || k == QLatin1String("kick"));
		fill->setEnabled(Auth::instance().connected());
		hint->setText(QString::fromUtf8(p->hint));
		if (!mainPlat.isEmpty() && k == mainPlat && k != QLatin1String("custom"))
			hint->setText(
				QStringLiteral(
					"⚠ %1 is already your main OBS stream (OBS → Settings → Stream). Adding it "
					"here too sends it a second stream, so only do this for a second %1 channel.")
					.arg(QString::fromLatin1(p->name)));
		const bool own = encoder->currentData().toBool();
		bitrate->setEnabled(own);
		if (QWidget *lbl = form->labelForField(bitrate))
			lbl->setEnabled(own);
	};
	refresh(!existing);
	connect(platform, &QComboBox::currentIndexChanged, &dlg, [refresh](int) { refresh(true); });
	connect(encoder, &QComboBox::currentIndexChanged, &dlg, [refresh](int) { refresh(false); });
	connect(key, &QLineEdit::textEdited, &dlg, [&fromAccount](const QString &) { fromAccount = false; });
	connect(fill, &QPushButton::clicked, &dlg, [this, platform, server, key, hint, fill, &fromAccount] {
		fill->setEnabled(false);
		hint->setText(QStringLiteral("Asking GoalForgeX…"));
		QPointer<QLineEdit> keyP(key), serverP(server);
		QPointer<QLabel> hintP(hint);
		QPointer<QPushButton> fillP(fill);
		const QString plat = platform->currentData().toString();
		bool *fromAccountP = &fromAccount;
		fetchKeys([plat, keyP, serverP, hintP, fillP, fromAccountP](const QJsonObject &keys,
									    const QString &error) {
			if (!keyP || !hintP)
				return;
			if (fillP)
				fillP->setEnabled(true);
			if (!error.isEmpty()) {
				hintP->setText(error);
				return;
			}
			const QJsonObject k = keys.value(plat).toObject();
			const QString e = k.value(QStringLiteral("error")).toString();
			const QString srv = k.value(QStringLiteral("server")).toString();
			if (serverP && srv.startsWith(QLatin1String("rtmp")))
				serverP->setText(srv);
			if (!k.value(QStringLiteral("key")).toString().isEmpty()) {
				keyP->setText(k.value(QStringLiteral("key")).toString());
				*fromAccountP = true;
				hintP->setText(
					QStringLiteral("✓ Filled in from your connected account. It's refreshed every "
						       "time this destination goes live."));
			} else if (e == QLatin1String("needs_reconnect")) {
				hintP->setText(
					QStringLiteral("Your login needs one more permission for this: click "
						       "Reconnect in the Events dock (or sign in to GoalForgeX with "
						       "this platform again), then try once more."));
			} else if (e == QLatin1String("manual")) {
				hintP->setText(
					QStringLiteral("Server filled in. Kick doesn't share stream keys with apps "
						       "yet, so paste the key from Kick → Creator Dashboard → "
						       "Settings → Stream URL & Key."));
			} else if (e == QLatin1String("not_connected")) {
				hintP->setText(
					QStringLiteral("That platform isn't connected on goalforgex.com → Account."));
			} else {
				hintP->setText(QStringLiteral("Couldn't get the key right now — paste it instead."));
			}
		});
	});

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
	if (QPushButton *ok = buttons->button(QDialogButtonBox::Save))
		theme::setRole(ok, "primary");
	root->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	if (dlg.exec() != QDialog::Accepted)
		return;

	d.platform = platform->currentData().toString();
	d.name = name->text().trimmed().isEmpty() ? QString::fromLatin1(preset(d.platform)->name)
						  : name->text().trimmed();
	d.server = server->text().trimmed();
	d.key = key->text().trimmed();
	d.withMain = withMain->isChecked();
	d.ownEncoder = encoder->currentData().toBool();
	d.bitrate = bitrate->value();
	d.keyFromAccount = fromAccount;
	if (Destination *again = find(id); again && !id.isEmpty()) {
		*again = d;
	} else {
		dests_.push_back(d);
	}
	save();
	QTimer::singleShot(0, this, [this] { renderList(); });
	if (runners_.contains(d.id))
		setMsg(QStringLiteral("Saved. Changes apply the next time “%1” goes live.").arg(d.name));
}

// ── GoalForgeX extras (Pro) ─────────────────────────────────────

void GfxMultistreamDock::fetchKeys(std::function<void(const QJsonObject &, const QString &)> done)
{
	if (!Auth::instance().connected()) {
		done(QJsonObject(), QStringLiteral("Connect GoalForgeX in the GoalForgeX dock first."));
		return;
	}
	runAsync(
		this, [] { return apiGet(QStringLiteral("/api/obs/multistream/keys")); },
		[done](const ApiResult &r) {
			if (r.http.status == 403 && r.http.errorCode() == QLatin1String("pro_required")) {
				done(QJsonObject(),
				     QStringLiteral(
					     "Automatic stream keys are part of GoalForgeX Pro — paste the key instead."));
				return;
			}
			if (r.http.status == 404) {
				done(QJsonObject(),
				     QStringLiteral("Your GoalForgeX site doesn't support this yet — paste the key."));
				return;
			}
			if (!r.ok()) {
				done(QJsonObject(), describe(r));
				return;
			}
			done(r.http.json(), QString());
		});
}

void GfxMultistreamDock::loadStreamInfo()
{
	runAsync(
		this, [] { return apiGet(QStringLiteral("/api/obs/stream-info")); },
		[this](const ApiResult &r) {
			if (r.http.status == 403 && r.http.errorCode() == QLatin1String("pro_required")) {
				infoNote_->setText(
					QStringLiteral("Changing your title and category everywhere at once is part "
						       "of GoalForgeX Pro."));
				infoNote_->show();
				title_->setEnabled(false);
				category_->setEnabled(false);
				pushBtn_->setEnabled(false);
				return;
			}
			if (!r.ok())
				return; // older site or offline — the card still works when it can
			const QJsonObject o = r.http.json();
			const QJsonObject tw = o.value(QStringLiteral("twitch")).toObject();
			const QJsonObject kk = o.value(QStringLiteral("kick")).toObject();
			toTwitch_->setVisible(o.contains(QStringLiteral("twitch")));
			toKick_->setVisible(o.contains(QStringLiteral("kick")));
			const QJsonObject first = !tw.isEmpty() && !tw.contains(QStringLiteral("error")) ? tw : kk;
			if (title_->text().isEmpty())
				title_->setText(first.value(QStringLiteral("title")).toString());
			if (category_->text().isEmpty())
				category_->setText(first.value(QStringLiteral("category")).toString());
		});
}

void GfxMultistreamDock::pushStreamInfo()
{
	QJsonArray platforms;
	if (toTwitch_->isVisible() && toTwitch_->isChecked())
		platforms.append(QStringLiteral("twitch"));
	if (toKick_->isVisible() && toKick_->isChecked())
		platforms.append(QStringLiteral("kick"));
	if (platforms.isEmpty()) {
		setMsg(QStringLiteral("Tick Twitch and/or Kick first."), true);
		return;
	}
	QJsonObject body;
	body.insert(QStringLiteral("title"), title_->text().trimmed());
	body.insert(QStringLiteral("category"), category_->text().trimmed());
	body.insert(QStringLiteral("platforms"), platforms);
	pushBtn_->setEnabled(false);
	runAsync(
		this, [body] { return apiPost(QStringLiteral("/api/obs/stream-info"), body); },
		[this](const ApiResult &r) {
			pushBtn_->setEnabled(true);
			if (!r.ok()) {
				setMsg(r.http.serverMessage().isEmpty() ? describe(r) : r.http.serverMessage(), true);
				return;
			}
			const QJsonObject res = r.http.json().value(QStringLiteral("results")).toObject();
			QStringList done, failed;
			bool reconnect = false;
			for (auto it = res.begin(); it != res.end(); ++it) {
				const QJsonObject v = it.value().toObject();
				const QString name = it.key() == QLatin1String("twitch") ? QStringLiteral("Twitch")
											 : QStringLiteral("Kick");
				if (v.value(QStringLiteral("ok")).toBool()) {
					done << name;
					const QString cat = v.value(QStringLiteral("category")).toString();
					if (!cat.isEmpty() && it.key() == QLatin1String("twitch"))
						category_->setText(cat);
				} else {
					const QString e = v.value(QStringLiteral("error")).toString();
					reconnect = reconnect || e == QLatin1String("needs_reconnect");
					failed << name + (e == QLatin1String("no_category")
								  ? QStringLiteral(" (no category by that name)")
							  : e == QLatin1String("needs_reconnect")
								  ? QStringLiteral(" (reconnect needed)")
								  : QString());
				}
			}
			if (failed.isEmpty())
				setMsg(QStringLiteral("✓ Updated on %1.").arg(done.join(QStringLiteral(" and "))));
			else
				setMsg((done.isEmpty() ? QString()
						       : QStringLiteral("Updated on %1. ")
								 .arg(done.join(QStringLiteral(" and ")))) +
					       QStringLiteral("Couldn't update %1.")
						       .arg(failed.join(QStringLiteral(", "))),
				       true);
			if (reconnect)
				reconnectTwitch();
		});
}

void GfxMultistreamDock::reconnectTwitch()
{
	runAsync(
		this, [] { return apiPost(QStringLiteral("/api/obs/twitch/reconnect")); },
		[](const ApiResult &r) {
			const QString url = r.ok() ? r.http.json().value(QStringLiteral("url")).toString() : QString();
			if (url.startsWith(QLatin1String("https://id.twitch.tv/")))
				QDesktopServices::openUrl(QUrl(url));
		});
}
