/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-dock.hpp"
#include "gfx-api.hpp"
#include "gfx-auth.hpp"
#include "gfx-chat.hpp"
#include "gfx-health.hpp"
#include "gfx-http.hpp"
#include "gfx-live.hpp"
#include "gfx-settings.hpp"
#include "gfx-theme.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPair>
#include <QPushButton>
#include <QSpinBox>
#include <QSysInfo>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

using namespace gfx;

namespace {

// Widget URLs must point at GoalForgeX (or the dev server set via
// GOALFORGEX_BASE_URL) — never load an arbitrary page into a Browser Source.
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

QString fmtSecs(int s)
{
	s = qAbs(s);
	return s >= 3600 ? QStringLiteral("%1:%2:%3")
				   .arg(s / 3600)
				   .arg((s % 3600) / 60, 2, 10, QLatin1Char('0'))
				   .arg(s % 60, 2, 10, QLatin1Char('0'))
			 : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

enum PlaceIdx { PlaceHeader = 0 };
const QList<QPair<QString, Place>> kPlaces = {
	{QStringLiteral("Top left"), Place::TopLeft},         {QStringLiteral("Top center"), Place::TopCenter},
	{QStringLiteral("Top right"), Place::TopRight},       {QStringLiteral("Center"), Place::Center},
	{QStringLiteral("Bottom left"), Place::BottomLeft},   {QStringLiteral("Bottom center"), Place::BottomCenter},
	{QStringLiteral("Bottom right"), Place::BottomRight},
};

} // namespace

GfxDock::GfxDock(QWidget *parent) : QWidget(parent), reactions_(this)
{
	setMinimumWidth(300);
	// The whole look lives in one style sheet on this widget — it cascades to
	// the dock's children (and the dialogs/menus it opens) and nothing else.
	setObjectName(QStringLiteral("GfxDock"));
	setAttribute(Qt::WA_StyledBackground, true);
	setStyleSheet(theme::dockStyleSheet());
	loadSettings();
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
	stateTimer_.setInterval(2000);
	connect(&stateTimer_, &QTimer::timeout, this, [this] { pollState(); });
	updateTimer_.setInterval(6 * 60 * 60 * 1000);
	connect(&updateTimer_, &QTimer::timeout, this, [this] { checkForUpdate(); });
	msgClear_.setSingleShot(true);
	connect(&msgClear_, &QTimer::timeout, this, [this] {
		msgLbl_->clear();
		msgLbl_->hide();
	});
	clockTimer_.setInterval(1000);
	connect(&clockTimer_, &QTimer::timeout, this, [this] {
		if (isVisible())
			renderAirChip();
	});
	clockTimer_.start();
	chatTimer_.setSingleShot(true);
	connect(&chatTimer_, &QTimer::timeout, this, [this] { pollChat(); });

	Auth::instance().load();
	if (Auth::instance().connected())
		setState(State::Connected, QStringLiteral("Loading…"));
	else
		setState(State::Disconnected);
}

// ── UI ───────────────────────────────────────────────────────────

void GfxDock::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	// ── Header: mark · GoalForgeX / account · ON AIR chip · Reconnect · ⚙ ──
	auto *header = new QFrame(this);
	header->setObjectName(QStringLiteral("gfxHeader"));
	auto *head = new QHBoxLayout(header);
	head->setContentsMargins(10, 8, 6, 8);
	head->setSpacing(8);
	brandLbl_ = new QLabel(header);
	brandLbl_->setObjectName(QStringLiteral("gfxBrandMark"));
	brandLbl_->setPixmap(theme::brandMark(28, devicePixelRatioF()));
	brandLbl_->setFixedSize(28, 28);
	auto *titles = new QVBoxLayout();
	titles->setContentsMargins(0, 0, 0, 0);
	titles->setSpacing(0);
	auto *brandName = new QLabel(QStringLiteral("GoalForgeX"), header);
	brandName->setObjectName(QStringLiteral("gfxBrandName"));
	statusLbl_ = new QLabel(header);
	statusLbl_->setObjectName(QStringLiteral("gfxBrandSub"));
	statusLbl_->setTextFormat(Qt::RichText);
	titles->addWidget(brandName);
	titles->addWidget(statusLbl_);
	airChip_ = new QLabel(header);
	airChip_->setObjectName(QStringLiteral("gfxAirChip"));
	airChip_->setAlignment(Qt::AlignCenter);
	connectBtn_ = new QPushButton(QStringLiteral("Connect"), header);
	theme::setRole(connectBtn_, "primary");
	menuBtn_ = new QToolButton(header);
	menuBtn_->setText(QStringLiteral("⚙"));
	menuBtn_->setToolTip(QStringLiteral("Settings"));
	menuBtn_->setPopupMode(QToolButton::InstantPopup);
	theme::setRole(menuBtn_, "ghost");
	auto *menu = new QMenu(menuBtn_);
	connect(menu->addAction(QStringLiteral("Settings…")), &QAction::triggered, this, [this] { openSettings(); });
	connect(menu->addAction(QStringLiteral("Manage connected devices…")), &QAction::triggered, this,
		[] { QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/obs"))); });
	connect(menu->addAction(QStringLiteral("Open GoalForgeX dashboard")), &QAction::triggered, this,
		[] { QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/dashboard"))); });
	connect(menu->addAction(QStringLiteral("Check for updates")), &QAction::triggered, this, [this] {
		verCheck_ = VerCheck::Checking;
		renderVersion();
		checkForUpdate();
	});
	menu->addSeparator();
	disconnectAct_ = menu->addAction(QStringLiteral("Disconnect this OBS"));
	connect(disconnectAct_, &QAction::triggered, this, [this] { disconnectAccount(); });
	menuBtn_->setMenu(menu);
	head->addWidget(brandLbl_);
	head->addLayout(titles, 1);
	head->addWidget(airChip_);
	head->addWidget(connectBtn_);
	head->addWidget(menuBtn_);
	root->addWidget(header);
	connect(connectBtn_, &QPushButton::clicked, this, [this] { startConnect(); });

	// Everything below the header gets the page margins.
	auto *body = new QVBoxLayout();
	body->setContentsMargins(10, 8, 10, 8);
	body->setSpacing(8);
	root->addLayout(body, 1);

	// Update bar: "⬆ 1.2.0 is out [Update]" → downloading → "ready [Restart now]"
	auto *updateBar = new QFrame(this);
	updateBar->setObjectName(QStringLiteral("gfxUpdateBar"));
	updateRow_ = updateBar;
	auto *ur = new QHBoxLayout(updateRow_);
	ur->setContentsMargins(10, 6, 6, 6);
	updateLbl_ = new QLabel(updateRow_);
	updateLbl_->setWordWrap(true);
	updateBtn_ = new QPushButton(updateRow_);
	theme::setRole(updateBtn_, "primary");
	ur->addWidget(updateLbl_, 1);
	ur->addWidget(updateBtn_);
	updateRow_->hide();
	body->addWidget(updateRow_);
	connect(updateBtn_, &QPushButton::clicked, this, [this] {
		if (upd_ == Upd::Ready)
			restartAndUpdate();
		else if (upd_ == Upd::Available || upd_ == Upd::Failed)
			startUpdateDownload();
	});

	// Shown instead of the tabs when this version is no longer supported.
	auto *required = new QFrame(this);
	required->setObjectName(QStringLiteral("gfxRequired"));
	requiredPanel_ = required;
	auto *rp = new QVBoxLayout(requiredPanel_);
	rp->setContentsMargins(16, 18, 16, 16);
	rp->setSpacing(10);
	requiredLbl_ = new QLabel(requiredPanel_);
	requiredLbl_->setWordWrap(true);
	requiredLbl_->setAlignment(Qt::AlignCenter);
	requiredBtn_ = new QPushButton(QStringLiteral("Update now"), requiredPanel_);
	requiredBtn_->setObjectName(QStringLiteral("gfxBigBtn"));
	theme::setRole(requiredBtn_, "primary");
	auto *requiredNote = new QLabel(QStringLiteral("Widgets already in your scenes keep working in the meantime."),
					requiredPanel_);
	requiredNote->setObjectName(QStringLiteral("gfxMuted"));
	requiredNote->setWordWrap(true);
	requiredNote->setAlignment(Qt::AlignCenter);
	rp->addWidget(requiredLbl_);
	rp->addWidget(requiredBtn_, 0, Qt::AlignHCenter);
	rp->addWidget(requiredNote);
	rp->addStretch(1);
	requiredPanel_->hide();
	body->addWidget(requiredPanel_, 1);
	connect(requiredBtn_, &QPushButton::clicked, this, [this] {
		if (upd_ == Upd::Ready)
			restartAndUpdate();
		else if (update_.installable())
			startUpdateDownload();
		else
			QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/obs")));
	});

	// Welcome (disconnected): what this does + one big Connect button.
	auto *welcome = new QFrame(this);
	welcome->setObjectName(QStringLiteral("gfxWelcome"));
	welcomePanel_ = welcome;
	auto *wl = new QVBoxLayout(welcomePanel_);
	wl->setContentsMargins(18, 20, 18, 18);
	wl->setSpacing(10);
	auto *heroMark = new QLabel(welcomePanel_);
	heroMark->setPixmap(theme::brandMark(56, devicePixelRatioF()));
	heroMark->setAlignment(Qt::AlignHCenter);
	auto *hero = new QLabel(QStringLiteral("Run your stream from OBS"), welcomePanel_);
	hero->setObjectName(QStringLiteral("gfxHero"));
	hero->setAlignment(Qt::AlignHCenter);
	hero->setWordWrap(true);
	auto *heroSub = new QLabel(
		QStringLiteral(
			"Connect your GoalForgeX account to add widgets in one click and run timers, goals, the wheel and alerts without leaving OBS."),
		welcomePanel_);
	heroSub->setObjectName(QStringLiteral("gfxMuted"));
	heroSub->setAlignment(Qt::AlignHCenter);
	heroSub->setWordWrap(true);
	auto *features = new QLabel(welcomePanel_);
	features->setObjectName(QStringLiteral("gfxFeatures"));
	features->setTextFormat(Qt::RichText);
	features->setWordWrap(true);
	features->setText(
		QStringLiteral(
			"<table cellspacing='0' cellpadding='3'>"
			"<tr><td style='color:%1'>▸</td><td>Widgets added to your scenes, already sized and placed</td></tr>"
			"<tr><td style='color:%1'>▸</td><td>Live controls and OBS hotkeys for timers, goals and alerts</td></tr>"
			"<tr><td style='color:%1'>▸</td><td>Reactions that switch scenes when alerts fire</td></tr>"
			"<tr><td style='color:%1'>▸</td><td>A health check that fixes setup problems in one click</td></tr>"
			"</table>")
			.arg(QLatin1String(theme::kAmber)));
	auto *welcomeBtn = new QPushButton(QStringLiteral("Connect GoalForgeX"), welcomePanel_);
	welcomeBtn->setObjectName(QStringLiteral("gfxBigBtn"));
	welcomeBtn->setCursor(Qt::PointingHandCursor);
	theme::setRole(welcomeBtn, "primary");
	auto *welcomeNote = new QLabel(
		QStringLiteral("Opens your browser to approve this OBS. No password is stored here."), welcomePanel_);
	welcomeNote->setObjectName(QStringLiteral("gfxMuted"));
	welcomeNote->setAlignment(Qt::AlignHCenter);
	welcomeNote->setWordWrap(true);
	wl->addStretch(1);
	wl->addWidget(heroMark);
	wl->addWidget(hero);
	wl->addWidget(heroSub);
	wl->addSpacing(4);
	wl->addWidget(features);
	wl->addSpacing(4);
	wl->addWidget(welcomeBtn);
	wl->addWidget(welcomeNote);
	wl->addStretch(1);
	welcomePanel_->hide();
	body->addWidget(welcomePanel_, 1);
	connect(welcomeBtn, &QPushButton::clicked, this, [this] { startConnect(); });

	// Linking panel (device code)
	auto *link = new QFrame(this);
	link->setObjectName(QStringLiteral("gfxLinkPanel"));
	linkPanel_ = link;
	auto *lp = new QVBoxLayout(linkPanel_);
	lp->setContentsMargins(16, 16, 16, 14);
	lp->setSpacing(10);
	auto *lpTitle = new QLabel(QStringLiteral("Approve this OBS"), linkPanel_);
	lpTitle->setObjectName(QStringLiteral("gfxSectionTitle"));
	auto *lpSub = new QLabel(
		QStringLiteral("Your browser opened GoalForgeX. Check the code there matches this one:"), linkPanel_);
	lpSub->setObjectName(QStringLiteral("gfxMuted"));
	lpSub->setWordWrap(true);
	codeLbl_ = new QLabel(linkPanel_);
	codeLbl_->setObjectName(QStringLiteral("gfxCode"));
	QFont f = codeLbl_->font();
	f.setPointSize(f.pointSize() + 10);
	f.setBold(true);
	f.setFamily(QStringLiteral("Consolas"));
	f.setStyleHint(QFont::Monospace);
	f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
	codeLbl_->setFont(f);
	codeLbl_->setAlignment(Qt::AlignCenter);
	codeLbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	linkHint_ = new QLabel(linkPanel_);
	linkHint_->setObjectName(QStringLiteral("gfxMuted"));
	linkHint_->setWordWrap(true);
	linkHint_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
	linkHint_->setOpenExternalLinks(true);
	auto *lpRow = new QHBoxLayout();
	lpRow->setSpacing(6);
	auto *openBtn = new QPushButton(QStringLiteral("Open browser again"), linkPanel_);
	theme::setRole(openBtn, "primary");
	auto *copyBtn = new QPushButton(QStringLiteral("Copy code"), linkPanel_);
	auto *cancelBtn = new QPushButton(QStringLiteral("Cancel"), linkPanel_);
	theme::setRole(cancelBtn, "ghost");
	lpRow->addWidget(openBtn);
	lpRow->addWidget(copyBtn);
	lpRow->addStretch(1);
	lpRow->addWidget(cancelBtn);
	lp->addWidget(lpTitle);
	lp->addWidget(lpSub);
	lp->addWidget(codeLbl_);
	lp->addWidget(linkHint_);
	lp->addLayout(lpRow);
	lp->addStretch(1);
	linkPanel_->hide();
	body->addWidget(linkPanel_, 1);
	connect(openBtn, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(verifyUrl_)); });
	connect(copyBtn, &QPushButton::clicked, this, [this] {
		QGuiApplication::clipboard()->setText(userCode_);
		showMessage(QStringLiteral("Code copied."));
	});
	connect(cancelBtn, &QPushButton::clicked, this, [this] { cancelLink(); });

	// Tabs
	tabs_ = new QTabWidget(this);
	tabs_->setDocumentMode(true);
	livePanel_ = new GfxLivePanel(tabs_);
	livePanel_->setActionHandler([this](const QString &id, const QJsonValue &arg) { performAction(id, arg); });
	widgetsPage_ = buildWidgetsPage();
	reactionsPanel_ = new GfxReactionsPanel(&reactions_, tabs_);
	healthPanel_ = new GfxHealthPanel(tabs_);
	chatPanel_ = new GfxChatPanel(tabs_);
	chatPanel_->setSendHandler(
		[this](const QStringList &platforms, const QString &text) { sendChat(platforms, text); });
	connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
		if (chatPanel_ && tabs_->currentWidget() == chatPanel_) {
			chatUnread_ = 0;
			updateChatTabTitle();
			scheduleChat(0); // switch to the fast cadence right away
		}
	});
	body->addWidget(tabs_, 1);
	rebuildTabs();

