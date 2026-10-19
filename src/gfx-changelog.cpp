/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#include "gfx-changelog.hpp"
#include "gfx-settings.hpp"
#include "gfx-theme.hpp"

#include <plugin-support.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

#include <iterator>

namespace gfx::changelog {

namespace {

// Newest first. Each line: an emoji, a bold lead-in, then the detail.
struct Note {
	const char *icon;
	const char *title;
	const char *detail;
};
struct Release {
	const char *version;
	const char *date;
	const Note *notes;
	int count;
};

const Note k281[] = {
	{"🎯", "Your OBS stream in Multistream",
	 "The Multistream dock now shows the stream set in OBS → Settings → Stream at the top: which platform, the "
	 "masked key, live time, bitrate and dropped frames, with its own Start / Stop. With Pro it also checks a "
	 "Twitch key matches your connected channel."},
	{"🛑", "No double streams",
	 "Adding a destination for the platform OBS already streams to now warns you, and a destination using the "
	 "same key as your OBS stream won't start (it would knock your main stream off)."},
};
const Note k280[] = {
	{"📡", "Multistream",
	 "Go live on Twitch, Kick, TikTok, YouTube or any RTMP server at the same time from the new GoalForgeX "
	 "Multistream dock (Docks → GoalForgeX Multistream). Each destination can start with your OBS stream, "
	 "share its encoder so it costs no extra CPU/GPU, and shows its own live time, bitrate and dropped frames."},
	{"🔑", "Stream keys filled in for you",
	 "Twitch stream keys come straight from your connected account (Pro), refreshed every time you go live. "
	 "Kick fills in its server; paste the Kick key once. Keys are saved encrypted on this PC."},
	{"✏️", "One title, every platform",
	 "Change your stream title and category on Twitch and Kick at once from the Multistream dock (Pro)."},
};
const Note k273[] = {
	{"🛡", "GoalForgeX staff badge",
	 "When a member of the GoalForgeX team chats in your Twitch, Kick or TikTok chat, MultiChat marks them with a "
	 "GOALFORGEX STAFF badge and a highlighted line, so you always know it's really us. Switch it off in Settings → "
	 "MultiChat."},
	{"🔐", "Reconnect without signing out",
	 "The Reconnect button in the Events dock now only refreshes your Twitch permissions. It no longer signs your "
	 "browser in or out of the GoalForgeX dashboard, and it checks you approved the same Twitch channel."},
};
const Note k272[] = {
	{"📣", "Twitch shoutouts",
	 "Shoutouts now show in MultiChat and the Events dock, sent and received, including Twitch's own /shoutout and "
	 "other bots' shoutout commands. If the Events dock shows a Reconnect bar, click it once to approve Twitch's "
	 "shoutout permission."},
	{"💬", "Your chat stays put",
	 "When GoalForgeX updates its servers, your MultiChat and Events no longer clear. Everything on screen stays and "
	 "new messages carry on below it."},
	{"⚡", "Back in seconds",
	 "After a server update, MultiChat reconnects within a couple of seconds instead of waiting up to ten."},
	{"⚙", "Shoutouts switch", "Settings → Events → Shoutouts turns shoutout cards in the Events dock on or off."},
	{"🆕", "This popup", "You'll see what's new here after each update. Open it any time from ⚙ → What's new."},
};
const Note k270[] = {
	{"🔔", "Events dock",
	 "Events have their own dock (Docks → GoalForgeX Events) with the same cards as MultiChat, filters, session "
	 "totals and gift bombs that fill in live."},
	{"💬", "Event cards in MultiChat",
	 "Subs, gift bombs, raids, cheers, Kicks, follows, watch streaks, redemptions, announcements and TikTok gifts show "
	 "between your messages."},
	{"🎨", "Make it yours",
	 "Card, Strip or Minimal looks, platform / event / accent colours, and a switch for every kind of event in "
	 "Settings → MultiChat."},
	{"⚡", "Instant", "Chat and events arrive the moment they happen."},
};
const Release kReleases[] = {
	{"2.8.1", "October 2026", k281, static_cast<int>(std::size(k281))},
	{"2.8.0", "October 2026", k280, static_cast<int>(std::size(k280))},
	{"2.7.3", "October 2026", k273, static_cast<int>(std::size(k273))},
	{"2.7.2", "October 2026", k272, static_cast<int>(std::size(k272))},
	{"2.7.0", "October 2026", k270, static_cast<int>(std::size(k270))},
};

// "2.7.10" > "2.7.2" — compare numerically, part by part.
int compareVersions(const QString &a, const QString &b)
{
	const QStringList pa = a.split(QLatin1Char('.')), pb = b.split(QLatin1Char('.'));
	for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
		const int x = i < pa.size() ? pa.at(i).toInt() : 0;
		const int y = i < pb.size() ? pb.at(i).toInt() : 0;
		if (x != y)
			return x < y ? -1 : 1;
	}
	return 0;
}

QString notesHtml(const QString &since)
{
	const QString accent = theme::accentColor();
	QString h;
	bool older = false;
	for (const Release &r : kReleases) {
		const QString v = QString::fromLatin1(r.version);
		// Releases the streamer already saw go under "Earlier", dimmed.
		const bool seen = !since.isEmpty() && compareVersions(v, since) <= 0;
		if (seen && !older) {
			h += QStringLiteral("<p style='margin:18px 0 6px;color:#6b7280;font-size:8pt;font-weight:800;"
					    "letter-spacing:1px'>EARLIER</p>");
			older = true;
		}
		h += QStringLiteral(
			     "<p style='margin:%1px 0 6px'><span style='color:%2;font-weight:800;font-size:11pt'>"
			     "%3</span>&nbsp;&nbsp;<span style='color:#6b7280;font-size:8pt'>%4</span></p>")
			     .arg(h.isEmpty() ? 0 : 12)
			     .arg(seen ? QStringLiteral("#9aa1b0") : accent, v, QString::fromLatin1(r.date));
		for (int i = 0; i < r.count; ++i) {
			const Note &n = r.notes[i];
			h += QStringLiteral("<p style='margin:0 0 9px'><span style='font-size:11pt'>%1</span>&nbsp; "
					    "<b style='color:%2'>%3</b><br><span style='color:%4'>%5</span></p>")
				     .arg(QString::fromUtf8(n.icon),
					  seen ? QStringLiteral("#c9ced8") : QStringLiteral("#ffffff"),
					  QString::fromUtf8(n.title).toHtmlEscaped(),
					  seen ? QStringLiteral("#8b93a3") : QStringLiteral("#c9ced8"),
					  QString::fromUtf8(n.detail).toHtmlEscaped());
		}
	}
	return h;
}

void showDialog(QWidget *parent, const QString &since)
{
	QDialog dlg(parent);
	dlg.setWindowTitle(QStringLiteral("What's new in GoalForgeX for OBS"));
	dlg.setMinimumSize(440, 460);
	auto *root = new QVBoxLayout(&dlg);
	root->setContentsMargins(16, 14, 16, 12);
	root->setSpacing(10);

	auto *head = new QHBoxLayout();
	head->setSpacing(12);
	auto *mark = new QLabel(&dlg);
	mark->setPixmap(theme::brandMark(40, dlg.devicePixelRatioF()));
	mark->setFixedSize(40, 40);
	auto *titles = new QVBoxLayout();
	titles->setSpacing(0);
	auto *title = new QLabel(QStringLiteral("What's new"), &dlg);
	title->setObjectName(QStringLiteral("gfxSectionTitle"));
	auto *sub = new QLabel(QStringLiteral("GoalForgeX for OBS %1").arg(QString::fromLatin1(PLUGIN_VERSION)), &dlg);
	sub->setObjectName(QStringLiteral("gfxMuted"));
	titles->addWidget(title);
	titles->addWidget(sub);
	head->addWidget(mark);
	head->addLayout(titles, 1);
	root->addLayout(head);

	auto *body = new QTextBrowser(&dlg);
	body->setObjectName(QStringLiteral("gfxChatView")); // the feed's framed look
	body->setOpenLinks(false);
	body->document()->setDocumentMargin(12);
	body->setHtml(notesHtml(since));
	root->addWidget(body, 1);

	auto *buttons = new QDialogButtonBox(&dlg);
	QPushButton *close = buttons->addButton(QStringLiteral("Close"), QDialogButtonBox::AcceptRole);
	theme::setRole(close, "primary");
	close->setDefault(true);
	close->setCursor(Qt::PointingHandCursor);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	root->addWidget(buttons);
	dlg.exec();
}

} // namespace

void showIfUpdated(QWidget *parent)
{
	Settings &s = settings();
	const QString current = QString::fromLatin1(PLUGIN_VERSION);
	if (!s.lastSeenVersion.isEmpty() && compareVersions(s.lastSeenVersion, current) >= 0)
		return;
	const QString since = s.lastSeenVersion;
	// Fresh install (no settings yet): no popup, the welcome card explains everything.
	const bool updated = !since.isEmpty() || settingsFileExisted();
	s.lastSeenVersion = current;
	saveSettings();
	if (updated)
		showDialog(parent, since);
}

void show(QWidget *parent)
{
	showDialog(parent, QString());
}

} // namespace gfx::changelog
