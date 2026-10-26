/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-theme.hpp"
#include "gfx-settings.hpp"

#include <QApplication>
#include <QColor>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QStringView>
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

// ── Appearance ──────────────────────────────────────────────────────
// The style sheet below is written once in the default "graphite + amber"
// palette. Settings → Appearance re-colours it on the way out instead of
// keeping five copies: every colour literal is classified and remapped —
//   • the amber family (hue ≈ 25–50°, saturated) follows the chosen accent,
//     keeping each shade's lightness, so hover/pressed/soft variants stay
//     consistent for any accent;
//   • the graphite neutrals (low saturation) are re-tinted by the preset;
//   • everything else (ON-AIR red, live green, Twitch purple, Kick green)
//     is left alone — those colours mean something.
// Text size scales every `pt` and adds a base size; Compact shrinks paddings.

namespace {

constexpr double kAmberHue = 39.0 / 360.0; // #ffb020

bool isAmberFamily(const QColor &c)
{
	const double h = c.hslHueF(), s = c.hslSaturationF();
	return h >= 0 && h >= 25.0 / 360.0 && h <= 50.0 / 360.0 && s >= 0.35;
}

bool isNeutral(const QColor &c)
{
	return c.hslSaturationF() < 0.35 || c.lightnessF() < 0.06;
}

struct Preset {
	double hue = -1;     // neutral tint hue (0–1), −1 = keep
	double satMul = 1.0; // neutral saturation
	double satAdd = 0.0;
	double darkMul = 1.0; // lightness of dark surfaces (L < 0.5)
	double darkAdd = 0.0;
};

Preset presetFor(const QString &name)
{
	Preset p;
	if (name == QLatin1String("midnight")) {
		p.hue = 228.0 / 360.0;
		p.satMul = 1.6;
		p.satAdd = 0.10;
		p.darkMul = 0.92;
	} else if (name == QLatin1String("oled")) {
		p.satMul = 0.5;
		p.darkMul = 0.55;
	} else if (name == QLatin1String("slate")) {
		p.hue = 215.0 / 360.0;
		p.satMul = 0.9;
		p.darkMul = 1.22;
		p.darkAdd = 0.035;
	} else if (name == QLatin1String("warm")) {
		p.hue = 28.0 / 360.0;
		p.satMul = 1.3;
		p.satAdd = 0.04;
	}
	return p;
}

QColor remapColor(const QColor &in)
{
	if (!in.isValid())
		return in;
	const gfx::Settings &s = gfx::settings();
	const QColor accent(s.accent);
	QColor c = in;
	if (isAmberFamily(in) && accent.isValid() && accent.name() != QLatin1String("#ffb020")) {
		// Same lightness, accent hue; saturation follows the accent's own.
		const double aSat = accent.hslSaturationF() < 0 ? 0.0 : accent.hslSaturationF();
		const double hue = accent.hslHueF() < 0 ? kAmberHue : accent.hslHueF();
		double sat = in.hslSaturationF() * qBound(0.0, aSat / 1.0, 1.0);
		double light = in.lightnessF();
		// Very light/dark accents: pull the mid-tones toward the accent's own lightness.
		if (light > 0.45 && light < 0.8)
			light = light + (accent.lightnessF() - 0.56) * 0.8;
		c.setHslF(static_cast<float>(hue), static_cast<float>(qBound(0.0, sat, 1.0)),
			  static_cast<float>(qBound(0.0, light, 1.0)), in.alphaF());
		return c;
	}
	if (isNeutral(in) && s.theme != QLatin1String("graphite")) {
		const Preset p = presetFor(s.theme);
		const double l = in.lightnessF();
		double sat = qBound(0.0, in.hslSaturationF() * p.satMul + (l < 0.5 ? p.satAdd : p.satAdd * 0.3), 1.0);
		double hue = p.hue >= 0 ? p.hue : (in.hslHueF() < 0 ? 0.0 : in.hslHueF());
		double light = l < 0.5 ? qBound(0.0, l * p.darkMul + p.darkAdd, 0.5) : l;
		if (in.hslSaturationF() <= 0.0 && l >= 0.98)
			return in; // pure white stays white
		c.setHslF(static_cast<float>(hue), static_cast<float>(sat), static_cast<float>(light), in.alphaF());
		return c;
	}
	return in;
}

QString hex(const QColor &c)
{
	return c.name(QColor::HexRgb);
}

// Primary buttons print dark text on the accent; a dark accent needs white.
QString primaryTextColor()
{
	const QColor a(gfx::settings().accent);
	return a.isValid() && a.lightnessF() < 0.55 ? QStringLiteral("#ffffff") : QStringLiteral("#1b1300");
}

QString applyAppearance(QString qss)
{
	const gfx::Settings &s = gfx::settings();
	// Text on the primary (accent) button — decided before the generic remap.
	qss.replace(QStringLiteral("color: #1b1300;"), QStringLiteral("color: @PRIMARYTEXT@;"));

	// #rrggbb literals
	static const QRegularExpression hexRe(QStringLiteral("#([0-9a-fA-F]{6})\\b"));
	QString out;
	out.reserve(qss.size());
	qsizetype last = 0;
	auto it = hexRe.globalMatch(qss);
	while (it.hasNext()) {
		const QRegularExpressionMatch m = it.next();
		out += QStringView(qss).mid(last, m.capturedStart() - last);
		out += hex(remapColor(QColor(m.captured(0))));
		last = m.capturedEnd();
	}
	out += QStringView(qss).mid(last);
	qss = out;

	// rgba(r, g, b, a) literals (tab-bar scroller)
	static const QRegularExpression rgbaRe(QStringLiteral("rgba\\((\\d+),\\s*(\\d+),\\s*(\\d+),\\s*([0-9.]+)\\)"));
	out.clear();
	last = 0;
	it = rgbaRe.globalMatch(qss);
	while (it.hasNext()) {
		const QRegularExpressionMatch m = it.next();
		out += QStringView(qss).mid(last, m.capturedStart() - last);
		const QColor c =
			remapColor(QColor(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()));
		out += QStringLiteral("rgba(%1, %2, %3, %4)")
			       .arg(c.red())
			       .arg(c.green())
			       .arg(c.blue())
			       .arg(m.captured(4));
		last = m.capturedEnd();
	}
	out += QStringView(qss).mid(last);
	qss = out;
	qss.replace(QStringLiteral("@PRIMARYTEXT@"), primaryTextColor());

	// Text size: every explicit pt size, plus a base size for everything else.
	if (s.textScale != 100) {
		const double f = s.textScale / 100.0;
		static const QRegularExpression ptRe(QStringLiteral("(\\d+(?:\\.\\d+)?)pt\\b"));
		out.clear();
		last = 0;
		it = ptRe.globalMatch(qss);
		while (it.hasNext()) {
			const QRegularExpressionMatch m = it.next();
			out += QStringView(qss).mid(last, m.capturedStart() - last);
			out += QString::number(qMax(6.0, m.captured(1).toDouble() * f), 'f', 1) + QStringLiteral("pt");
			last = m.capturedEnd();
		}
		out += QStringView(qss).mid(last);
		qss = out;
		const double basePt = QApplication::font().pointSizeF() > 0 ? QApplication::font().pointSizeF() : 9.0;
		qss.prepend(QStringLiteral("QWidget { font-size: %1pt; }\n").arg(QString::number(basePt * f, 'f', 1)));
	}

	// Compact: tighter padding everywhere (layout margins stay as they are).
	if (s.compact) {
		static const QRegularExpression padRe(QStringLiteral("padding:([^;}]*)"));
		static const QRegularExpression pxRe(QStringLiteral("(\\d+)px"));
		out.clear();
		last = 0;
		it = padRe.globalMatch(qss);
		while (it.hasNext()) {
			const QRegularExpressionMatch m = it.next();
			out += QStringView(qss).mid(last, m.capturedStart() - last);
			QString vals = m.captured(1);
			QString shrunk;
			qsizetype l2 = 0;
			auto pit = pxRe.globalMatch(vals);
			while (pit.hasNext()) {
				const QRegularExpressionMatch pm = pit.next();
				shrunk += QStringView(vals).mid(l2, pm.capturedStart() - l2);
				shrunk += QString::number(qRound(pm.captured(1).toInt() * 0.6)) + QStringLiteral("px");
				l2 = pm.capturedEnd();
			}
			shrunk += QStringView(vals).mid(l2);
			out += QStringLiteral("padding:") + shrunk;
			last = m.capturedEnd();
		}
		out += QStringView(qss).mid(last);
		qss = out;
	}
	return qss;
}

} // namespace

