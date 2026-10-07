/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QPixmap>
#include <QString>

class QWidget;

// "Broadcast Pro" — the dock's own look: graphite surfaces, an amber accent
// for actions and a red ON-AIR accent for anything live.
//
// The whole look is one style sheet set on the dock root. A widget's style
// sheet cascades to its children only (dialogs and menus the dock opens
// included), so nothing here ever restyles the rest of OBS.
//
// Widgets opt into variants with a dynamic property, e.g.
//   theme::setRole(button, "primary")      → QPushButton[gfxRole="primary"]
//   theme::setState(chip, "onair")         → QLabel[gfxState="onair"]
namespace gfx::theme {

// Palette (also used for the occasional rich-text label).
inline constexpr const char *kAmber = "#ffb020";
inline constexpr const char *kRed = "#ff3b4f";
inline constexpr const char *kGreen = "#34d399";
inline constexpr const char *kText = "#e8eaf0";
inline constexpr const char *kMuted = "#9aa1b0";

QString dockStyleSheet();

// The GoalForgeX mark: a red→amber tile with a broadcast ring, drawn in code
// (crisp at any size / DPI, no image resources to ship).
QPixmap brandMark(int px, qreal devicePixelRatio);

// Set a variant property and re-polish so the style sheet picks it up.
void setRole(QWidget *w, const char *role);
void setState(QWidget *w, const char *state);

} // namespace gfx::theme
