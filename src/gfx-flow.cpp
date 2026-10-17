/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-flow.hpp"

#include <QWidget>

namespace gfx {

FlowLayout::FlowLayout(QWidget *parent, int hSpacing, int vSpacing) : QLayout(parent), h_(hSpacing), v_(vSpacing)
{
	setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
	while (QLayoutItem *item = takeAt(0))
		delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
	items_.append(item);
}

int FlowLayout::count() const
{
	return static_cast<int>(items_.size());
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
	return index >= 0 && index < items_.size() ? items_.at(index) : nullptr;
}

QLayoutItem *FlowLayout::takeAt(int index)
{
	return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
	return {};
}

bool FlowLayout::hasHeightForWidth() const
{
	return true;
}

int FlowLayout::heightForWidth(int width) const
{
	return doLayout(QRect(0, 0, width, 0), true);
}

QSize FlowLayout::minimumSize() const
{
	// As narrow as the widest single item: everything else wraps.
	QSize size;
	for (QLayoutItem *item : items_)
		if (!item->isEmpty())
			size = size.expandedTo(item->minimumSize());
	const QMargins m = contentsMargins();
	return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

QSize FlowLayout::sizeHint() const
{
	// One line, everything side by side.
	int w = 0, h = 0, n = 0;
	for (QLayoutItem *item : items_) {
		if (item->isEmpty())
			continue;
		const QSize s = item->sizeHint();
		w += s.width() + (n++ ? h_ : 0);
		h = qMax(h, s.height());
	}
	const QMargins m = contentsMargins();
	return QSize(w + m.left() + m.right(), h + m.top() + m.bottom());
}

void FlowLayout::setGeometry(const QRect &rect)
{
	QLayout::setGeometry(rect);
	doLayout(rect, false);
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
	const QMargins m = contentsMargins();
	const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
	int x = r.x(), y = r.y(), lineH = 0;
	for (QLayoutItem *item : items_) {
		if (item->isEmpty())
			continue;
		const QSize s = item->sizeHint().boundedTo(QSize(qMax(1, r.width()), QWIDGETSIZE_MAX));
		if (x > r.x() && x + s.width() > r.right() + 1) {
			x = r.x();
			y += lineH + v_;
			lineH = 0;
		}
		if (!testOnly)
			item->setGeometry(QRect(QPoint(x, y), s));
		x += s.width() + h_;
		lineH = qMax(lineH, s.height());
	}
	return y + lineH - rect.y() + m.bottom();
}

} // namespace gfx