	msgLbl_ = new QLabel(this);
	msgLbl_->setObjectName(QStringLiteral("gfxMsg"));
	msgLbl_->setWordWrap(true);
	msgLbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	msgLbl_->hide();
	body->addWidget(msgLbl_);

	versionLbl_ = new QLabel(this);
	versionLbl_->setObjectName(QStringLiteral("gfxFooter"));
	versionLbl_->setAlignment(Qt::AlignRight);
	versionLbl_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	const QString obsVersion = QString::fromUtf8(obs_get_version_string());
	versionLbl_->setToolTip(
		QStringLiteral("GoalForgeX for OBS %1 · OBS %2").arg(QString::fromUtf8(PLUGIN_VERSION), obsVersion));
	body->addWidget(versionLbl_);
	renderVersion();
	renderAirChip();
}

QWidget *GfxDock::buildWidgetsPage()
{
	auto *page = new QWidget(tabs_);
	auto *mp = new QVBoxLayout(page);
	mp->setContentsMargins(0, 4, 0, 4);
	mp->setSpacing(6);

	auto *sceneRow = new QHBoxLayout();
	auto *sceneLbl = new QLabel(QStringLiteral("Scene"), page);
	sceneLbl->setObjectName(QStringLiteral("gfxFieldLabel"));
	sceneRow->addWidget(sceneLbl);
	sceneCombo_ = new QComboBox(page);
	sceneCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	auto *refreshBtn = new QToolButton(page);
	refreshBtn->setText(QStringLiteral("↻"));
	refreshBtn->setToolTip(QStringLiteral("Reload your widget list from GoalForgeX"));
	theme::setRole(refreshBtn, "ghost");
	auto *starterBtn = new QToolButton(page);
	starterBtn->setText(QStringLiteral("✨ Starter layout"));
	starterBtn->setToolTip(
		QStringLiteral("Add alerts, chat box, goal and timer to this scene, already positioned"));
	starterBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	theme::setRole(starterBtn, "soft");
	sceneRow->addWidget(sceneCombo_, 1);
	sceneRow->addWidget(refreshBtn);
	mp->addLayout(sceneRow);
	mp->addWidget(starterBtn);
	connect(sceneCombo_, &QComboBox::currentIndexChanged, this, [this](int) { refreshStatuses(); });
	connect(refreshBtn, &QToolButton::clicked, this, [this] { loadCatalogue(false); });
	connect(starterBtn, &QToolButton::clicked, this, [this] { addStarterLayout(); });

	list_ = new QListWidget(page);
	list_->setSelectionMode(QAbstractItemView::SingleSelection);
	list_->setMinimumHeight(150);
	mp->addWidget(list_, 1);
	connect(list_, &QListWidget::currentRowChanged, this, [this](int) { updateControls(); });
	connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { addSelected(); });

	auto *grid = new QGridLayout();
	grid->setHorizontalSpacing(6);
	grid->setVerticalSpacing(6);
	addBtn_ = new QPushButton(QStringLiteral("Add to scene"), page);
	toggleBtn_ = new QPushButton(QStringLiteral("Hide"), page);
	removeBtn_ = new QPushButton(QStringLiteral("Remove"), page);
	removeBtn_->setToolTip(QStringLiteral("Remove from this scene"));
	theme::setRole(addBtn_, "primary");
	theme::setRole(removeBtn_, "danger");
	grid->addWidget(addBtn_, 0, 0);
	grid->addWidget(toggleBtn_, 0, 1);
	grid->addWidget(removeBtn_, 0, 2);

	placeCombo_ = new QComboBox(page);
	placeCombo_->addItem(QStringLiteral("Move to…"));
	for (const auto &p : kPlaces)
		placeCombo_->addItem(p.first);
	scaleSpin_ = new QSpinBox(page);
	scaleSpin_->setRange(10, 400);
	scaleSpin_->setSuffix(QStringLiteral(" %"));
	scaleSpin_->setToolTip(QStringLiteral("Size in the scene (100% = the widget's native size)"));
	scaleSpin_->setKeyboardTracking(false);
	fitBtn_ = new QPushButton(QStringLiteral("Fit canvas"), page);
	fitBtn_->setToolTip(QStringLiteral("Fill the whole canvas, keeping proportions (for Alerts and Chat Box)"));
	grid->addWidget(placeCombo_, 1, 0);
	grid->addWidget(scaleSpin_, 1, 1);
	grid->addWidget(fitBtn_, 1, 2);

	reloadBtn_ = new QPushButton(QStringLiteral("Reload"), page);
	reloadBtn_->setToolTip(QStringLiteral("Refresh this widget in OBS"));
	propsBtn_ = new QPushButton(QStringLiteral("Properties"), page);
	configureBtn_ = new QPushButton(QStringLiteral("Customize…"), page);
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
	return page;
}

