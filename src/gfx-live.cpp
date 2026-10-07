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
#include <QDateTime>
#include <QDesktopServices>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPair>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
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

	// ── Action timer ───────────────────────────────────────────────
	QVBoxLayout *ab = nullptr;
	atCard_ = makeCard(QStringLiteral("⚡ Action timer"), &ab);
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
	auto *aPlus1 = smallBtn(atCtl_, QStringLiteral("+1m"));
	auto *aPlus5 = smallBtn(atCtl_, QStringLiteral("+5m"));
	auto *aReset = smallBtn(atCtl_, QStringLiteral("Reset"));
	ac->addWidget(atToggle_, 1);
	ac->addWidget(aPlus1);
	ac->addWidget(aPlus5);
	ac->addWidget(aReset);
	ab->addWidget(atCtl_);
	atLock_ = lockLine(atCard_, QStringLiteral("The action timer needs Pro"));
	ab->addWidget(atLock_);
	connect(atToggle_, &QPushButton::clicked, this, [this] { act(QStringLiteral("at.toggle")); });
	connect(aPlus1, &QPushButton::clicked, this, [this] { act(QStringLiteral("at.add"), 60); });
	connect(aPlus5, &QPushButton::clicked, this, [this] { act(QStringLiteral("at.add"), 300); });
	connect(aReset, &QPushButton::clicked, this, [this] {
		if (QMessageBox::question(this, QStringLiteral("Reset action timer"),
					  QStringLiteral("Reset the action timer?")) == QMessageBox::Yes)
			act(QStringLiteral("at.reset"));
	});
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
	alertsCard_->setVisible(s.cardAlerts);
	activityCard_->setVisible(s.cardActivity);
	const bool any = s.cardStats || s.cardTimer || s.cardActionTimer || s.cardGoal || s.cardWheel ||
			 s.cardCounters || s.cardAlerts || s.cardActivity;
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
				   ? QStringLiteral("STOPPED")
				   : (s.actionTimer.paused ? QStringLiteral("PAUSED") : QStringLiteral("RUNNING")));
	gfx::theme::setState(atStatus_,
			     !s.actionTimer.active ? "stopped" : (s.actionTimer.paused ? "paused" : "running"));

	// Goal
	goalLbl_->setText(QStringLiteral("Subs  <b style='color:%1'>%2</b> / %3")
				  .arg(QLatin1String(gfx::theme::kAmber))
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
	applySettings();
	tick();
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
