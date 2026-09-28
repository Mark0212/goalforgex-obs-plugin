/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-dock.hpp"
#include "gfx-auth.hpp"
#include "gfx-http.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QSysInfo>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <thread>

using namespace gfx;

namespace {

// Result of a call that needs an access token.
struct ApiResult {
	TokenStatus token = TokenStatus::Ok;
	HttpResult http;
	QString error;
};

// Runs `call` with a fresh access token; on a 401 it refreshes once and
// retries, and a revoked link is reported as NeedsReconnect.
ApiResult authedCall(const std::function<HttpResult(const QString &)> &call)
{
	ApiResult r;
	QString tok, err;
	r.token = Auth::instance().accessToken(&tok, &err);
	if (r.token != TokenStatus::Ok) {
		r.error = err;
		return r;
	}
	r.http = call(tok);
	if (r.http.status == 401) {
		if (r.http.errorCode() == QLatin1String("link_revoked") ||
		    r.http.errorCode() == QLatin1String("account_missing")) {
			r.token = TokenStatus::NeedsReconnect;
			r.error = r.http.serverMessage();
			return r;
		}
		Auth::instance().invalidateAccess();
		r.token = Auth::instance().accessToken(&tok, &err);
		if (r.token != TokenStatus::Ok) {
			r.error = err;
			return r;
		}
		r.http = call(tok);
		if (r.http.status == 401) {
			r.token = TokenStatus::NeedsReconnect;
			r.error = r.http.serverMessage();
		}
	}
	return r;
}

// Plain-English version of a failed call.
QString describe(const HttpResult &h)
{
	if (h.status == 0)
		return QStringLiteral("Can't reach GoalForgeX — check your internet connection.");
	if (h.status == 429)
		return QStringLiteral("Too many requests to GoalForgeX — waiting a moment before trying again.");
	if (h.status >= 500)
		return QStringLiteral("GoalForgeX is having a problem right now — retrying shortly.");
	const QString m = h.serverMessage();
	return m.isEmpty() ? QStringLiteral("GoalForgeX returned an error (HTTP %1).").arg(h.status) : m;
}

// Widget URLs must point at GoalForgeX (or the dev server set via
// GOALFORGEX_BASE_URL) — never load an arbitrary page into a Browser Source.
// Host-based, so goalforgex.com vs www.goalforgex.com can't silently drop
// every widget.
bool trustedUrl(const QString &s)
{
	const QUrl u(s);
	const QUrl base(baseUrl());
	if (!u.isValid() || u.host().isEmpty())
		return false;
	if (u.scheme() == base.scheme() && u.host() == base.host())
		return true;
	const QString h = u.host().toLower();
	return u.scheme() == QLatin1String("https") &&
	       (h == QLatin1String("goalforgex.com") || h.endsWith(QLatin1String(".goalforgex.com")));
}

enum PlaceIdx { PlaceHeader = 0 };
const QList<QPair<QString, Place>> kPlaces = {
	{QStringLiteral("Top left"), Place::TopLeft},
	{QStringLiteral("Top center"), Place::TopCenter},
	{QStringLiteral("Top right"), Place::TopRight},
	{QStringLiteral("Center"), Place::Center},
	{QStringLiteral("Bottom left"), Place::BottomLeft},
	{QStringLiteral("Bottom center"), Place::BottomCenter},
	{QStringLiteral("Bottom right"), Place::BottomRight},
};

} // namespace

template<class Work, class Done> void GfxDock::runAsync(Work work, Done done)
{
	QPointer<GfxDock> self(this);
	std::thread([self, work, done]() mutable {
		auto result = work();
		QMetaObject::invokeMethod(
			qApp,
			[self, done, result]() mutable {
				if (self && !self->shuttingDown_)
					done(result);
			},
			Qt::QueuedConnection);
	}).detach();
}

GfxDock::GfxDock(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(280);
	buildUi();

	statusTimer_.setInterval(2000);
	connect(&statusTimer_, &QTimer::timeout, this, [this] {
		if (isVisible())
			refreshStatuses();
	});
	catalogueTimer_.setSingleShot(true);
	connect(&catalogueTimer_, &QTimer::timeout, this, [this] { loadCatalogue(true); });
	pollTimer_.setSingleShot(true);
	connect(&pollTimer_, &QTimer::timeout, this, [this] { pollOnce(); });

	Auth::instance().load();
	if (Auth::instance().connected()) {
		setState(State::Connected, QStringLiteral("Loading your widgets…"));
	} else {
		setState(State::Disconnected);
	}
}

