/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-chat-dock.hpp"
#include "gfx-theme.hpp"

#include <QDockWidget>
#include <QLabel>
#include <QShowEvent>
#include <QVBoxLayout>

using namespace gfx;

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
	if (panel_ && panel_->parentWidget() == this) {
		body_->removeWidget(panel_);
		panel_->hide(); // stays our child until it's handed back
	}
	panel_ = panel;
	if (panel_) {
		body_->addWidget(panel_, 1);
		panel_->show();
	}
	render();
}

void GfxChatDock::setConnected(bool connected)
{
	if (connected == connected_)
		return;
	connected_ = connected;
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
		note_->setText(QStringLiteral("Connect your GoalForgeX account in the GoalForgeX dock and your Twitch, "
					      "Kick, YouTube and TikTok chat shows up here in one feed — with each "
					      "platform's online / offline status on top."));
	else if (!panel_)
		note_->setText(QStringLiteral("MultiChat is switched off.\n\n"
					      "GoalForgeX dock → ⚙ Settings → MultiChat turns it back on."));
}
