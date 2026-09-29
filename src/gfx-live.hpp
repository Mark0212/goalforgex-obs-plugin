/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include "gfx-state.hpp"

#include <QJsonValue>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <functional>

class QGroupBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// "Live" tab: stats line + one card per live control. Every card can be
// switched off in Settings; locked (non-Pro) features collapse to one line.
class GfxLivePanel : public QWidget {
public:
	using ActionFn = std::function<void(const QString &id, const QJsonValue &arg)>;

	explicit GfxLivePanel(QWidget *parent = nullptr);

	void setActionHandler(ActionFn fn) { action_ = std::move(fn); }
	void setState(const gfx::LiveState &s);
	void addEvents(const QList<gfx::FeedEvent> &events);
	void clearEvents();
	void applySettings();
	void setBusy(bool busy);

private:
	void buildUi();
	void tick();
	void rebuildCounters();
	void act(const QString &id, const QJsonValue &arg = QJsonValue());
	QGroupBox *makeCard(const QString &title, QVBoxLayout **body);
	QPushButton *lockLine(QWidget *parent, const QString &text);

	ActionFn action_;
	gfx::LiveState state_;
	QStringList counterIds_;
	QTimer tick_;

	QLabel *stats_ = nullptr;
	QLabel *empty_ = nullptr;

	QGroupBox *timerCard_ = nullptr;
	QLabel *timerTime_ = nullptr;
	QLabel *timerStatus_ = nullptr;
	QWidget *timerCtl_ = nullptr;
	QPushButton *timerToggle_ = nullptr;
	QPushButton *timerLock_ = nullptr;

	QGroupBox *atCard_ = nullptr;
	QLabel *atTime_ = nullptr;
	QLabel *atStatus_ = nullptr;
	QWidget *atCtl_ = nullptr;
	QPushButton *atToggle_ = nullptr;
	QPushButton *atLock_ = nullptr;

	QGroupBox *goalCard_ = nullptr;
	QLabel *goalLbl_ = nullptr;
	QProgressBar *goalBar_ = nullptr;
	QLabel *followerLbl_ = nullptr;

	QGroupBox *wheelCard_ = nullptr;
	QPushButton *wheelBtn_ = nullptr;
	QLabel *wheelLast_ = nullptr;
	QPushButton *wheelLock_ = nullptr;

	QGroupBox *countersCard_ = nullptr;
	QVBoxLayout *countersBox_ = nullptr;

	QGroupBox *alertsCard_ = nullptr;
	QWidget *alertsCtl_ = nullptr;
	QPushButton *replayBtn_ = nullptr;
	QPushButton *alertsLock_ = nullptr;

	QGroupBox *activityCard_ = nullptr;
	QListWidget *activity_ = nullptr;
};
