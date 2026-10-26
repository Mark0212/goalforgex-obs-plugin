/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-live.hpp"
#include "gfx-theme.hpp"
#include "gfx-http.hpp"
#include "gfx-settings.hpp"

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QAbstractItemView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPair>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTime>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

using namespace gfx;

static QString fmtClock(qint64 secs)
{
	secs = qMax<qint64>(0, secs);
	const qint64 h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
	return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

static QPushButton *smallBtn(QWidget *parent, const QString &text, const QString &tip = QString())
{
	auto *b = new QPushButton(text, parent);
	b->setToolTip(tip);
	b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	return b;
}

GfxLivePanel::GfxLivePanel(QWidget *parent) : QWidget(parent)
{
	buildUi();
	tick_.setInterval(1000);
	connect(&tick_, &QTimer::timeout, this, [this] { tick(); });
	tick_.start();
}

QGroupBox *GfxLivePanel::makeCard(const QString &title, QVBoxLayout **body)
{
	auto *box = new QGroupBox(title, this);
	auto *l = new QVBoxLayout(box);
	l->setContentsMargins(8, 6, 8, 8);
	l->setSpacing(6);
	*body = l;
	return box;
}

QPushButton *GfxLivePanel::lockLine(QWidget *parent, const QString &text)
{
	auto *b = new QPushButton(QStringLiteral("🔒 %1 — Unlock with Pro").arg(text), parent);
	b->setFlat(true);
	b->setCursor(Qt::PointingHandCursor);
	gfx::theme::setRole(b, "link");
	connect(b, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl(baseUrl() + QStringLiteral("/subscribe"))); });
	b->hide();
	return b;
}