GfxDock::~GfxDock()
{
	shuttingDown_ = true;
}

void GfxDock::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(8);

	// ── Account row ───────────────────────────────────────────────
	statusLbl_ = new QLabel(this);
	statusLbl_->setWordWrap(true);
	statusLbl_->setTextFormat(Qt::RichText);
	root->addWidget(statusLbl_);

	auto *acctRow = new QHBoxLayout();
	connectBtn_ = new QPushButton(QStringLiteral("Connect GoalForgeX"), this);
	disconnectBtn_ = new QPushButton(QStringLiteral("Disconnect"), this);
	manageBtn_ = new QPushButton(QStringLiteral("Manage…"), this);
	manageBtn_->setToolTip(QStringLiteral("Open goalforgex.com/obs — see and remove connected devices"));
	acctRow->addWidget(connectBtn_);
	acctRow->addWidget(disconnectBtn_);
	acctRow->addStretch(1);
	acctRow->addWidget(manageBtn_);
	root->addLayout(acctRow);
	connect(connectBtn_, &QPushButton::clicked, this, [this] { startConnect(); });
	connect(disconnectBtn_, &QPushButton::clicked, this, [this] { disconnectAccount(); });
	connect(manageBtn_, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/obs"))); });

	// ── Linking panel (device code) ──────────────────────────────
	linkPanel_ = new QFrame(this);
	auto *lp = new QVBoxLayout(linkPanel_);
	lp->setContentsMargins(0, 4, 0, 4);
	auto *lpTitle = new QLabel(QStringLiteral("Approve this OBS in your browser. Check the code matches:"), linkPanel_);
	lpTitle->setWordWrap(true);
	codeLbl_ = new QLabel(linkPanel_);
	QFont f = codeLbl_->font();
	f.setPointSize(f.pointSize() + 8);
	f.setBold(true);
	f.setFamily(QStringLiteral("Consolas"));
	f.setStyleHint(QFont::Monospace);
	codeLbl_->setFont(f);
	codeLbl_->setAlignment(Qt::AlignCenter);
	codeLbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	linkHint_ = new QLabel(linkPanel_);
	linkHint_->setWordWrap(true);
	linkHint_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
	linkHint_->setOpenExternalLinks(true);
	auto *lpRow = new QHBoxLayout();
	auto *openBtn = new QPushButton(QStringLiteral("Open browser again"), linkPanel_);
	auto *copyBtn = new QPushButton(QStringLiteral("Copy code"), linkPanel_);
	auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), linkPanel_);
	lpRow->addWidget(openBtn);
	lpRow->addWidget(copyBtn);
	lpRow->addStretch(1);
	lpRow->addWidget(cancelBtn);
	lp->addWidget(lpTitle);
	lp->addWidget(codeLbl_);
	lp->addWidget(linkHint_);
	lp->addLayout(lpRow);
	root->addWidget(linkPanel_);
	connect(openBtn, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(verifyUrl_)); });
	connect(copyBtn, &QPushButton::clicked, this, [this] {
		QGuiApplication::clipboard()->setText(userCode_);
		showMessage(QStringLiteral("Code copied."));
	});
	connect(cancelBtn, &QPushButton::clicked, this, [this] { cancelLink(); });

	// ── Widgets panel ─────────────────────────────────────────────
	mainPanel_ = new QWidget(this);
	auto *mp = new QVBoxLayout(mainPanel_);
	mp->setContentsMargins(0, 0, 0, 0);
	mp->setSpacing(6);

	auto *sceneRow = new QHBoxLayout();
	sceneRow->addWidget(new QLabel(QStringLiteral("Scene"), mainPanel_));
	sceneCombo_ = new QComboBox(mainPanel_);
	sceneCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	refreshBtn_ = new QToolButton(mainPanel_);
	refreshBtn_->setText(QStringLiteral("↻"));
	refreshBtn_->setToolTip(QStringLiteral("Reload your widget list from GoalForgeX"));
	sceneRow->addWidget(sceneCombo_, 1);
	sceneRow->addWidget(refreshBtn_);
	mp->addLayout(sceneRow);
	connect(sceneCombo_, &QComboBox::currentIndexChanged, this, [this](int) { refreshStatuses(); });
	connect(refreshBtn_, &QToolButton::clicked, this, [this] { loadCatalogue(false); });

	list_ = new QListWidget(mainPanel_);
	list_->setSelectionMode(QAbstractItemView::SingleSelection);
	list_->setMinimumHeight(160);
	mp->addWidget(list_, 1);
	connect(list_, &QListWidget::currentRowChanged, this, [this](int) { updateControls(); });
	connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { addSelected(); });

	auto *grid = new QGridLayout();
	grid->setHorizontalSpacing(6);
	grid->setVerticalSpacing(6);
	addBtn_ = new QPushButton(QStringLiteral("Add to scene"), mainPanel_);
	toggleBtn_ = new QPushButton(QStringLiteral("Hide"), mainPanel_);
	removeBtn_ = new QPushButton(QStringLiteral("Remove from scene"), mainPanel_);
	grid->addWidget(addBtn_, 0, 0);
	grid->addWidget(toggleBtn_, 0, 1);
	grid->addWidget(removeBtn_, 0, 2);

	placeCombo_ = new QComboBox(mainPanel_);
	placeCombo_->addItem(QStringLiteral("Move to…"));
	for (const auto &p : kPlaces)
		placeCombo_->addItem(p.first);
	scaleSpin_ = new QSpinBox(mainPanel_);
	scaleSpin_->setRange(10, 400);
	scaleSpin_->setSuffix(QStringLiteral(" %"));
	scaleSpin_->setToolTip(QStringLiteral("Size in the scene (100% = the widget's native size)"));
	scaleSpin_->setKeyboardTracking(false);
	fitBtn_ = new QPushButton(QStringLiteral("Fit canvas"), mainPanel_);
	fitBtn_->setToolTip(QStringLiteral("Stretch to fill the whole canvas, keeping proportions (for Alerts and Chat Box)"));
	grid->addWidget(placeCombo_, 1, 0);
	grid->addWidget(scaleSpin_, 1, 1);
	grid->addWidget(fitBtn_, 1, 2);

	reloadBtn_ = new QPushButton(QStringLiteral("Reload"), mainPanel_);
	reloadBtn_->setToolTip(QStringLiteral("Refresh this widget in OBS"));
	propsBtn_ = new QPushButton(QStringLiteral("Properties"), mainPanel_);
	configureBtn_ = new QPushButton(QStringLiteral("Customize…"), mainPanel_);
	configureBtn_->setToolTip(QStringLiteral("Change this widget's look and settings on goalforgex.com"));
	grid->addWidget(reloadBtn_, 2, 0);
	grid->addWidget(propsBtn_, 2, 1);
	grid->addWidget(configureBtn_, 2, 2);
	mp->addLayout(grid);

	connect(addBtn_, &QPushButton::clicked, this, [this] { addSelected(); });
	connect(toggleBtn_, &QPushButton::clicked, this, [this] { toggleSelected(); });
	connect(removeBtn_, &QPushButton::clicked, this, [this] { removeSelected(); });
	connect(placeCombo_, &QComboBox::activated, this, [this](int i) { placeSelected(i); });
	connect(scaleSpin_, &QSpinBox::valueChanged, this, [this](int) { scaleSelected(); });
	connect(fitBtn_, &QPushButton::clicked, this, [this] { fitSelected(); });
	connect(reloadBtn_, &QPushButton::clicked, this, [this] { reloadSelected(); });
	connect(propsBtn_, &QPushButton::clicked, this, [this] { propertiesSelected(); });
	connect(configureBtn_, &QPushButton::clicked, this, [this] { configureSelected(); });

	root->addWidget(mainPanel_, 1);

	msgLbl_ = new QLabel(this);
	msgLbl_->setWordWrap(true);
	msgLbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	root->addWidget(msgLbl_);
}

