/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-settings-dialog.hpp"
#include "gfx-notice.hpp"
#include "gfx-theme.hpp"

#include <plugin-support.h>

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QIcon>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSlider>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidgetAction>

#include <utility>

using gfx::Settings;

namespace {

Settings &S()
{
	return gfx::settings();
}

QString col(const char *hex)
{
	return gfx::theme::mapColor(QString::fromLatin1(hex));
}

QString rgba(const QColor &c, int alpha)
{
	return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

qreal basePt()
{
	const qreal app = QApplication::font().pointSizeF() > 0 ? QApplication::font().pointSizeF() : 9.0;
	return app * S().textScale / 100.0;
}

// ── A sliding on / off switch ──────────────────────────────────────────
class Toggle : public QAbstractButton {
public:
	explicit Toggle(QWidget *parent) : QAbstractButton(parent)
	{
		setCheckable(true);
		setCursor(Qt::PointingHandCursor);
		setFocusPolicy(Qt::StrongFocus);
		setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
		anim_ = new QVariantAnimation(this);
		anim_->setDuration(160);
		anim_->setEasingCurve(QEasingCurve::OutCubic);
		connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
			pos_ = v.toReal();
			update();
		});
		connect(this, &QAbstractButton::toggled, this, [this](bool on) {
			anim_->stop();
			anim_->setStartValue(pos_);
			anim_->setEndValue(on ? 1.0 : 0.0);
			anim_->start();
		});
	}
	// Without the animation or the toggled signal (reset, first fill).
	void setNow(bool on)
	{
		const QSignalBlocker block(this);
		setChecked(on);
		anim_->stop();
		pos_ = on ? 1.0 : 0.0;
		update();
	}
	QSize sizeHint() const override { return QSize(42, 24); }

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
		const QColor off(col("#2b303c")), on(gfx::theme::accentColor());
		const auto mixF = [this](float a, float b) {
			return static_cast<float>(a + (b - a) * pos_);
		};
		QColor track = off;
		track.setRedF(mixF(off.redF(), on.redF()));
		track.setGreenF(mixF(off.greenF(), on.greenF()));
		track.setBlueF(mixF(off.blueF(), on.blueF()));
		if (!isEnabled())
			track.setAlpha(110);
		p.setPen(Qt::NoPen);
		p.setBrush(track);
		p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
		if (hasFocus()) {
			p.setPen(QPen(QColor(255, 255, 255, 90), 1.2));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), r.height() / 2, r.height() / 2);
		}
		const qreal d = r.height() - 6;
		const qreal x = r.left() + 3 + (r.width() - d - 6) * pos_;
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 60));
		p.drawEllipse(QRectF(x, r.top() + 4, d, d));
		p.setBrush(QColor(255, 255, 255));
		p.drawEllipse(QRectF(x, r.top() + 3, d, d));
	}

private:
	QVariantAnimation *anim_ = nullptr;
	qreal pos_ = 0;
};

// A settings line; clicking anywhere on it can flip its switch.
class RowFrame : public QFrame {
public:
	explicit RowFrame(QWidget *parent) : QFrame(parent) {}
	std::function<void()> onClick;

protected:
	void mousePressEvent(QMouseEvent *e) override
	{
		if (onClick && e->button() == Qt::LeftButton)
			e->accept();
		else
			QFrame::mousePressEvent(e);
	}
	void mouseReleaseEvent(QMouseEvent *e) override
	{
		if (onClick && e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) {
			onClick();
			e->accept();
			return;
		}
		QFrame::mouseReleaseEvent(e);
	}
};

// ── Theme card: a tiny drawn dock in that theme's colours ──────────────
struct ThemeColors {
	QColor bg, panel, card, line, text, muted;
};

ThemeColors colorsFor(const QString &themeId)
{
	Settings &s = S();
	const QString keep = s.theme;
	s.theme = themeId;
	ThemeColors c{QColor(col("#0e1014")), QColor(col("#15181e")), QColor(col("#1b1f27")),
		      QColor(col("#2a2f3a")), QColor(col("#e8eaf0")), QColor(col("#9aa1b0"))};
	s.theme = keep;
	return c;
}

class ThemeCard : public QAbstractButton {
public:
	ThemeCard(const gfx::theme::ThemeInfo &themeInfo, QWidget *parent) : QAbstractButton(parent), info_(themeInfo)
	{
		setCheckable(true);
		setCursor(Qt::PointingHandCursor);
		setToolTip(themeInfo.name + QStringLiteral(" — ") + themeInfo.blurb);
		setMinimumSize(132, 104);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	}
	const gfx::theme::ThemeInfo &info() const { return info_; }
	QSize sizeHint() const override { return QSize(150, 108); }

protected:
	void enterEvent(QEnterEvent *e) override
	{
		hover_ = true;
		update();
		QAbstractButton::enterEvent(e);
	}
	void leaveEvent(QEvent *e) override
	{
		hover_ = false;
		update();
		QAbstractButton::leaveEvent(e);
	}
	void paintEvent(QPaintEvent *) override
	{
		const ThemeColors c = colorsFor(info_.id);
		const QColor accent(gfx::theme::accentColor());
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		const QRectF outer = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
		// frame
		p.setPen(QPen(isChecked() ? accent : (hover_ ? c.muted : c.line), isChecked() ? 2.4 : 1.2));
		p.setBrush(c.bg);
		p.drawRoundedRect(outer, 12, 12);
		// mock: sidebar + cards + chat lines
		const QRectF mock = outer.adjusted(8, 8, -8, -30);
		QPainterPath clip;
		clip.addRoundedRect(mock, 7, 7);
		p.save();
		p.setClipPath(clip);
		p.fillRect(mock, c.panel);
		const QRectF side(mock.left(), mock.top(), mock.width() * 0.26, mock.height());
		p.fillRect(side, c.bg);
		p.setPen(Qt::NoPen);
		p.setBrush(accent);
		p.drawRoundedRect(QRectF(side.left() + 4, side.top() + 6, side.width() - 8, 5), 2, 2);
		p.setBrush(c.line);
		for (int i = 1; i < 4; ++i)
			p.drawRoundedRect(QRectF(side.left() + 4, side.top() + 6 + i * 9, side.width() - 10, 4), 2, 2);
		const QRectF body(side.right() + 6, mock.top() + 6, mock.right() - side.right() - 12,
				  mock.height() - 12);
		p.setBrush(c.card);
		p.drawRoundedRect(body, 5, 5);
		const QColor names[] = {QColor(QStringLiteral("#ff5d73")), QColor(QStringLiteral("#34d399")),
					QColor(QStringLiteral("#f472b6"))};
		for (int i = 0; i < 3; ++i) {
			const qreal y = body.top() + 6 + i * 10;
			p.setBrush(names[i]);
			p.drawRoundedRect(QRectF(body.left() + 6, y, 14, 4), 2, 2);
			p.setBrush(c.muted);
			p.drawRoundedRect(QRectF(body.left() + 24, y, body.width() * (0.62 - i * 0.12), 4), 2, 2);
		}
		p.setBrush(accent);
		p.drawRoundedRect(QRectF(body.right() - 26, body.bottom() - 10, 20, 6), 3, 3);
		p.restore();
		// label
		QFont f = font();
		f.setBold(true);
		p.setFont(f);
		p.setPen(c.text);
		const QRectF label(outer.left() + 10, outer.bottom() - 26, outer.width() - 20, 22);
		p.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, info_.name);
		if (isChecked()) {
			const QRectF tick(outer.right() - 26, outer.bottom() - 23, 16, 16);
			p.setPen(Qt::NoPen);
			p.setBrush(accent);
			p.drawEllipse(tick);
			p.setPen(QPen(accent.lightnessF() < 0.55 ? Qt::white : QColor(QStringLiteral("#1b1300")), 2));
			p.drawLine(QPointF(tick.left() + 4, tick.center().y()),
				   QPointF(tick.center().x() - 1, tick.bottom() - 4.5));
			p.drawLine(QPointF(tick.center().x() - 1, tick.bottom() - 4.5),
				   QPointF(tick.right() - 3.5, tick.top() + 4.5));
		}
	}

private:
	gfx::theme::ThemeInfo info_;
	bool hover_ = false;
};

// A swatch as an icon (colour buttons), or a checkered "Auto".
QIcon swatchIcon(const QString &color, int px, qreal dpr)
{
	QPixmap pm(QSize(px, px) * dpr);
	pm.setDevicePixelRatio(dpr);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r(1, 1, px - 2, px - 2);
	if (color.isEmpty()) {
		QPainterPath path;
		path.addEllipse(r);
		p.setClipPath(path);
		const QColor a(QStringLiteral("#ff5d73")), b(QStringLiteral("#34d399")), c(QStringLiteral("#7aa2ff")),
			d(QStringLiteral("#ffb020"));
		p.fillRect(QRectF(0, 0, px / 2.0, px / 2.0), a);
		p.fillRect(QRectF(px / 2.0, 0, px / 2.0, px / 2.0), b);
		p.fillRect(QRectF(0, px / 2.0, px / 2.0, px / 2.0), c);
		p.fillRect(QRectF(px / 2.0, px / 2.0, px / 2.0, px / 2.0), d);
		p.setClipping(false);
	} else {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(color));
		p.drawEllipse(r);
	}
	p.setPen(QPen(QColor(255, 255, 255, 140), 1.2));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(r);
	return QIcon(pm);
}

// Badge pill like the feed's (rounded, tinted fill, coloured text).
QImage badgeImage(const QString &text, const QColor &color, int heightPx, qreal dpr)
{
	QFont f = QApplication::font();
	f.setBold(true);
	f.setPixelSize(qMax(7, qRound(heightPx * 0.58)));
	const QFontMetricsF fm(f);
	const int w = qRound(fm.horizontalAdvance(text) + heightPx * 0.9);
	QImage img(QSize(w, heightPx) * dpr, QImage::Format_ARGB32_Premultiplied);
	img.setDevicePixelRatio(dpr);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	QColor fill = color;
	fill.setAlpha(56);
	p.setPen(QPen(color, 1));
	p.setBrush(fill);
	p.drawRoundedRect(QRectF(0.5, 0.5, w - 1, heightPx - 1), heightPx * 0.32, heightPx * 0.32);
	p.setFont(f);
	p.setPen(color.lighter(130));
	p.drawText(QRectF(0, 0, w, heightPx), Qt::AlignCenter, text);
	return img;
}