QString mapColor(const QString &hexColor)
{
	return hex(remapColor(QColor(hexColor)));
}

QString accentColor()
{
	const QColor a(gfx::settings().accent);
	return a.isValid() ? a.name() : QString::fromLatin1(kAmber);
}

void applyTo(QWidget *dockRoot)
{
	if (dockRoot)
		dockRoot->setStyleSheet(dockStyleSheet());
}

QString dockStyleSheet()
{
	return applyAppearance(baseStyleSheet());
}

// Split into several literals: MSVC caps a single string literal at ~16 KB.
QString baseStyleSheet()
{
	QString qss;

	// ── Base ─────────────────────────────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QWidget#GfxDock { background: #101217; color: #e8eaf0; }
QDialog { background: #101217; color: #e8eaf0; }
QLabel { background: transparent; color: #e8eaf0; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
QFrame[frameShape="4"], QFrame[frameShape="5"] { color: #262a33; }

QLabel#gfxMuted, QLabel[gfxRole="muted"] { color: #9aa1b0; }
QLabel#gfxFieldLabel { color: #9aa1b0; font-weight: 600; }
QLabel#gfxSectionTitle { color: #ffffff; font-weight: 700; font-size: 11pt; }
/* Small uppercase heading above a group (Multistream dock, Live cards). */
QLabel#gfxSectionHead { color: #8a91a0; font-weight: 700; font-size: 8pt; }

QToolTip { background: #1f232c; color: #e8eaf0; border: 1px solid #3a4150; padding: 4px 6px; }
)QSS");

	// ── Header ───────────────────────────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QFrame#gfxHeader { background: #14171d; border: 0; border-bottom: 1px solid #20242c; }
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
	background: #1d2129; color: #e8eaf0; border: 1px solid #2b313b; border-radius: 7px; padding: 5px 10px;
}
QPushButton:hover, QToolButton:hover { background: #252a34; border-color: #3a414e; }
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
	background: transparent; color: #7f8796; border: 1px solid transparent; border-radius: 7px;
	padding: 5px 10px; margin: 0 2px 6px 0; font-weight: 600;
}
QTabBar::tab:hover:!selected { color: #d3d7df; background: #181b22; }
QTabBar::tab:selected { color: #ffffff; background: #1f232c; border: 1px solid #2d333e; border-bottom: 2px solid #ffb020; }

/* Tabs that don't fit scroll: a slim see-through panel with chevrons instead of
   the boxed buttons (the generic QToolButton rule above would otherwise apply). */
QTabBar::scroller { width: 44px; }
QTabBar QToolButton {
	background: rgba(18, 20, 25, 0.92); border: 0; border-left: 1px solid rgba(255, 255, 255, 0.08);
	border-radius: 0; padding: 0; margin: 0;
}
QTabBar QToolButton:hover { background: rgba(42, 47, 58, 0.95); }
QTabBar QToolButton:pressed { background: rgba(30, 34, 42, 0.95); }
QTabBar QToolButton:disabled { background: rgba(18, 20, 25, 0.92); }
QTabBar QToolButton::right-arrow { image: url(:/goalforgex/chevron-right.png); width: 11px; height: 11px; }
QTabBar QToolButton::left-arrow { image: url(:/goalforgex/chevron-left.png); width: 11px; height: 11px; }
QTabBar QToolButton::right-arrow:disabled { image: url(:/goalforgex/chevron-right-off.png); }
QTabBar QToolButton::left-arrow:disabled { image: url(:/goalforgex/chevron-left-off.png); }

QGroupBox {
	background: #161920; border: 1px solid #22262f; border-radius: 11px;
	margin-top: 0; padding: 28px 10px 9px 10px; font-weight: 700; color: #c9ced8;
}
QGroupBox::title {
	subcontrol-origin: padding; subcontrol-position: top left; left: 11px; top: 8px; padding: 0;
	color: #aab0bd;
}
/* Live tab cards draw their own title row (fold / unfold) — no built-in title. */
QGroupBox#gfxLiveCard { padding: 0; border-radius: 12px; }
QGroupBox#gfxLiveCard[gfxState="folded"] { background: #13161b; }
QWidget#gfxCardHead { background: transparent; border-radius: 11px; }
QWidget#gfxCardHead:hover { background: #1b1f27; }
QLabel#gfxCardTitle { color: #aab0bd; font-size: 8pt; }
QGroupBox#gfxLiveCard[gfxState="folded"] QLabel#gfxCardTitle { color: #7f8796; }
QLabel#gfxCardChevron { color: #5d6472; font-size: 8pt; }
)QSS");

	// ── Inputs / lists ───────────────────────────────────────────────
	qss += QString::fromUtf8(R"QSS(
QComboBox, QSpinBox, QLineEdit {
	background: #0f1115; color: #e8eaf0; border: 1px solid #272c35; border-radius: 7px;
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

QTreeWidget#gfxEvents {
	background: #0f1115; color: #e8eaf0; border: 1px solid #262a33; border-radius: 10px; padding: 4px; outline: 0;
}
QTreeWidget#gfxEvents::item { padding: 4px 4px; border-radius: 5px; }
QTreeWidget#gfxEvents::item:hover { background: #1a1d24; }
QTreeWidget#gfxEvents::item:selected { background: #2a2212; color: #ffffff; }
QTreeWidget#gfxEvents::branch { background: transparent; }

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
	background: #161920; border: 1px solid #22262f; border-radius: 10px; padding: 7px 11px; color: #c9ced8;
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
/* addSection() headings */
QMenu::section { color: #7f8796; font-size: 8pt; font-weight: 700; padding: 6px 12px 2px 12px; background: transparent; }
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
QPushButton#gfxIconBtn {
	background: transparent; color: #8b93a3; border: 1px solid transparent; border-radius: 6px;
	padding: 3px 5px; min-width: 18px;
}
QPushButton#gfxIconBtn:hover { color: #e8eaf0; background: #1d2129; }
QPushButton#gfxIconBtn:checked { color: #ffffff; background: #3a2a0c; border-color: #ffb020; }

QFrame#gfxStatCard { background: #161920; border: 1px solid #22262f; border-radius: 10px; }
QFrame#gfxStatCard:hover { border-color: #3a4150; }
QFrame#gfxStatCard[gfxState="twitch"] { background: #1a1528; border-color: #4b2f8f; }
QFrame#gfxStatCard[gfxState="kick"] { background: #12200c; border-color: #2c6b12; }
QFrame#gfxStatCard[gfxState="youtube"] { background: #261012; border-color: #8f2430; }
QFrame#gfxStatCard[gfxState="tiktok"] { background: #0b1f24; border-color: #1d7480; }
QLabel#gfxStatSub, QLabel#gfxStatSummary { color: #8b93a3; font-size: 8pt; }

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
QPushButton#gfxTarget[gfxState="youtube"]:checked { color: #ffffff; background: #3a1216; border-color: #ff3048; }
QPushButton#gfxTarget:disabled { color: #3f4552; background: #15171c; border-color: #22262d; }

QLineEdit#gfxChatInput { padding: 6px 10px; border-radius: 8px; background: #0f1115; }
QLineEdit#gfxChatInput:disabled { color: #6b7280; }

QFrame#gfxCard { background: #15181e; border: 1px solid #3a4150; border-radius: 12px; }
QLabel#gfxCardAvatar { background: #1d2129; border-radius: 22px; }
QLabel#gfxCardFacts { color: #c9ced8; }
QTextBrowser#gfxCardRecent {
	background: #0f1115; color: #e8eaf0; border: 1px solid #262a33; border-radius: 8px; font-size: 8pt;
}
QPushButton#gfxCardBtn { padding: 4px 9px; border-radius: 6px; font-size: 8pt; font-weight: 600; }
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

	// Tile: red → accent (amber by default)
	QLinearGradient tile(r.topLeft(), r.bottomRight());
	tile.setColorAt(0.0, QColor(0xff, 0x3b, 0x4f));
	tile.setColorAt(1.0, QColor(accentColor()));
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