void GfxLivePanel::buildUi()
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	outer->addWidget(scroll);
	auto *content = new QWidget(scroll);
	scroll->setWidget(content);
	auto *root = new QVBoxLayout(content);
	root->setContentsMargins(0, 4, 0, 4);
	root->setSpacing(8);

	stats_ = new QLabel(content);
	stats_->setObjectName(QStringLiteral("gfxStats"));
	stats_->setTextFormat(Qt::RichText);
	root->addWidget(stats_);

	empty_ = new QLabel(
		QStringLiteral("Everything on this tab is switched off — turn cards back on in ⚙ Settings."), content);
	empty_->setWordWrap(true);
	empty_->setObjectName(QStringLiteral("gfxMuted"));
	empty_->hide();
	root->addWidget(empty_);

	QFont big = font();
	big.setPointSize(big.pointSize() + 9);
	big.setBold(true);
	big.setStyleHint(QFont::Monospace);
	big.setFamily(QStringLiteral("Consolas"));

	// ── Subathon timer ─────────────────────────────────────────────
	QVBoxLayout *tb = nullptr;
	timerCard_ = makeCard(QStringLiteral("⏱ Subathon timer"), &tb);
	auto *tRow = new QHBoxLayout();
	timerTime_ = new QLabel(QStringLiteral("0:00:00"), timerCard_);
	timerTime_->setFont(big);
	timerTime_->setObjectName(QStringLiteral("gfxBigTime"));
	timerStatus_ = new QLabel(timerCard_);
	timerStatus_->setObjectName(QStringLiteral("gfxStatusChip"));
	tRow->addWidget(timerTime_);
	tRow->addStretch(1);
	tRow->addWidget(timerStatus_);
	tb->addLayout(tRow);
	timerCtl_ = new QWidget(timerCard_);
	auto *tc = new QHBoxLayout(timerCtl_);
	tc->setContentsMargins(0, 0, 0, 0);
	tc->setSpacing(4);
	timerToggle_ = smallBtn(timerCtl_, QStringLiteral("▶ Start"));
	gfx::theme::setRole(timerToggle_, "primary");
	auto *tPlus1 = smallBtn(timerCtl_, QStringLiteral("+1m"), QStringLiteral("Add 1 minute"));
	auto *tPlus5 = smallBtn(timerCtl_, QStringLiteral("+5m"), QStringLiteral("Add 5 minutes"));
	auto *tMinus1 = smallBtn(timerCtl_, QStringLiteral("−1m"), QStringLiteral("Remove 1 minute"));
	auto *tMore = new QToolButton(timerCtl_);
	tMore->setText(QStringLiteral("⋯"));
	tMore->setPopupMode(QToolButton::InstantPopup);
	auto *tMenu = new QMenu(tMore);
	connect(tMenu->addAction(QStringLiteral("Add 10 minutes")), &QAction::triggered, this,
		[this] { act(QStringLiteral("timer.add"), 600); });
	connect(tMenu->addAction(QStringLiteral("Add 30 minutes")), &QAction::triggered, this,
		[this] { act(QStringLiteral("timer.add"), 1800); });
	connect(tMenu->addAction(QStringLiteral("Set time…")), &QAction::triggered, this, [this] {
		bool ok = false;
		const QString v = QInputDialog::getText(this, QStringLiteral("Set timer"),
							QStringLiteral("New time (H:MM:SS or minutes):"),
							QLineEdit::Normal, QString(), &ok);
		if (!ok || v.trimmed().isEmpty())
			return;
		const QStringList p = v.trimmed().split(QLatin1Char(':'));
		qint64 secs = 0;
		if (p.size() == 1)
			secs = p[0].toLongLong() * 60;
		else
			for (const QString &part : p)
				secs = secs * 60 + part.toLongLong();
		act(QStringLiteral("timer.set"), static_cast<double>(secs));
	});
	tMenu->addSeparator();
	connect(tMenu->addAction(QStringLiteral("Reset timer…")), &QAction::triggered, this, [this] {
		if (QMessageBox::question(this, QStringLiteral("Reset timer"),
					  QStringLiteral("Reset the subathon timer to its starting time?")) ==
		    QMessageBox::Yes)
			act(QStringLiteral("timer.reset"));
	});
	tMore->setMenu(tMenu);
	tc->addWidget(timerToggle_, 1);
	tc->addWidget(tPlus1);
	tc->addWidget(tPlus5);
	tc->addWidget(tMinus1);
	tc->addWidget(tMore);
	tb->addWidget(timerCtl_);
	timerLock_ = lockLine(timerCard_, QStringLiteral("The subathon timer needs Pro"));
	tb->addWidget(timerLock_);
	connect(timerToggle_, &QPushButton::clicked, this, [this] { act(QStringLiteral("timer.toggle")); });
	connect(tPlus1, &QPushButton::clicked, this, [this] { act(QStringLiteral("timer.add"), 60); });
	connect(tPlus5, &QPushButton::clicked, this, [this] { act(QStringLiteral("timer.add"), 300); });
	connect(tMinus1, &QPushButton::clicked, this, [this] { act(QStringLiteral("timer.add"), -60); });
	root->addWidget(timerCard_);

	// ── Intro timer ("Starting Soon" countdown; "actiontimer" server-side) ──
	QVBoxLayout *ab = nullptr;
	atCard_ = makeCard(QStringLiteral("⏳ Intro timer"), &ab);
	auto *aRow = new QHBoxLayout();
	QFont mid = big;
	mid.setPointSize(big.pointSize() - 4);
	atTime_ = new QLabel(QStringLiteral("0:00:00"), atCard_);
	atTime_->setFont(mid);
	atTime_->setObjectName(QStringLiteral("gfxMidTime"));
	atStatus_ = new QLabel(atCard_);
	atStatus_->setObjectName(QStringLiteral("gfxStatusChip"));
	aRow->addWidget(atTime_);
	aRow->addStretch(1);
	aRow->addWidget(atStatus_);
	ab->addLayout(aRow);
	atCtl_ = new QWidget(atCard_);
	auto *ac = new QHBoxLayout(atCtl_);
	ac->setContentsMargins(0, 0, 0, 0);
	ac->setSpacing(4);
	atToggle_ = smallBtn(atCtl_, QStringLiteral("▶ Start"));
	gfx::theme::setRole(atToggle_, "primary");
	auto *aMinus1 = smallBtn(atCtl_, QStringLiteral("−1m"), QStringLiteral("Remove 1 minute"));
	auto *aPlus1 = smallBtn(atCtl_, QStringLiteral("+1m"), QStringLiteral("Add 1 minute"));
	auto *aPlus5 = smallBtn(atCtl_, QStringLiteral("+5m"), QStringLiteral("Add 5 minutes"));
	auto *aMore = new QToolButton(atCtl_);
	aMore->setText(QStringLiteral("⋯"));
	aMore->setPopupMode(QToolButton::InstantPopup);
	auto *aMenu = new QMenu(aMore);
	for (const int mins : {2, 5, 10, 15}) {
		connect(aMenu->addAction(QStringLiteral("Start for %1 minutes").arg(mins)), &QAction::triggered, this,
			[this, mins] { startIntroFor(mins * 60); });
	}
	connect(aMenu->addAction(QStringLiteral("Start for…")), &QAction::triggered, this, [this] {
		bool ok = false;
		const int mins = QInputDialog::getInt(this, QStringLiteral("Start intro timer"),
						      QStringLiteral("Count down for how many minutes?"),
						      qMax(1, state_.intro.length / 60), 1, 1440, 1, &ok);
		if (ok)
			startIntroFor(mins * 60);
	});
	connect(aMenu->addAction(QStringLiteral("Count down until…")), &QAction::triggered, this, [this] {
		// The streamer's own clock, so "7:00 PM" means their 7:00 PM.
		const QDateTime now = QDateTime::currentDateTime();
		const QTime soon = now.time().addSecs(15 * 60);
		bool ok = false;
		const QString v = QInputDialog::getText(
			this, QStringLiteral("Count down until"),
			QStringLiteral("Stream starts at (24-hour HH:MM, your local time):"), QLineEdit::Normal,
			QStringLiteral("%1:%2")
				.arg(soon.hour(), 2, 10, QLatin1Char('0'))
				.arg(soon.minute(), 2, 10, QLatin1Char('0')),
			&ok);
		if (!ok)
			return;
		QTime t = QTime::fromString(v.trimmed(), QStringLiteral("H:mm"));
		if (!t.isValid())
			t = QTime::fromString(v.trimmed(), QStringLiteral("HH:mm"));
		if (!t.isValid()) {
			QMessageBox::warning(this, QStringLiteral("Count down until"),
					     QStringLiteral("Use a 24-hour time like 19:30."));
			return;
		}
		QDateTime target(now.date(), t);
		if (target <= now)
			target = target.addDays(1);
		startIntroFor(static_cast<int>(qMin<qint64>(86400, now.secsTo(target))));
	});
	aMenu->addSeparator();
	connect(aMenu->addAction(QStringLiteral("Reset…")), &QAction::triggered, this, [this] {
		if (QMessageBox::question(this, QStringLiteral("Reset intro timer"),
					  QStringLiteral("Reset the intro timer back to its full length?")) ==
		    QMessageBox::Yes)
			act(QStringLiteral("at.reset"));
	});
	aMore->setMenu(aMenu);
	ac->addWidget(atToggle_, 1);
	ac->addWidget(aMinus1);
	ac->addWidget(aPlus1);
	ac->addWidget(aPlus5);
	ac->addWidget(aMore);
	ab->addWidget(atCtl_);
	connect(atToggle_, &QPushButton::clicked, this, [this] { act(QStringLiteral("at.toggle")); });
	connect(aMinus1, &QPushButton::clicked, this, [this] { act(QStringLiteral("at.add"), -60); });
	connect(aPlus1, &QPushButton::clicked, this, [this] { act(QStringLiteral("at.add"), 60); });
	connect(aPlus5, &QPushButton::clicked, this, [this] { act(QStringLiteral("at.add"), 300); });

	// Live options — every change goes straight to the OBS source.
	atOptBtn_ = new QToolButton(atCard_);
	atOptBtn_->setText(QStringLiteral("⚙ Options"));
	atOptBtn_->setCheckable(true);
	atOptBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
	atOptBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	atOptBtn_->hide();
	ab->addWidget(atOptBtn_);
	atOpts_ = new QWidget(atCard_);
	auto *of = new QFormLayout(atOpts_);
	of->setContentsMargins(0, 2, 0, 0);
	of->setSpacing(4);
	atTitle_ = new QLineEdit(atOpts_);
	atTitle_->setMaxLength(80);
	atTitle_->setPlaceholderText(QStringLiteral("Starting Soon"));
	of->addRow(QStringLiteral("Title"), atTitle_);
	atSubtitle_ = new QLineEdit(atOpts_);
	atSubtitle_->setMaxLength(140);
	atSubtitle_->setPlaceholderText(QStringLiteral("Grab a drink — the stream starts shortly!"));
	of->addRow(QStringLiteral("Subtitle"), atSubtitle_);
	atLength_ = new QSpinBox(atOpts_);
	atLength_->setRange(1, 1440);
	atLength_->setSuffix(QStringLiteral(" min"));
	atLength_->setToolTip(QStringLiteral("What ▶ Start counts down from"));
	of->addRow(QStringLiteral("Length"), atLength_);
	auto *rotRow = new QHBoxLayout();
	atRotate_ = new QCheckBox(QStringLiteral("Rotate messages"), atOpts_);
	atRotateEdit_ = smallBtn(atOpts_, QStringLiteral("Add messages…"),
				 QStringLiteral("Lines that take turns under the title, one per line"));
	rotRow->addWidget(atRotate_, 1);
	rotRow->addWidget(atRotateEdit_);
	of->addRow(rotRow);
	auto *flashRow = new QHBoxLayout();
	atFlash_ = new QCheckBox(QStringLiteral("Flash at 0:00"), atOpts_);
	atFlashColor_ = smallBtn(atOpts_, QString(), QStringLiteral("Flash colour"));
	atFlashColor_->setFixedWidth(28);
	atFlashSpeed_ = new QComboBox(atOpts_);
	atFlashSpeed_->addItem(QStringLiteral("Slow"), QStringLiteral("slow"));
	atFlashSpeed_->addItem(QStringLiteral("Normal"), QStringLiteral("normal"));
	atFlashSpeed_->addItem(QStringLiteral("Fast"), QStringLiteral("fast"));
	flashRow->addWidget(atFlash_, 1);
	flashRow->addWidget(atFlashColor_);
	flashRow->addWidget(atFlashSpeed_);
	of->addRow(flashRow);
	atAutoStart_ = new QCheckBox(QStringLiteral("Auto-start when I go live"), atOpts_);
	of->addRow(atAutoStart_);
	atOpts_->hide();
	ab->addWidget(atOpts_);
	connect(atOptBtn_, &QToolButton::toggled, atOpts_, &QWidget::setVisible);
	connect(atTitle_, &QLineEdit::editingFinished, this, [this] {
		if (atTitle_->text().trimmed() != state_.intro.title)
			sendIntro({{QStringLiteral("title"), atTitle_->text().trimmed()}});
	});
	connect(atSubtitle_, &QLineEdit::editingFinished, this, [this] {
		if (atSubtitle_->text().trimmed() != state_.intro.subtitle)
			sendIntro({{QStringLiteral("subtitle"), atSubtitle_->text().trimmed()}});
	});
	atLengthSend_.setSingleShot(true);
	atLengthSend_.setInterval(700);
	connect(atLength_, &QSpinBox::valueChanged, this, [this](int) { atLengthSend_.start(); });
	connect(&atLengthSend_, &QTimer::timeout, this, [this] {
		const int secs = atLength_->value() * 60;
		if (secs != state_.intro.length)
			sendIntro({{QStringLiteral("baseTime"), secs}});
	});
	// clicked (not toggled) — only fires for the streamer's own clicks, never
	// for setState() syncing the boxes to the server.
	connect(atRotate_, &QCheckBox::clicked, this,
		[this](bool on) { sendIntro({{QStringLiteral("rotateEnabled"), on}}); });
	connect(atRotateEdit_, &QPushButton::clicked, this, [this] {
		bool ok = false;
		const QString text = QInputDialog::getMultiLineText(
			this, QStringLiteral("Rotating messages"),
			QStringLiteral("One message per line (up to 20). They take turns under the title."),
			atMessages_.join(QLatin1Char('\n')), &ok);
		if (!ok)
			return;
		QJsonArray msgs;
		for (const QString &line : text.split(QLatin1Char('\n'))) {
			const QString m = line.trimmed().left(140);
			if (!m.isEmpty() && msgs.size() < 20)
				msgs.append(m);
		}
		sendIntro(
			{{QStringLiteral("rotateMessages"), msgs}, {QStringLiteral("rotateEnabled"), !msgs.isEmpty()}});
	});
	connect(atFlash_, &QCheckBox::clicked, this,
		[this](bool on) { sendIntro({{QStringLiteral("endFlash"), on}}); });
	connect(atFlashColor_, &QPushButton::clicked, this, [this] {
		const QColor c = QColorDialog::getColor(QColor(atFlashColorVal_), this, QStringLiteral("Flash colour"));
		if (c.isValid())
			sendIntro({{QStringLiteral("endFlashColor"), c.name(QColor::HexRgb)}});
	});
	connect(atFlashSpeed_, &QComboBox::activated, this, [this](int i) {
		sendIntro({{QStringLiteral("endFlashSpeed"), atFlashSpeed_->itemData(i).toString()}});
	});
	connect(atAutoStart_, &QCheckBox::clicked, this,
		[this](bool on) { sendIntro({{QStringLiteral("autoStartOnLive"), on}}); });

	atLock_ = lockLine(atCard_, QStringLiteral("The intro timer needs Pro"));
	ab->addWidget(atLock_);
	root->addWidget(atCard_);

	// ── Goal ───────────────────────────────────────────────────────
	QVBoxLayout *gb = nullptr;
	goalCard_ = makeCard(QStringLiteral("🎯 Goal"), &gb);
	auto *gRow = new QHBoxLayout();
	goalLbl_ = new QLabel(goalCard_);
	auto *gMinus = smallBtn(goalCard_, QStringLiteral("−1"), QStringLiteral("Take one off the sub goal"));
	auto *gPlus = smallBtn(goalCard_, QStringLiteral("+1"), QStringLiteral("Add one to the sub goal"));
	gRow->addWidget(goalLbl_, 1);
	gRow->addWidget(gMinus);
	gRow->addWidget(gPlus);
	gb->addLayout(gRow);
	goalBar_ = new QProgressBar(goalCard_);
	goalBar_->setTextVisible(false);
	goalBar_->setFixedHeight(6);
	gb->addWidget(goalBar_);
	followerLbl_ = new QLabel(goalCard_);
	followerLbl_->setObjectName(QStringLiteral("gfxMuted"));
	gb->addWidget(followerLbl_);
	connect(gMinus, &QPushButton::clicked, this, [this] { act(QStringLiteral("goal.adjust"), -1); });
	connect(gPlus, &QPushButton::clicked, this, [this] { act(QStringLiteral("goal.adjust"), 1); });
	root->addWidget(goalCard_);

	// ── Presets ────────────────────────────────────────────────────
	// Switch the Sub Goal / Follower Goal look (or every alert, 2.11+) to one
	// of the presets saved on goalforgex.com. ✓ = the one on screen right now
	// (server-computed).
	QVBoxLayout *pb = nullptr;
	presetsCard_ = makeCard(QStringLiteral("🎨 Presets"), &pb);
	const auto presetRow = [this, pb](const QString &label, QWidget **row, QComboBox **box, QPushButton **apply,
					  int kind) {
		*row = new QWidget(presetsCard_);
		auto *h = new QHBoxLayout(*row);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(4);
		auto *lbl = new QLabel(label, *row);
		lbl->setMinimumWidth(62);
		*box = new QComboBox(*row);
		(*box)->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		(*box)->setMinimumContentsLength(8);
		(*box)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		*apply = smallBtn(
			*row, QStringLiteral("Apply"),
			kind == 2   ? QStringLiteral("Switch every alert to this preset — the Alerts source "
						       "changes right away")
			: kind == 1 ? QStringLiteral("Switch the Follower Goal to this preset — live on your "
						     "OBS source")
				    : QStringLiteral("Switch the Sub Goal to this preset — live on your OBS source"));
		gfx::theme::setRole(*apply, "primary");
		h->addWidget(lbl);
		h->addWidget(*box, 1);
		h->addWidget(*apply);
		pb->addWidget(*row);
		QComboBox *b = *box;
		QPushButton *a = *apply;
		connect(b, &QComboBox::currentIndexChanged, this,
			[this, b, a, kind](int) { refreshPresetApply(b, a, presetList(kind)); });
		connect(a, &QPushButton::clicked, this, [this, kind] { applyPreset(kind); });
		if (kind == 2)
			return; // alerts play on the one Alerts source — nothing to pin per scene
		// 📌 = a source that ALWAYS shows this preset's look, in the current
		// scene — so each scene can have its own look at the same time.
		auto *pin = smallBtn(
			*row, QStringLiteral("📌"),
			QStringLiteral("Add a source pinned to this preset to the current scene — that scene keeps "
				       "this look while other scenes show theirs"));
		pin->setFixedWidth(30);
		h->addWidget(pin);
		connect(pin, &QPushButton::clicked, this, [this, b] {
			const QString id = b->currentData().toString();
			if (id.isEmpty())
				return;
			QJsonObject o;
			o.insert(QStringLiteral("id"), id);
			act(QStringLiteral("preset.pin"), o);
		});
	};
	presetRow(QStringLiteral("Sub Goal"), &goalPresetRow_, &goalPresetBox_, &goalPresetApply_, 0);
	presetRow(QStringLiteral("Follower"), &folPresetRow_, &folPresetBox_, &folPresetApply_, 1);
	presetRow(QStringLiteral("Alerts"), &alertPresetRow_, &alertPresetBox_, &alertPresetApply_, 2);
	alertPresetRow_->hide();
	presetsEmpty_ = new QLabel(presetsCard_);
	presetsEmpty_->setObjectName(QStringLiteral("gfxMuted"));
	presetsEmpty_->setWordWrap(true);
	presetsEmpty_->setTextFormat(Qt::RichText);
	presetsEmpty_->setOpenExternalLinks(true);
	presetsEmpty_->setText(
		QStringLiteral("No presets yet — save a Sub Goal or Follower Goal look on "
			       "<a href='%1/dashboard'>goalforgex.com</a> (Presets), then switch between them here.")
			.arg(baseUrl()));
	presetsEmpty_->hide();
	pb->addWidget(presetsEmpty_);
	presetsLock_ = lockLine(presetsCard_, QStringLiteral("Presets need Pro"));
	pb->addWidget(presetsLock_);
	root->addWidget(presetsCard_);

	// ── Spin wheel ─────────────────────────────────────────────────
	QVBoxLayout *wb = nullptr;
	wheelCard_ = makeCard(QStringLiteral("🎡 Spin wheel"), &wb);
	auto *wRow = new QHBoxLayout();
	wheelBtn_ = smallBtn(wheelCard_, QStringLiteral("🎡 Spin"));
	gfx::theme::setRole(wheelBtn_, "primary");
	wheelLast_ = new QLabel(wheelCard_);
	wheelLast_->setObjectName(QStringLiteral("gfxMuted"));
	wRow->addWidget(wheelBtn_);
	wRow->addWidget(wheelLast_, 1);
	wb->addLayout(wRow);
	wheelLock_ = lockLine(wheelCard_, QStringLiteral("The spin wheel needs Pro"));
	wb->addWidget(wheelLock_);
	connect(wheelBtn_, &QPushButton::clicked, this, [this] { act(QStringLiteral("wheel.spin")); });
	root->addWidget(wheelCard_);

	// ── Counters ───────────────────────────────────────────────────
	countersCard_ = makeCard(QStringLiteral("🔢 Counters"), &countersBox_);
	root->addWidget(countersCard_);

	// ── Alerts ─────────────────────────────────────────────────────
	QVBoxLayout *alb = nullptr;
	alertsCard_ = makeCard(QStringLiteral("🔔 Alerts"), &alb);
	alertsCtl_ = new QWidget(alertsCard_);
	auto *alr = new QHBoxLayout(alertsCtl_);
	alr->setContentsMargins(0, 0, 0, 0);
	alr->setSpacing(4);
	auto *testBtn = new QToolButton(alertsCtl_);
	testBtn->setText(QStringLiteral("Test alert ▾"));
	testBtn->setPopupMode(QToolButton::InstantPopup);
	testBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	auto *testMenu = new QMenu(testBtn);
	const QList<QPair<QString, QString>> tests = {
		{QStringLiteral("follower"), QStringLiteral("Follow")},
		{QStringLiteral("subscriber"), QStringLiteral("Sub")},
		{QStringLiteral("resub"), QStringLiteral("Resub")},
		{QStringLiteral("gifted"), QStringLiteral("Gift subs")},
		{QStringLiteral("bits"), QStringLiteral("Bits")},
		{QStringLiteral("raid"), QStringLiteral("Raid")},
	};
	for (const auto &t : tests) {
		const QString type = t.first;
		connect(testMenu->addAction(t.second), &QAction::triggered, this,
			[this, type] { act(QStringLiteral("alert.test"), type); });
	}
	testBtn->setMenu(testMenu);
	replayBtn_ =
		smallBtn(alertsCtl_, QStringLiteral("↺ Replay last"), QStringLiteral("Show the last real alert again"));
	auto *clearBtn =
		smallBtn(alertsCtl_, QStringLiteral("Clear chat box"), QStringLiteral("Clear the on-screen chat box"));
	alr->addWidget(testBtn, 1);
	alr->addWidget(replayBtn_);
	alr->addWidget(clearBtn);
	alb->addWidget(alertsCtl_);
	alertsLock_ = lockLine(alertsCard_, QStringLiteral("Alerts need Pro"));
	alb->addWidget(alertsLock_);
	connect(replayBtn_, &QPushButton::clicked, this, [this] { act(QStringLiteral("alert.replay")); });
	connect(clearBtn, &QPushButton::clicked, this, [this] { act(QStringLiteral("chat.clear")); });
	root->addWidget(alertsCard_);

	// ── Activity ───────────────────────────────────────────────────
	QVBoxLayout *acb = nullptr;
	activityCard_ = makeCard(QStringLiteral("📋 Activity"), &acb);
	activity_ = new QListWidget(activityCard_);
	activity_->setObjectName(QStringLiteral("gfxActivity"));
	activity_->setMinimumHeight(90);
	activity_->setMaximumHeight(170);
	activity_->setSelectionMode(QAbstractItemView::NoSelection);
	activity_->setWordWrap(true);
	acb->addWidget(activity_);
	root->addWidget(activityCard_);

	root->addStretch(1);
	applySettings();
}