// Stand-in emotes for the preview, drawn so they scale crisply.
QImage emoteImage(int kind, int px, qreal dpr)
{
	QImage img(QSize(px, px) * dpr, QImage::Format_ARGB32_Premultiplied);
	img.setDevicePixelRatio(dpr);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r(px * 0.06, px * 0.06, px * 0.88, px * 0.88);
	if (kind == 1) { // heart
		QPainterPath h;
		const qreal w = r.width();
		h.moveTo(r.center().x(), r.bottom() - w * 0.08);
		h.cubicTo(r.left() - w * 0.1, r.top() + w * 0.55, r.left() + w * 0.12, r.top() - w * 0.05,
			  r.center().x(), r.top() + w * 0.26);
		h.cubicTo(r.right() - w * 0.12, r.top() - w * 0.05, r.right() + w * 0.1, r.top() + w * 0.55,
			  r.center().x(), r.bottom() - w * 0.08);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(QStringLiteral("#ff4d7d")));
		p.drawPath(h);
		return img;
	}
	QRadialGradient g(r.center() - QPointF(r.width() * 0.15, r.height() * 0.18), r.width() * 0.75);
	g.setColorAt(0, QColor(QStringLiteral("#ffe27a")));
	g.setColorAt(1, QColor(QStringLiteral("#f5a623")));
	p.setPen(Qt::NoPen);
	p.setBrush(g);
	p.drawEllipse(r);
	p.setBrush(QColor(QStringLiteral("#3b2a10")));
	const qreal e = r.width() * 0.11;
	p.drawEllipse(QRectF(r.left() + r.width() * 0.3 - e / 2, r.top() + r.height() * 0.36, e, e * 1.3));
	p.drawEllipse(QRectF(r.left() + r.width() * 0.7 - e / 2, r.top() + r.height() * 0.36, e, e * 1.3));
	p.setPen(QPen(QColor(QStringLiteral("#3b2a10")), qMax(1.0, r.width() * 0.07), Qt::SolidLine, Qt::RoundCap));
	p.setBrush(Qt::NoBrush);
	p.drawArc(QRectF(r.left() + r.width() * 0.25, r.top() + r.height() * 0.3, r.width() * 0.5, r.height() * 0.45),
		  200 * 16, 140 * 16);
	return img;
}

// Text sizes in the style sheet follow Settings → Appearance → Text size.
QString scalePt(QString qss)
{
	const double f = S().textScale / 100.0;
	if (qAbs(f - 1.0) < 0.001)
		return qss;
	static const QRegularExpression ptRe(QStringLiteral("(\\d+(?:\\.\\d+)?)pt\\b"));
	QString out;
	qsizetype last = 0;
	auto it = ptRe.globalMatch(qss);
	while (it.hasNext()) {
		const QRegularExpressionMatch m = it.next();
		out += QStringView(qss).mid(last, m.capturedStart() - last);
		out += QString::number(qMax(6.0, m.captured(1).toDouble() * f), 'f', 1) + QStringLiteral("pt");
		last = m.capturedEnd();
	}
	out += QStringView(qss).mid(last);
	return out;
}

const char *kSheet = R"QSS(
QDialog#gfxSettings { background: @BG@; }
QFrame#gfxSetHero { border: none; border-bottom: 1px solid @LINE@;
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @ACC40@, stop:0.45 @ACC10@, stop:1 @BG@); }
QLabel#gfxSetHeroTitle { color: @TEXT@; font-size: 18pt; font-weight: 800; background: transparent; }
QLabel#gfxSetHeroSub { color: @MUTED@; font-size: 9pt; background: transparent; }
QLabel#gfxSetLive { color: @ACC@; background: @ACC20@; border: 1px solid @ACC50@; border-radius: 10px;
  padding: 3px 10px; font-size: 8pt; font-weight: 800; }
QFrame#gfxSetSide { background: @PANEL@; border: none; border-right: 1px solid @LINE@; }
QLineEdit#gfxSetSearch { background: @CARD@; color: @TEXT@; border: 1px solid @LINE@; border-radius: 10px;
  padding: 7px 10px; font-size: 9.5pt; }
QLineEdit#gfxSetSearch:focus { border: 1px solid @ACC@; }
QListWidget#gfxSetNav { background: transparent; border: none; outline: 0; font-size: 10pt; }
QListWidget#gfxSetNav::item { color: @MUTED@; padding: 8px 10px; margin: 1px 0; border-radius: 9px; border: none; }
QListWidget#gfxSetNav::item:hover { background: @HOVER@; color: @TEXT@; }
QListWidget#gfxSetNav::item:selected { background: @ACC20@; color: @TEXT@; font-weight: 700; }
QListWidget#gfxSetNav::item:disabled { color: @FAINT@; }
QLabel#gfxSetSideFoot { color: @FAINT@; font-size: 8pt; background: transparent; }
QScrollArea#gfxSetScroll { background: @BG@; border: none; }
QWidget#gfxSetPageInner { background: @BG@; }
QLabel#gfxSetPageIcon { font-size: 22pt; background: transparent; }
QLabel#gfxSetPageTitle { color: @TEXT@; font-size: 16pt; font-weight: 800; background: transparent; }
QLabel#gfxSetPageBlurb { color: @MUTED@; font-size: 9pt; background: transparent; }
QLabel#gfxSetEmpty { color: @MUTED@; font-size: 9.5pt; padding: 30px; background: transparent; }
QFrame#gfxSetCard { background: @CARD@; border: 1px solid @LINE@; border-radius: 14px; }
QLabel#gfxSetCardIcon { background: @ACC20@; border-radius: 10px; font-size: 13pt; }
QLabel#gfxSetCardTitle { color: @TEXT@; font-size: 10.5pt; font-weight: 800; background: transparent; }
QLabel#gfxSetCardSub { color: @MUTED@; font-size: 8.5pt; background: transparent; }
QFrame#gfxSetRow { background: transparent; border: none; border-radius: 10px; }
QFrame#gfxSetRow:hover { background: @HOVER@; }
QLabel#gfxSetRowTitle { color: @TEXT@; font-size: 9.5pt; font-weight: 600; background: transparent; }
QLabel#gfxSetRowHint { color: @MUTED@; font-size: 8.5pt; background: transparent; }
QLabel#gfxSetNote { color: @MUTED@; font-size: 8.5pt; background: @HOVER@; border-radius: 10px; padding: 8px 10px; }
QLabel#gfxSetValue { color: @ACC@; background: @ACC20@; border-radius: 9px; padding: 2px 10px;
  font-size: 9pt; font-weight: 800; }
QSlider#gfxSetSlider { min-height: 22px; background: transparent; }
QSlider#gfxSetSlider::groove:horizontal { height: 6px; background: @LINE@; border-radius: 3px; }
QSlider#gfxSetSlider::sub-page:horizontal { border-radius: 3px;
  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @ACC50@, stop:1 @ACC@); }
QSlider#gfxSetSlider::handle:horizontal { background: #ffffff; border: 3px solid @ACC@; width: 12px; height: 12px;
  margin: -6px 0; border-radius: 9px; }
QSlider#gfxSetSlider::handle:horizontal:hover { border: 3px solid #ffffff; background: @ACC@; }
QToolButton#gfxSetMini { color: @MUTED@; background: transparent; border: none; border-radius: 8px; padding: 2px 6px; }
QToolButton#gfxSetMini:hover { color: @TEXT@; background: @HOVER@; }
QPushButton#gfxSetSeg { color: @MUTED@; background: @PANEL@; border: 1px solid @LINE@; border-radius: 9px;
  padding: 5px 12px; font-weight: 600; }
QPushButton#gfxSetSeg:hover { color: @TEXT@; }
QPushButton#gfxSetSeg:checked { color: @TEXT@; background: @ACC20@; border: 1px solid @ACC@; }
QToolButton#gfxSetColorBtn { color: @TEXT@; background: @PANEL@; border: 1px solid @LINE@; border-radius: 10px;
  padding: 4px 9px; font-weight: 600; }
QToolButton#gfxSetColorBtn:hover { border: 1px solid @ACC@; }
QToolButton#gfxSetColorBtn::menu-indicator { image: none; width: 0; }
QPushButton#gfxSetSwatch { border-radius: 11px; min-width: 22px; max-width: 22px; min-height: 22px; max-height: 22px;
  padding: 0; }
QTextBrowser#gfxSetPreview { background: @PREVIEW@; border: 1px solid @LINE@; border-radius: 12px; }
QComboBox#gfxSetCombo { background: @PANEL@; color: @TEXT@; border: 1px solid @LINE@; border-radius: 9px;
  padding: 5px 10px; min-width: 150px; }
QFrame#gfxSetFoot { background: @PANEL@; border: none; border-top: 1px solid @LINE@; }
QLabel#gfxSetFootNote { color: @FAINT@; font-size: 8.5pt; background: transparent; }
)QSS";

} // namespace

// ═══════════════════════════════════════════════════════════════════════

QStringList GfxSettingsDialog::pageTitles()
{
	return {QStringLiteral("General"),    QStringLiteral("Layout"),     QStringLiteral("MultiChat"),
		QStringLiteral("Chat style"), QStringLiteral("Notices"),    QStringLiteral("Who's here"),
		QStringLiteral("Events"),     QStringLiteral("Appearance"), QStringLiteral("About")};
}