void GfxDock::rebuildTabs()
{
	QWidget *current = tabs_->currentWidget();
	tabs_->clear();
	const Settings &s = settings();
	if (s.tabLive)
		tabs_->addTab(livePanel_, QStringLiteral("Live"));
	if (s.tabChat)
		tabs_->addTab(chatPanel_, QStringLiteral("Chat"));
	if (s.tabWidgets)
		tabs_->addTab(widgetsPage_, QStringLiteral("Widgets"));
	if (s.tabReactions)
		tabs_->addTab(reactionsPanel_, QStringLiteral("Reactions"));
	if (s.tabHealth)
		tabs_->addTab(healthPanel_, QStringLiteral("Health"));
	if (tabs_->count() == 0)
		tabs_->addTab(widgetsPage_, QStringLiteral("Widgets")); // never leave the dock empty
	const int i = tabs_->indexOf(current);
	if (i >= 0)
		tabs_->setCurrentIndex(i);
	livePanel_->applySettings();
	updateChatTabTitle();
}

void GfxDock::openSettings()
{
	Settings &s = settings();
	QDialog dlg(this);
	dlg.setWindowTitle(QStringLiteral("GoalForgeX settings"));
	dlg.setMinimumWidth(440);
	auto *root = new QVBoxLayout(&dlg);
	root->setContentsMargins(14, 12, 14, 12);
	root->setSpacing(6);

	struct Opt {
		QCheckBox *box;
		bool *value;
	};
	QList<Opt> opts;
	const auto group = [&dlg, root, &opts](const QString &title, const QList<QPair<QString, bool *>> &items) {
		auto *g = new QGroupBox(title, &dlg);
		auto *l = new QVBoxLayout(g);
		for (const auto &it : items) {
			auto *c = new QCheckBox(it.first, g);
			c->setChecked(*it.second);
			l->addWidget(c);
			opts.push_back({c, it.second});
		}
		root->addWidget(g);
	};
	group(QStringLiteral("Tabs"), {{QStringLiteral("Live"), &s.tabLive},
				       {QStringLiteral("Widgets"), &s.tabWidgets},
				       {QStringLiteral("Chat (Twitch + Kick)"), &s.tabChat},
				       {QStringLiteral("Reactions"), &s.tabReactions},
				       {QStringLiteral("Health"), &s.tabHealth}});
	group(QStringLiteral("Chat tab"), {{QStringLiteral("Show the time next to each message"), &s.chatTimestamps}});
	group(QStringLiteral("Live tab"), {{QStringLiteral("Stream stats line"), &s.cardStats},
					   {QStringLiteral("Subathon timer"), &s.cardTimer},
					   {QStringLiteral("Action timer"), &s.cardActionTimer},
					   {QStringLiteral("Goal"), &s.cardGoal},
					   {QStringLiteral("Spin wheel"), &s.cardWheel},
					   {QStringLiteral("Counters"), &s.cardCounters},
					   {QStringLiteral("Alerts (test / replay / clear chat)"), &s.cardAlerts},
					   {QStringLiteral("Activity feed"), &s.cardActivity}});
	group(QStringLiteral("Automation"),
	      {{QStringLiteral("Start / resume the subathon timer when I start streaming"), &s.autoStartTimer},
	       {QStringLiteral("Pause the subathon timer when I stop streaming"), &s.autoPauseTimer}});
	group(QStringLiteral("Updates"),
	      {{QStringLiteral("Tell me when a new version of the plugin is out"), &s.checkUpdates},
	       {QStringLiteral("Download updates automatically and install them when I close OBS"),
		&s.autoInstallUpdates}});
	auto *hint = new QLabel(
		QStringLiteral(
			"Hotkeys for timers, goal, wheel, counters and alerts are in OBS → Settings → Hotkeys (search “GoalForgeX”)."),
		&dlg);
	hint->setWordWrap(true);
	hint->setObjectName(QStringLiteral("gfxMuted"));
	root->addWidget(hint);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	if (QPushButton *okBtn = buttons->button(QDialogButtonBox::Ok))
		theme::setRole(okBtn, "primary");
	root->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	if (dlg.exec() != QDialog::Accepted)
		return;
	for (const Opt &o : opts)
		*o.value = o.box->isChecked();
	saveSettings();
	rebuildTabs();
	scheduleChat(0);
	checkForUpdate();
}

