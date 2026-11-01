/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QWidget>

#include <functional>

class QDockWidget;
class QLabel;
class QShowEvent;
class QVBoxLayout;

// "GoalForgeX MultiChat" — its own OBS dock (Docks menu) since 2.12, so the
// chat gets the full height of a dock instead of sharing one with the Live
// tab. The main GoalForgeX dock owns the MultiChat panel, its polling and its
// data and hands the panel to this dock (setPanel). No header of its own: the
// OBS title bar names it and every pixel below goes to the chat.
class GfxChatDock : public QWidget {
public:
	explicit GfxChatDock(QWidget *parent = nullptr);

	// nullptr = MultiChat is switched off (Settings → MultiChat).
	void setPanel(QWidget *panel);
	QWidget *panel() const { return panel_; }
	// Not connected (or an update is required): a short note instead of the chat.
	void setConnected(bool connected);
	// New messages while this dock is hidden or behind another dock's tab.
	void setUnread(int n);
	// Whether the streamer can see the chat right now.
	bool showingChat() const;
	// Re-applies Settings → Appearance (style sheet).
	void applyAppearance();
	// The OBS dock frame around this widget (set once OBS has added it).
	QDockWidget *dockFrame() const;

	std::function<void()> onShown; // became visible → the main dock clears the unread count

protected:
	void showEvent(QShowEvent *event) override;

private:
	void render();

	QVBoxLayout *body_ = nullptr;
	QLabel *note_ = nullptr;
	QWidget *panel_ = nullptr;
	bool connected_ = false;
	int unread_ = 0;
};