// ── State + messages ─────────────────────────────────────────────

void GfxDock::setState(State s, const QString &message, bool isError)
{
	state_ = s;
	const Credentials c = Auth::instance().credentials();
	const QString who = (c.displayName.isEmpty() ? c.username : c.displayName).toHtmlEscaped();
	switch (s) {
	case State::Disconnected:
		statusLbl_->setText(QStringLiteral("<b>Not connected.</b> Connect your GoalForgeX account to add your widgets to OBS."));
		break;
	case State::Linking:
		statusLbl_->setText(QStringLiteral("<b>Connecting…</b>"));
		break;
	case State::Connected:
		statusLbl_->setText(QStringLiteral("Connected as <b>%1</b>").arg(who));
		break;
	case State::NeedsReconnect:
		statusLbl_->setText(QStringLiteral("<b>⚠ Disconnected from GoalForgeX.</b> Widgets already in your scenes that this OBS added won't update until you connect again."));
		break;
	}
	connectBtn_->setVisible(s == State::Disconnected || s == State::NeedsReconnect);
	connectBtn_->setText(s == State::NeedsReconnect ? QStringLiteral("Reconnect") : QStringLiteral("Connect GoalForgeX"));
	disconnectBtn_->setVisible(s == State::Connected || s == State::NeedsReconnect);
	linkPanel_->setVisible(s == State::Linking);
	// Existing sources can still be shown/hidden/moved while reconnecting.
	mainPanel_->setVisible((s == State::Connected || s == State::NeedsReconnect) && !widgets_.isEmpty());
	if (s == State::Connected || s == State::NeedsReconnect)
		statusTimer_.start();
	else
		statusTimer_.stop();
	if (!message.isNull())
		showMessage(message, isError);
	updateControls();
}

