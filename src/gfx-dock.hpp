/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include "gfx-sources.hpp"

#include <obs-frontend-api.h>

#include <QList>
#include <QString>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QToolButton;

// The "GoalForgeX" dock: account connection + widget list + scene controls.
class GfxDock : public QWidget {
public:
	explicit GfxDock(QWidget *parent = nullptr);
	~GfxDock() override;

	void onFrontendEvent(enum obs_frontend_event event);

private:
	enum class State { Disconnected, Linking, Connected, NeedsReconnect };

	void buildUi();
	void setState(State s, const QString &message = QString(), bool isError = false);
	void showMessage(const QString &text, bool isError = false);

	// Account linking (RFC 8628 device flow)
	void startConnect();
	void schedulePoll();
	void pollOnce();
	void cancelLink();
	void disconnectAccount();

	// Widgets
	void loadCatalogue(bool quiet);
	void populateList();
	void refreshScenes();
	void refreshStatuses();
	void updateControls();
	const gfx::WidgetInfo *selectedWidget() const;
	obs_source_t *selectedScene() const; // new reference or nullptr

	void addSelected();
	void toggleSelected();
	void removeSelected();
	void placeSelected(int comboIndex);
	void scaleSelected();
	void fitSelected();
	void reloadSelected();
	void propertiesSelected();
	void configureSelected();

	template<class Work, class Done> void runAsync(Work work, Done done);

	// UI
	QLabel *statusLbl_ = nullptr;
	QPushButton *connectBtn_ = nullptr;
	QPushButton *disconnectBtn_ = nullptr;
	QPushButton *manageBtn_ = nullptr;
	QWidget *linkPanel_ = nullptr;
	QLabel *codeLbl_ = nullptr;
	QLabel *linkHint_ = nullptr;
	QWidget *mainPanel_ = nullptr;
	QComboBox *sceneCombo_ = nullptr;
	QToolButton *refreshBtn_ = nullptr;
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

	// State
	State state_ = State::Disconnected;
	QList<gfx::WidgetInfo> widgets_;
	QString deviceCode_;
	QString userCode_;
	QString verifyUrl_;
	int pollIntervalS_ = 5;
	qint64 linkExpiresMs_ = 0;
	int linkGeneration_ = 0;
	int netBackoffS_ = 5;
	bool loaded_ = false;
	bool shuttingDown_ = false;
	bool catalogueInFlight_ = false;
};
