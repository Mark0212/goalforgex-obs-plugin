/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-theme.hpp"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRadialGradient>
#include <QRectF>
#include <QStyle>
#include <QVariant>
#include <QWidget>

namespace gfx::theme {

// Split into several literals: MSVC caps a single string literal at ~16 KB.
QString dockStyleSheet()
{
	QString qss;

	// ── Base ─────────────────────────────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QWidget#GfxDock { background: #121419; color: #e8eaf0; }
QDialog { background: #121419; color: #e8eaf0; }
QLabel { background: transparent; color: #e8eaf0; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
QFrame[frameShape="4"], QFrame[frameShape="5"] { color: #262a33; }

QLabel#gfxMuted, QLabel[gfxRole="muted"] { color: #9aa1b0; }
QLabel#gfxFieldLabel { color: #9aa1b0; font-weight: 600; }
QLabel#gfxSectionTitle { color: #ffffff; font-weight: 700; font-size: 11pt; }

QToolTip { background: #1f232c; color: #e8eaf0; border: 1px solid #3a4150; padding: 4px 6px; }
)QSS");

	// ── Header ───────────────────────────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QFrame#gfxHeader {
	background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #1a1d24, stop:1 #15171c);
	border: 0; border-bottom: 1px solid #262a33;
}
QLabel#gfxBrandName { color: #ffffff; font-weight: 800; font-size: 11pt; }
QLabel#gfxBrandSub { color: #9aa1b0; font-size: 8pt; }

QLabel#gfxAirChip {
	border-radius: 9px; padding: 2px 9px; font-weight: 800; font-size: 8pt;
	color: #8b93a3; background: #1c1f26; border: 1px solid #2c313c;
}
QLabel#gfxAirChip[gfxState="onair"] { color: #ffffff; background: #ff3b4f; border: 1px solid #ff6b7a; }
QLabel#gfxAirChip[gfxState="rec"] { color: #ffd27a; background: #2b2110; border: 1px solid #6b4d14; }

QFrame#gfxUpdateBar {
	background: #251d0c; border: 1px solid #5c4413; border-radius: 8px;
}
QFrame#gfxUpdateBar QLabel { color: #ffd27a; }

QLabel#gfxMsg { color: #9aa1b0; padding: 2px 2px; }
QLabel#gfxMsg[gfxState="error"] {
	color: #ff8a96; background: #2a1519; border: 1px solid #5a2730; border-radius: 6px; padding: 6px 8px;
}

QLabel#gfxFooter { color: #6b7280; font-size: 8pt; }
QLabel#gfxFooter[gfxState="ok"] { color: #5fbf95; }
QLabel#gfxFooter[gfxState="warn"] { color: #ffb020; }
)QSS");

	// ── Welcome / linking / required panels ──────────────────────────
	qss += QString::fromUtf8(R"QSS(
QFrame#gfxWelcome, QFrame#gfxLinkPanel, QFrame#gfxRequired {
	background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1a1d24, stop:1 #15171c);
	border: 1px solid #262a33; border-radius: 12px;
}
QLabel#gfxHero { color: #ffffff; font-size: 14pt; font-weight: 800; }
QLabel#gfxFeatures { color: #c9ced8; }
QLabel#gfxCode {
	color: #ffb020; background: #0c0e12; border: 1px dashed #6b4d14; border-radius: 10px; padding: 10px 6px;
}
QPushButton#gfxBigBtn { padding: 9px 16px; font-size: 10pt; }
)QSS");

	// ── Buttons ──────────────────────────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QPushButton, QToolButton {
	background: #1f232c; color: #e8eaf0; border: 1px solid #2f3541; border-radius: 6px; padding: 5px 10px;
}
QPushButton:hover, QToolButton:hover { background: #272c37; border-color: #3d4452; }
QPushButton:pressed, QToolButton:pressed { background: #1a1d24; }
QPushButton:disabled, QToolButton:disabled { color: #5b6270; background: #171a20; border-color: #23272f; }
QPushButton:focus, QToolButton:focus { border-color: #8a6a1c; }
QToolButton::menu-indicator { image: none; width: 0; }

QPushButton[gfxRole="primary"], QToolButton[gfxRole="primary"] {
	color: #1b1300; font-weight: 700; border: 1px solid #ffc04d;
	background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffc24a, stop:1 #ffa412);
}
QPushButton[gfxRole="primary"]:hover, QToolButton[gfxRole="primary"]:hover {
	background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffcd66, stop:1 #ffb020);
}
QPushButton[gfxRole="primary"]:pressed { background: #f09a0c; }
QPushButton[gfxRole="primary"]:disabled { color: #6b5a33; background: #2a2416; border-color: #3a321d; }

QPushButton[gfxRole="danger"], QToolButton[gfxRole="danger"] { color: #ff8a96; background: #24161a; border-color: #4a242b; }
QPushButton[gfxRole="danger"]:hover, QToolButton[gfxRole="danger"]:hover { color: #ffffff; background: #ff3b4f; border-color: #ff6b7a; }

QPushButton[gfxRole="ghost"], QToolButton[gfxRole="ghost"] { background: transparent; border-color: transparent; color: #9aa1b0; }
QPushButton[gfxRole="ghost"]:hover, QToolButton[gfxRole="ghost"]:hover { background: #20242d; color: #ffffff; border-color: #2f3541; }

QPushButton[gfxRole="soft"], QToolButton[gfxRole="soft"] { color: #ffd27a; background: #221c10; border-color: #4a3a17; }
QPushButton[gfxRole="soft"]:hover, QToolButton[gfxRole="soft"]:hover { background: #2e2512; border-color: #6b5320; }

QPushButton[gfxRole="link"] {
	background: transparent; border: 0; padding: 2px 0; text-align: left; color: #ffb020; font-weight: 600;
}
QPushButton[gfxRole="link"]:hover { color: #ffcd66; }
)QSS");

	// ── Tabs / cards ─────────────────────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QTabWidget::pane { border: 0; top: -1px; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab {
	background: transparent; color: #8b93a3; border: 0; border-bottom: 2px solid transparent;
	padding: 7px 11px; margin-right: 2px; font-weight: 600;
}
QTabBar::tab:hover:!selected { color: #d3d7df; border-bottom-color: #3a4150; }
QTabBar::tab:selected { color: #ffffff; border-bottom: 2px solid #ffb020; }

QGroupBox {
	background: #181b22; border: 1px solid #262a33; border-radius: 10px;
	margin-top: 16px; padding: 10px 8px 8px 8px; font-weight: 700; color: #c9ced8;
}
QGroupBox::title {
	subcontrol-origin: margin; subcontrol-position: top left; left: 10px; top: 1px; padding: 0 4px;
	color: #9aa1b0;
}
)QSS");

	// ── Inputs / lists ───────────────────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QComboBox, QSpinBox, QLineEdit {
	background: #1a1d24; color: #e8eaf0; border: 1px solid #2f3541; border-radius: 6px;
	padding: 4px 8px; min-height: 18px; selection-background-color: #6b4d14;
}
QComboBox:hover, QSpinBox:hover, QLineEdit:hover { border-color: #3d4452; }
QComboBox:focus, QSpinBox:focus, QLineEdit:focus { border-color: #ffb020; }
QComboBox::drop-down { border: 0; width: 20px; }
QComboBox::down-arrow {
	image: none; width: 0; height: 0;
	border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #9aa1b0;
}
QComboBox QAbstractItemView {
	background: #181b22; color: #e8eaf0; border: 1px solid #2f3541; outline: 0;
	selection-background-color: #3a2c10; selection-color: #ffffff;
}
QSpinBox::up-button, QSpinBox::down-button { border: 0; background: transparent; width: 16px; }
QSpinBox::up-arrow {
	image: none; width: 0; height: 0;
	border-left: 4px solid transparent; border-right: 4px solid transparent; border-bottom: 5px solid #9aa1b0;
}
QSpinBox::down-arrow {
	image: none; width: 0; height: 0;
	border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #9aa1b0;
}

QListWidget {
	background: #15181e; color: #e8eaf0; border: 1px solid #262a33; border-radius: 8px; padding: 4px; outline: 0;
}
QListWidget::item { padding: 6px 8px; border-radius: 6px; border: 1px solid transparent; }
QListWidget::item:hover:!selected { background: #1d2129; }
QListWidget::item:selected { background: #2a2212; color: #ffffff; border: 1px solid #6b4d14; }
QListWidget#gfxActivity::item { padding: 4px 6px; color: #c9ced8; }

QProgressBar { background: #232731; border: 0; border-radius: 3px; }
QProgressBar::chunk {
	border-radius: 3px;
	background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffb020, stop:1 #ff3b4f);
}

QCheckBox { spacing: 8px; color: #e8eaf0; }
QCheckBox::indicator { width: 15px; height: 15px; border-radius: 4px; border: 1px solid #3a4150; background: #1a1d24; }
QCheckBox::indicator:hover { border-color: #ffb020; }
QCheckBox::indicator:checked { background: #ffb020; border-color: #ffb020; image: none; }
)QSS");

	// ── Live tab / menus / scrollbars ────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QFrame#gfxHealthRow { background: #181b22; border: 1px solid #262a33; border-left: 3px solid #3a4150; border-radius: 8px; }
QFrame#gfxHealthRow[gfxState="ok"] { border-left-color: #34d399; }
QFrame#gfxHealthRow[gfxState="warn"] { border-left-color: #ffb020; background: #1c1912; }

QLabel#gfxStats {
	background: #181b22; border: 1px solid #262a33; border-radius: 8px; padding: 6px 10px; color: #c9ced8;
}
QLabel#gfxBigTime { color: #ffffff; }
QLabel#gfxMidTime { color: #e8eaf0; }
QLabel#value { color: #ffb020; }

QLabel#gfxStatusChip {
	border-radius: 8px; padding: 1px 8px; font-size: 8pt; font-weight: 700;
	color: #8b93a3; background: #1f232c; border: 1px solid #2f3541;
}
QLabel#gfxStatusChip[gfxState="running"] { color: #0b1f17; background: #34d399; border-color: #5fe0b0; }
QLabel#gfxStatusChip[gfxState="paused"] { color: #ffd27a; background: #2b2110; border-color: #6b4d14; }

QMenu { background: #181b22; color: #e8eaf0; border: 1px solid #2f3541; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 6px 22px 6px 12px; border-radius: 5px; background: transparent; }
QMenu::item:selected { background: #2a2212; color: #ffffff; }
QMenu::item:disabled { color: #5b6270; }
QMenu::separator { height: 1px; background: #2a2f3a; margin: 4px 6px; }

QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 2px; }
QScrollBar::handle:vertical { background: #2c313c; border-radius: 3px; min-height: 26px; }
QScrollBar::handle:horizontal { background: #2c313c; border-radius: 3px; min-width: 26px; }
QScrollBar::handle:hover { background: #3b4250; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QDialogButtonBox QPushButton { min-width: 72px; }
)QSS");

	// ── Chat tab ─────────────────────────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QPushButton#gfxSeg {
	background: transparent; color: #8b93a3; border: 1px solid transparent; border-radius: 6px;
	padding: 3px 10px; font-weight: 600;
}
QPushButton#gfxSeg:hover { color: #e8eaf0; background: #1d2129; }
QPushButton#gfxSeg:checked { color: #ffffff; background: #262b36; border-color: #3a4150; }
QLabel#gfxChatStatus { font-size: 8pt; }

QTextBrowser#gfxChatView {
	background: #0f1115; color: #e8eaf0; border: 1px solid #262a33; border-radius: 10px;
	selection-background-color: #6b4d14;
}

QPushButton#gfxTarget {
	background: #1a1d24; color: #6b7280; border: 1px solid #2f3541; border-radius: 6px;
	padding: 5px 9px; font-weight: 700; font-size: 8pt;
}
QPushButton#gfxTarget:hover { color: #c9ced8; border-color: #3d4452; }
QPushButton#gfxTarget[gfxState="twitch"]:checked { color: #ffffff; background: #2a1f4d; border-color: #9146ff; }
QPushButton#gfxTarget[gfxState="kick"]:checked { color: #d9ffc7; background: #16300a; border-color: #53fc18; }
QPushButton#gfxTarget:disabled { color: #3f4552; background: #15171c; border-color: #22262d; }

QLineEdit#gfxChatInput { padding: 6px 10px; border-radius: 8px; background: #0f1115; }
QLineEdit#gfxChatInput:disabled { color: #6b7280; }
)QSS");

	return qss;
}

QPixmap brandMark(int px, qreal devicePixelRatio)
{
	const qreal dpr = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
	QPixmap pm(qRound(px * dpr), qRound(px * dpr));
	pm.setDevicePixelRatio(dpr);
	pm.fill(Qt::transparent);

	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing, true);
	const QRectF r(0.5, 0.5, px - 1.0, px - 1.0);
	const qreal radius = px * 0.26;

	// Tile: red → amber
	QLinearGradient tile(r.topLeft(), r.bottomRight());
	tile.setColorAt(0.0, QColor(0xff, 0x3b, 0x4f));
	tile.setColorAt(1.0, QColor(0xff, 0xb0, 0x20));
	QPainterPath path;
	path.addRoundedRect(r, radius, radius);
	p.fillPath(path, tile);

	// Soft top highlight
	QRadialGradient shine(QPointF(px * 0.3, px * 0.15), px * 0.75);
	shine.setColorAt(0.0, QColor(255, 255, 255, 70));
	shine.setColorAt(1.0, QColor(255, 255, 255, 0));
	p.fillPath(path, shine);

	// Broadcast ring + centre dot
	const QPointF c(px / 2.0, px / 2.0);
	QPen ring(QColor(255, 255, 255, 240));
	ring.setWidthF(qMax(1.5, px * 0.09));
	p.setPen(ring);
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(c, px * 0.26, px * 0.26);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255));
	p.drawEllipse(c, px * 0.105, px * 0.105);

	p.end();
	return pm;
}

static void repolish(QWidget *w)
{
	if (!w)
		return;
	w->style()->unpolish(w);
	w->style()->polish(w);
	w->update();
}

void setRole(QWidget *w, const char *role)
{
	if (!w)
		return;
	w->setProperty("gfxRole", QString::fromLatin1(role));
	repolish(w);
}

void setState(QWidget *w, const char *state)
{
	if (!w)
		return;
	const QString s = QString::fromLatin1(state);
	if (w->property("gfxState").toString() == s)
		return; // unchanged — skip the re-polish (called every second for the air chip)
	w->setProperty("gfxState", s);
	repolish(w);
}

} // namespace gfx::theme