void GfxDock::showMessage(const QString &text, bool isError)
{
	msgLbl_->setStyleSheet(isError ? QStringLiteral("color:#f87171;") : QString());
	msgLbl_->setText(text);
}

// ── Linking ──────────────────────────────────────────────────────

void GfxDock::startConnect()
{
	pollTimer_.stop();
	const int gen = ++linkGeneration_;
	setState(State::Linking, QStringLiteral("Getting a code from GoalForgeX…"));
	codeLbl_->setText(QStringLiteral("…"));
	linkHint_->clear();

	QJsonObject body;
	body.insert(QStringLiteral("device_name"), QSysInfo::machineHostName());
	body.insert(QStringLiteral("client_version"), QString::fromUtf8(PLUGIN_VERSION));
	body.insert(QStringLiteral("obs_version"), QString::fromUtf8(obs_get_version_string()));

	runAsync([body] { return httpPostJson(baseUrl() + QStringLiteral("/api/obs/device"), body); },
		 [this, gen](const HttpResult &r) {
			 if (gen != linkGeneration_ || state_ != State::Linking)
				 return;
			 if (!r.ok()) {
				 setState(State::Disconnected, describe(r), true);
				 return;
			 }
			 const QJsonObject o = r.json();
			 deviceCode_ = o.value(QStringLiteral("device_code")).toString();
			 userCode_ = o.value(QStringLiteral("user_code")).toString();
			 verifyUrl_ = o.value(QStringLiteral("verification_uri_complete")).toString();
			 const QString plainUrl = o.value(QStringLiteral("verification_uri")).toString();
			 pollIntervalS_ = qMax(1, o.value(QStringLiteral("interval")).toInt(5));
			 linkExpiresMs_ = QDateTime::currentMSecsSinceEpoch() +
					  static_cast<qint64>(o.value(QStringLiteral("expires_in")).toInt(600)) * 1000;
			 if (deviceCode_.isEmpty() || userCode_.isEmpty() || verifyUrl_.isEmpty()) {
				 setState(State::Disconnected, QStringLiteral("GoalForgeX sent an unexpected response — please try again."), true);
				 return;
			 }
			 codeLbl_->setText(userCode_);
			 linkHint_->setText(QStringLiteral("If your browser didn't open, go to <a href=\"%1\">%2</a> and enter the code above. It expires in 10 minutes.")
						    .arg(verifyUrl_.toHtmlEscaped(), plainUrl.toHtmlEscaped()));
			 if (!QDesktopServices::openUrl(QUrl(verifyUrl_)))
				 showMessage(QStringLiteral("Couldn't open your browser automatically — use the link above."));
			 else
				 showMessage(QStringLiteral("Waiting for you to approve in the browser…"));
			 schedulePoll();
		 });
}

void GfxDock::schedulePoll()
{
	pollTimer_.start(pollIntervalS_ * 1000);
}