GfxSettingsDialog::GfxSettingsDialog(QWidget *parent, Options options)
	: QDialog(parent),
	  options_(std::move(options)),
	  before_(gfx::settings())
{
	setObjectName(QStringLiteral("gfxSettings"));
	setWindowTitle(QStringLiteral("GoalForgeX settings"));
	setMinimumSize(760, 560);
	resize(920, 660);

	previewTimer_ = new QTimer(this);
	previewTimer_->setSingleShot(true);
	previewTimer_->setInterval(140);
	connect(previewTimer_, &QTimer::timeout, this, [this] {
		if (options_.preview)
			options_.preview();
	});

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	// ── Hero ──
	hero_ = new QFrame(this);
	hero_->setObjectName(QStringLiteral("gfxSetHero"));
	auto *hl = new QHBoxLayout(hero_);
	hl->setContentsMargins(20, 16, 20, 16);
	hl->setSpacing(14);
	heroMark_ = new QLabel(hero_);
	heroMark_->setFixedSize(46, 46);
	heroMark_->setPixmap(gfx::theme::brandMark(46, devicePixelRatioF()));
	auto *ht = new QVBoxLayout();
	ht->setSpacing(1);
	auto *title = new QLabel(QStringLiteral("Settings"), hero_);
	title->setObjectName(QStringLiteral("gfxSetHeroTitle"));
	const QString build = QString::fromUtf8(PLUGIN_BUILD);
	auto *sub = new QLabel(QStringLiteral("GoalForgeX for OBS %1%2  ·  everything here previews live")
				       .arg(QString::fromLatin1(PLUGIN_VERSION),
					    build.toInt() > 0 ? QStringLiteral(" (build %1)").arg(build) : QString()),
			       hero_);
	sub->setObjectName(QStringLiteral("gfxSetHeroSub"));
	ht->addWidget(title);
	ht->addWidget(sub);
	auto *live = new QLabel(QStringLiteral("●  LIVE PREVIEW"), hero_);
	live->setObjectName(QStringLiteral("gfxSetLive"));
	hl->addWidget(heroMark_);
	hl->addLayout(ht, 1);
	hl->addWidget(live, 0, Qt::AlignVCenter);
	root->addWidget(hero_);

	// ── Sidebar + pages ──
	auto *mid = new QHBoxLayout();
	mid->setContentsMargins(0, 0, 0, 0);
	mid->setSpacing(0);
	auto *side = new QFrame(this);
	side->setObjectName(QStringLiteral("gfxSetSide"));
	side->setFixedWidth(206);
	auto *sl = new QVBoxLayout(side);
	sl->setContentsMargins(12, 12, 12, 10);
	sl->setSpacing(10);
	search_ = new QLineEdit(side);
	search_->setObjectName(QStringLiteral("gfxSetSearch"));
	search_->setPlaceholderText(QStringLiteral("🔍  Search settings…"));
	search_->setClearButtonEnabled(true);
	nav_ = new QListWidget(side);
	nav_->setObjectName(QStringLiteral("gfxSetNav"));
	nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	nav_->setFocusPolicy(Qt::NoFocus);
	nav_->setCursor(Qt::PointingHandCursor);
	auto *sideFoot = new QLabel(QStringLiteral("Ctrl+F to search · Esc to cancel"), side);
	sideFoot->setObjectName(QStringLiteral("gfxSetSideFoot"));
	sl->addWidget(search_);
	sl->addWidget(nav_, 1);
	sl->addWidget(sideFoot);
	stack_ = new QStackedWidget(this);
	mid->addWidget(side);
	mid->addWidget(stack_, 1);
	root->addLayout(mid, 1);

	// ── Footer ──
	auto *foot = new QFrame(this);
	foot->setObjectName(QStringLiteral("gfxSetFoot"));
	auto *fl = new QHBoxLayout(foot);
	fl->setContentsMargins(16, 10, 16, 10);
	fl->setSpacing(8);
	auto *resetBtn = new QPushButton(QStringLiteral("↺  Reset this page"), foot);
	gfx::theme::setRole(resetBtn, "ghost");
	resetBtn->setCursor(Qt::PointingHandCursor);
	footNote_ =
		new QLabel(QStringLiteral("Changes show on your docks right away · Cancel puts everything back"), foot);
	footNote_->setObjectName(QStringLiteral("gfxSetFootNote"));
	auto *cancel = new QPushButton(QStringLiteral("Cancel"), foot);
	cancel->setCursor(Qt::PointingHandCursor);
	auto *save = new QPushButton(QStringLiteral("Save changes"), foot);
	gfx::theme::setRole(save, "primary");
	save->setDefault(true);
	save->setCursor(Qt::PointingHandCursor);
	fl->addWidget(resetBtn);
	fl->addSpacing(6);
	fl->addWidget(footNote_, 1);
	fl->addWidget(cancel);
	fl->addWidget(save);
	root->addWidget(foot);
	connect(resetBtn, &QPushButton::clicked, this, [this] { resetCurrentPage(); });
	connect(cancel, &QPushButton::clicked, this, [this] { reject(); });
	connect(save, &QPushButton::clicked, this, [this] { accept(); });

	buildPages();

	connect(nav_, &QListWidget::currentRowChanged, this, [this](int r) {
		if (r >= 0)
			stack_->setCurrentIndex(r);
	});
	connect(search_, &QLineEdit::textChanged, this, [this](const QString &t) { applySearch(t); });
	auto *find = new QShortcut(QKeySequence::Find, this);
	connect(find, &QShortcut::activated, this, [this] {
		search_->setFocus();
		search_->selectAll();
	});

	int start = 0;
	const QStringList titles = pageTitles();
	for (int i = 0; i < titles.size(); ++i)
		if (titles.at(i).compare(options_.startPage, Qt::CaseInsensitive) == 0)
			start = i;
	selectPage(start);
	restyle();
	renderPreview();
}

void GfxSettingsDialog::reject()
{
	previewTimer_->stop();
	gfx::settings() = before_;
	if (options_.preview)
		options_.preview();
	QDialog::reject();
}

void GfxSettingsDialog::selectPage(int index)
{
	if (index < 0 || index >= nav_->count())
		return;
	nav_->setCurrentRow(index);
	stack_->setCurrentIndex(index);
}

// ── Building blocks ──────────────────────────────────────────────────────

QVBoxLayout *GfxSettingsDialog::addPage(const QString &icon, const QString &title, const QString &blurb)
{
	auto *scroll = new QScrollArea(stack_);
	scroll->setObjectName(QStringLiteral("gfxSetScroll"));
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *inner = new QWidget(scroll);
	inner->setObjectName(QStringLiteral("gfxSetPageInner"));
	auto *l = new QVBoxLayout(inner);
	l->setContentsMargins(22, 18, 22, 22);
	l->setSpacing(14);
	auto *head = new QHBoxLayout();
	head->setSpacing(12);
	auto *ic = new QLabel(icon, inner);
	ic->setObjectName(QStringLiteral("gfxSetPageIcon"));
	auto *tl = new QVBoxLayout();
	tl->setSpacing(1);
	auto *t = new QLabel(title, inner);
	t->setObjectName(QStringLiteral("gfxSetPageTitle"));
	auto *b = new QLabel(blurb, inner);
	b->setObjectName(QStringLiteral("gfxSetPageBlurb"));
	b->setWordWrap(true);
	tl->addWidget(t);
	tl->addWidget(b);
	head->addWidget(ic, 0, Qt::AlignTop);
	head->addLayout(tl, 1);
	l->addLayout(head);
	auto *empty = new QLabel(QStringLiteral("Nothing on this page matches your search."), inner);
	empty->setObjectName(QStringLiteral("gfxSetEmpty"));
	empty->setAlignment(Qt::AlignCenter);
	empty->hide();
	l->addWidget(empty);
	emptyNotes_ << empty;
	scroll->setWidget(inner);
	stack_->addWidget(scroll);
	auto *item = new QListWidgetItem(icon + QStringLiteral("   ") + title, nav_);
	item->setData(Qt::UserRole, icon + QStringLiteral("   ") + title);
	item->setSizeHint(QSize(0, 36));
	buildingPage_ = stack_->count() - 1;
	return l;
}

QVBoxLayout *GfxSettingsDialog::addCard(QVBoxLayout *page, const QString &icon, const QString &title,
					const QString &sub)
{
	auto *card = new QFrame(page->parentWidget());
	card->setObjectName(QStringLiteral("gfxSetCard"));
	auto *cl = new QVBoxLayout(card);
	cl->setContentsMargins(14, 12, 14, 12);
	cl->setSpacing(4);
	auto *head = new QHBoxLayout();
	head->setSpacing(10);
	auto *ic = new QLabel(icon, card);
	ic->setObjectName(QStringLiteral("gfxSetCardIcon"));
	ic->setFixedSize(32, 32);
	ic->setAlignment(Qt::AlignCenter);
	auto *tl = new QVBoxLayout();
	tl->setSpacing(0);
	auto *t = new QLabel(title, card);
	t->setObjectName(QStringLiteral("gfxSetCardTitle"));
	tl->addWidget(t);
	if (!sub.isEmpty()) {
		auto *s = new QLabel(sub, card);
		s->setObjectName(QStringLiteral("gfxSetCardSub"));
		s->setWordWrap(true);
		tl->addWidget(s);
	}
	head->addWidget(ic, 0, Qt::AlignTop);
	head->addLayout(tl, 1);
	cl->addLayout(head);
	cl->addSpacing(4);
	page->addWidget(card);
	cards_.push_back({buildingPage_, card});
	buildingCard_ = card;
	card->setProperty("gfxSearch", (title + QLatin1Char(' ') + sub).toLower());
	return cl;
}