void GfxLivePanel::act(const QString &id, const QJsonValue &arg)
{
	if (action_)
		action_(id, arg);
}

void GfxLivePanel::sendIntro(const QJsonObject &changes)
{
	if (!changes.isEmpty())
		act(QStringLiteral("at.live"), changes);
}

// Starts a fresh intro countdown of `seconds` (asks first if one is running).
void GfxLivePanel::startIntroFor(int seconds)
{
	if (state_.actionTimer.active &&
	    QMessageBox::question(
		    this, QStringLiteral("Restart intro timer"),
		    QStringLiteral("The intro timer is already running. Restart it from %1?").arg(fmtClock(seconds))) !=
		    QMessageBox::Yes)
		return;
	act(QStringLiteral("at.startfor"), seconds);
}

void GfxLivePanel::paintFlashSwatch()
{
	const QColor c(atFlashColorVal_);
	atFlashColor_->setStyleSheet(
		QStringLiteral("QPushButton{background:%1;border:1px solid rgba(255,255,255,0.35);border-radius:4px;}")
			.arg(c.isValid() ? c.name(QColor::HexRgb) : QStringLiteral("#ff3b5c")));
}

void GfxLivePanel::setBusy(bool busy)
{
	setCursor(busy ? Qt::BusyCursor : Qt::ArrowCursor);
}

