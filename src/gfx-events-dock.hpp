/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class QDockWidget;
class QLabel;
class QShowEvent;
class QToolButton;
class QVBoxLayout;

// "GoalForgeX Events" — its own OBS dock (Docks menu), so the event list can
// sit next to the scene preview or chat instead of behind a tab. The main
// GoalForgeX dock owns the Events panel and its data and hands the panel to
// this dock (setPanel); with Settings → Events → "own dock" off, the panel goes
// back to being a tab there and this dock just says so.
class GfxEventsDock : public QWidget {
public:
	explicit GfxEventsDock(QWidget *parent = nullptr);

	// nullptr = the panel is a tab in the main dock right now.
	void setPanel(QWidget *panel);
	QWidget *panel() const { return panel_; }
	// Not connected (or an update is required): a short note instead of the list.
	void setConnected(bool connected);
	// New events while this dock is hidden or behind another dock's tab.
	void setUnread(int n);
	// Whether the streamer can see the list right now.
	bool showingEvents() const;
	// Re-applies Settings → Appearance (style sheet, brand mark).
	void applyAppearance();
	// The OBS dock frame around this widget (set once OBS has added it).
	QDockWidget *dockFrame() const;

	std::function<void()> onOpenSettings; // ⚙ → the main dock's Settings, Events page
	std::function<void()> onShown;        // became visible → the main dock clears the unread count

protected:
	void showEvent(QShowEvent *event) override;

private:
	void render();

	QLabel *mark_ = nullptr;
	QLabel *title_ = nullptr;
	QToolButton *gear_ = nullptr;
	QVBoxLayout *body_ = nullptr;
	QLabel *note_ = nullptr;
	QWidget *panel_ = nullptr;
	bool connected_ = false;
	int unread_ = 0;
};