QWidget *GfxSettingsDialog::rowFrame(QVBoxLayout *card, const QString &title, const QString &hint, QWidget *control,
				     bool controlBelow)
{
	auto *row = new RowFrame(card->parentWidget());
	row->setObjectName(QStringLiteral("gfxSetRow"));
	auto *outer = new QVBoxLayout(row);
	outer->setContentsMargins(8, 6, 8, 6);
	outer->setSpacing(6);
	auto *line = new QHBoxLayout();
	line->setSpacing(12);
	auto *texts = new QVBoxLayout();
	texts->setSpacing(1);
	auto *t = new QLabel(title, row);
	t->setObjectName(QStringLiteral("gfxSetRowTitle"));
	t->setWordWrap(true);
	texts->addWidget(t);
	if (!hint.isEmpty()) {
		auto *h = new QLabel(hint, row);
		h->setObjectName(QStringLiteral("gfxSetRowHint"));
		h->setWordWrap(true);
		texts->addWidget(h);
	}
	line->addLayout(texts, 1);
	if (control && !controlBelow) {
		control->setParent(row);
		line->addWidget(control, 0, Qt::AlignVCenter);
	}
	outer->addLayout(line);
	if (control && controlBelow) {
		control->setParent(row);
		outer->addWidget(control);
	}
	card->addWidget(row);
	return row;
}

void GfxSettingsDialog::addRow(QWidget *row, const QString &title, const QString &hint, std::function<void()> reset)
{
	Row r;
	r.page = buildingPage_;
	r.widget = row;
	r.card = buildingCard_;
	const QString cardText = buildingCard_ ? buildingCard_->property("gfxSearch").toString() : QString();
	r.hay = (title + QLatin1Char(' ') + hint + QLatin1Char(' ') + cardText).toLower();
	r.reset = std::move(reset);
	rows_.push_back(r);
}

void GfxSettingsDialog::addToggle(QVBoxLayout *card, const QString &title, const QString &hint, bool Settings::*member,
				  bool indent)
{
	auto *tg = new Toggle(nullptr);
	tg->setNow(S().*member);
	QWidget *row = rowFrame(card, title, hint, tg);
	if (indent)
		row->setContentsMargins(22, 0, 0, 0);
	connect(tg, &QAbstractButton::toggled, this, [this, member](bool on) {
		S().*member = on;
		changed();
	});
	// The whole row is the click target.
	row->setCursor(Qt::PointingHandCursor);
	static_cast<RowFrame *>(row)->onClick = [tg] {
		tg->toggle();
	};
	syncs_.push_back([tg, member] { tg->setNow(S().*member); });
	addRow(row, title, hint, [tg, member] {
		const Settings d;
		tg->setNow(d.*member);
		S().*member = d.*member;
	});
}

void GfxSettingsDialog::addSlider(QVBoxLayout *card, const QString &title, const QString &hint, int lo, int hi,
				  int step, std::function<int()> get, std::function<void(int)> set,
				  std::function<QString(int)> fmt, int defaultValue)
{
	auto *box = new QWidget(nullptr);
	auto *bl = new QHBoxLayout(box);
	bl->setContentsMargins(0, 0, 0, 0);
	bl->setSpacing(10);
	auto *slider = new QSlider(Qt::Horizontal, box);
	slider->setObjectName(QStringLiteral("gfxSetSlider"));
	slider->setRange(lo, hi);
	slider->setSingleStep(step);
	slider->setPageStep(step * 4);
	slider->setCursor(Qt::PointingHandCursor);
	slider->setValue(qBound(lo, get(), hi));
	auto *value = new QLabel(fmt(slider->value()), box);
	value->setObjectName(QStringLiteral("gfxSetValue"));
	value->setMinimumWidth(64);
	value->setAlignment(Qt::AlignCenter);
	auto *undo = new QToolButton(box);
	undo->setObjectName(QStringLiteral("gfxSetMini"));
	undo->setText(QStringLiteral("↺"));
	undo->setToolTip(QStringLiteral("Back to %1").arg(fmt(defaultValue)));
	undo->setCursor(Qt::PointingHandCursor);
	bl->addWidget(slider, 1);
	bl->addWidget(value);
	bl->addWidget(undo);
	const auto setValue = [slider, value, fmt, set, step, lo](int v) {
		const int snapped = lo + qRound((v - lo) / static_cast<double>(step)) * step;
		value->setText(fmt(snapped));
		set(snapped);
		if (slider->value() != snapped) {
			const QSignalBlocker block(slider);
			slider->setValue(snapped);
		}
	};
	connect(slider, &QSlider::valueChanged, this, [this, setValue](int v) {
		setValue(v);
		changed();
	});
	connect(undo, &QToolButton::clicked, this, [this, slider, defaultValue] {
		slider->setValue(defaultValue);
		changed();
	});
	QWidget *row = rowFrame(card, title, hint, box, true);
	syncs_.push_back([slider, value, fmt, get] {
		const QSignalBlocker block(slider);
		slider->setValue(get());
		value->setText(fmt(get()));
	});
	addRow(row, title, hint, [slider, value, fmt, set, defaultValue] {
		const QSignalBlocker block(slider);
		slider->setValue(defaultValue);
		value->setText(fmt(defaultValue));
		set(defaultValue);
	});
}

void GfxSettingsDialog::addSlider(QVBoxLayout *card, const QString &title, const QString &hint, int Settings::*member,
				  int lo, int hi, int step, std::function<QString(int)> fmt)
{
	const Settings d;
	addSlider(
		card, title, hint, lo, hi, step, [member] { return S().*member; }, [member](int v) { S().*member = v; },
		std::move(fmt), d.*member);
}

void GfxSettingsDialog::addColor(QVBoxLayout *card, const QString &title, const QString &hint,
				 QString Settings::*member, const QString &badge, const QString &badgeColor,
				 const QString &sampleName)
{
	auto *box = new QWidget(nullptr);
	auto *bl = new QHBoxLayout(box);
	bl->setContentsMargins(0, 0, 0, 0);
	bl->setSpacing(10);
	auto *sample = new QLabel(box);
	sample->setTextFormat(Qt::RichText);
	sample->setMinimumWidth(130);
	auto *btn = new QToolButton(box);
	btn->setObjectName(QStringLiteral("gfxSetColorBtn"));
	btn->setPopupMode(QToolButton::InstantPopup);
	btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	btn->setCursor(Qt::PointingHandCursor);
	btn->setIconSize(QSize(16, 16));
	bl->addWidget(sample);
	bl->addWidget(btn);
	const qreal dpr = devicePixelRatioF();
	const auto paint = [btn, sample, member, badge, badgeColor, sampleName, dpr] {
		const QString v = S().*member;
		btn->setIcon(swatchIcon(v, 16, dpr));
		btn->setText(v.isEmpty() ? QStringLiteral("Automatic  ▾") : v.toUpper() + QStringLiteral("  ▾"));
		const QString shown = v.isEmpty() ? QStringLiteral("#c9ced8") : v;
		QString html;
		if (!badge.isEmpty())
			html += QStringLiteral(
					"<span style='color:%1;font-weight:800;font-size:7.5pt'>%2</span>&nbsp; ")
					.arg(badgeColor, badge);
		html += QStringLiteral("<span style='color:%1;font-weight:700'>%2</span>").arg(shown, sampleName);
		sample->setText(html);
	};
	paint();
	auto *menu = new QMenu(btn);
	auto *grid = new QWidget(menu);
	auto *gl = new QGridLayout(grid);
	gl->setContentsMargins(10, 10, 10, 6);
	gl->setSpacing(6);
	static const char *kSwatches[] = {"#ff5d73", "#ff8a3d", "#ffb020", "#facc15", "#a3e635", "#34d399",
					  "#22d3ee", "#7aa2ff", "#8b5cf6", "#c084fc", "#f472b6", "#ffffff",
					  "#e8eaf0", "#9aa1b0", "#ff3b4f", "#00c853"};
	int i = 0;
	for (const char *hex : kSwatches) {
		auto *sw = new QPushButton(grid);
		sw->setObjectName(QStringLiteral("gfxSetSwatch"));
		sw->setCursor(Qt::PointingHandCursor);
		sw->setToolTip(QString::fromLatin1(hex).toUpper());
		sw->setStyleSheet(
			QStringLiteral("QPushButton { background: %1; border: 2px solid rgba(255,255,255,60); }"
				       "QPushButton:hover { border: 2px solid #ffffff; }")
				.arg(QString::fromLatin1(hex)));
		gl->addWidget(sw, i / 8, i % 8);
		connect(sw, &QPushButton::clicked, this, [this, menu, member, paint, hex] {
			S().*member = QString::fromLatin1(hex);
			paint();
			menu->close();
			changed();
		});
		++i;
	}
	auto *wa = new QWidgetAction(menu);
	wa->setDefaultWidget(grid);
	menu->addAction(wa);
	menu->addSeparator();
	connect(menu->addAction(QStringLiteral("🎨   Custom colour…")), &QAction::triggered, this,
		[this, member, paint] {
			const QString cur = S().*member;
			const QColor c = QColorDialog::getColor(QColor(cur.isEmpty() ? QStringLiteral("#7aa2ff") : cur),
								this, QStringLiteral("Pick a colour"));
			if (!c.isValid())
				return;
			S().*member = c.name().toLower();
			paint();
			changed();
		});
	connect(menu->addAction(QStringLiteral("✨   Automatic (chatter's own colour)")), &QAction::triggered, this,
		[this, member, paint] {
			S().*member = QString();
			paint();
			changed();
		});
	btn->setMenu(menu);
	QWidget *row = rowFrame(card, title, hint, box);
	syncs_.push_back(paint);
	addRow(row, title, hint, [member, paint] {
		S().*member = Settings().*member;
		paint();
	});
}

