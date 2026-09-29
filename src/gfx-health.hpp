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
#include "gfx-state.hpp"

#include <QTimer>
#include <QWidget>

#include <functional>

class QVBoxLayout;

// What the Health tab needs to know about the rest of the dock.
struct GfxHealthContext {
	const QList<gfx::WidgetInfo> *widgets = nullptr;
	const gfx::LiveState *state = nullptr;
	bool connected = false;
	bool needsReconnect = false;
	QString latestVersion; // newer plugin version available, or empty
	std::function<void()> reconnect;
	std::function<void(const QString &)> message;
};

// "Health" tab: ✅ / ⚠️ checks, each problem with a one-click Fix.
class GfxHealthPanel : public QWidget {
public:
	explicit GfxHealthPanel(QWidget *parent = nullptr);
	void setContext(const GfxHealthContext &ctx) { ctx_ = ctx; }
	void refresh();

protected:
	void showEvent(QShowEvent *e) override;

private:
	void addRow(bool ok, const QString &text, const QString &fixLabel = QString(),
		    std::function<void()> fix = nullptr);

	GfxHealthContext ctx_;
	QVBoxLayout *rows_ = nullptr;
	QTimer timer_;
};