void GfxLivePanel::applySettings()
{
	const Settings &s = settings();
	const LiveState &st = state_;
	stats_->setVisible(s.cardStats);
	timerCard_->setVisible(s.cardTimer);
	atCard_->setVisible(s.cardActionTimer);
	goalCard_->setVisible(s.cardGoal);
	wheelCard_->setVisible(s.cardWheel);
	countersCard_->setVisible(s.cardCounters && (!st.valid || !st.counters.isEmpty() || st.countersLocked));
	// Only once the server says it knows about presets — older servers never send them.
	presetsCard_->setVisible(s.cardPresets && st.valid && st.presetsKnown);
	alertsCard_->setVisible(s.cardAlerts);
	activityCard_->setVisible(s.cardActivity);
	const bool any = s.cardStats || s.cardTimer || s.cardActionTimer || s.cardGoal || s.cardWheel ||
			 s.cardCounters || s.cardPresets || s.cardAlerts || s.cardActivity;
	empty_->setVisible(!any);
}

void GfxLivePanel::setState(const LiveState &s)
{
	state_ = s;

	// Timers
	timerCtl_->setVisible(!s.timer.locked);
	timerLock_->setVisible(s.timer.locked);
	timerTime_->setVisible(!s.timer.locked);
	timerStatus_->setVisible(!s.timer.locked);
	timerToggle_->setText(!s.timer.active
				      ? QStringLiteral("▶ Start")
				      : (s.timer.paused ? QStringLiteral("▶ Resume") : QStringLiteral("⏸ Pause")));
	timerStatus_->setText(!s.timer.active
				      ? QStringLiteral("STOPPED")
				      : (s.timer.paused ? QStringLiteral("PAUSED") : QStringLiteral("RUNNING")));
	gfx::theme::setState(timerStatus_, !s.timer.active ? "stopped" : (s.timer.paused ? "paused" : "running"));

	atCtl_->setVisible(!s.actionTimer.locked);
	atLock_->setVisible(s.actionTimer.locked);
	atTime_->setVisible(!s.actionTimer.locked);
	atStatus_->setVisible(!s.actionTimer.locked);
	atToggle_->setText(!s.actionTimer.active
				   ? QStringLiteral("▶ Start")
				   : (s.actionTimer.paused ? QStringLiteral("▶ Resume") : QStringLiteral("⏸ Pause")));
	atStatus_->setText(!s.actionTimer.active
				   ? (s.actionTimer.ended ? QStringLiteral("AT ZERO") : QStringLiteral("READY"))
				   : (s.actionTimer.paused ? QStringLiteral("PAUSED") : QStringLiteral("RUNNING")));
	gfx::theme::setState(atStatus_,
			     !s.actionTimer.active ? "stopped" : (s.actionTimer.paused ? "paused" : "running"));

	// Intro options — synced to the server, but never over a field the
	// streamer is typing in (or a length change still waiting to send).
	const IntroOptions &io = s.intro;
	const bool optsOn = io.known && !s.actionTimer.locked;
	atOptBtn_->setVisible(optsOn);
	atOpts_->setVisible(optsOn && atOptBtn_->isChecked());
	if (io.known) {
		if (!atTitle_->hasFocus() && atTitle_->text() != io.title)
			atTitle_->setText(io.title);
		if (!atSubtitle_->hasFocus() && atSubtitle_->text() != io.subtitle)
			atSubtitle_->setText(io.subtitle);
		if (!atLength_->hasFocus() && !atLengthSend_.isActive()) {
			const QSignalBlocker block(atLength_);
			atLength_->setValue(qMax(1, (io.length + 30) / 60));
		}
		atRotate_->setChecked(io.rotateEnabled);
		atMessages_ = io.rotateMessages;
		atRotateEdit_->setText(atMessages_.isEmpty() ? QStringLiteral("Add messages…")
							     : QStringLiteral("Edit (%1)…").arg(atMessages_.size()));
		atFlash_->setChecked(io.endFlash);
		atFlashColorVal_ = io.endFlashColor.isEmpty() ? QStringLiteral("#ff3b5c") : io.endFlashColor;
		paintFlashSwatch();
		const int speed = atFlashSpeed_->findData(io.endFlashSpeed.isEmpty() ? QStringLiteral("normal")
										     : io.endFlashSpeed);
		if (speed >= 0)
			atFlashSpeed_->setCurrentIndex(speed);
		atAutoStart_->setChecked(io.autoStartOnLive);
	}

	// Goal
	goalLbl_->setText(QStringLiteral("Subs  <b style='color:%1'>%2</b> / %3")
				  .arg(gfx::theme::accentColor())
				  .arg(s.goalCurrent)
				  .arg(s.goalTarget));
	goalBar_->setRange(0, qMax(1, s.goalTarget));
	goalBar_->setValue(qMin(s.goalCurrent, qMax(1, s.goalTarget)));
	followerLbl_->setText(QStringLiteral("Followers  %1 / %2").arg(s.followerCurrent).arg(s.followerTarget));
	followerLbl_->setVisible(s.followerTarget > 0);

	// Wheel
	wheelBtn_->setVisible(!s.wheelLocked);
	wheelLast_->setVisible(!s.wheelLocked);
	wheelLock_->setVisible(s.wheelLocked);
	wheelBtn_->setEnabled(s.wheelSegments > 0);
	if (s.wheelSegments <= 0)
		wheelLast_->setText(QStringLiteral("Add wheel slices on goalforgex.com first."));
	else if (wheelLast_->text().startsWith(QStringLiteral("Add wheel")))
		wheelLast_->clear();

	// Alerts
	alertsCtl_->setVisible(!s.alertsLocked);
	alertsLock_->setVisible(s.alertsLocked);
	replayBtn_->setEnabled(s.hasLastAlert);

	rebuildCounters();
	syncPresets();
	applySettings();
	tick();
}

