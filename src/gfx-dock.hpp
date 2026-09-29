/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include "gfx-reactions.hpp"
#include "gfx-sources.hpp"
#include "gfx-state.hpp"
#include "gfx-update.hpp"

#include <obs-frontend-api.h>

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QTimer>
#include <QWidget>

class GfxHealthPanel;
class GfxLivePanel;
class GfxReactionsPanel;
class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QToolButton;

// The "GoalForgeX" dock: account connection + Live / Widgets / Reactions /
// Health tabs. Everything the streamer doesn't want to see can be switched
// off in ⚙ Settings.
class GfxDock : public QWidget {
public:
	explicit GfxDock(QWidget *parent = nullptr);

	void onFrontendEvent(enum obs_frontend_event event);
	// Shared by the Live tab buttons, OBS hotkeys and stream automation.
	void performAction(const QString &id, const QJsonValue &arg);

private:
	enum class State { Disconnected, Linking, Connected, NeedsReconnect };

	void buildUi();
	QWidget *buildWidgetsPage();
	void rebuildTabs();
	void setState(State s, const QString &message = QString(), bool isError = false);
	void showMessage(const QString &text, bool isError = false);
	void openSettings();

	// Account linking (RFC 8628 device flow)
	void startConnect();
	void schedulePoll();
	void pollOnce();
	void cancelLink();
	void disconnectAccount();

	// Live state + updates
	void pollState();
	void checkForUpdate();
	void renderUpdate();
	void startUpdateDownload();
	void restartAndUpdate();
	void enterUpdateRequired(const QString &message);
	bool handleUpdateRequired(int httpStatus, const QString &message);
	void streamingChanged(bool started);
	void updateHealthContext();

	// Widgets tab
	void loadCatalogue(bool quiet);
	void populateList();
	void refreshScenes();
	void refreshStatuses();
	void updateControls();
	const gfx::WidgetInfo *selectedWidget() const;
	obs_source_t *selectedScene() const; // new reference or nullptr
	void addSelected();
	void addStarterLayout();
	void toggleSelected();
	void removeSelected();
	void placeSelected(int comboIndex);
	void scaleSelected();
	void fitSelected();
	void reloadSelected();
	void propertiesSelected();
	void configureSelected();

	// Header
	QLabel *statusLbl_ = nullptr;
	QPushButton *connectBtn_ = nullptr;
	QToolButton *menuBtn_ = nullptr;
	QAction *disconnectAct_ = nullptr;
	QWidget *updateRow_ = nullptr;
	QLabel *updateLbl_ = nullptr;
	QPushButton *updateBtn_ = nullptr;
	// Shown instead of the tabs when this version is below the minimum.
	QWidget *requiredPanel_ = nullptr;
	QLabel *requiredLbl_ = nullptr;
	QPushButton *requiredBtn_ = nullptr;
	// Linking
	QWidget *linkPanel_ = nullptr;
	QLabel *codeLbl_ = nullptr;
	QLabel *linkHint_ = nullptr;
	// Tabs
	QTabWidget *tabs_ = nullptr;
	GfxLivePanel *livePanel_ = nullptr;
	QWidget *widgetsPage_ = nullptr;
	GfxReactionsPanel *reactionsPanel_ = nullptr;
	GfxHealthPanel *healthPanel_ = nullptr;
	// Widgets tab
	QComboBox *sceneCombo_ = nullptr;
	QListWidget *list_ = nullptr;
	QPushButton *addBtn_ = nullptr;
	QPushButton *toggleBtn_ = nullptr;
	QPushButton *removeBtn_ = nullptr;
	QComboBox *placeCombo_ = nullptr;
	QSpinBox *scaleSpin_ = nullptr;
	QPushButton *fitBtn_ = nullptr;
	QPushButton *reloadBtn_ = nullptr;
	QPushButton *propsBtn_ = nullptr;
	QPushButton *configureBtn_ = nullptr;
	QLabel *msgLbl_ = nullptr;

	QTimer statusTimer_;
	QTimer catalogueTimer_;
	QTimer pollTimer_;
	QTimer stateTimer_;
	QTimer updateTimer_;
	QTimer msgClear_;

	gfx::ReactionEngine reactions_;

	State state_ = State::Disconnected;
	QList<gfx::WidgetInfo> widgets_;
	gfx::LiveState lastState_;
	QString feedId_;
	qint64 since_ = 0;
	QString latestVersion_;
	enum class Upd { None, Available, Downloading, Ready, Failed };
	Upd upd_ = Upd::None;
	gfx::UpdateInfo update_;
	QString updatePath_;
	QString updateError_;
	bool updateRequired_ = false;
	bool installerLaunched_ = false;
	QString deviceCode_;
	QString userCode_;
	QString verifyUrl_;
	int pollIntervalS_ = 5;
	qint64 linkExpiresMs_ = 0;
	int linkGeneration_ = 0;
	int netBackoffS_ = 5;
	bool loaded_ = false;
	bool catalogueInFlight_ = false;
	bool stateInFlight_ = false;
};