// ── State + messages ─────────────────────────────────────────────

void GfxDock::setState(State s, const QString &message, bool isError)
{
	state_ = s;
	const Credentials c = Auth::instance().credentials();
	const QString who = (c.displayName.isEmpty() ? c.username : c.displayName).toHtmlEscaped();
	switch (s) {
	case State::Disconnected:
		statusLbl_->setText(QStringLiteral("Not connected"));
		break;
	case State::Linking:
		statusLbl_->setText(QStringLiteral("Connecting…"));
		break;
	case State::Connected:
		statusLbl_->setText(
			QStringLiteral("<span style='color:%1'>●</span> %2").arg(QLatin1String(theme::kGreen), who));
		break;
	case State::NeedsReconnect:
		statusLbl_->setText(QStringLiteral("<span style='color:%1'>⚠ Needs reconnecting</span>")
					    .arg(QLatin1String(theme::kAmber)));
		break;
	}
	// Disconnected has the big Connect button on the welcome card instead.
	connectBtn_->setVisible(s == State::NeedsReconnect);
	connectBtn_->setText(QStringLiteral("Reconnect"));
	disconnectAct_->setEnabled(s == State::Connected || s == State::NeedsReconnect);
	welcomePanel_->setVisible(s == State::Disconnected && !updateRequired_);
	linkPanel_->setVisible(s == State::Linking);
	tabs_->setVisible((s == State::Connected || s == State::NeedsReconnect) && !updateRequired_);
	requiredPanel_->setVisible(updateRequired_ && s != State::Linking);
	if (s == State::Connected) {
		statusTimer_.start();
		stateTimer_.start();
		scheduleChat(300);
	} else {
		stateTimer_.stop();
		chatTimer_.stop();
		if (s != State::NeedsReconnect)
			statusTimer_.stop();
	}
	if (!message.isNull())
		showMessage(message, isError);
	else if (s == State::Disconnected)
		showMessage(QString()); // the welcome card explains what Connect does
	updateControls();
	renderAirChip();
	updateHealthContext();
}

void GfxDock::showMessage(const QString &text, bool isError)
{
	theme::setState(msgLbl_, isError ? "error" : "info");
	msgLbl_->setText(text);
	msgLbl_->setVisible(!text.isEmpty());
	// Confirmations fade on their own; errors stay until something replaces them.
	if (!isError && !text.isEmpty())
		msgClear_.start(5000);
	else
		msgClear_.stop();
}