void GfxLivePanel::syncPresets()
{
	const LiveState &s = state_;
	const bool locked = s.presetsLocked;
	const bool none = s.goalPresets.isEmpty() && s.followerPresets.isEmpty() && s.alertPresets.isEmpty();
	presetsLock_->setVisible(locked);
	presetsEmpty_->setVisible(!locked && none);
	goalPresetRow_->setVisible(!locked && !s.goalPresets.isEmpty());
	folPresetRow_->setVisible(!locked && !s.followerPresets.isEmpty());
	alertPresetRow_->setVisible(!locked && s.alertPresetsKnown && !s.alertPresets.isEmpty());
	if (locked)
		return;
	syncPresetBox(goalPresetBox_, goalPresetApply_, s.goalPresets, goalPresetSig_);
	syncPresetBox(folPresetBox_, folPresetApply_, s.followerPresets, folPresetSig_);
	syncPresetBox(alertPresetBox_, alertPresetApply_, s.alertPresets, alertPresetSig_);
}

const QList<PresetState> &GfxLivePanel::presetList(int kind) const
{
	return kind == 2 ? state_.alertPresets : kind == 1 ? state_.followerPresets : state_.goalPresets;
}

// Rebuilds a preset combo only when the list itself changed (names, order or
// which one is on screen) and never while its popup is open, so the 2 s state
// poll can't yank a choice out from under the streamer.
void GfxLivePanel::syncPresetBox(QComboBox *box, QPushButton *apply, const QList<PresetState> &list, QString &sig)
{
	QString next;
	for (const PresetState &p : list)
		next += p.id + QLatin1Char('|') + p.name + (p.active ? QStringLiteral("|1\n") : QStringLiteral("|0\n"));
	if (next != sig && !(box->view() && box->view()->isVisible())) {
		sig = next;
		const QString keep = box->currentData().toString();
		const QSignalBlocker block(box);
		box->clear();
		int pick = -1, activeIdx = -1;
		for (int i = 0; i < list.size(); ++i) {
			const PresetState &p = list.at(i);
			const QString label = p.game.isEmpty() ? p.name : QStringLiteral("%1 · %2").arg(p.name, p.game);
			box->addItem(p.active ? QStringLiteral("%1  ✓").arg(label) : label, p.id);
			if (p.active) {
				activeIdx = i;
				box->setItemData(i, QStringLiteral("On screen now"), Qt::ToolTipRole);
			}
			if (p.id == keep)
				pick = i;
		}
		// Keep what the streamer had picked; otherwise show what's on screen.
		box->setCurrentIndex(pick >= 0 ? pick : (activeIdx >= 0 ? activeIdx : 0));
	}
	refreshPresetApply(box, apply, list);
}

