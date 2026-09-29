/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include "gfx-settings.hpp"
#include "gfx-state.hpp"

#include <QHash>
#include <QWidget>

class QListWidget;
class QPushButton;

namespace gfx {

// Runs the streamer's reaction rules against live events. UI thread only.
class ReactionEngine {
public:
	explicit ReactionEngine(QObject *ctx) : ctx_(ctx) {}

	// New feed events since the last poll (never replayed history).
	void onEvents(const QList<FeedEvent> &events);
	// Goal / follower-goal crossings between two consecutive polls.
	void onStateChange(const LiveState &prev, const LiveState &cur);
	// Fire one rule right now (the "Test" button).
	void run(const Reaction &r);

private:
	void fire(const QString &trigger);
	QObject *ctx_;
	QHash<QString, qint64> lastFired_;
};

} // namespace gfx

// "Reactions" tab: list of rules with on/off, add/edit/remove/test.
class GfxReactionsPanel : public QWidget {
public:
	GfxReactionsPanel(gfx::ReactionEngine *engine, QWidget *parent = nullptr);
	void reload();

private:
	bool editRule(gfx::Reaction *r);
	gfx::ReactionEngine *engine_;
	QListWidget *list_ = nullptr;
	QPushButton *edit_ = nullptr;
	QPushButton *remove_ = nullptr;
	QPushButton *test_ = nullptr;
};