void GfxSettingsDialog::addChoice(QVBoxLayout *card, const QString &title, const QString &hint,
				  QString Settings::*member, const QList<QPair<QString, QString>> &choices)
{
	auto *box = new QWidget(nullptr);
	auto *bl = new QHBoxLayout(box);
	bl->setContentsMargins(0, 0, 0, 0);
	bl->setSpacing(6);
	auto *group = new QButtonGroup(box);
	group->setExclusive(true);
	QList<QPushButton *> btns;
	for (const auto &c : choices) {
		auto *b = new QPushButton(c.second, box);
		b->setObjectName(QStringLiteral("gfxSetSeg"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		b->setProperty("gfxValue", c.first);
		b->setChecked(S().*member == c.first);
		group->addButton(b);
		bl->addWidget(b);
		btns << b;
		connect(b, &QPushButton::clicked, this, [this, member, b] {
			S().*member = b->property("gfxValue").toString();
			changed();
		});
	}
	bl->addStretch(1);
	const auto sync = [btns, member] {
		for (QPushButton *b : btns)
			b->setChecked(b->property("gfxValue").toString() == S().*member);
	};
	QWidget *row = rowFrame(card, title, hint, box, true);
	syncs_.push_back(sync);
	QString hay = hint;
	for (const auto &c : choices)
		hay += QLatin1Char(' ') + c.second;
	addRow(row, title, hay, [member, sync] {
		S().*member = Settings().*member;
		sync();
	});
}

void GfxSettingsDialog::addIntChoice(QVBoxLayout *card, const QString &title, const QString &hint,
				     int Settings::*member, const QList<QPair<int, QString>> &choices)
{
	auto *combo = new QComboBox(nullptr);
	combo->setObjectName(QStringLiteral("gfxSetCombo"));
	combo->setCursor(Qt::PointingHandCursor);
	for (const auto &c : choices)
		combo->addItem(c.second, c.first);
	combo->setCurrentIndex(qMax(0, combo->findData(S().*member)));
	connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, member](int) {
		S().*member = combo->currentData().toInt();
		changed();
	});
	QWidget *row = rowFrame(card, title, hint, combo);
	const auto sync = [combo, member] {
		const QSignalBlocker block(combo);
		combo->setCurrentIndex(qMax(0, combo->findData(S().*member)));
	};
	syncs_.push_back(sync);
	addRow(row, title, hint, [member, sync] {
		S().*member = Settings().*member;
		sync();
	});
}

void GfxSettingsDialog::addNote(QVBoxLayout *card, const QString &text)
{
	auto *l = new QLabel(text, card->parentWidget());
	l->setObjectName(QStringLiteral("gfxSetNote"));
	l->setWordWrap(true);
	l->setTextFormat(Qt::PlainText);
	card->addWidget(l);
	addRow(l, text, QString(), nullptr);
}

QWidget *GfxSettingsDialog::buildThemeGrid(QWidget *parent)
{
	auto *w = new QWidget(parent);
	auto *gl = new QGridLayout(w);
	gl->setContentsMargins(0, 0, 0, 0);
	gl->setSpacing(10);
	auto *group = new QButtonGroup(w);
	group->setExclusive(true);
	QList<ThemeCard *> all;
	int i = 0;
	for (const gfx::theme::ThemeInfo &t : gfx::theme::themes()) {
		auto *card = new ThemeCard(t, w);
		card->setChecked(S().theme == t.id);
		group->addButton(card);
		gl->addWidget(card, i / 3, i % 3);
		all << card;
		connect(card, &QAbstractButton::clicked, this, [this, card, group] {
			Settings &s = S();
			// Still on the old theme's own accent (or the default)? Take the new theme's.
			QString oldSuggested;
			for (const gfx::theme::ThemeInfo &t2 : gfx::theme::themes())
				if (t2.id == s.theme)
					oldSuggested = t2.accent;
			if (s.accent == QLatin1String("#ffb020") || s.accent == oldSuggested)
				s.accent = card->info().accent;
			s.theme = card->info().id;
			for (QAbstractButton *b : group->buttons())
				b->update();
			changed();
		});
		++i;
	}
	syncs_.push_back([all] {
		for (ThemeCard *c : all) {
			c->setChecked(c->info().id == S().theme);
			c->update();
		}
	});
	return w;
}

QWidget *GfxSettingsDialog::buildAccentRow(QWidget *parent)
{
	auto *w = new QWidget(parent);
	auto *l = new QHBoxLayout(w);
	l->setContentsMargins(0, 0, 0, 0);
	l->setSpacing(7);
	static const char *kAccents[] = {"#ffb020", "#f97316", "#ef4444", "#ec4899", "#c084fc",
					 "#8b5cf6", "#3b82f6", "#22d3ee", "#06b6d4", "#22c55e"};
	QList<QPushButton *> btns;
	const auto paintAll = [](const QList<QPushButton *> &list) {
		for (QPushButton *b : list) {
			const QString c = b->property("gfxColor").toString();
			const bool on = c == S().accent;
			b->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: %2; }"
							"QPushButton:hover { border: 3px solid #ffffff; }")
						 .arg(c, on ? QStringLiteral("3px solid #ffffff")
							    : QStringLiteral("2px solid rgba(255,255,255,40)")));
		}
	};
	for (const char *hex : kAccents) {
		auto *b = new QPushButton(w);
		b->setObjectName(QStringLiteral("gfxSetSwatch"));
		b->setCursor(Qt::PointingHandCursor);
		b->setProperty("gfxColor", QString::fromLatin1(hex));
		b->setToolTip(QString::fromLatin1(hex).toUpper());
		l->addWidget(b);
		btns << b;
	}
	auto *custom = new QToolButton(w);
	custom->setObjectName(QStringLiteral("gfxSetColorBtn"));
	custom->setText(QStringLiteral("🎨 Custom"));
	custom->setCursor(Qt::PointingHandCursor);
	l->addWidget(custom);
	l->addStretch(1);
	for (QPushButton *b : btns)
		connect(b, &QPushButton::clicked, this, [this, b, btns, paintAll] {
			S().accent = b->property("gfxColor").toString();
			paintAll(btns);
			changed();
		});
	connect(custom, &QToolButton::clicked, this, [this, btns, paintAll] {
		const QColor c = QColorDialog::getColor(QColor(S().accent), this, QStringLiteral("Accent colour"));
		if (!c.isValid())
			return;
		S().accent = c.name().toLower();
		paintAll(btns);
		changed();
	});
	paintAll(btns);
	syncs_.push_back([btns, paintAll] { paintAll(btns); });
	return w;
}

// ── The pages ────────────────────────────────────────────────────────────