void GfxLivePanel::refreshPresetApply(QComboBox *box, QPushButton *apply, const QList<PresetState> &list)
{
	const QString id = box->currentData().toString();
	bool onScreen = false;
	for (const PresetState &p : list)
		if (p.id == id)
			onScreen = p.active;
	apply->setEnabled(!id.isEmpty() && !onScreen);
	apply->setText(onScreen ? QStringLiteral("On screen") : QStringLiteral("Apply"));
}

void GfxLivePanel::applyPreset(int kind)
{
	QComboBox *box = kind == 2 ? alertPresetBox_ : kind == 1 ? folPresetBox_ : goalPresetBox_;
	const QList<PresetState> &list = presetList(kind);
	const QString id = box->currentData().toString();
	QString name;
	bool anyOnScreen = false;
	for (const PresetState &p : list) {
		anyOnScreen = anyOnScreen || p.active;
		if (p.id == id)
			name = p.name;
	}
	if (id.isEmpty())
		return;
	// Switching is instant mid-stream — only stop to ask when it would throw
	// away a look that isn't saved as any preset (tweaked since the last load).
	const QString what = kind == 2   ? QStringLiteral("alerts aren't")
			     : kind == 1 ? QStringLiteral("Follower Goal look isn't")
					 : QStringLiteral("Sub Goal look isn't");
	if (!anyOnScreen && QMessageBox::question(this, QStringLiteral("Switch preset"),
						  QStringLiteral("Your current %1 saved as a preset. Switching to “%2” "
								 "replaces what's on screen.\n\nSwitch anyway?")
							  .arg(what, name)) != QMessageBox::Yes)
		return;
	QJsonObject a;
	a.insert(QStringLiteral("id"), id);
	a.insert(QStringLiteral("name"), name);
	act(kind == 2   ? QStringLiteral("preset.alerts")
	    : kind == 1 ? QStringLiteral("preset.follower")
			: QStringLiteral("preset.goal"),
	    a);
}

