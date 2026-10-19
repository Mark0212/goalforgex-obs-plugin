/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "gfx-dock.hpp"
#include "gfx-events-dock.hpp"
#include "gfx-multistream.hpp"
#include "gfx-hotkeys.hpp"
#include "gfx-http.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QPointer>

#include <chrono>
#include <thread>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "GoalForgeX — add and control your GoalForgeX widgets from inside OBS.";
}

static QPointer<GfxDock> g_dock;

static void onFrontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT)
		gfx::httpBeginShutdown();
	if (g_dock)
		g_dock->onFrontendEvent(event);
}

bool obs_module_load(void)
{
	auto *dock = new GfxDock();
	// Adds a "GoalForgeX" toggle to OBS's Docks menu (OBS 30.0+).
	if (!obs_frontend_add_dock_by_id("goalforgex_dock", "GoalForgeX", dock)) {
		obs_log(LOG_ERROR, "Could not add the GoalForgeX dock (id already in use?)");
		delete dock;
		return false;
	}
	g_dock = dock;
	// "GoalForgeX Events" — the Events list as its own dock. If OBS won't add it,
	// Events simply stay a tab in the main dock.
	auto *eventsDock = new GfxEventsDock();
	if (obs_frontend_add_dock_by_id("goalforgex_events_dock", "GoalForgeX Events", eventsDock)) {
		dock->attachEventsDock(eventsDock);
	} else {
		obs_log(LOG_WARNING, "Could not add the GoalForgeX Events dock — Events stay a tab");
		delete eventsDock;
	}
	// "GoalForgeX Multistream" — go live on several platforms at once (2.8).
	auto *msDock = new GfxMultistreamDock();
	if (obs_frontend_add_dock_by_id("goalforgex_multistream_dock", "GoalForgeX Multistream", msDock)) {
		dock->attachMultistreamDock(msDock);
	} else {
		obs_log(LOG_WARNING, "Could not add the GoalForgeX Multistream dock");
		delete msDock;
	}
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);
	// OBS Settings → Hotkeys → "GoalForgeX: …" — same actions as the Live tab.
	gfx::registerHotkeys([](const QString &id, const QJsonValue &arg) {
		if (g_dock)
			g_dock->performAction(id, arg);
	});
	obs_log(LOG_INFO, "GoalForgeX loaded (version %s, server %s)", PLUGIN_VERSION,
		gfx::baseUrl().toUtf8().constData());
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onFrontendEvent, nullptr);
	gfx::unregisterHotkeys();
	gfx::httpBeginShutdown();
	// Worker threads abort their transfers once shutdown is flagged; give any
	// still in flight a moment to return before this DLL's code goes away.
	for (int i = 0; i < 50 && gfx::httpInFlight() > 0; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	obs_log(LOG_INFO, "GoalForgeX unloaded");
}
