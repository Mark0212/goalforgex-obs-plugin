/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-reactions.hpp"
#include "gfx-theme.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace gfx {

static QStringList sceneNames()
{
	QStringList out;
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; ++i)
		out << QString::fromUtf8(obs_source_get_name(scenes.sources.array[i]));
	obs_frontend_source_list_free(&scenes);
	return out;
}

static QStringList inputNames()
{
	QStringList out;
	obs_enum_sources(
		[](void *param, obs_source_t *src) {
			static_cast<QStringList *>(param)->append(QString::fromUtf8(obs_source_get_name(src)));
			return true;
		},
		&out);
	out.sort(Qt::CaseInsensitive);
	return out;
}

static QString programSceneName()
{
	obs_source_t *cur = obs_frontend_get_current_scene();
	const QString n = cur ? QString::fromUtf8(obs_source_get_name(cur)) : QString();
	if (cur)
		obs_source_release(cur);
	return n;
}

// Switch the LIVE (program) scene — in Studio Mode that means preview +
// transition, since obs_frontend_set_current_scene only changes the preview.
static bool goToScene(const QString &name)
{
	obs_source_t *s = obs_get_source_by_name(name.toUtf8().constData());
	if (!s)
		return false;
	const bool isScene = obs_scene_from_source(s) != nullptr;
	if (isScene) {
		if (obs_frontend_preview_program_mode_active()) {
			obs_frontend_set_current_preview_scene(s);
			obs_frontend_preview_program_trigger_transition();
		} else {
			obs_frontend_set_current_scene(s);
		}
	}
	obs_source_release(s);
	return isScene;
}

static bool setVisibleInScene(const QString &sceneName, const QString &sourceName, bool visible)
{
	obs_source_t *sc = obs_get_source_by_name(sceneName.toUtf8().constData());
	if (!sc)
		return false;
	bool done = false;
	if (obs_scene_t *scene = obs_scene_from_source(sc)) {
		if (obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, sourceName.toUtf8().constData())) {
			obs_sceneitem_set_visible(item, visible);
			done = true;
		}
	}
	obs_source_release(sc);
	return done;
}

void ReactionEngine::run(const Reaction &r)
{
	const QString live = programSceneName();
	if (r.action == QLatin1String("switch_scene")) {
		if (live == r.target || !goToScene(r.target))
			return;
		if (r.seconds > 0) {
			const QString back = live, target = r.target;
			QTimer::singleShot(r.seconds * 1000, ctx_, [back, target] {
				// Only go back if nobody switched away in the meantime.
				if (programSceneName() == target)
					goToScene(back);
			});
		}
	} else if (r.action == QLatin1String("show_source")) {
		if (!setVisibleInScene(live, r.target, true))
			return;
		if (r.seconds > 0) {
			const QString scene = live, source = r.target;
			QTimer::singleShot(r.seconds * 1000, ctx_,
					   [scene, source] { setVisibleInScene(scene, source, false); });
		}
	}
}

void ReactionEngine::fire(const QString &trigger)
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	for (const Reaction &r : settings().reactions) {
		if (!r.enabled || r.trigger != trigger)
			continue;
		// A short cooldown so a raid train / gift bomb can't make scenes flicker.
		if (now - lastFired_.value(r.id, 0) < 4000)
			continue;
		lastFired_.insert(r.id, now);
		run(r);
	}
}

void ReactionEngine::onEvents(const QList<FeedEvent> &events)
{
	for (const FeedEvent &e : events) {
		if (e.kind == QLatin1String("alert") && !e.alertType.isEmpty())
			fire(e.alertType);
		else if (e.kind == QLatin1String("timer") && e.event == QLatin1String("ended"))
			fire(QStringLiteral("timer_ended"));
	}
}

void ReactionEngine::onStateChange(const LiveState &prev, const LiveState &cur)
{
	if (!prev.valid || !cur.valid)
		return;
	if (cur.goalTarget > 0 && prev.goalTarget == cur.goalTarget && prev.goalCurrent < cur.goalTarget &&
	    cur.goalCurrent >= cur.goalTarget)
		fire(QStringLiteral("goal_reached"));
	if (cur.followerTarget > 0 && prev.followerTarget == cur.followerTarget &&
	    prev.followerCurrent < cur.followerTarget && cur.followerCurrent >= cur.followerTarget)
		fire(QStringLiteral("follower_goal_reached"));
}

} // namespace gfx

using namespace gfx;