void GfxLivePanel::rebuildCounters()
{
	QStringList ids;
	for (const CounterState &c : state_.counters)
		ids << c.id + QLatin1Char('|') + c.name;
	const bool locked = state_.countersLocked;
	if (ids == counterIds_ && countersBox_->count() > 0 && !locked) {
		// Same counters — just refresh the numbers.
		for (int i = 0; i < countersBox_->count() && i < state_.counters.size(); ++i) {
			QWidget *row = countersBox_->itemAt(i)->widget();
			if (auto *val = row ? row->findChild<QLabel *>(QStringLiteral("value")) : nullptr)
				val->setText(QStringLiteral("<b>%1</b>").arg(state_.counters[i].value));
		}
		return;
	}
	counterIds_ = ids;
	while (QLayoutItem *it = countersBox_->takeAt(0)) {
		if (it->widget())
			it->widget()->deleteLater();
		delete it;
	}
	if (locked) {
		auto *l = lockLine(countersCard_, QStringLiteral("Custom counters need Pro"));
		l->show();
		countersBox_->addWidget(l);
		return;
	}
	for (const CounterState &c : state_.counters) {
		auto *row = new QWidget(countersCard_);
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(4);
		auto *name = new QLabel(c.name, row);
		auto *val = new QLabel(QStringLiteral("<b>%1</b>").arg(c.value), row);
		val->setObjectName(QStringLiteral("value"));
		val->setMinimumWidth(36);
		val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		auto *minus = smallBtn(row, QStringLiteral("−"));
		auto *plus = smallBtn(row, QStringLiteral("+"));
		minus->setFixedWidth(28);
		plus->setFixedWidth(28);
		h->addWidget(name, 1);
		h->addWidget(val);
		h->addWidget(minus);
		h->addWidget(plus);
		const QString id = c.id;
		connect(minus, &QPushButton::clicked, this, [this, id] {
			QJsonObject a;
			a.insert(QStringLiteral("id"), id);
			a.insert(QStringLiteral("delta"), -1);
			act(QStringLiteral("counter.adjust"), a);
		});
		connect(plus, &QPushButton::clicked, this, [this, id] {
			QJsonObject a;
			a.insert(QStringLiteral("id"), id);
			a.insert(QStringLiteral("delta"), 1);
			act(QStringLiteral("counter.adjust"), a);
		});
		countersBox_->addWidget(row);
	}
}

