/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-chat-dock.hpp"
#include "gfx-settings.hpp"
#include "gfx-theme.hpp"
#include "gfx-viewers.hpp"

#include <QDockWidget>
#include <QLabel>
#include <QList>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

using namespace gfx;

// Below this the chat needs the room, so the list stays out of the way.
static constexpr int kViewersMinDockWidth = 460;

GfxChatDock::GfxChatDock(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(240);
	// Same root name as the main dock: the shared style sheet keys its background off it.
	setObjectName(QStringLiteral("GfxDock"));
	setAttribute(Qt::WA_StyledBackground, true);

	auto *root = new QVBoxLayout(this);
	// Thin edges: the chat is what this dock is for.
	root->setContentsMargins(6, 2, 6, 4);
	root->setSpacing(0);

	note_ = new QLabel(this);
	note_->setObjectName(QStringLiteral("gfxMuted"));
	note_->setWordWrap(true);
	note_->setAlignment(Qt::AlignCenter);
	note_->setMargin(16);
	root->addWidget(note_);

	body_ = new QVBoxLayout();
	body_->setContentsMargins(0, 0, 0, 0);
	root->addLayout(body_, 1);
	split_ = new QSplitter(Qt::Horizontal, this);
	split_->setChildrenCollapsible(false);
	split_->setHandleWidth(5);
	body_->addWidget(split_);
	widthSave_ = new QTimer(this);
	widthSave_->setSingleShot(true);
	widthSave_->setInterval(600);
	connect(widthSave_, &QTimer::timeout, this, [] { saveSettings(); });
	connect(split_, &QSplitter::splitterMoved, this, [this] {
		if (viewers_ && viewers_->isVisible()) {
			settings().vwWidth = viewers_->width();
			widthSave_->start();
		}
	});

	theme::applyTo(this);
	render();
}

QDockWidget *GfxChatDock::dockFrame() const
{
	for (QWidget *w = parentWidget(); w; w = w->parentWidget())
		if (auto *d = qobject_cast<QDockWidget *>(w))
			return d;
	return nullptr;
}

void GfxChatDock::setPanel(QWidget *panel)
{
	if (panel == panel_)
		return;
	if (panel_ && panel_->parentWidget() == split_) {
		panel_->hide();
		panel_->setParent(this); // stays our child until it's handed back
	}
	panel_ = panel;
	if (panel_) {
		split_->addWidget(panel_);
		panel_->show();
	}
	applyViewers();
	render();
}

void GfxChatDock::setViewers(GfxViewerPanel *viewers)
{
	viewers_ = viewers;
	if (viewers_) {
		split_->addWidget(viewers_);
		viewers_->hide();
	}
	applyViewers();
}

bool GfxChatDock::showingViewers() const
{
	return viewers_ && viewersOn_ && showingChat() && viewers_->isVisible() && !viewers_->visibleRegion().isEmpty();
}

void GfxChatDock::applyViewers()
{
	if (!viewers_)
		return;
	const bool wasOn = viewersOn_;
	viewersOn_ = settings().chatViewers && panel_ && connected_ && width() >= kViewersMinDockWidth;
	viewers_->setVisible(viewersOn_);
	if (!viewersOn_ || !panel_)
		return;
	const Settings &s = settings();
	split_->insertWidget(s.vwLeft ? 0 : 1, viewers_);
	split_->setStretchFactor(split_->indexOf(panel_), 1);
	split_->setStretchFactor(split_->indexOf(viewers_), 0);
	const int total = qMax(width() - 12, split_->width());
	const int vw = qBound(170, s.vwWidth, qMax(170, total - 250));
	split_->setSizes(s.vwLeft ? QList<int>{vw, total - vw} : QList<int>{total - vw, vw});
	if (!wasOn && onViewersShown)
		onViewersShown();
}

void GfxChatDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	// Only when the width crosses the line; dragging the splitter must keep working.
	if (viewers_ && settings().chatViewers &&
	    viewersOn_ != (panel_ && connected_ && width() >= kViewersMinDockWidth))
		applyViewers();
}

void GfxChatDock::setConnected(bool connected)
{
	if (connected == connected_)
		return;
	connected_ = connected;
	applyViewers();
	render();
}

void GfxChatDock::setUnread(int n)
{
	if (n == unread_)
		return;
	unread_ = n;
	render();
}

bool GfxChatDock::showingChat() const
{
	return panel_ && connected_ && isVisible() && !visibleRegion().isEmpty();
}

void GfxChatDock::applyAppearance()
{
	theme::applyTo(this);
	if (viewers_)
		viewers_->refresh();
	render();
}

void GfxChatDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (onShown)
		onShown();
}

void GfxChatDock::render()
{
	if (QDockWidget *d = dockFrame())
		d->setWindowTitle(
			unread_ > 0 ? QStringLiteral("GoalForgeX MultiChat (%1)")
					      .arg(unread_ > 99 ? QStringLiteral("99+") : QString::number(unread_))
				    : QStringLiteral("GoalForgeX MultiChat"));

	const bool chat = panel_ && connected_;
	if (panel_)
		panel_->setVisible(chat);
	note_->setVisible(!chat);
	if (!connected_)
		note_->setText(
			QStringLiteral("Connect your GoalForgeX account in the GoalForgeX dock and your Twitch, "
				       "Kick, YouTube, TikTok and Xive chat shows up here in one feed — with each "
				       "platform's online / offline status on top."));
	else if (!panel_)
		note_->setText(QStringLiteral("MultiChat is switched off.\n\n"
					      "GoalForgeX dock → ⚙ Settings → MultiChat turns it back on."));
}