void GfxSettingsDialog::buildPages()
{
	using St = Settings;
	const auto pct = [](int v) {
		return QStringLiteral("%1%").arg(v);
	};

	// ── General ──
	QVBoxLayout *gen = addPage(QStringLiteral("🏠"), QStringLiteral("General"),
				   QStringLiteral("Automation and updates for GoalForgeX in this OBS."));
	QVBoxLayout *c = addCard(gen, QStringLiteral("⚡"), QStringLiteral("Automation"),
				 QStringLiteral("Things GoalForgeX does for you when the stream starts or stops."));
	addToggle(c, QStringLiteral("Start the subathon timer when I go live"),
		  QStringLiteral("Starts it, or resumes it if it was paused."), &St::autoStartTimer);
	addToggle(c, QStringLiteral("Pause the subathon timer when I stop streaming"), QString(), &St::autoPauseTimer);
	c = addCard(gen, QStringLiteral("⬆"), QStringLiteral("Updates"));
	addToggle(c, QStringLiteral("Tell me when a new version is out"), QString(), &St::checkUpdates);
	addToggle(c, QStringLiteral("Download updates and install them when I close OBS"),
		  QStringLiteral("You never have to stop a stream to update."), &St::autoInstallUpdates);
	addNote(c, QStringLiteral("Hotkeys for timers, goal, wheel, counters and alerts are in OBS → Settings → "
				  "Hotkeys (search “GoalForgeX”)."));
	gen->addStretch(1);

	// ── Layout ──
	QVBoxLayout *lay = addPage(QStringLiteral("🧩"), QStringLiteral("Layout"),
				   QStringLiteral("Pick which tabs and Live cards the GoalForgeX dock shows."));
	c = addCard(lay, QStringLiteral("🗂"), QStringLiteral("Tabs"));
	addToggle(c, QStringLiteral("Live"), QStringLiteral("Timers, goal, wheel, counters, presets and alerts"),
		  &St::tabLive);
	addToggle(c, QStringLiteral("Events tab"),
		  QStringLiteral("Only used when Events aren't in their own dock (see Events)"), &St::tabEvents);
	addToggle(c, QStringLiteral("Widgets"), QStringLiteral("Add your overlays to scenes"), &St::tabWidgets);
	addToggle(c, QStringLiteral("Reactions"), QStringLiteral("Scene changes when things happen"),
		  &St::tabReactions);
	addToggle(c, QStringLiteral("Health"), QStringLiteral("Checks and one-click fixes for your sources"),
		  &St::tabHealth);
	c = addCard(lay, QStringLiteral("🃏"), QStringLiteral("Live tab cards"),
		    QStringLiteral("Tip: click a card's title on the Live tab to fold it away."));
	addToggle(c, QStringLiteral("Stream stats line"), QString(), &St::cardStats);
	addToggle(c, QStringLiteral("Subathon timer"), QString(), &St::cardTimer);
	addToggle(c, QStringLiteral("Intro timer"), QString(), &St::cardActionTimer);
	addToggle(c, QStringLiteral("Goal"), QString(), &St::cardGoal);
	addToggle(c, QStringLiteral("Spin wheel"), QString(), &St::cardWheel);
	addToggle(c, QStringLiteral("Counters"), QString(), &St::cardCounters);
	addToggle(c, QStringLiteral("Presets"), QStringLiteral("Switch saved goal looks"), &St::cardPresets);
	addToggle(c, QStringLiteral("Alerts"), QStringLiteral("Test / replay / clear chat"), &St::cardAlerts);
	addToggle(c, QStringLiteral("Activity feed"), QString(), &St::cardActivity);
	if (!S().liveCollapsed.isEmpty()) {
		auto *unfold = new QPushButton(QStringLiteral("Unfold every Live card"), nullptr);
		gfx::theme::setRole(unfold, "ghost");
		unfold->setCursor(Qt::PointingHandCursor);
		QWidget *row = rowFrame(
			c, QStringLiteral("Folded cards"),
			QStringLiteral("%1 card(s) are folded on the Live tab").arg(S().liveCollapsed.size()), unfold);
		addRow(row, QStringLiteral("Folded cards unfold"), QString(), nullptr);
		connect(unfold, &QPushButton::clicked, this, [this, unfold] {
			S().liveCollapsed.clear();
			unfold->setEnabled(false);
			unfold->setText(QStringLiteral("Unfolded ✓"));
			changed();
		});
	}
	lay->addStretch(1);

	// ── MultiChat ──
	QVBoxLayout *mc = addPage(QStringLiteral("💬"), QStringLiteral("MultiChat"),
				  QStringLiteral("Twitch, Kick, YouTube, TikTok and Xive chat in one dock."));
	c = addCard(mc, QStringLiteral("🪟"), QStringLiteral("Dock"),
		    QStringLiteral("Docks menu → GoalForgeX MultiChat shows it again if you closed it."));
	addToggle(c, QStringLiteral("MultiChat on"), QStringLiteral("Its own “GoalForgeX MultiChat” dock"),
		  &St::tabChat);
	addToggle(c, QStringLiteral("Online / offline for each platform"),
		  QStringLiteral("A slim row above the chat, with viewers"), &St::chatStats);
	addToggle(c, QStringLiteral("…as big cards"),
		  QStringLiteral("Uptime, category and chat pace (takes chat room)"), &St::chatStatsCards, true);
	c = addCard(mc, QStringLiteral("🌐"), QStringLiteral("Platforms"),
		    QStringLiteral(
			    "Twitch and Kick are always included. Others show once connected on goalforgex.com → "
			    "Account."));
	addToggle(c, QStringLiteral("YouTube chat"), QStringLiteral("Read and reply, while you're live there"),
		  &St::chatYouTube);
	addToggle(c, QStringLiteral("TikTok LIVE chat"), QStringLiteral("Read-only, while you're live there"),
		  &St::chatTikTok);
	addToggle(c, QStringLiteral("TikTok likes"),
		  QStringLiteral("One slim line above the chat: total likes, likes a minute and who's tapping"),
		  &St::chatTikTokLikes, true);
	addToggle(c, QStringLiteral("Xive chat"),
		  QStringLiteral("Read, and reply as the GoalForgeX bot (add the bot to your Xive channel first)"),
		  &St::chatXive);
	c = addCard(mc, QStringLiteral("🧠"), QStringLiteral("Behaviour"));
	addToggle(c, QStringLiteral("Highlight mentions"),
		  QStringLiteral("Messages that mention you or a highlight word (⋯ → Highlight words)"),
		  &St::chatHighlightMentions);
	addToggle(c, QStringLiteral("Quick moderation buttons"),
		  QStringLiteral("🗑 delete / ⏱ 10 min timeout on each line"), &St::chatQuickMod);
	addToggle(c, QStringLiteral("Keep deleted messages"), QStringLiteral("Struck through instead of hidden"),
		  &St::chatShowDeleted);
	addToggle(c, QStringLiteral("Pause while my mouse is over the chat"), QString(), &St::chatPauseOnHover);
	mc->addStretch(1);

	// ── Chat style (sliders, colours, live preview) ──
	QVBoxLayout *cs =
		addPage(QStringLiteral("🎨"), QStringLiteral("Chat style"),
			QStringLiteral("Sizes and colours for every message. The preview updates as you drag."));
	c = addCard(cs, QStringLiteral("👀"), QStringLiteral("Preview"));
	preview_ = new QTextBrowser(c->parentWidget());
	preview_->setObjectName(QStringLiteral("gfxSetPreview"));
	preview_->setOpenLinks(false);
	preview_->setFocusPolicy(Qt::NoFocus);
	preview_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	preview_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	preview_->setMinimumHeight(190);
	preview_->document()->setDocumentMargin(10);
	c->addWidget(preview_);
	addRow(preview_, QStringLiteral("preview"), QString(), nullptr);

	c = addCard(cs, QStringLiteral("🧱"), QStringLiteral("Layout"),
		    QStringLiteral("How every message sits in the chat."));
	addChoice(c, QStringLiteral("Message layout"),
		  QStringLiteral("Cozy gives every line room to breathe · Bubbles puts each message on its own card"),
		  &St::chatLayout,
		  {{QStringLiteral("classic"), QStringLiteral("Classic")},
		   {QStringLiteral("cozy"), QStringLiteral("Cozy")},
		   {QStringLiteral("bubbles"), QStringLiteral("Bubbles")}});

	c = addCard(cs, QStringLiteral("🔠"), QStringLiteral("Sizes"));
	addSlider(
		c, QStringLiteral("Text size"), QStringLiteral("Also Ctrl + scroll over the chat"), 6, 24, 1,
		[] { return S().chatFontSize > 0 ? S().chatFontSize : 6; },
		[](int v) { S().chatFontSize = v <= 6 ? 0 : v; },
		[](int v) { return v <= 6 ? QStringLiteral("Auto") : QStringLiteral("%1 pt").arg(v); }, 6);
	addSlider(c, QStringLiteral("Emote size"), QStringLiteral("Twitch, Kick, BTTV, FFZ and 7TV emotes"),
		  &St::chatEmoteScale, 50, 250, 10, pct);
	addSlider(c, QStringLiteral("Badge size"), QStringLiteral("HOST / MOD / VIP / SUB"), &St::chatBadgeScale, 60,
		  180, 10, pct);
	addSlider(c, QStringLiteral("Space between messages"), QString(), &St::chatSpacing, 0, 16, 1,
		  [](int v) { return QStringLiteral("%1 px").arg(v); });

	c = addCard(cs, QStringLiteral("🌈"), QStringLiteral("Name colours"),
		    QStringLiteral("Automatic = the colour each chatter picked. Someone with several roles gets the "
				   "first one: broadcaster, mod, VIP, sub."));
	addColor(c, QStringLiteral("Broadcaster"), QString(), &St::colorHost, QStringLiteral("HOST"),
		 QStringLiteral("#ff3b4f"), QStringLiteral("YourChannel"));
	addColor(c, QStringLiteral("Moderators"), QString(), &St::colorMod, QStringLiteral("MOD"),
		 QStringLiteral("#34d399"), QStringLiteral("ModSquad"));
	addColor(c, QStringLiteral("VIPs"), QString(), &St::colorVip, QStringLiteral("VIP"), QStringLiteral("#f472b6"),
		 QStringLiteral("VeryImportant"));
	addColor(c, QStringLiteral("Subscribers / members"), QString(), &St::colorSub, QStringLiteral("SUB"),
		 QStringLiteral("#ffb020"), QStringLiteral("LoyalSub"));
	addColor(c, QStringLiteral("Viewers"), QStringLiteral("Everyone without a role"), &St::colorViewer, QString(),
		 QString(), QStringLiteral("JustWatching"));
	addColor(c, QStringLiteral("Message text"), QStringLiteral("The words themselves"), &St::chatTextColor,
		 QString(), QString(), QStringLiteral("hello chat!"));
	addToggle(c, QStringLiteral("Use chatters' own colours for Automatic"),
		  QStringLiteral("Off = one calm colour for every Automatic name"), &St::chatNameColors);

	c = addCard(cs, QStringLiteral("✨"), QStringLiteral("Look"));
	addToggle(c, QStringLiteral("Platform icon on each message"), QString(), &St::chatIcons);
	addToggle(c, QStringLiteral("HOST / MOD / VIP / SUB badges"), QString(), &St::chatBadges);
	addToggle(c, QStringLiteral("GoalForgeX STAFF badge"), QStringLiteral("So you know it's really us"),
		  &St::chatStaffBadge);
	addToggle(c, QStringLiteral("Emotes as images"), QStringLiteral("Off = their names as text"), &St::chatEmotes);
	addToggle(c, QStringLiteral("Time next to each message"), QString(), &St::chatTimestamps);
	addToggle(c, QStringLiteral("Shade every other message"), QString(), &St::chatAltRows);
	addToggle(c, QStringLiteral("Compact lines"), QString(), &St::chatCompact);
	cs->addStretch(1);

	// ── Notices ──
	QVBoxLayout *nc =
		addPage(QStringLiteral("🔔"), QStringLiteral("Notices"),
			QStringLiteral("Subs, gift bombs, raids and more as styled cards between messages. Only "
				       "in this dock — nothing is posted to your chat."));
	c = addCard(nc, QStringLiteral("🎛"), QStringLiteral("Style"));
	addToggle(c, QStringLiteral("Show event notices in MultiChat"), QString(), &St::chatNotices);
	addToggle(c, QStringLiteral("Show Twitch's pinned bar"),
		  QStringLiteral("The message you or a mod pinned, the Hype Train, polls and predictions — above "
				 "MultiChat, like at the top of Twitch chat"),
		  &St::chatTopBar);
	addChoice(c, QStringLiteral("Look"), QString(), &St::noticeStyle,
		  {{QStringLiteral("card"), QStringLiteral("Card")},
		   {QStringLiteral("strip"), QStringLiteral("Strip")},
		   {QStringLiteral("minimal"), QStringLiteral("Minimal")}});
	addChoice(c, QStringLiteral("Colour"), QString(), &St::noticeTint,
		  {{QStringLiteral("platform"), QStringLiteral("Platform")},
		   {QStringLiteral("event"), QStringLiteral("Event")},
		   {QStringLiteral("accent"), QStringLiteral("My accent")}});
	c = addCard(nc, QStringLiteral("📬"), QStringLiteral("Which events"));
	addToggle(c, QStringLiteral("⭐  Subs and resubs"),
		  QStringLiteral("Prime, upgrades, YouTube members, TikTok subs"), &St::ncSubs);
	addToggle(c, QStringLiteral("🎁  Gifted subs and gift bombs"), QStringLiteral("And gifted memberships"),
		  &St::ncGifts);
	addToggle(c, QStringLiteral("List who got a sub under each gift bomb"), QString(), &St::ncGiftNames, true);
	addToggle(c, QStringLiteral("🚀  Raids"), QString(), &St::ncRaids);
	addToggle(c, QStringLiteral("💎  Cheers, Kicks, Super Chats"), QString(), &St::ncCheers);
	addToggle(c, QStringLiteral("🎁  TikTok gifts"), QString(), &St::ncTikTokGifts);
	addIntChoice(c, QStringLiteral("Smallest TikTok gift shown"), QString(), &St::ncTikTokMinCoins,
		     {{0, QStringLiteral("Every gift")},
		      {5, QStringLiteral("5+ diamonds")},
		      {10, QStringLiteral("10+ diamonds")},
		      {50, QStringLiteral("50+ diamonds")},
		      {100, QStringLiteral("100+ diamonds")},
		      {500, QStringLiteral("500+ diamonds")},
		      {1000, QStringLiteral("1000+ diamonds")}});
	addToggle(c, QStringLiteral("➕  Twitch and Kick follows"), QString(), &St::ncFollows);
	addToggle(c, QStringLiteral("➕  TikTok follows"), QStringLiteral("Can be a lot"), &St::ncTikTokFollows);
	addToggle(c, QStringLiteral("📣  Shoutouts"),
		  QStringLiteral("ForgeBot !so and Twitch /shoutout, sent and received"), &St::ncShoutouts);
	addToggle(c, QStringLiteral("🚂  Hype Trains"), QStringLiteral("Started, each new level, ended"),
		  &St::ncHypeTrain);
	addToggle(c, QStringLiteral("🔥  Watch streaks"), QString(), &St::ncStreaks);
	addToggle(c, QStringLiteral("🎯  Channel point redemptions"), QString(), &St::ncPoints);
	addToggle(c, QStringLiteral("📢  Announcements"), QStringLiteral("/announce"), &St::ncAnnouncements);
	addToggle(c, QStringLiteral("ℹ  Everything else Twitch posts"),
		  QStringLiteral("Bits badges, charity, pay it forward…"), &St::ncOther);
	if (!options_.noticesNote.isEmpty())
		addNote(c, options_.noticesNote);
	nc->addStretch(1);

	// ── Who's here ──
	QVBoxLayout *vw =
		addPage(QStringLiteral("👥"), QStringLiteral("Who's here"),
			QStringLiteral("The live viewer list beside MultiChat. It hides itself when the dock is "
				       "narrower than 460 px."));
	c = addCard(vw, QStringLiteral("📋"), QStringLiteral("The list"));
	addToggle(c, QStringLiteral("Show Who's here"), QString(), &St::chatViewers);
	addToggle(c, QStringLiteral("On the left of the chat"), QString(), &St::vwLeft);
	addToggle(c, QStringLiteral("Include lurkers"), QStringLiteral("People who aren't chatting (Twitch)"),
		  &St::vwLurkers);
	addToggle(c, QStringLiteral("Hide bots"), QStringLiteral("StreamElements, Nightbot, ForgeXBot…"),
		  &St::vwHideBots);
	addToggle(c, QStringLiteral("Newest arrivals first"), QStringLiteral("Off = A to Z"), &St::vwNewestFirst);
	c = addCard(vw, QStringLiteral("🎨"), QStringLiteral("Look"));
	addToggle(c, QStringLiteral("Highlight people who just arrived"), QString(), &St::vwArrivals);
	addToggle(c, QStringLiteral("Platform icons"), QString(), &St::vwPlatformIcons);
	addToggle(c, QStringLiteral("Role badges"), QString(), &St::vwRoleBadges);
	addToggle(c, QStringLiteral("Chatting / lurking labels"), QString(), &St::vwActivity);
	addToggle(c, QStringLiteral("Name colours"), QStringLiteral("Follows Chat style → Name colours"),
		  &St::vwNameColors);
	addToggle(c, QStringLiteral("Compact rows"), QString(), &St::vwCompact);
	addNote(c, QStringLiteral(
			   "Twitch lists everyone in your chat. Kick, YouTube, TikTok and Xive don't share a viewer "
			   "list, so people there appear once they chat."));
	vw->addStretch(1);

	// ── Events ──
	QVBoxLayout *ev =
		addPage(QStringLiteral("📋"), QStringLiteral("Events"),
			QStringLiteral("The GoalForgeX Events feed: gift bombs, redemptions, streaks and alerts."));
	c = addCard(ev, QStringLiteral("📍"), QStringLiteral("Where"));
	addToggle(c, QStringLiteral("Events in their own dock"),
		  QStringLiteral("“GoalForgeX Events” in OBS's Docks menu"), &St::eventsOwnDock);
	c = addCard(ev, QStringLiteral("📬"), QStringLiteral("Show in the list"));
	addToggle(c, QStringLiteral("🎁  Gift bombs"), QStringLiteral("With the list of who got a sub"),
		  &St::evGiftBombs);
	addToggle(c, QStringLiteral("Open gift bombs with their recipients showing"), QString(), &St::evAutoExpand,
		  true);
	addToggle(c, QStringLiteral("🎯  Channel point redemptions"), QString(), &St::evPoints);
	addToggle(c, QStringLiteral("🔥  Watch streaks"), QString(), &St::evStreaks);
	addToggle(c, QStringLiteral("🔔  Sub / resub / bits / raid / follow alerts"), QString(), &St::evAlerts);
	addToggle(c, QStringLiteral("📣  Shoutouts"), QStringLiteral("Sent and received"), &St::evShoutouts);
	addToggle(c, QStringLiteral("🚂  Hype Trains"), QStringLiteral("Level, progress, top contributors, cooldown"),
		  &St::evHypeTrain);
	addToggle(c, QStringLiteral("🎵  TikTok gifts, follows and subs"), QString(), &St::evTikTok);
	addToggle(c, QStringLiteral("▶️  YouTube Super Chats and memberships"), QString(), &St::evYouTube);
	addToggle(c, QStringLiteral("⏱  Timer and spin-wheel activity"), QString(), &St::evTimers);
	addNote(c,
		QStringLiteral("Right-click a gift bomb to copy every recipient's name, or a ready-made thank-you."));
	ev->addStretch(1);

	// ── Appearance ──
	QVBoxLayout *ap = addPage(QStringLiteral("🌈"), QStringLiteral("Appearance"),
				  QStringLiteral("The look of every GoalForgeX dock and this window. Live red and the "
						 "platform colours always stay the same."));
	c = addCard(ap, QStringLiteral("🖼"), QStringLiteral("Theme"),
		    QStringLiteral("Picking a theme also picks its accent, unless you chose your own."));
	QWidget *grid = buildThemeGrid(c->parentWidget());
	c->addWidget(grid);
	addRow(grid, QStringLiteral("Theme graphite midnight oled slate warm nebula forest crimson ocean"), QString(),
	       [] { S().theme = Settings().theme; });
	c = addCard(ap, QStringLiteral("🎯"), QStringLiteral("Accent colour"),
		    QStringLiteral("Buttons, switches, sliders and highlights."));
	QWidget *accents = buildAccentRow(c->parentWidget());
	c->addWidget(accents);
	addRow(accents, QStringLiteral("Accent colour"), QString(), [] { S().accent = Settings().accent; });
	c = addCard(ap, QStringLiteral("📐"), QStringLiteral("Size and density"));
	addSlider(c, QStringLiteral("Dock text size"), QStringLiteral("Every GoalForgeX dock"), &St::textScale, 80, 140,
		  5, pct);
	addToggle(c, QStringLiteral("Compact"), QStringLiteral("Tighter buttons and lists"), &St::compact);
	ap->addStretch(1);

	// ── About ──
	QVBoxLayout *ab =
		addPage(QStringLiteral("ℹ️"), QStringLiteral("About"),
			QStringLiteral("GoalForgeX for OBS — goals, alerts, MultiChat and more, right in OBS."));
	c = addCard(ab, QStringLiteral("🧩"), QStringLiteral("This plugin"));
	const QString build = QString::fromUtf8(PLUGIN_BUILD);
	addNote(c, QStringLiteral("Version %1%2")
			   .arg(QString::fromLatin1(PLUGIN_VERSION),
				build.toInt() > 0 ? QStringLiteral(" · build %1").arg(build) : QString()));
	auto *links = new QWidget(nullptr);
	auto *ll = new QHBoxLayout(links);
	ll->setContentsMargins(0, 0, 0, 0);
	ll->setSpacing(8);
	auto *wn = new QPushButton(QStringLiteral("✨  What's new"), links);
	gfx::theme::setRole(wn, "primary");
	auto *dash = new QPushButton(QStringLiteral("🌐  My dashboard"), links);
	auto *devs = new QPushButton(QStringLiteral("🖥  Connected OBS devices"), links);
	for (QPushButton *b : {wn, dash, devs}) {
		b->setCursor(Qt::PointingHandCursor);
		ll->addWidget(b);
	}
	ll->addStretch(1);
	QWidget *row = rowFrame(c, QStringLiteral("Links"), QString(), links, true);
	addRow(row, QStringLiteral("What's new changelog dashboard devices"), QString(), nullptr);
	connect(wn, &QPushButton::clicked, this, [this] {
		if (options_.openChangelog)
			options_.openChangelog();
	});
	connect(dash, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://goalforgex.com/dashboard"))); });
	connect(devs, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://goalforgex.com/obs"))); });
	ab->addStretch(1);
}

// ── Live behaviour ───────────────────────────────────────────────────────

void GfxSettingsDialog::changed()
{
	restyle();
	renderPreview();
	previewTimer_->start();
}

void GfxSettingsDialog::restyle()
{
	// Only when the look changed (a slider drag shouldn't restyle the window every step).
	const QString key = S().theme + S().accent + QString::number(S().textScale);
	if (key == styleKey_)
		return;
	styleKey_ = key;
	const QColor accent(gfx::theme::accentColor());
	QString qss = QString::fromUtf8(kSheet);
	const QList<QPair<QString, QString>> tokens = {
		{QStringLiteral("@BG@"), col("#0e1014")},      {QStringLiteral("@PANEL@"), col("#13161c")},
		{QStringLiteral("@CARD@"), col("#181b22")},    {QStringLiteral("@LINE@"), col("#262b36")},
		{QStringLiteral("@HOVER@"), col("#1f232c")},   {QStringLiteral("@PREVIEW@"), col("#101217")},
		{QStringLiteral("@TEXT@"), col("#e8eaf0")},    {QStringLiteral("@MUTED@"), col("#9aa1b0")},
		{QStringLiteral("@FAINT@"), col("#5b6270")},   {QStringLiteral("@ACC@"), accent.name()},
		{QStringLiteral("@ACC10@"), rgba(accent, 26)}, {QStringLiteral("@ACC20@"), rgba(accent, 46)},
		{QStringLiteral("@ACC40@"), rgba(accent, 90)}, {QStringLiteral("@ACC50@"), rgba(accent, 128)},
	};
	for (const auto &t : tokens)
		qss.replace(t.first, t.second);
	setStyleSheet(scalePt(qss));
	if (heroMark_)
		heroMark_->setPixmap(gfx::theme::brandMark(46, devicePixelRatioF()));
	update();
}

void GfxSettingsDialog::renderPreview()
{
	if (!preview_)
		return;
	const Settings &s = S();
	const qreal dpr = devicePixelRatioF();
	const qreal base = s.chatFontSize > 0 ? s.chatFontSize : basePt();
	const qreal tagPt = qMax<qreal>(6.0, base * 0.78 * s.chatBadgeScale / 100.0);
	const int badgePx = qBound(10, qRound(tagPt * 2.05), 48);
	const int emotePx = qBound(12, qRound(base * 2.6 * s.chatEmoteScale / 100.0), 160);
	const int iconPx = qBound(12, qRound(base * 1.65), 32);
	QTextDocument *doc = preview_->document();
	const auto res = [doc](const QString &name, const QImage &img) {
		doc->addResource(QTextDocument::ImageResource, QUrl(name), QVariant::fromValue(img));
	};
	struct Sample {
		const char *platform;
		const char *name;
		const char *own; // their own colour
		bool host, mod, vip, sub;
		const char *text;
		int emotes; // 0 none, 1 smile, 2 smile+heart
	};
	static const Sample kSamples[] = {
		{"twitch", "YourChannel", "#ff7a59", true, false, false, false, "Welcome in everyone!", 1},
		{"kick", "ModSquad", "#53fc18", false, true, false, true, "Keep it friendly in here", 0},
		{"twitch", "VeryImportant", "#b57bff", false, false, true, false, "LET'S GOOO", 2},
		{"youtube", "LoyalSub", "#4fc3f7", false, false, false, true, "6 months strong!", 1},
		{"tiktok", "JustWatching", "#ffd166", false, false, false, false, "first time here, love the stream",
		 2},
	};
	const QString textColor = s.chatTextColor.isEmpty() ? col("#e8eaf0") : s.chatTextColor;
	const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
	QString h = QStringLiteral("<html><body style='font-size:%1pt'>").arg(QString::number(base, 'f', 1));
	int n = 0;
	for (const Sample &m : kSamples) {
		const QString platform = QString::fromLatin1(m.platform);
		QString line;
		if (s.chatIcons) {
			const QString key = QStringLiteral("pv:icon/%1/%2").arg(platform).arg(iconPx);
			res(key, gfx::notice::platformIcon(platform, iconPx, dpr));
			line += QStringLiteral(
					"<img src='%1' width='%2' height='%2' style='vertical-align:middle'>&nbsp;")
					.arg(key)
					.arg(iconPx);
		}
		if (s.chatTimestamps)
			line += QStringLiteral("<span style='color:%1;font-size:%2pt'>%3</span> ")
					.arg(col("#5b6270"), QString::number(qMax<qreal>(6.5, base * 0.88), 'f', 1),
					     stamp);
		const auto badge = [&](const char *text, const QString &roleColor, const char *fallback) {
			const QColor bc(roleColor.isEmpty() ? QString::fromLatin1(fallback) : roleColor);
			const QString key = QStringLiteral("pv:badge/%1/%2/%3")
						    .arg(QString::fromLatin1(text), bc.name().mid(1))
						    .arg(badgePx);
			res(key, badgeImage(QString::fromLatin1(text), bc, badgePx, dpr));
			line += QStringLiteral("<img src='%1' style='vertical-align:middle'>&nbsp;").arg(key);
		};
		if (s.chatBadges) {
			if (m.host)
				badge("HOST", s.colorHost, "#ff3b4f");
			else if (m.mod)
				badge("MOD", s.colorMod, "#34d399");
			if (m.vip)
				badge("VIP", s.colorVip, "#f472b6");
			if (m.sub)
				badge(platform == QLatin1String("youtube") ? "MEMBER" : "SUB", s.colorSub, "#ffb020");
		}
		QString nameColor = gfx::roleNameColor(m.host, m.mod, m.vip, m.sub);
		if (nameColor.isEmpty())
			nameColor = s.chatNameColors ? QString::fromLatin1(m.own) : col("#c9ced8");
		const bool bubbles = s.chatLayout == QLatin1String("bubbles");
		if (bubbles)
			line += QStringLiteral("<b style='color:%1'>%2</b><br>")
					.arg(nameColor, QString::fromLatin1(m.name));
		else
			line += QStringLiteral("<b style='color:%1'>%2</b><span style='color:%3'>:</span> ")
					.arg(nameColor, QString::fromLatin1(m.name), col("#5b6270"));
		line += QStringLiteral("<span style='color:%1'>%2</span>")
				.arg(textColor, QString::fromUtf8(m.text).toHtmlEscaped());
		for (int e = 0; e < m.emotes; ++e) {
			const QString key = QStringLiteral("pv:emote/%1/%2").arg(e).arg(emotePx);
			if (s.chatEmotes) {
				res(key, emoteImage(e, emotePx, dpr));
				line += QStringLiteral(
						" <img src='%1' width='%2' height='%2' style='vertical-align:middle'>")
						.arg(key)
						.arg(emotePx);
			} else {
				line += QStringLiteral(" <span style='color:%1'>%2</span>")
						.arg(textColor,
						     e == 0 ? QStringLiteral("PogChamp") : QStringLiteral("<3"));
			}
		}
		int gap = s.chatCompact ? qMin(2, s.chatSpacing) : s.chatSpacing;
		if (s.chatLayout == QLatin1String("cozy"))
			gap += 2;
		const bool shade = s.chatAltRows && (n % 2 == 1);
		QString bg = m.host ? col("#1f1a10") : (shade ? col("#161920") : QString());
		if (s.chatLayout == QLatin1String("bubbles")) {
			gap = qMax(3, s.chatSpacing) + 4;
			bg = m.host ? QStringLiteral("#2a2112") : col("#1b1f28");
		}
		h += QStringLiteral("<p style='margin-top:%1px;margin-bottom:%1px;%2'>%3</p>")
			     .arg(gap)
			     .arg(bg.isEmpty() ? QString() : QStringLiteral("background-color:%1;").arg(bg), line);
		++n;
	}
	h += QStringLiteral("</body></html>");
	const int scroll = preview_->verticalScrollBar() ? preview_->verticalScrollBar()->value() : 0;
	preview_->setHtml(h);
	if (preview_->verticalScrollBar())
		preview_->verticalScrollBar()->setValue(scroll);
}

void GfxSettingsDialog::applySearch(const QString &text)
{
	const QString q = text.trimmed().toLower();
	const int pages = stack_->count();
	QList<int> hits(pages, 0);
	QHash<QFrame *, int> cardHits;
	for (const Row &r : rows_) {
		const bool ok = q.isEmpty() || r.hay.contains(q);
		r.widget->setVisible(ok);
		if (ok) {
			hits[r.page] += 1;
			if (r.card)
				cardHits[r.card] += 1;
		}
	}
	for (const Card &cd : cards_) {
		const bool titleHit = q.isEmpty() || cd.frame->property("gfxSearch").toString().contains(q);
		if (titleHit && !q.isEmpty() && cardHits.value(cd.frame) == 0) {
			// The card's title matched: show the whole card.
			for (const Row &r : rows_)
				if (r.card == cd.frame)
					r.widget->setVisible(true);
			cardHits[cd.frame] = 1;
			hits[cd.page] += 1;
		}
		cd.frame->setVisible(q.isEmpty() || cardHits.value(cd.frame) > 0);
	}
	int first = -1;
	for (int i = 0; i < pages; ++i) {
		QListWidgetItem *item = nav_->item(i);
		const QString label = item->data(Qt::UserRole).toString();
		const bool any = q.isEmpty() || hits.at(i) > 0;
		item->setText(q.isEmpty() || !any ? label : QStringLiteral("%1   ·  %2").arg(label).arg(hits.at(i)));
		item->setFlags(any ? Qt::ItemFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable)
				   : Qt::ItemFlags(Qt::NoItemFlags));
		if (i < emptyNotes_.size())
			emptyNotes_.at(i)->setVisible(!any);
		if (any && first < 0)
			first = i;
	}
	if (!q.isEmpty() && first >= 0 && hits.at(qMax(0, nav_->currentRow())) == 0)
		selectPage(first);
	footNote_->setText(
		q.isEmpty() ? QStringLiteral("Changes show on your docks right away · Cancel puts everything back")
			    : (first < 0 ? QStringLiteral("No setting matches “%1”").arg(text.trimmed())
					 : QStringLiteral("Showing settings that match “%1”").arg(text.trimmed())));
}

void GfxSettingsDialog::resetCurrentPage()
{
	const int page = stack_->currentIndex();
	for (const Row &r : rows_)
		if (r.page == page && r.reset)
			r.reset();
	for (const auto &sync : syncs_)
		sync();
	changed();
}
