/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QLayout>
#include <QList>

class QLayoutItem;

namespace gfx {

// Lays its widgets out left to right and wraps onto the next line when they
// don't fit — chip rows that stay tidy in a narrow dock. Hidden widgets take
// no space.
class FlowLayout : public QLayout {
public:
	explicit FlowLayout(QWidget *parent = nullptr, int hSpacing = 3, int vSpacing = 3);
	~FlowLayout() override;

	void addItem(QLayoutItem *item) override;
	int count() const override;
	QLayoutItem *itemAt(int index) const override;
	QLayoutItem *takeAt(int index) override;
	Qt::Orientations expandingDirections() const override;
	bool hasHeightForWidth() const override;
	int heightForWidth(int width) const override;
	QSize minimumSize() const override;
	QSize sizeHint() const override;
	void setGeometry(const QRect &rect) override;

private:
	int doLayout(const QRect &rect, bool testOnly) const;
	QList<QLayoutItem *> items_;
	int h_;
	int v_;
};

} // namespace gfx