void GfxDock::pollOnce()
{
	if (state_ != State::Linking || deviceCode_.isEmpty())
		return;
	if (QDateTime::currentMSecsSinceEpoch() > linkExpiresMs_) {
		setState(State::Disconnected, QStringLiteral("The code expired before it was approved. Click Connect to get a new one."), true);
		return;
	}
	const int gen = linkGeneration_;
	const QString dc = deviceCode_;
	runAsync(
		[dc] {
			QJsonObject body;
			body.insert(QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
			body.insert(QStringLiteral("device_code"), dc);
			return httpPostJson(baseUrl() + QStringLiteral("/api/obs/token"), body);
		},
		[this, gen](const HttpResult &r) {
			if (gen != linkGeneration_ || state_ != State::Linking)
				return;
			if (r.ok()) {
				QString err;
				if (!Auth::instance().acceptLink(r.json(), &err)) {
					setState(State::Disconnected, err, true);
					return;
				}
				deviceCode_.clear();
				netBackoffS_ = 5;
				setState(State::Connected, QStringLiteral("Connected! Loading your widgets…"));
				loadCatalogue(false);
				return;
			}
			const QString code = r.errorCode();
			if (code == QLatin1String("authorization_pending")) {
				schedulePoll();
			} else if (code == QLatin1String("slow_down")) {
				pollIntervalS_ = qMax(pollIntervalS_ + 5, r.json().value(QStringLiteral("interval")).toInt(0));
				schedulePoll();
			} else if (code == QLatin1String("access_denied")) {
				setState(State::Disconnected,
					 r.serverMessage().isEmpty() ? QStringLiteral("The connection was declined in the browser.") : r.serverMessage(),
					 true);
			} else if (code == QLatin1String("expired_token")) {
				setState(State::Disconnected, QStringLiteral("The code expired before it was approved. Click Connect to get a new one."), true);
			} else {
				// Network blip or server hiccup — keep waiting, the code is still good.
				showMessage(describe(r) + QStringLiteral(" Still waiting for approval…"), true);
				schedulePoll();
			}
		});
}

void GfxDock::cancelLink()
{
	++linkGeneration_;
	pollTimer_.stop();
	deviceCode_.clear();
	setState(Auth::instance().connected() ? State::Connected : State::Disconnected, QStringLiteral("Cancelled."));
}

void GfxDock::disconnectAccount()
{
	const auto ans = QMessageBox::question(
		this, QStringLiteral("Disconnect GoalForgeX"),
		QStringLiteral("Disconnect this OBS from your GoalForgeX account?\n\nWidgets this OBS added stay in your scenes but "
			       "stop showing until you connect again."));
	if (ans != QMessageBox::Yes)
		return;
	catalogueTimer_.stop();
	const bool wasConnected = state_ == State::Connected;
	runAsync(
		[wasConnected] {
			if (!wasConnected)
				return ApiResult();
			return authedCall([](const QString &tok) {
				return httpPostJson(baseUrl() + QStringLiteral("/api/obs/disconnect"), QJsonObject(), tok);
			});
		},
		[this, wasConnected](const ApiResult &r) {
			Auth::instance().clear();
			widgets_.clear();
			list_->clear();
			// NeedsReconnect means the server had already dropped this link — nothing left to remove.
			const bool serverOk = !wasConnected || r.token == TokenStatus::NeedsReconnect ||
					      (r.token == TokenStatus::Ok && r.http.ok());
			setState(State::Disconnected,
				 serverOk ? QStringLiteral("Disconnected.")
					  : QStringLiteral("Disconnected here. GoalForgeX couldn't be reached — to be sure, remove this device at goalforgex.com/obs."));
		});
}

// ── Widget catalogue ─────────────────────────────────────────────

void GfxDock::loadCatalogue(bool quiet)
{
	if (!Auth::instance().connected() || catalogueInFlight_)
		return;
	catalogueInFlight_ = true;
	if (!quiet)
		showMessage(QStringLiteral("Loading your widgets…"));
	runAsync(
		[] {
			return authedCall([](const QString &tok) {
				return httpGet(baseUrl() + QStringLiteral("/api/obs/widgets"), tok);
			});
		},
		[this, quiet](const ApiResult &r) {
			catalogueInFlight_ = false;
			if (r.token == TokenStatus::NeedsReconnect) {
				setState(State::NeedsReconnect, r.error.isEmpty() ? QStringLiteral("This OBS was disconnected from GoalForgeX. Click Reconnect.") : r.error, true);
				return;
			}
			if (r.token == TokenStatus::Transient || !r.http.ok()) {
				const QString why = r.token == TokenStatus::Transient ? r.error : describe(r.http);
				showMessage(QStringLiteral("%1 Widgets already in your scenes keep working. Retrying in %2s.").arg(why).arg(netBackoffS_), true);
				catalogueTimer_.start(netBackoffS_ * 1000);
				netBackoffS_ = qMin(netBackoffS_ * 2, 300);
				if (state_ != State::Connected)
					setState(State::Connected);
				return;
			}
			netBackoffS_ = 5;
			QList<WidgetInfo> next;
			const QJsonArray arr = r.http.json().value(QStringLiteral("widgets")).toArray();
			for (const QJsonValue &v : arr) {
				const QJsonObject o = v.toObject();
				WidgetInfo w;
				w.id = o.value(QStringLiteral("id")).toString();
				w.name = o.value(QStringLiteral("name")).toString();
				w.icon = o.value(QStringLiteral("icon")).toString();
				w.url = o.value(QStringLiteral("url")).toString();
				w.configureUrl = o.value(QStringLiteral("configureUrl")).toString();
				w.lockReason = o.value(QStringLiteral("lockReason")).toString();
				w.width = qBound(100, o.value(QStringLiteral("width")).toInt(800), 7680);
				w.height = qBound(100, o.value(QStringLiteral("height")).toInt(600), 4320);
				w.fullCanvas = o.value(QStringLiteral("fullCanvas")).toBool();
				w.audio = o.value(QStringLiteral("audio")).toBool();
				w.locked = o.value(QStringLiteral("locked")).toBool();
				// Only ever load widgets from the GoalForgeX site itself.
				if (!w.id.isEmpty() && trustedUrl(w.url))
					next.push_back(w);
			}
			widgets_ = next;
			populateList();
			const Credentials c = Auth::instance().credentials();
			const int updated = sources::syncUrls(widgets_, c.overlayKey, c.username);
			setState(State::Connected);
			if (updated > 0)
				showMessage(QStringLiteral("Updated %1 GoalForgeX source(s) to use this connection.").arg(updated));
			else if (!quiet)
				showMessage(widgets_.isEmpty() ? QStringLiteral("No widgets found on your account.") : QString());
			catalogueTimer_.start(5 * 60 * 1000); // pick up new counters etc.
		});
}

void GfxDock::populateList()
{
	const QString keep = selectedWidget() ? selectedWidget()->id : QString();
	list_->blockSignals(true);
	list_->clear();
	int select = -1;
	for (int i = 0; i < widgets_.size(); ++i) {
		auto *item = new QListWidgetItem(widgets_[i].icon + QLatin1Char(' ') + widgets_[i].name, list_);
		item->setData(Qt::UserRole, i);
		if (widgets_[i].id == keep)
			select = i;
	}
	list_->blockSignals(false);
	if (select >= 0)
		list_->setCurrentRow(select);
	else if (!widgets_.isEmpty())
		list_->setCurrentRow(0);
	refreshScenes();
	refreshStatuses();
}

// ── Scenes + per-widget status ───────────────────────────────────

void GfxDock::refreshScenes()
{
	if (!loaded_)
		return;
	QString keep = sceneCombo_->currentText();
	if (keep.isEmpty()) {
		obs_source_t *cur = obs_frontend_get_current_scene();
		if (cur) {
			keep = QString::fromUtf8(obs_source_get_name(cur));
			obs_source_release(cur);
		}
	}
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	sceneCombo_->blockSignals(true);
	sceneCombo_->clear();
	for (size_t i = 0; i < scenes.sources.num; ++i)
		sceneCombo_->addItem(QString::fromUtf8(obs_source_get_name(scenes.sources.array[i])));
	obs_frontend_source_list_free(&scenes);
	const int idx = sceneCombo_->findText(keep);
	sceneCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
	sceneCombo_->blockSignals(false);
	refreshStatuses();
}

obs_source_t *GfxDock::selectedScene() const
{
	const QString name = sceneCombo_->currentText();
	if (name.isEmpty())
		return nullptr;
	obs_source_t *src = obs_get_source_by_name(name.toUtf8().constData());
	if (src && !obs_scene_from_source(src)) {
		obs_source_release(src);
		return nullptr;
	}
	return src;
}

const WidgetInfo *GfxDock::selectedWidget() const
{
	const QListWidgetItem *it = list_ ? list_->currentItem() : nullptr;
	if (!it)
		return nullptr;
	const int i = it->data(Qt::UserRole).toInt();
	return (i >= 0 && i < widgets_.size()) ? &widgets_[i] : nullptr;
}

void GfxDock::refreshStatuses()
{
	if (!loaded_ || widgets_.isEmpty())
		return;
	const QString account = Auth::instance().credentials().username;
	obs_source_t *scene = selectedScene();
	for (int row = 0; row < list_->count(); ++row) {
		QListWidgetItem *it = list_->item(row);
		const int i = it->data(Qt::UserRole).toInt();
		if (i < 0 || i >= widgets_.size())
			continue;
		const WidgetInfo &w = widgets_[i];
		QString status;
		obs_source_t *src = sources::find(account, w.id);
		const bool hasSource = src != nullptr;
		if (src) {
			obs_sceneitem_t *item = scene ? sources::findItem(scene, src) : nullptr;
			if (item)
				status = obs_sceneitem_visible(item) ? QStringLiteral("● in this scene") : QStringLiteral("◌ in this scene (hidden)");
			else
				status = QStringLiteral("in another scene");
			obs_source_release(src);
		} else if (w.locked) {
			status = QStringLiteral("🔒 ") + (w.lockReason.isEmpty() ? QStringLiteral("locked") : w.lockReason);
		}
		const QString text = w.icon + QLatin1Char(' ') + w.name + (status.isEmpty() ? QString() : QStringLiteral("   ·  ") + status);
		if (it->text() != text)
			it->setText(text);
		it->setForeground(w.locked && !hasSource ? palette().color(QPalette::Disabled, QPalette::Text) : palette().color(QPalette::Text));
		it->setToolTip(w.locked ? QStringLiteral("%1 — upgrade on goalforgex.com to use this widget.").arg(w.lockReason) : w.name);
	}
	if (scene)
		obs_source_release(scene);
	updateControls();
}

void GfxDock::updateControls()
{
	if (!addBtn_)
		return;
	const WidgetInfo *w = selectedWidget();
	const QString account = Auth::instance().credentials().username;
	obs_source_t *scene = selectedScene();
	obs_source_t *src = (w && loaded_) ? sources::find(account, w->id) : nullptr;
	obs_sceneitem_t *item = (scene && src) ? sources::findItem(scene, src) : nullptr;

	addBtn_->setEnabled(w && !w->locked && scene && !item && state_ == State::Connected);
	addBtn_->setToolTip(w && w->locked ? w->lockReason : QString());
	toggleBtn_->setEnabled(item != nullptr);
	toggleBtn_->setText(item && !obs_sceneitem_visible(item) ? QStringLiteral("Show") : QStringLiteral("Hide"));
	removeBtn_->setEnabled(item != nullptr);
	placeCombo_->setEnabled(item != nullptr);
	scaleSpin_->setEnabled(item != nullptr);
	fitBtn_->setEnabled(item != nullptr);
	reloadBtn_->setEnabled(src != nullptr);
	propsBtn_->setEnabled(src != nullptr);
	configureBtn_->setEnabled(w != nullptr && !w->configureUrl.isEmpty());
	if (item && !scaleSpin_->hasFocus()) {
		scaleSpin_->blockSignals(true);
		scaleSpin_->setValue(static_cast<int>(sources::scalePercent(item) + 0.5));
		scaleSpin_->blockSignals(false);
	}
	if (src)
		obs_source_release(src);
	if (scene)
		obs_source_release(scene);
}

// ── Actions ──────────────────────────────────────────────────────

void GfxDock::addSelected()
{
	const WidgetInfo *w = selectedWidget();
	if (!w || state_ != State::Connected)
		return;
	if (w->locked) {
		showMessage(w->lockReason + QStringLiteral(" — upgrade on goalforgex.com to use this widget."), true);
		return;
	}
	obs_source_t *scene = selectedScene();
	if (!scene) {
		showMessage(QStringLiteral("Pick a scene first."), true);
		return;
	}
	const Credentials c = Auth::instance().credentials();
	QString err;
	const auto res = sources::addToScene(scene, *w, c.overlayKey, c.username, &err);
	const QString sceneName = QString::fromUtf8(obs_source_get_name(scene));
	obs_source_release(scene);
	switch (res) {
	case sources::AddResult::Added:
		showMessage(QStringLiteral("Added %1 to \"%2\".").arg(w->name, sceneName));
		break;
	case sources::AddResult::Reused:
		showMessage(QStringLiteral("Added %1 to \"%2\" — it's the same source as in your other scenes, so changes apply everywhere.").arg(w->name, sceneName));
		break;
	case sources::AddResult::AlreadyInScene:
		showMessage(QStringLiteral("%1 is already in \"%2\" — selected it for you.").arg(w->name, sceneName));
		break;
	case sources::AddResult::Failed:
		showMessage(err, true);
		break;
	}
	refreshStatuses();
}

// Runs `fn` on the selected widget's scene item in the selected scene.
static bool withSelectedItem(const WidgetInfo *w, obs_source_t *scene, const std::function<void(obs_sceneitem_t *)> &fn)
{
	if (!w || !scene)
		return false;
	obs_source_t *src = sources::find(Auth::instance().credentials().username, w->id);
	if (!src)
		return false;
	obs_sceneitem_t *item = sources::findItem(scene, src);
	if (item)
		fn(item);
	obs_source_release(src);
	return item != nullptr;
}

void GfxDock::toggleSelected()
{
	obs_source_t *scene = selectedScene();
	withSelectedItem(selectedWidget(), scene, [](obs_sceneitem_t *it) { obs_sceneitem_set_visible(it, !obs_sceneitem_visible(it)); });
	if (scene)
		obs_source_release(scene);
	refreshStatuses();
}

void GfxDock::removeSelected()
{
	const WidgetInfo *w = selectedWidget();
	if (!w)
		return;
	const auto ans = QMessageBox::question(this, QStringLiteral("Remove from scene"),
					       QStringLiteral("Remove %1 from this scene?\n\nIt stays in any other scenes it's in, and you can add it back any time.").arg(w->name));
	if (ans != QMessageBox::Yes)
		return;
	obs_source_t *scene = selectedScene();
	withSelectedItem(w, scene, [](obs_sceneitem_t *it) { obs_sceneitem_remove(it); });
	if (scene)
		obs_source_release(scene);
	refreshStatuses();
}

void GfxDock::placeSelected(int comboIndex)
{
	if (comboIndex > PlaceHeader && comboIndex - 1 < kPlaces.size()) {
		const Place where = kPlaces[comboIndex - 1].second;
		obs_source_t *scene = selectedScene();
		withSelectedItem(selectedWidget(), scene, [where](obs_sceneitem_t *it) { sources::place(it, where); });
		if (scene)
			obs_source_release(scene);
	}
	placeCombo_->setCurrentIndex(PlaceHeader);
}

void GfxDock::scaleSelected()
{
	const double pct = static_cast<double>(scaleSpin_->value());
	obs_source_t *scene = selectedScene();
	withSelectedItem(selectedWidget(), scene, [pct](obs_sceneitem_t *it) { sources::setScalePercent(it, pct); });
	if (scene)
		obs_source_release(scene);
}

void GfxDock::fitSelected()
{
	obs_source_t *scene = selectedScene();
	withSelectedItem(selectedWidget(), scene, [](obs_sceneitem_t *it) { sources::fitToCanvas(it); });
	if (scene)
		obs_source_release(scene);
	updateControls();
}

void GfxDock::reloadSelected()
{
	const WidgetInfo *w = selectedWidget();
	if (!w)
		return;
	obs_source_t *src = sources::find(Auth::instance().credentials().username, w->id);
	if (!src)
		return;
	showMessage(sources::reload(src) ? QStringLiteral("Reloaded %1.").arg(w->name) : QStringLiteral("Couldn't reload %1.").arg(w->name),
		    false);
	obs_source_release(src);
}

void GfxDock::propertiesSelected()
{
	const WidgetInfo *w = selectedWidget();
	if (!w)
		return;
	obs_source_t *src = sources::find(Auth::instance().credentials().username, w->id);
	if (!src)
		return;
	obs_frontend_open_source_properties(src);
	obs_source_release(src);
}

void GfxDock::configureSelected()
{
	const WidgetInfo *w = selectedWidget();
	if (w && !w->configureUrl.isEmpty())
		QDesktopServices::openUrl(QUrl(w->configureUrl));
}

// ── OBS frontend events ──────────────────────────────────────────

void GfxDock::onFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		loaded_ = true;
		refreshScenes();
		if (Auth::instance().connected())
			loadCatalogue(false);
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		refreshScenes();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		loaded_ = true;
		sceneCombo_->clear();
		refreshScenes();
		// The new collection may hold GoalForgeX sources with an older key.
		if (Auth::instance().connected() && !widgets_.isEmpty()) {
			const Credentials c = Auth::instance().credentials();
			sources::syncUrls(widgets_, c.overlayKey, c.username);
		}
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		// Sources are being torn down — stop touching them until CHANGED.
		loaded_ = false;
		sceneCombo_->blockSignals(true);
		sceneCombo_->clear();
		sceneCombo_->blockSignals(false);
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		shuttingDown_ = true;
		loaded_ = false;
		statusTimer_.stop();
		catalogueTimer_.stop();
		pollTimer_.stop();
		httpBeginShutdown();
		break;
	default:
		break;
	}
}