GfxReactionsPanel::GfxReactionsPanel(ReactionEngine *engine, QWidget *parent) : QWidget(parent), engine_(engine)
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 4, 0, 4);
	root->setSpacing(6);
	auto *intro = new QLabel(
		QStringLiteral(
			"Make OBS react to your stream — e.g. switch to a celebration scene when your sub goal is hit, or show a “welcome raiders” source on a raid."),
		this);
	intro->setWordWrap(true);
	intro->setObjectName(QStringLiteral("gfxMuted"));
	root->addWidget(intro);

	list_ = new QListWidget(this);
	root->addWidget(list_, 1);

	auto *row = new QHBoxLayout();
	auto *add = new QPushButton(QStringLiteral("Add"), this);
	edit_ = new QPushButton(QStringLiteral("Edit"), this);
	remove_ = new QPushButton(QStringLiteral("Remove"), this);
	test_ = new QPushButton(QStringLiteral("▶ Test"), this);
	test_->setToolTip(QStringLiteral("Run the selected reaction now"));
	theme::setRole(add, "primary");
	theme::setRole(remove_, "danger");
	theme::setRole(test_, "soft");
	row->addWidget(add);
	row->addWidget(edit_);
	row->addWidget(remove_);
	row->addStretch(1);
	row->addWidget(test_);
	root->addLayout(row);

	connect(add, &QPushButton::clicked, this, [this] {
		Reaction r;
		r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		r.trigger = QStringLiteral("goal_reached");
		r.action = QStringLiteral("switch_scene");
		if (editRule(&r)) {
			settings().reactions.push_back(r);
			saveSettings();
			reload();
		}
	});
	connect(edit_, &QPushButton::clicked, this, [this] {
		const int i = list_->currentRow();
		if (i < 0 || i >= settings().reactions.size())
			return;
		Reaction r = settings().reactions[i];
		if (editRule(&r)) {
			settings().reactions[i] = r;
			saveSettings();
			reload();
		}
	});
	connect(remove_, &QPushButton::clicked, this, [this] {
		const int i = list_->currentRow();
		if (i < 0 || i >= settings().reactions.size())
			return;
		settings().reactions.removeAt(i);
		saveSettings();
		reload();
	});
	connect(test_, &QPushButton::clicked, this, [this] {
		const int i = list_->currentRow();
		if (i >= 0 && i < settings().reactions.size())
			engine_->run(settings().reactions[i]);
	});
	connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
		const int i = list_->row(it);
		if (i < 0 || i >= settings().reactions.size())
			return;
		const bool on = it->checkState() == Qt::Checked;
		if (settings().reactions[i].enabled != on) {
			settings().reactions[i].enabled = on;
			saveSettings();
		}
	});
	connect(list_, &QListWidget::currentRowChanged, this, [this](int i) {
		const bool sel = i >= 0;
		edit_->setEnabled(sel);
		remove_->setEnabled(sel);
		test_->setEnabled(sel);
	});
	connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { edit_->click(); });
	reload();
}

void GfxReactionsPanel::reload()
{
	list_->blockSignals(true);
	list_->clear();
	for (const Reaction &r : settings().reactions) {
		auto *it = new QListWidgetItem(reactionSummary(r), list_);
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		it->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
	}
	if (settings().reactions.isEmpty()) {
		auto *it = new QListWidgetItem(QStringLiteral("No reactions yet — click Add."), list_);
		it->setFlags(Qt::NoItemFlags);
	}
	list_->blockSignals(false);
	const bool sel = list_->currentRow() >= 0 && !settings().reactions.isEmpty();
	edit_->setEnabled(sel);
	remove_->setEnabled(sel);
	test_->setEnabled(sel);
}

bool GfxReactionsPanel::editRule(Reaction *r)
{
	QDialog dlg(this);
	dlg.setWindowTitle(QStringLiteral("Reaction"));
	auto *form = new QFormLayout(&dlg);

	auto *trigger = new QComboBox(&dlg);
	for (const auto &t : reactionTriggers())
		trigger->addItem(t.second, t.first);
	trigger->setCurrentIndex(qMax(0, trigger->findData(r->trigger)));

	auto *action = new QComboBox(&dlg);
	action->addItem(QStringLiteral("Switch to scene"), QStringLiteral("switch_scene"));
	action->addItem(QStringLiteral("Show a source"), QStringLiteral("show_source"));
	action->setCurrentIndex(qMax(0, action->findData(r->action)));

	auto *target = new QComboBox(&dlg);
	target->setEditable(false);
	const auto fillTargets = [target, action, r] {
		target->clear();
		const bool scene = action->currentData().toString() == QLatin1String("switch_scene");
		target->addItems(scene ? sceneNames() : inputNames());
		const int i = target->findText(r->target);
		if (i >= 0)
			target->setCurrentIndex(i);
	};
	fillTargets();
	connect(action, &QComboBox::currentIndexChanged, &dlg, [fillTargets](int) { fillTargets(); });

	auto *secs = new QSpinBox(&dlg);
	secs->setRange(0, 3600);
	secs->setSuffix(QStringLiteral(" s"));
	secs->setSpecialValueText(QStringLiteral("Stay"));
	secs->setValue(r->seconds);
	secs->setToolTip(QStringLiteral("Switch back / hide again after this long. “Stay” leaves it."));

	form->addRow(QStringLiteral("When"), trigger);
	form->addRow(QStringLiteral("Do"), action);
	form->addRow(QStringLiteral("Target"), target);
	form->addRow(QStringLiteral("For"), secs);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	if (dlg.exec() != QDialog::Accepted || target->currentText().isEmpty())
		return false;
	r->trigger = trigger->currentData().toString();
	r->action = action->currentData().toString();
	r->target = target->currentText();
	r->seconds = secs->value();
	return true;
}
