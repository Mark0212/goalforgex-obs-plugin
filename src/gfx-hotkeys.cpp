/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-hotkeys.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QApplication>
#include <QJsonObject>
#include <QMetaObject>

namespace gfx {

struct HotkeyDef {
	const char *name; // stable id — also the saved-binding key
	const char *description;
	const char *action;
	int arg;   // seconds / delta
	int index; // counter slot (-1 = not a counter)
	obs_hotkey_id id;
};

static HotkeyDef g_hotkeys[] = {
	{"goalforgex.timer_toggle", "GoalForgeX: Start / pause subathon timer", "timer.toggle", 0, -1,
	 OBS_INVALID_HOTKEY_ID},
	{"goalforgex.timer_add_1m", "GoalForgeX: Subathon timer +1 minute", "timer.add", 60, -1, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.timer_add_5m", "GoalForgeX: Subathon timer +5 minutes", "timer.add", 300, -1,
	 OBS_INVALID_HOTKEY_ID},
	{"goalforgex.timer_sub_1m", "GoalForgeX: Subathon timer −1 minute", "timer.add", -60, -1,
	 OBS_INVALID_HOTKEY_ID},
	{"goalforgex.at_toggle", "GoalForgeX: Start / pause action timer", "at.toggle", 0, -1, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.goal_plus", "GoalForgeX: Sub goal +1", "goal.adjust", 1, -1, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.goal_minus", "GoalForgeX: Sub goal −1", "goal.adjust", -1, -1, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.wheel_spin", "GoalForgeX: Spin the wheel", "wheel.spin", 0, -1, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.alert_replay", "GoalForgeX: Replay last alert", "alert.replay", 0, -1, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.chat_clear", "GoalForgeX: Clear chat box", "chat.clear", 0, -1, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.counter1_plus", "GoalForgeX: Counter 1 +1", "counter.nth", 1, 0, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.counter1_minus", "GoalForgeX: Counter 1 −1", "counter.nth", -1, 0, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.counter2_plus", "GoalForgeX: Counter 2 +1", "counter.nth", 1, 1, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.counter2_minus", "GoalForgeX: Counter 2 −1", "counter.nth", -1, 1, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.counter3_plus", "GoalForgeX: Counter 3 +1", "counter.nth", 1, 2, OBS_INVALID_HOTKEY_ID},
	{"goalforgex.counter3_minus", "GoalForgeX: Counter 3 −1", "counter.nth", -1, 2, OBS_INVALID_HOTKEY_ID},
};

static std::function<void(const QString &, const QJsonValue &)> g_dispatch;
static const char *kSaveKey = "goalforgex_hotkeys";

// Runs on libobs' hotkey thread — hop to the UI thread before touching anything.
static void onHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	const HotkeyDef *def = static_cast<const HotkeyDef *>(data);
	QMetaObject::invokeMethod(
		qApp,
		[def] {
			if (!g_dispatch)
				return;
			if (def->index >= 0) {
				QJsonObject a;
				a.insert(QStringLiteral("index"), def->index);
				a.insert(QStringLiteral("delta"), def->arg);
				g_dispatch(QString::fromUtf8(def->action), a);
			} else {
				g_dispatch(QString::fromUtf8(def->action), def->arg);
			}
		},
		Qt::QueuedConnection);
}

// Saved/loaded with the scene collection, like OBS's own hotkeys.
static void onSave(obs_data_t *saveData, bool saving, void *)
{
	if (saving) {
		obs_data_t *obj = obs_data_create();
		for (HotkeyDef &h : g_hotkeys) {
			if (h.id == OBS_INVALID_HOTKEY_ID)
				continue;
			obs_data_array_t *arr = obs_hotkey_save(h.id);
			obs_data_set_array(obj, h.name, arr);
			obs_data_array_release(arr);
		}
		obs_data_set_obj(saveData, kSaveKey, obj);
		obs_data_release(obj);
	} else {
		obs_data_t *obj = obs_data_get_obj(saveData, kSaveKey);
		if (!obj)
			return;
		for (HotkeyDef &h : g_hotkeys) {
			if (h.id == OBS_INVALID_HOTKEY_ID)
				continue;
			obs_data_array_t *arr = obs_data_get_array(obj, h.name);
			obs_hotkey_load(h.id, arr);
			obs_data_array_release(arr);
		}
		obs_data_release(obj);
	}
}

void registerHotkeys(std::function<void(const QString &, const QJsonValue &)> dispatch)
{
	g_dispatch = std::move(dispatch);
	for (HotkeyDef &h : g_hotkeys)
		h.id = obs_hotkey_register_frontend(h.name, h.description, onHotkey, &h);
	obs_frontend_add_save_callback(onSave, nullptr);
}

void unregisterHotkeys()
{
	obs_frontend_remove_save_callback(onSave, nullptr);
	for (HotkeyDef &h : g_hotkeys) {
		if (h.id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(h.id);
		h.id = OBS_INVALID_HOTKEY_ID;
	}
	g_dispatch = nullptr;
}

} // namespace gfx
