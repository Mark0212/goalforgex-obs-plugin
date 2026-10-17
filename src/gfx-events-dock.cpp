/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-events-dock.hpp"
#include "gfx-theme.hpp"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>

using namespace gfx;

GfxEventsDock::GfxEventsDock(QWidget *parent) : QWidget(parent)
{
	setMinimumWidth(260);
	// Same root name as the main dock: the shared style sheet keys its background off it.
	setObjectName(QStringLiteral("GfxDock"));
	setAttribute(Qt::WA_StyledBackground, true);

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(10, 8, 10, 8);
	root->setSpacing(6);

	auto *head = new QHBoxLayout();
	head->setSpacing(8);
	mark_ = new QLabel(this);
	mark_->setFixedSize(22, 22);
	title_ = new QLabel(this);
	title_->setObjectName(QStringLiteral("gfxBrandName"));
	title_->setTextFormat(Qt::RichText);
	gear_ = new QToolButton(this);
	gear_->setText(QStringLiteral("⚙"));
	gear_->setToolTip(QStringLiteral("Events settings"));
	gear_->setCursor(Qt::PointingHandCursor);
	theme::setRole(gear_, "ghost");
	connect(gear_, &QToolButton::clicked, this, [this] {
		if (onOpenSettings)
			onOpenSettings();
	});
	head->addWidget(mark_);
	head->addWidget(title_, 1);
	head->addWidget(gear_);
	root->addLayout(head);

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
	applyAppearance();
	render();
}

QDockWidget *GfxEventsDock::dockFrame() const
{
	for (QWidget *w = parentWidget(); w; w = w->parentWidget())
		if (auto *d = qobject_cast<QDockWidget *>(w))
			return d;
	return nullptr;
}

void GfxEventsDock::setPanel(QWidget *panel)
{
	if (panel == panel_)
		return;
	if (panel_ && panel_->parentWidget() == this) {
		body_->removeWidget(panel_);
		panel_->hide(); // stays our child until the main dock's tabs take it back
	}
	panel_ = panel;
	if (panel_) {
		body_->addWidget(panel_, 1);
		panel_->show();
	}
	render();
}

void GfxEventsDock::setConnected(bool connected)
{
	if (connected == connected_)
		return;
	connected_ = connected;
	render();
}

void GfxEventsDock::setUnread(int n)
{
	if (n == unread_)
		return;
	unread_ = n;
	render();
}

bool GfxEventsDock::showingEvents() const
{
	return panel_ && connected_ && isVisible() && !visibleRegion().isEmpty();
}

void GfxEventsDock::applyAppearance()
{
	theme::applyTo(this);
	mark_->setPixmap(theme::brandMark(22, devicePixelRatioF()));
	render();
}

void GfxEventsDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	if (onShown)
		onShown();
}

void GfxEventsDock::render()
{
	const QString badge = unread_ > 0 ? QStringLiteral(" <span style='color:%1'>· %2 new</span>")
						    .arg(theme::accentColor(), unread_ > 99 ? QStringLiteral("99+")
											    : QString::number(unread_))
					  : QString();
	title_->setText(QStringLiteral("<b>Events</b>") + badge);
	if (QDockWidget *d = dockFrame())
		d->setWindowTitle(
			unread_ > 0 ? QStringLiteral("GoalForgeX Events (%1)")
					      .arg(unread_ > 99 ? QStringLiteral("99+") : QString::number(unread_))
				    : QStringLiteral("GoalForgeX Events"));

	const bool list = panel_ && connected_;
	if (panel_)
		panel_->setVisible(list);
	note_->setVisible(!list);
	if (!connected_)
		note_->setText(QStringLiteral("Connect your GoalForgeX account in the GoalForgeX dock and your gift "
					      "bombs, redemptions, watch streaks, subs and TikTok / YouTube support "
					      "show up here as they happen."));
	else if (!panel_)
		note_->setText(
			QStringLiteral("Events are showing as a tab in the GoalForgeX dock right now.\n\n"
				       "⚙ Settings → Events → “Show Events in their own dock” moves them here."));
}