void GfxDock::updateHealthContext()
{
	GfxHealthContext ctx;
	ctx.widgets = &widgets_;
	ctx.state = &lastState_;
	ctx.connected = Auth::instance().connected();
	ctx.needsReconnect = state_ == State::NeedsReconnect;
	ctx.latestVersion = latestVersion_;
	ctx.reconnect = [this] {
		startConnect();
	};
	ctx.message = [this](const QString &m) {
		showMessage(m);
	};
	healthPanel_->setContext(ctx);
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

	runAsync(
		this, [body] { return httpPostJson(baseUrl() + QStringLiteral("/api/obs/device"), body); },
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
				setState(State::Disconnected,
					 QStringLiteral("GoalForgeX sent an unexpected response — please try again."),
					 true);
				return;
			}
			codeLbl_->setText(userCode_);
			linkHint_->setText(
				QStringLiteral(
					"If your browser didn't open, go to <a href=\"%1\">%2</a> and enter the code above. It expires in 10 minutes.")
					.arg(verifyUrl_.toHtmlEscaped(), plainUrl.toHtmlEscaped()));
			if (!QDesktopServices::openUrl(QUrl(verifyUrl_)))
				showMessage(QStringLiteral(
					"Couldn't open your browser automatically — use the link above."));
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
		setState(State::Disconnected,
			 QStringLiteral("The code expired before it was approved. Click Connect to get a new one."),
			 true);
		return;
	}
	const int gen = linkGeneration_;
	const QString dc = deviceCode_;
	runAsync(
		this,
		[dc] {
			QJsonObject body;
			body.insert(QStringLiteral("grant_type"),
				    QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
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
				feedId_.clear();
				since_ = 0;
				setState(State::Connected, QStringLiteral("Connected! Loading your widgets…"));
				loadCatalogue(false);
				pollState();
				checkForUpdate();
				return;
			}
			const QString code = r.errorCode();
			if (code == QLatin1String("authorization_pending")) {
				schedulePoll();
			} else if (code == QLatin1String("slow_down")) {
				pollIntervalS_ =
					qMax(pollIntervalS_ + 5, r.json().value(QStringLiteral("interval")).toInt(0));
				schedulePoll();
			} else if (code == QLatin1String("access_denied")) {
				setState(State::Disconnected,
					 r.serverMessage().isEmpty()
						 ? QStringLiteral("The connection was declined in the browser.")
						 : r.serverMessage(),
					 true);
			} else if (code == QLatin1String("expired_token")) {
				setState(
					State::Disconnected,
					QStringLiteral(
						"The code expired before it was approved. Click Connect to get a new one."),
					true);
			} else {
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
		QStringLiteral(
			"Disconnect this OBS from your GoalForgeX account?\n\nWidgets this OBS added stay in your scenes but "
			"stop showing until you connect again."));
	if (ans != QMessageBox::Yes)
		return;
	catalogueTimer_.stop();
	const bool wasConnected = state_ == State::Connected;
	runAsync(
		this,
		[wasConnected] { return wasConnected ? apiPost(QStringLiteral("/api/obs/disconnect")) : ApiResult(); },
		[this, wasConnected](const ApiResult &r) {
			Auth::instance().clear();
			widgets_.clear();
			catalogueRev_.clear();
			list_->clear();
			lastState_ = LiveState();
			livePanel_->clearEvents();
			chatPanel_->clearChat();
			chatFeed_.clear();
			chatSeq_ = 0;
			chatUnread_ = 0;
			updateChatTabTitle();
			const bool serverOk = !wasConnected || r.token == TokenStatus::NeedsReconnect || r.ok();
			setState(
				State::Disconnected,
				serverOk
					? QStringLiteral("Disconnected.")
					: QStringLiteral(
						  "Disconnected here. GoalForgeX couldn't be reached — to be sure, remove this device at goalforgex.com/obs."));
		});
}

// ── Live state (every 2s while connected) ────────────────────────

void GfxDock::pollState()
{
	if (state_ != State::Connected || stateInFlight_)
		return;
	stateInFlight_ = true;
	const QString path = QStringLiteral("/api/obs/state?since=%1").arg(since_);
	runAsync(
		this, [path] { return apiGet(path); },
		[this](const ApiResult &r) {
			stateInFlight_ = false;
			if (r.token == TokenStatus::NeedsReconnect) {
				setState(
					State::NeedsReconnect,
					r.error.isEmpty()
						? QStringLiteral(
							  "This OBS was disconnected from GoalForgeX. Click Reconnect.")
						: r.error,
					true);
				return;
			}
			if (handleUpdateRequired(r.http.status, r.http.serverMessage()))
				return;
			if (!r.ok())
				return; // transient — the next poll will catch up
			const LiveState st = LiveState::fromJson(r.http.json());
			const bool firstOrReset = st.feed != feedId_;
			if (firstOrReset) {
				feedId_ = st.feed;
				livePanel_->clearEvents();
			}
			QList<FeedEvent> fresh;
			for (const FeedEvent &e : st.events)
				if (firstOrReset || e.seq > since_)
					fresh.push_back(e);
			since_ = st.seq;
			const LiveState prev = lastState_;
			lastState_ = st;
			livePanel_->setState(st);
			livePanel_->addEvents(fresh);
			renderAirChip();
			// History from before OBS connected is shown, never reacted to.
			if (!firstOrReset)
				reactions_.onEvents(fresh);
			reactions_.onStateChange(prev, st);
			updateHealthContext();
			// Widget sizes/list changed on the site (e.g. Chat Box width in the
			// dashboard) → re-fetch now instead of waiting for the 5-min refresh.
			if (!st.catalogueRev.isEmpty() && !catalogueRev_.isEmpty() && st.catalogueRev != catalogueRev_)
				loadCatalogue(true);
		});
}

void GfxDock::performAction(const QString &id, const QJsonValue &arg)
{
	if (state_ != State::Connected) {
		showMessage(QStringLiteral("Connect GoalForgeX first."), true);
		return;
	}
	const LiveState &s = lastState_;
	QString path, ok;
	QJsonObject body;
	const auto timerPath = [](bool action, const QString &verb) {
		return (action ? QStringLiteral("/api/actiontimer/") : QStringLiteral("/api/subathon/")) + verb;
	};
	const bool isAt = id.startsWith(QLatin1String("at."));
	const QString what = isAt ? QStringLiteral("Action timer") : QStringLiteral("Timer");

	if (id == QLatin1String("timer.toggle") || id == QLatin1String("at.toggle")) {
		if (!s.valid) {
			showMessage(QStringLiteral("Still loading — try again in a second."), true);
			return;
		}
		const TimerState &t = isAt ? s.actionTimer : s.timer;
		if (t.locked) {
			showMessage(QStringLiteral("%1 needs GoalForgeX Pro.").arg(what), true);
			return;
		}
		const QString verb = !t.active ? QStringLiteral("start")
					       : (t.paused ? QStringLiteral("resume") : QStringLiteral("pause"));
		path = timerPath(isAt, verb);
		ok = QStringLiteral("%1 %2.").arg(what, !t.active ? QStringLiteral("started")
								  : (t.paused ? QStringLiteral("resumed")
									      : QStringLiteral("paused")));
	} else if (id == QLatin1String("timer.add") || id == QLatin1String("at.add")) {
		const int secs = arg.toInt();
		path = timerPath(isAt, QStringLiteral("add-time"));
		body.insert(QStringLiteral("seconds"), secs);
		ok = QStringLiteral("%1 %2%3.")
			     .arg(what, secs >= 0 ? QStringLiteral("+") : QStringLiteral("−"), fmtSecs(secs));
	} else if (id == QLatin1String("timer.set")) {
		const int secs = qMax(0, arg.toInt());
		path = QStringLiteral("/api/subathon/set-time");
		body.insert(QStringLiteral("seconds"), secs);
		ok = QStringLiteral("Timer set to %1.").arg(fmtSecs(secs));
	} else if (id == QLatin1String("timer.reset") || id == QLatin1String("at.reset")) {
		path = timerPath(isAt, QStringLiteral("reset"));
		ok = QStringLiteral("%1 reset.").arg(what);
	} else if (id == QLatin1String("goal.adjust")) {
		path = QStringLiteral("/api/goal/adjust");
		body.insert(QStringLiteral("delta"), arg.toInt());
		ok = arg.toInt() >= 0 ? QStringLiteral("Goal +1.") : QStringLiteral("Goal −1.");
	} else if (id == QLatin1String("counter.adjust") || id == QLatin1String("counter.nth")) {
		const QJsonObject a = arg.toObject();
		QString cid = a.value(QStringLiteral("id")).toString();
		QString cname;
		if (id == QLatin1String("counter.nth")) {
			const int idx = a.value(QStringLiteral("index")).toInt();
			if (idx < 0 || idx >= s.counters.size()) {
				showMessage(QStringLiteral("There's no counter #%1 on your account.").arg(idx + 1),
					    true);
				return;
			}
			cid = s.counters[idx].id;
		}
		for (const CounterState &c : s.counters)
			if (c.id == cid)
				cname = c.name;
		const int delta = a.value(QStringLiteral("delta")).toInt();
		path = QStringLiteral("/api/counters/%1/adjust").arg(QString::fromUtf8(QUrl::toPercentEncoding(cid)));
		body.insert(QStringLiteral("delta"), delta);
		ok = QStringLiteral("%1 %2.").arg(cname.isEmpty() ? QStringLiteral("Counter") : cname,
						  delta >= 0 ? QStringLiteral("+1") : QStringLiteral("−1"));
	} else if (id == QLatin1String("alert.test")) {
		path = QStringLiteral("/api/obs/alerts/test");
		body.insert(QStringLiteral("type"), arg.toString());
		ok = QStringLiteral("Test alert sent.");
	} else if (id == QLatin1String("alert.replay")) {
		path = QStringLiteral("/api/obs/alerts/replay");
		ok = QStringLiteral("Replaying the last alert.");
	} else if (id == QLatin1String("chat.clear")) {
		path = QStringLiteral("/api/chatbox/clear");
		ok = QStringLiteral("Chat box cleared.");
	} else if (id == QLatin1String("wheel.spin")) {
		runAsync(
			this,
			[] {
				// Spin with the wheel exactly as saved on goalforgex.com.
				ApiResult w = apiGet(QStringLiteral("/api/spinwheel"));
				if (!w.ok())
					return w;
				const QJsonObject wheel = w.http.json().value(QStringLiteral("wheel")).toObject();
				const QJsonArray segs = wheel.value(QStringLiteral("segments")).toArray();
				if (segs.isEmpty()) {
					ApiResult e;
					e.http.status = 400;
					e.http.body = QByteArrayLiteral(
						"{\"message\":\"Your wheel has no slices yet — add some on goalforgex.com.\"}");
					return e;
				}
				QJsonObject b;
				b.insert(QStringLiteral("segments"), segs);
				b.insert(QStringLiteral("equalWeight"),
					 wheel.value(QStringLiteral("equalWeight")).toBool());
				b.insert(QStringLiteral("removeOnWin"),
					 wheel.value(QStringLiteral("removeOnWin")).toBool());
				return apiPost(QStringLiteral("/api/spinwheel/spin"), b);
			},
			[this](const ApiResult &r) {
				if (r.token == TokenStatus::NeedsReconnect)
					setState(State::NeedsReconnect, describe(r), true);
				else if (r.ok())
					showMessage(
						QStringLiteral("🎡 Spinning… landed on %1.")
							.arg(r.http.json().value(QStringLiteral("winner")).toString()));
				else
					showMessage(describe(r), true);
				pollState();
			});
		return;
	} else {
		return;
	}

	runAsync(
		this, [path, body] { return apiPost(path, body); },
		[this, ok](const ApiResult &r) {
			if (handleUpdateRequired(r.http.status, r.http.serverMessage()))
				return;
			if (r.token == TokenStatus::NeedsReconnect)
				setState(State::NeedsReconnect, describe(r), true);
			else if (r.ok())
				showMessage(ok);
			else
				showMessage(describe(r), true);
			pollState();
		});
}

// Start/pause the subathon timer with the stream (⚙ Settings → Automation).
void GfxDock::streamingChanged(bool started)
{
	const Settings &s = settings();
	if (state_ != State::Connected || (started ? !s.autoStartTimer : !s.autoPauseTimer))
		return;
	runAsync(
		this,
		[started] {
			ApiResult st = apiGet(QStringLiteral("/api/obs/state"));
			if (!st.ok())
				return st;
			const LiveState ls = LiveState::fromJson(st.http.json());
			if (ls.timer.locked)
				return ApiResult();
			if (started) {
				if (!ls.timer.active)
					return apiPost(QStringLiteral("/api/subathon/start"));
				if (ls.timer.paused)
					return apiPost(QStringLiteral("/api/subathon/resume"));
			} else if (ls.timer.active && !ls.timer.paused) {
				return apiPost(QStringLiteral("/api/subathon/pause"));
			}
			return ApiResult();
		},
		[this, started](const ApiResult &r) {
			if (r.http.status == 0 && r.token == TokenStatus::Ok)
				return; // nothing needed doing
			if (r.ok())
				showMessage(started ? QStringLiteral("Subathon timer started with your stream.")
						    : QStringLiteral("Subathon timer paused — stream ended."));
			else
				showMessage(
					QStringLiteral("Couldn't update the timer automatically: %1").arg(describe(r)),
					true);
			pollState();
		});
}

// ── Updates ──────────────────────────────────────────────────────
// Always asks (a required update must be noticed even with notices off);
// what's SHOWN follows ⚙ Settings → Updates.
void GfxDock::checkForUpdate()
{
	runAsync(
		this, [] { return httpGet(baseUrl() + QStringLiteral("/api/obs/latest")); },
		[this](const HttpResult &r) {
			if (!r.ok()) {
				verCheck_ = VerCheck::Failed;
				renderVersion();
				return;
			}
			verCheck_ = VerCheck::Done;
			const UpdateInfo info = UpdateInfo::fromJson(r.json());
			if (info.version != update_.version)
				upd_ = Upd::None; // a newer release replaced the one we knew about
			update_ = info;
			const bool newer = !info.version.isEmpty() &&
					   versionNewer(info.version, QString::fromUtf8(PLUGIN_VERSION));
			latestVersion_ = newer ? info.version : QString();
			if (info.required() && !updateRequired_)
				enterUpdateRequired(QString());
			if (newer && upd_ == Upd::None) {
				upd_ = Upd::Available;
				updatePath_ = installerPath(info.version);
				// Background download so it's ready to install on exit.
				if (info.installable() && (settings().autoInstallUpdates || updateRequired_))
					startUpdateDownload();
			}
			renderUpdate();
			renderVersion();
			updateHealthContext();
		});
}

void GfxDock::renderVersion()
{
	const QString current = QStringLiteral("v%1").arg(QString::fromUtf8(PLUGIN_VERSION));
	QString status;
	const char *tone = "muted";
	switch (verCheck_) {
	case VerCheck::Checking:
		status = QStringLiteral("checking for updates…");
		break;
	case VerCheck::Failed:
		status = QStringLiteral("couldn't check for updates");
		break;
	case VerCheck::Done:
		if (latestVersion_.isEmpty()) {
			status = QStringLiteral("✓ up to date");
			tone = "ok";
		} else {
			status = QStringLiteral("⬆ v%1 available").arg(latestVersion_);
			tone = "warn";
		}
		break;
	}
	theme::setState(versionLbl_, tone);
	versionLbl_->setText(QStringLiteral("GoalForgeX for OBS %1 · %2").arg(current, status));
}

// ON AIR (red, stream clock) / REC (amber, recording clock) / OFF AIR, from
// OBS's own outputs — plus the Twitch viewer count GoalForgeX reports.
void GfxDock::renderAirChip()
{
	if (!airChip_)
		return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const auto clock = [now](qint64 since) {
		return since > 0 ? QStringLiteral(" ") + fmtSecs(static_cast<int>((now - since) / 1000)) : QString();
	};
	const bool streaming = obs_frontend_streaming_active();
	const bool recording = obs_frontend_recording_active();
	if (streaming) {
		QString t = QStringLiteral("● ON AIR") + clock(streamStartMs_);
		if (lastState_.valid && lastState_.isLive && lastState_.viewers >= 0)
			t += QStringLiteral(" · 👁 %1").arg(lastState_.viewers);
		theme::setState(airChip_, "onair");
		airChip_->setText(t);
		airChip_->setToolTip(recording ? QStringLiteral("Streaming and recording")
					       : QStringLiteral("Streaming"));
	} else if (recording) {
		theme::setState(airChip_, "rec");
		airChip_->setText(QStringLiteral("● REC") + clock(recordStartMs_));
		airChip_->setToolTip(QStringLiteral("Recording"));
	} else {
		theme::setState(airChip_, "off");
		airChip_->setText(QStringLiteral("OFF AIR"));
		airChip_->setToolTip(QStringLiteral("Not streaming or recording"));
	}
}

// ── Chat tab ─────────────────────────────────────────────────────

void GfxDock::scheduleChat(int ms)
{
	if (state_ != State::Connected || !settings().tabChat || updateRequired_) {
		chatTimer_.stop();
		return;
	}
	chatTimer_.start(ms);
}

// Polls fast (1.5 s) while the Chat tab is on screen, slower otherwise so the
// unread count on the tab stays current. Each poll also keeps the server's
// Twitch/Kick chat connection up for this account.
void GfxDock::pollChat()
{
	if (state_ != State::Connected || !settings().tabChat || chatInFlight_)
		return;
	chatInFlight_ = true;
	const QString path = QStringLiteral("/api/obs/chat?feed=%1&since=%2")
				     .arg(chatFeed_.isEmpty() ? QStringLiteral("none") : chatFeed_)
				     .arg(chatSeq_);
	runAsync(
		this, [path] { return apiGet(path); },
		[this](const ApiResult &r) {
			chatInFlight_ = false;
			const bool onTab = isVisible() && tabs_->currentWidget() == chatPanel_;
			if (r.token == TokenStatus::NeedsReconnect)
				return; // the state poll shows Reconnect
			if (handleUpdateRequired(r.http.status, r.http.serverMessage()))
				return;
			if (r.http.status == 403 && r.http.errorCode() == QLatin1String("pro_required")) {
				chatPanel_->setLocked(r.http.serverMessage());
				scheduleChat(120000);
				return;
			}
			if (!r.ok()) {
				scheduleChat(onTab ? 4000 : 10000); // transient — try again shortly
				return;
			}
			chatPanel_->setLocked(QString());
			const QJsonObject o = r.http.json();
			chatFeed_ = o.value(QStringLiteral("feed")).toString();
			chatSeq_ = static_cast<qint64>(o.value(QStringLiteral("seq")).toDouble());
			const int added = chatPanel_->applyPoll(o);
			if (!onTab && added > 0) {
				chatUnread_ += added;
				updateChatTabTitle();
			}
			scheduleChat(onTab ? 1500 : 4000);
		});
}

void GfxDock::sendChat(const QStringList &platforms, const QString &text)
{
	QJsonObject body;
	body.insert(QStringLiteral("text"), text);
	body.insert(QStringLiteral("platforms"), QJsonArray::fromStringList(platforms));
	runAsync(
		this, [body] { return apiPost(QStringLiteral("/api/obs/chat/send"), body); },
		[this](const ApiResult &r) {
			if (!r.ok()) {
				chatPanel_->sendFinished(false);
				showMessage(QStringLiteral("Message not sent: %1").arg(describe(r)), true);
				return;
			}
			const QJsonObject results = r.http.json().value(QStringLiteral("results")).toObject();
			bool any = false;
			QStringList failed;
			for (auto it = results.begin(); it != results.end(); ++it) {
				const QJsonObject one = it.value().toObject();
				if (one.value(QStringLiteral("ok")).toBool())
					any = true;
				else
					failed << QStringLiteral("%1: %2").arg(
						it.key() == QLatin1String("kick") ? QStringLiteral("Kick")
										  : QStringLiteral("Twitch"),
						one.value(QStringLiteral("error")).toString());
			}
			chatPanel_->sendFinished(any);
			if (!failed.isEmpty())
				showMessage(
					QStringLiteral("Not delivered — %1").arg(failed.join(QStringLiteral(" · "))),
					true);
			scheduleChat(200); // show the echo right away
		});
}

void GfxDock::updateChatTabTitle()
{
	const int i = chatPanel_ ? tabs_->indexOf(chatPanel_) : -1;
	if (i < 0)
		return;
	tabs_->setTabText(i, chatUnread_ > 0 ? QStringLiteral("Chat · %1")
						       .arg(chatUnread_ > 99 ? QStringLiteral("99+")
									     : QString::number(chatUnread_))
					     : QStringLiteral("Chat"));
}

void GfxDock::renderUpdate()
{
	const bool show = upd_ != Upd::None && (settings().checkUpdates || updateRequired_ || upd_ == Upd::Ready);
	updateRow_->setVisible(show && !updateRequired_);
	const QString v = update_.version.toHtmlEscaped();
	updateBtn_->setEnabled(upd_ != Upd::Downloading);
	switch (upd_) {
	case Upd::None:
		break;
	case Upd::Available:
		updateLbl_->setText(QStringLiteral("⬆ Version %1 is out.").arg(v));
		updateBtn_->setText(update_.installable() ? QStringLiteral("Update") : QStringLiteral("Download"));
		break;
	case Upd::Downloading:
		updateLbl_->setText(QStringLiteral("⬇ Downloading version %1…").arg(v));
		updateBtn_->setText(QStringLiteral("Downloading…"));
		break;
	case Upd::Ready:
		updateLbl_->setText(
			settings().autoInstallUpdates
				? QStringLiteral("✅ Version %1 is ready — it installs when you close OBS.").arg(v)
				: QStringLiteral("✅ Version %1 is ready to install.").arg(v));
		updateBtn_->setText(QStringLiteral("Restart now"));
		break;
	case Upd::Failed:
		updateLbl_->setText(QStringLiteral("⚠ Update failed: %1").arg(updateError_.toHtmlEscaped()));
		updateBtn_->setText(QStringLiteral("Try again"));
		break;
	}
	if (updateRequired_) {
		requiredBtn_->setEnabled(upd_ != Upd::Downloading);
		requiredBtn_->setText(upd_ == Upd::Ready         ? QStringLiteral("Restart OBS and update")
				      : upd_ == Upd::Downloading ? QStringLiteral("Downloading…")
				      : update_.installable()    ? QStringLiteral("Update now")
								 : QStringLiteral("Download the update"));
	}
}

void GfxDock::startUpdateDownload()
{
	if (!update_.installable()) {
		QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/obs")));
		return;
	}
	if (upd_ == Upd::Downloading || upd_ == Upd::Ready)
		return;
	upd_ = Upd::Downloading;
	renderUpdate();
	const UpdateInfo info = update_;
	const QString path = updatePath_;
	runAsync(
		this, [info, path] { return downloadAndVerify(info, path); },
		[this, info](const QString &err) {
			if (info.version != update_.version)
				return; // superseded while downloading
			if (err.isEmpty()) {
				upd_ = Upd::Ready;
			} else {
				upd_ = Upd::Failed;
				updateError_ = err;
			}
			renderUpdate();
		});
}

void GfxDock::restartAndUpdate()
{
	if (upd_ != Upd::Ready)
		return;
	// Never interrupt a live show.
	if (obs_frontend_streaming_active() || obs_frontend_recording_active()) {
		showMessage(
			QStringLiteral(
				"Stop streaming and recording first — the update will also install automatically when you close OBS."),
			true);
		return;
	}
	if (QMessageBox::question(
		    this, QStringLiteral("Update GoalForgeX"),
		    QStringLiteral(
			    "OBS will close, update GoalForgeX to version %1 and open again.\n\nWindows will ask to allow the installer — click Yes.")
			    .arg(update_.version)) != QMessageBox::Yes)
		return;
	QString err;
	if (!launchInstaller(updatePath_, true, &err)) {
		showMessage(err, true);
		return;
	}
	installerLaunched_ = true;
	// The installer waits for OBS to exit, installs, then reopens OBS.
	if (auto *main = static_cast<QWidget *>(obs_frontend_get_main_window()))
		QMetaObject::invokeMethod(main, "close", Qt::QueuedConnection);
}

void GfxDock::enterUpdateRequired(const QString &message)
{
	updateRequired_ = true;
	stateTimer_.stop();
	requiredLbl_->setText(
		QStringLiteral("<b>Please update GoalForgeX for OBS</b><br>%1")
			.arg((message.isEmpty() ? QStringLiteral("This version (%1) is no longer supported.")
							  .arg(QString::fromUtf8(PLUGIN_VERSION))
						: message)
				     .toHtmlEscaped()));
	requiredLbl_->setTextFormat(Qt::RichText);
	setState(state_);
	if (update_.installable() && upd_ != Upd::Downloading && upd_ != Upd::Ready)
		startUpdateDownload();
	renderUpdate();
}

// A 426 from any plugin call means this version is below the minimum.
bool GfxDock::handleUpdateRequired(int httpStatus, const QString &message)
{
	if (httpStatus != 426)
		return false;
	if (!updateRequired_) {
		enterUpdateRequired(message);
		checkForUpdate();
	}
	return true;
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
		this, [] { return apiGet(QStringLiteral("/api/obs/widgets")); },
		[this, quiet](const ApiResult &r) {
			catalogueInFlight_ = false;
			if (handleUpdateRequired(r.http.status, r.http.serverMessage()))
				return;
			if (r.token == TokenStatus::NeedsReconnect) {
				setState(
					State::NeedsReconnect,
					r.error.isEmpty()
						? QStringLiteral(
							  "This OBS was disconnected from GoalForgeX. Click Reconnect.")
						: r.error,
					true);
				return;
			}
			if (!r.ok()) {
				showMessage(QStringLiteral(
						    "%1 Widgets already in your scenes keep working. Retrying in %2s.")
						    .arg(describe(r))
						    .arg(netBackoffS_),
					    true);
				catalogueTimer_.start(netBackoffS_ * 1000);
				netBackoffS_ = qMin(netBackoffS_ * 2, 300);
				return;
			}
			netBackoffS_ = 5;
			QList<WidgetInfo> next;
			const QJsonArray arr = r.http.json().value(QStringLiteral("widgets")).toArray();
			// Index loop: iterating a QJsonArray yields temporaries, which clang
			// rejects binding to a reference (-Wrange-loop-bind-reference).
			for (qsizetype i = 0; i < arr.size(); ++i) {
				const QJsonObject o = arr.at(i).toObject();
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
				if (!w.id.isEmpty() && trustedUrl(w.url))
					next.push_back(w);
			}
			widgets_ = next;
			catalogueRev_ = r.http.json().value(QStringLiteral("catalogueRev")).toString();
			populateList();
			const Credentials c = Auth::instance().credentials();
			const int updated = sources::syncUrls(widgets_, c.overlayKey, c.username);
			// Follow size changes made in the dashboard (sources resized by
			// hand in OBS are left alone — see sources::syncSizes).
			const int resized = sources::syncSizes(widgets_, c.username);
			if (state_ != State::Connected)
				setState(State::Connected);
			updateHealthContext();
			if (updated > 0)
				showMessage(QStringLiteral("Updated %1 GoalForgeX source(s) to use this connection.")
						    .arg(updated));
			else if (resized > 0)
				showMessage(QStringLiteral("Resized %1 GoalForgeX source(s) to match your dashboard.")
						    .arg(resized));
			else if (!quiet)
				showMessage(widgets_.isEmpty() ? QStringLiteral("No widgets found on your account.")
							       : QStringLiteral("Ready."));
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
				status = obs_sceneitem_visible(item) ? QStringLiteral("● in scene")
								     : QStringLiteral("◌ hidden");
			else
				status = QStringLiteral("in another scene");
			obs_source_release(src);
		} else if (w.locked) {
			status = QStringLiteral("🔒 Pro");
		}
		const QString text = w.icon + QLatin1Char(' ') + w.name +
				     (status.isEmpty() ? QString() : QStringLiteral("   ·  ") + status);
		if (it->text() != text)
			it->setText(text);
		it->setForeground(w.locked && !hasSource ? palette().color(QPalette::Disabled, QPalette::Text)
							 : palette().color(QPalette::Text));
		it->setToolTip(
			w.locked
				? QStringLiteral("%1 — upgrade on goalforgex.com to use this widget.").arg(w.lockReason)
				: w.name);
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

	const bool locked = w && w->locked;
	addBtn_->setText(locked ? QStringLiteral("Unlock with Pro") : QStringLiteral("Add to scene"));
	addBtn_->setEnabled(w && (locked || (scene && !item && state_ == State::Connected)));
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

// ── Widget actions ───────────────────────────────────────────────

void GfxDock::addSelected()
{
	const WidgetInfo *w = selectedWidget();
	if (!w)
		return;
	if (w->locked) {
		QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/subscribe")));
		return;
	}
	if (state_ != State::Connected)
		return;
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
		showMessage(QStringLiteral("Added %1 to “%2”.").arg(w->name, sceneName));
		break;
	case sources::AddResult::Reused:
		showMessage(QStringLiteral("Added %1 to “%2” — it's the same source as in your other scenes.")
				    .arg(w->name, sceneName));
		break;
	case sources::AddResult::AlreadyInScene:
		showMessage(QStringLiteral("%1 is already in “%2” — selected it for you.").arg(w->name, sceneName));
		break;
	case sources::AddResult::Failed:
		showMessage(err, true);
		break;
	}
	refreshStatuses();
}

// One click: alerts + chat box (full canvas), goal top-left, timer top-right.
void GfxDock::addStarterLayout()
{
	obs_source_t *scene = selectedScene();
	if (!scene || state_ != State::Connected) {
		if (scene)
			obs_source_release(scene);
		showMessage(QStringLiteral("Pick a scene first."), true);
		return;
	}
	const QString sceneName = QString::fromUtf8(obs_source_get_name(scene));
	if (QMessageBox::question(
		    this, QStringLiteral("Starter layout"),
		    QStringLiteral(
			    "Add your alerts, chat box, goal and subathon timer to “%1”, already positioned?\n\nAnything already in the scene is left where it is.")
			    .arg(sceneName)) != QMessageBox::Yes) {
		obs_source_release(scene);
		return;
	}
	const Credentials c = Auth::instance().credentials();
	const QList<QPair<QString, int>> plan = {
		{QStringLiteral("alerts"), -1},
		{QStringLiteral("chatbox"), -1},
		{QStringLiteral("goal"), static_cast<int>(Place::TopLeft)},
		{QStringLiteral("timer"), static_cast<int>(Place::TopRight)},
	};
	int added = 0;
	QStringList skipped;
	for (const auto &p : plan) {
		const WidgetInfo *w = nullptr;
		for (const WidgetInfo &x : widgets_)
			if (x.id == p.first)
				w = &x;
		if (!w)
			continue;
		if (w->locked) {
			skipped << w->name + QStringLiteral(" (Pro)");
			continue;
		}
		QString err;
		const auto res = sources::addToScene(scene, *w, c.overlayKey, c.username, &err);
		if (res == sources::AddResult::Added || res == sources::AddResult::Reused) {
			++added;
			if (p.second >= 0) {
				obs_source_t *src = sources::find(c.username, w->id);
				if (src) {
					sources::place(sources::findItem(scene, src), static_cast<Place>(p.second));
					obs_source_release(src);
				}
			}
		}
	}
	obs_source_release(scene);
	QString msg = added ? QStringLiteral("Added %1 widget%2 to “%3”.")
				      .arg(added)
				      .arg(added == 1 ? QString() : QStringLiteral("s"))
				      .arg(sceneName)
			    : QStringLiteral("Everything was already in “%1”.").arg(sceneName);
	if (!skipped.isEmpty())
		msg += QStringLiteral(" Skipped: %1.").arg(skipped.join(QStringLiteral(", ")));
	showMessage(msg);
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
	withSelectedItem(selectedWidget(), scene,
			 [](obs_sceneitem_t *it) { obs_sceneitem_set_visible(it, !obs_sceneitem_visible(it)); });
	if (scene)
		obs_source_release(scene);
	refreshStatuses();
}

void GfxDock::removeSelected()
{
	const WidgetInfo *w = selectedWidget();
	if (!w)
		return;
	const auto ans = QMessageBox::question(
		this, QStringLiteral("Remove from scene"),
		QStringLiteral(
			"Remove %1 from this scene?\n\nIt stays in any other scenes it's in, and you can add it back any time.")
			.arg(w->name));
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
	showMessage(sources::reload(src) ? QStringLiteral("Reloaded %1.").arg(w->name)
					 : QStringLiteral("Couldn't reload %1.").arg(w->name),
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
		if (Auth::instance().connected()) {
			loadCatalogue(false);
			pollState();
		}
		// Even when not connected — an update may be required to connect at all.
		checkForUpdate();
		updateTimer_.start();
		// Already live when OBS finished loading (rare) — start the clocks now.
		if (obs_frontend_streaming_active() && !streamStartMs_)
			streamStartMs_ = QDateTime::currentMSecsSinceEpoch();
		if (obs_frontend_recording_active() && !recordStartMs_)
			recordStartMs_ = QDateTime::currentMSecsSinceEpoch();
		renderAirChip();
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
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		streamStartMs_ = QDateTime::currentMSecsSinceEpoch();
		renderAirChip();
		streamingChanged(true);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		streamStartMs_ = 0;
		renderAirChip();
		streamingChanged(false);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		recordStartMs_ = QDateTime::currentMSecsSinceEpoch();
		renderAirChip();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		recordStartMs_ = 0;
		renderAirChip();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		// Downloaded + verified update: install it now that OBS is closing
		// (the installer waits for OBS to finish exiting first).
		if (upd_ == Upd::Ready && !installerLaunched_ && (settings().autoInstallUpdates || updateRequired_) &&
		    fileMatches(updatePath_, update_.sha256)) {
			QString err;
			installerLaunched_ = launchInstaller(updatePath_, false, &err);
		}
		loaded_ = false;
		statusTimer_.stop();
		catalogueTimer_.stop();
		pollTimer_.stop();
		stateTimer_.stop();
		updateTimer_.stop();
		clockTimer_.stop();
		chatTimer_.stop();
		httpBeginShutdown();
		break;
	default:
		break;
	}
}