void GfxLivePanel::tick()
{
	if (!state_.valid) {
		stats_->setText(QStringLiteral("<span style='opacity:.7'>Loading…</span>"));
		return;
	}
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const qint64 elapsed = qMax<qint64>(0, (now - state_.receivedMs) / 1000);
	const auto shown = [elapsed](const TimerState &t) -> qint64 {
		return (t.active && !t.paused) ? qMax<qint64>(0, t.remaining - elapsed) : t.remaining;
	};
	timerTime_->setText(fmtClock(shown(state_.timer)));
	atTime_->setText(fmtClock(shown(state_.actionTimer)));

	if (!state_.twitchConnected) {
		stats_->setText(
			QStringLiteral("<span style='opacity:.7'>Twitch isn't connected on goalforgex.com</span>"));
	} else if (state_.isLive) {
		QString t = QStringLiteral("<b style='color:%1'>● LIVE</b>").arg(QLatin1String(gfx::theme::kRed));
		if (state_.liveSince > 0)
			t += QStringLiteral(" · ") + fmtClock((now - state_.liveSince) / 1000);
		if (state_.viewers >= 0)
			t += QStringLiteral(" · %1 viewer%2")
				     .arg(state_.viewers)
				     .arg(state_.viewers == 1 ? QString() : QStringLiteral("s"));
		stats_->setText(t);
	} else {
		stats_->setText(QStringLiteral("<span style='opacity:.7'>○ Offline</span>"));
	}
}

void GfxLivePanel::addEvents(const QList<FeedEvent> &events)
{
	for (const FeedEvent &e : events) {
		QString line = e.text;
		if (!e.by.isEmpty())
			line += QStringLiteral("  · ") + e.by;
		const QDateTime at =
			QDateTime::fromMSecsSinceEpoch(e.at > 0 ? e.at : QDateTime::currentMSecsSinceEpoch());
		auto *item =
			new QListWidgetItem(QStringLiteral("%1   %2").arg(at.toString(QStringLiteral("HH:mm")), line));
		activity_->insertItem(0, item);
		if (e.kind == QLatin1String("wheel") && !state_.wheelLocked)
			wheelLast_->setText(e.text.mid(e.text.indexOf(QStringLiteral("on ")) + 3));
	}
	while (activity_->count() > 40)
		delete activity_->takeItem(activity_->count() - 1);
}

void GfxLivePanel::clearEvents()
{
	activity_->clear();
}
