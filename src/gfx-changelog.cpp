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

#include <QButtonGroup>
#include <QColor>
#include <QDialog>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
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

const Note k300[] = {
	{"🚀", "GoalForgeX for OBS 3.0",
	 "The biggest update yet: a brand-new Settings window, chat you can fine-tune, TikTok likes, new themes and a "
	 "fresh What's new. Here's everything in 3.0."},
	{"🎛", "A brand-new Settings",
	 "⚙ Settings is rebuilt from scratch: a sidebar of pages, search across every option (Ctrl+F), smooth on / off "
	 "switches and cards. Everything previews live on your docks as you change it, and Cancel puts it all back."},
	{"🔠", "Sliders for your chat",
	 "Chat style has sliders for text size, emote size, badge size and the space between messages, with a live "
	 "chat preview right above them."},
	{"🌈", "Name colours by role",
	 "Pick a colour for the broadcaster, mods, VIPs, subs and everyone else, plus the message text itself. Badges "
	 "match, and Who's here uses the same colours."},
	{"😀", "Send emotes",
	 "The new 😀 button beside the message box opens an emote picker: your Twitch channel emotes, Twitch globals "
	 "and BTTV / FFZ / 7TV, with search and ⭐ Recent. Tab completes emote names as you type, too."},
	{"🫧", "A roomier MultiChat",
	 "Pick Classic, Cozy (the new default, with more space around every line) or Bubbles, where each message "
	 "sits on its own card, in Settings → Chat style."},
	{"❓", "Never miss a question",
	 "The ❓ chip shows only messages with a question in them, so you can answer chat between games."},
	{"📌", "Pin a message",
	 "Right-click a name → Pin message keeps it on a card above the chat until you unpin it."},
	{"📊", "Twitch's pinned bar in MultiChat",
	 "Whatever sits at the top of your Twitch chat shows above MultiChat too: the message you or a mod pinned "
	 "(with its timer), the Hype Train with a live progress bar, polls with live votes and predictions with points "
	 "and odds. Right-click a name → Pin on Twitch pins it for everyone; the ✕ unpins it. Switch it off in Settings "
	 "→ Notices."},
	{"⚡", "Quick replies",
	 "Save the things you type all the time (socials, rules, schedule) and drop them in with the ⚡ button. "
	 "Ctrl + click sends one straight away."},
	{"❤️", "TikTok likes in MultiChat",
	 "While you're live on TikTok, one slim line above the chat shows your total likes, likes a minute and who's "
	 "tapping right now. It pulses as likes come in and takes almost no chat room."},
	{"⚙", "Settings on every dock",
	 "MultiChat, Events and Multistream each have a ⚙ button now, straight to the right Settings page."},
	{"🖼", "Four new themes",
	 "Nebula, Forest, Crimson and Ocean join the list, and Appearance shows every theme as a little preview card. "
	 "Picking one also picks its accent unless you chose your own."},
	{"✨", "What's new, redesigned",
	 "This window has a timeline, search across every update and four looks: Aurora, Terminal, Paper or My dock."},
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
const Note k2130[] = {
	{"👥", "Who's here, right beside your chat",
	 "MultiChat now has a live viewer list: broadcaster, mods, VIPs, chatters and Twitch lurkers, with search, "
	 "filters and who just arrived. Click anyone for their card. The 👥 button shows or hides it."},
	{"🎨", "Make it yours",
	 "Pick the side, sort order, icons, badges, labels and name colours, hide bots or specific people, and "
	 "drag it to the width you like. It's all in the list's ⋯ menu."},
};
const Note k2122[] = {
	{"🧩", "Dock no longer jumbled",
	 "With a tab switched off in Layout → Tabs, that page could pile up on top of the others at the top of the "
	 "GoalForgeX dock, covering the tabs. Pages you've switched off now stay out of sight."},
	{"🐝", "Clearer Xive send errors", "A message that couldn't be sent to Xive now says Xive instead of Twitch."},
	{"⚡", "Quick fixes, delivered automatically",
	 "Small fixes can now arrive as a new build of the same version — the dock shows it as “2.12.2 (fix, build "
	 "N)” and updates like any other release. Your build number is next to the version at the bottom of the dock."},
};
const Note k2121[] = {
	{"🐝", "Xive chat in MultiChat",
	 "Your Xive chat now shows up in MultiChat next to Twitch, Kick, YouTube and TikTok, with its own filter chip, "
	 "live / offline status and a Xive button to reply (posted by the GoalForgeX bot). Needs the GoalForgeX bot "
	 "added to your Xive channel; ⚙ Settings → MultiChat turns it off."},
};
const Note k2120[] = {
	{"💬", "MultiChat has its own dock",
	 "MultiChat moved out of the GoalForgeX dock into its own GoalForgeX MultiChat dock (Docks menu), so you can "
	 "put it anywhere and give it the full height — no more sharing room with the Live tab."},
	{"🟢", "Online / offline at a glance",
	 "A slim row on top of the chat shows whether you're live or offline on Twitch, Kick, YouTube and TikTok, with "
	 "viewers. Hover for uptime, title and category; ⋯ → big cards brings the old cards back."},
	{"📏", "More room for chat",
	 "Thinner edges and spacing and the slim status row leave far more of the dock for messages."},
};
const Note k2114[] = {
	{"📍", "Alerts you can place anywhere",
	 "Turn on Place alerts in OBS on the GoalForgeX alerts page and Add → Alerts gives you a source the size of your "
	 "alerts instead of a full-screen layer. Drag and resize it in each scene — every alert fires right there."},
	{"🔊", "Alert sound in the Audio Mixer",
	 "Alert sounds and video alerts with sound play through the Alerts source, so its slider in OBS's Audio Mixer "
	 "sets how loud they are."},
};

const Note k2113[] = {
	{"🪙", "Coin Drop follows your alerts instantly",
	 "Move or resize your Alerts or goal source in OBS and the very next alert's coins fly to the new spot — no "
	 "waiting for the next check."},
};

const Note k2112[] = {
	{"✨", "A brand-new Events feed",
	 "Every event is now its own card: a coloured tile with the event and platform, the time on the right, what "
	 "happened, what the viewer wrote and, for gift bombs, who got one. Cards have proper space between them, "
	 "and right-click still copies names or a thank-you message."},
	{"💬", "Room to breathe in MultiChat",
	 "Event cards in MultiChat no longer touch — there's a clear gap between each one."},
	{"⚡", "Snappier buttons",
	 "Test alerts and every other button in the dock now reuse the connection GoalForgeX already has open "
	 "instead of starting a new one each click, so they react noticeably faster."},
	{"🎯", "Coin Drop keeps up with your goal",
	 "Move or resize your Sub Goal in OBS and the Alert Coin Drop re-aims within a second."},
};
const Note k2111[] = {
	{"🎮", "Find any game in a couple of letters",
	 "In the Multistream dock, start typing a category (\"valo\", \"just chat\") and matching games show up "
	 "right under the box with their box art — click one or press Enter. Every game you switch to lands in "
	 "Your games, and Switch puts it on Twitch and Kick at once. Pin your regulars with ★ (Pro)."},
	{"💬", "Cleaner chat and event cards",
	 "MultiChat and the Events dock got a tidy-up: rounded event cards with a slim colour edge, clear event "
	 "labels, and neat rounded MOD / VIP / SUB badges."},
};
const Note k2110[] = {
	{"🧹", "A cleaner, tidier dock",
	 "Fresh look across the dock: card titles inside each card, rounded tabs and calmer colours. Click any Live "
	 "card's title to fold it away (it stays folded), and the cards you use most now come first. The ⚙ menu is "
	 "grouped, and Settings has a new Layout page for which tabs and cards show."},
	{"🎮", "Your games, one click away",
	 "The Multistream dock now keeps a list of your games. Start typing a category and pick it from the suggestions; "
	 "every game you switch to lands in Your games, and Switch puts it on Twitch and Kick at once. Pin your regulars "
	 "to the top with ★ (Pro)."},
};
const Note k2102[] = {
	{"🎯", "Finds your overlays more reliably",
	 "GoalForgeX now recognises your goal and alert sources in OBS even when you added them by hand or put them "
	 "inside a nested scene, and tells the site exactly where they sit — so on-screen effects line up."},
};
const Note k2100[] = {
	{"🎨", "Switch presets from OBS",
	 "New Presets card on the Live tab: pick one of your saved Sub Goal or Follower Goal looks and hit Apply — it "
	 "changes on stream right away, and your goal numbers stay put. The one on screen now is marked ✓ (Pro)."},
	{"📌", "A different look in every scene",
	 "Hit 📌 next to a preset to add a goal that always shows that look to the scene you're on — e.g. a gold goal "
	 "in Starting Soon and a neon one in Gameplay, both counting the same live subs. Pinned presets are in the "
	 "Widgets tab too (Pro)."},
	{"⬆️", "Newest events on top",
	 "The Events dock now shows the latest event at the top. Scrolled down reading older ones? New events won't "
	 "move your place — a “↑ new events” button takes you back up."},
	{"🚂", "Hype Trains",
	 "A live Hype Train card in Events — level, points to the next level, when it runs out, top contributors, your "
	 "record — and MultiChat notices when it starts, levels up and ends (with the cooldown). Golden Kappa, "
	 "Treasure and shared trains are labelled. Needs one Reconnect to approve the new Twitch permission."},
	{"⬆", "Updates that actually arrive",
	 "With auto-update on, GoalForgeX now checks every hour and again the moment your stream ends, retries a "
	 "download that failed, and tells you if Windows' “allow changes” prompt was declined — so the next version "
	 "installs when you close OBS after stream."},
};
const Note k291[] = {
	{"📡", "Offline straight away",
	 "When you stop streaming in OBS, GoalForgeX now marks you offline the same moment — even on a very short "
	 "stream (start and stop inside a minute), which used to stay LIVE for a few minutes."},
};
const Note k290[] = {
	{"⏳", "The Intro timer",
	 "The Action timer is now the Intro timer — a Starting Soon countdown for your pre-stream scene. At 0:00 it "
	 "holds the zeros and flashes in the colour you pick."},
	{"⚙", "Change it live from OBS",
	 "Open ⚙ Options in the Intro timer card to change the title, subtitle, length, rotating messages, flash colour "
	 "and speed, and auto-start when you go live. Every change shows on your OBS source straight away (Pro)."},
	{"⏰", "Start for… / Count down until…",
	 "The ⋯ menu starts a 2, 5, 10 or 15 minute countdown, any length you like, or a countdown to a time on your "
	 "own clock — plus a −1m button next to +1m / +5m."},
};
const Note k282[] = {
	{"🎵", "TikTok LIVE access, explained",
	 "Picking TikTok in the Multistream dock now shows how to get TikTok's OK to stream from a PC, with an "
	 "Apply button and a shortcut to TikTok LIVE Producer, where your server and stream key are."},
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
	{"3.0.0", "November 2026", k300, static_cast<int>(std::size(k300))},
	{"2.13.0", "November 2026", k2130, static_cast<int>(std::size(k2130))},
	{"2.12.2", "November 2026", k2122, static_cast<int>(std::size(k2122))},
	{"2.12.1", "November 2026", k2121, static_cast<int>(std::size(k2121))},
	{"2.12.0", "November 2026", k2120, static_cast<int>(std::size(k2120))},
	{"2.11.4", "October 2026", k2114, static_cast<int>(std::size(k2114))},
	{"2.11.3", "October 2026", k2113, static_cast<int>(std::size(k2113))},
	{"2.11.2", "October 2026", k2112, static_cast<int>(std::size(k2112))},
	{"2.11.1", "October 2026", k2111, static_cast<int>(std::size(k2111))},
	{"2.11.0", "October 2026", k2110, static_cast<int>(std::size(k2110))},
	{"2.10.2", "October 2026", k2102, static_cast<int>(std::size(k2102))},
	{"2.10.1", "October 2026", k2100, static_cast<int>(std::size(k2100))},
	{"2.9.1", "October 2026", k291, static_cast<int>(std::size(k291))},
	{"2.9.0", "October 2026", k290, static_cast<int>(std::size(k290))},
	{"2.8.2", "October 2026", k282, static_cast<int>(std::size(k282))},
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

// ── Looks ─────────────────────────────────────────────────────────────
// Settings → changelogTheme. "dock" follows Settings → Appearance.
struct Look {
	QString bg, panel, card, line, text, muted, faint, accent, accent2, font;
	bool light = false;
};

Look lookFor(const QString &id)
{
	if (id == QLatin1String("terminal"))
		return {QStringLiteral("#050805"),
			QStringLiteral("#0a110a"),
			QStringLiteral("#0c160c"),
			QStringLiteral("#1c3a1c"),
			QStringLiteral("#c8ffc8"),
			QStringLiteral("#6fbf73"),
			QStringLiteral("#3d6e40"),
			QStringLiteral("#39ff6a"),
			QStringLiteral("#00e5ff"),
			QStringLiteral("'Cascadia Mono', 'Consolas', 'Menlo', 'DejaVu Sans Mono', monospace"),
			false};
	if (id == QLatin1String("paper"))
		return {QStringLiteral("#f6f1e7"),
			QStringLiteral("#efe8da"),
			QStringLiteral("#fffdf8"),
			QStringLiteral("#e2d8c6"),
			QStringLiteral("#2b2418"),
			QStringLiteral("#7a6d58"),
			QStringLiteral("#a89a82"),
			QStringLiteral("#d9480f"),
			QStringLiteral("#f59f00"),
			QStringLiteral("'Georgia', 'Iowan Old Style', serif"),
			true};
	if (id == QLatin1String("dock")) {
		const QString a = theme::accentColor();
		return {theme::mapColor(QStringLiteral("#0e1014")),
			theme::mapColor(QStringLiteral("#13161c")),
			theme::mapColor(QStringLiteral("#181b22")),
			theme::mapColor(QStringLiteral("#262b36")),
			theme::mapColor(QStringLiteral("#e8eaf0")),
			theme::mapColor(QStringLiteral("#9aa1b0")),
			theme::mapColor(QStringLiteral("#5b6270")),
			a,
			QColor(a).lighter(135).name(),
			QString(),
			false};
	}
	return {QStringLiteral("#0b0d17"),
		QStringLiteral("#10132a"),
		QStringLiteral("#151936"),
		QStringLiteral("#272c58"),
		QStringLiteral("#eef0ff"),
		QStringLiteral("#a5abd6"),
		QStringLiteral("#646a9c"),
		QStringLiteral("#a78bfa"),
		QStringLiteral("#22d3ee"),
		QString(),
		false};
}

QString rgbaOf(const QString &hex, int alpha)
{
	const QColor c(hex);
	return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// Escaped text with the search term marked.
QString marked(const QString &raw, const QString &q, const QString &markBg)
{
	const QString esc = raw.toHtmlEscaped();
	if (q.isEmpty())
		return esc;
	const QRegularExpression re(QRegularExpression::escape(q.toHtmlEscaped()),
				    QRegularExpression::CaseInsensitiveOption);
	QString out;
	qsizetype last = 0;
	auto it = re.globalMatch(esc);
	while (it.hasNext()) {
		const QRegularExpressionMatch m = it.next();
		out += esc.mid(last, m.capturedStart() - last);
		out += QStringLiteral("<span style='background-color:%1'>%2</span>").arg(markBg, m.captured(0));
		last = m.capturedEnd();
	}
	out += esc.mid(last);
	return out;
}

bool noteMatches(const Note &n, const QString &q)
{
	return q.isEmpty() || QString::fromUtf8(n.title).contains(q, Qt::CaseInsensitive) ||
	       QString::fromUtf8(n.detail).contains(q, Qt::CaseInsensitive);
}

QString notesHtml(const QString &since, const QString &q, const Look &k)
{
	QString h = QStringLiteral("<html><body style='color:%1;%2'>")
			    .arg(k.text, k.font.isEmpty() ? QString() : QStringLiteral("font-family:%1;").arg(k.font));
	bool older = false, any = false, first = true;
	const QString markBg = rgbaOf(k.accent, k.light ? 70 : 90);
	for (const Release &r : kReleases) {
		const QString v = QString::fromLatin1(r.version);
		QString rows;
		for (int i = 0; i < r.count; ++i) {
			const Note &n = r.notes[i];
			if (!noteMatches(n, q))
				continue;
			rows += QStringLiteral(
					"<tr><td width='44' valign='top' align='center' style='font-size:17pt'>%1</td>"
					"<td valign='top'><span style='color:%2;font-size:10.5pt;font-weight:700'>%3</span>"
					"<br><span style='color:%4'>%5</span></td></tr>")
					.arg(QString::fromUtf8(n.icon), k.text,
					     marked(QString::fromUtf8(n.title), q, markBg), k.muted,
					     marked(QString::fromUtf8(n.detail), q, markBg));
		}
		if (rows.isEmpty())
			continue;
		any = true;
		const bool seen = !since.isEmpty() && compareVersions(v, since) <= 0;
		if (seen && !older && q.isEmpty()) {
			h += QStringLiteral("<p style='margin:20px 0 8px;color:%1;font-size:8pt;font-weight:800'>"
					    "EARLIER UPDATES&nbsp;&nbsp;·&nbsp;&nbsp;you've seen these</p>")
				     .arg(k.faint);
			older = true;
		}
		QString pill;
		if (first && q.isEmpty())
			pill = QStringLiteral("&nbsp;&nbsp;<span style='background-color:%1;color:%2;font-size:7.5pt;"
					      "font-weight:800'>&nbsp;LATEST&nbsp;</span>")
				       .arg(k.accent, k.light ? QStringLiteral("#ffffff") : k.bg);
		else if (!since.isEmpty() && !seen)
			pill = QStringLiteral("&nbsp;&nbsp;<span style='background-color:%1;color:%2;font-size:7.5pt;"
					      "font-weight:800'>&nbsp;NEW&nbsp;</span>")
				       .arg(rgbaOf(k.accent2, 60), k.accent2);
		h += QStringLiteral(
			     "<a name='v%1'></a>"
			     "<table width='100%' border='1' cellspacing='0' cellpadding='14' "
			     "style='border-color:%2;border-style:solid;background-color:%3'><tr><td>"
			     "<span style='color:%4;font-size:15pt;font-weight:800'>%1</span>"
			     "&nbsp;&nbsp;<span style='color:%5;font-size:8.5pt'>%6</span>%7"
			     "<table width='100%' border='0' cellspacing='0' cellpadding='6' style='margin-top:6px'>%8"
			     "</table></td></tr></table><p style='margin:0;font-size:5pt'>&nbsp;</p>")
			     .arg(v, k.line, seen ? k.panel : k.card, seen ? k.muted : k.accent, k.faint,
				  QString::fromLatin1(r.date), pill, rows);
		first = false;
	}
	if (!any)
		h += QStringLiteral("<p align='center' style='margin-top:60px;color:%1'>Nothing in the notes matches "
				    "“%2”.</p>")
			     .arg(k.muted, q.toHtmlEscaped());
	h += QStringLiteral("</body></html>");
	return h;
}

QString dialogSheet(const Look &k)
{
	QString qss = QStringLiteral(R"QSS(
QDialog#gfxChangelog { background: @BG@; }
QFrame#gfxClHero { border: none; border-bottom: 1px solid @LINE@;
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @A40@, stop:0.5 @B15@, stop:1 @BG@); }
QLabel#gfxClTitle { color: @TEXT@; font-size: 20pt; font-weight: 800; background: transparent; @FONT@ }
QLabel#gfxClSub { color: @MUTED@; font-size: 9pt; background: transparent; @FONT@ }
QLabel#gfxClNew { color: @ACC@; background: @A20@; border: 1px solid @A50@; border-radius: 11px;
  padding: 3px 11px; font-size: 8.5pt; font-weight: 800; }
QPushButton#gfxClLook { color: @MUTED@; background: @PANEL@; border: 1px solid @LINE@; border-radius: 10px;
  padding: 5px 11px; font-weight: 600; }
QPushButton#gfxClLook:hover { color: @TEXT@; }
QPushButton#gfxClLook:checked { color: @TEXT@; background: @A20@; border: 1px solid @ACC@; }
QFrame#gfxClSide { background: @PANEL@; border: none; border-right: 1px solid @LINE@; }
QLabel#gfxClSideHead { color: @FAINT@; font-size: 8pt; font-weight: 800; background: transparent; }
QListWidget#gfxClTimeline { background: transparent; border: none; outline: 0; color: @MUTED@; @FONT@ }
QListWidget#gfxClTimeline::item { padding: 7px 8px; border-radius: 8px; margin: 1px 0; }
QListWidget#gfxClTimeline::item:hover { background: @A10@; color: @TEXT@; }
QListWidget#gfxClTimeline::item:selected { background: @A20@; color: @TEXT@; }
QLineEdit#gfxClSearch { background: @CARD@; color: @TEXT@; border: 1px solid @LINE@; border-radius: 10px;
  padding: 7px 10px; }
QLineEdit#gfxClSearch:focus { border: 1px solid @ACC@; }
QTextBrowser#gfxClBody { background: @BG@; border: none; }
QFrame#gfxClFoot { background: @PANEL@; border: none; border-top: 1px solid @LINE@; }
QLabel#gfxClFootNote { color: @FAINT@; font-size: 8.5pt; background: transparent; }
QPushButton#gfxClClose { color: @ONACC@; background: @ACC@; border: none; border-radius: 10px; padding: 7px 18px;
  font-weight: 800; }
QPushButton#gfxClClose:hover { background: @ACC2@; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @LINE@; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
)QSS");
	const QList<QPair<QString, QString>> tokens = {
		{QStringLiteral("@BG@"), k.bg},
		{QStringLiteral("@PANEL@"), k.panel},
		{QStringLiteral("@CARD@"), k.card},
		{QStringLiteral("@LINE@"), k.line},
		{QStringLiteral("@TEXT@"), k.text},
		{QStringLiteral("@MUTED@"), k.muted},
		{QStringLiteral("@FAINT@"), k.faint},
		{QStringLiteral("@ACC2@"), k.accent2},
		{QStringLiteral("@ACC@"), k.accent},
		{QStringLiteral("@ONACC@"), k.light || QColor(k.accent).lightnessF() < 0.55
						    ? QStringLiteral("#ffffff")
						    : QStringLiteral("#10121a")},
		{QStringLiteral("@A10@"), rgbaOf(k.accent, 26)},
		{QStringLiteral("@A20@"), rgbaOf(k.accent, 46)},
		{QStringLiteral("@A40@"), rgbaOf(k.accent, k.light ? 60 : 96)},
		{QStringLiteral("@A50@"), rgbaOf(k.accent, 128)},
		{QStringLiteral("@B15@"), rgbaOf(k.accent2, k.light ? 30 : 40)},
		{QStringLiteral("@FONT@"),
		 k.font.isEmpty() ? QString() : QStringLiteral("font-family: %1;").arg(k.font)},
	};
	for (const auto &t : tokens)
		qss.replace(t.first, t.second);
	return qss;
}

void showDialog(QWidget *parent, const QString &since)
{
	QDialog dlg(parent);
	dlg.setObjectName(QStringLiteral("gfxChangelog"));
	dlg.setWindowTitle(QStringLiteral("What's new in GoalForgeX for OBS"));
	dlg.setMinimumSize(660, 500);
	dlg.resize(860, 640);
	auto *root = new QVBoxLayout(&dlg);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	// How many releases are new since the last one the streamer saw.
	int fresh = 0;
	for (const Release &r : kReleases)
		if (!since.isEmpty() && compareVersions(QString::fromLatin1(r.version), since) > 0)
			++fresh;

	// ── Hero ──
	auto *hero = new QFrame(&dlg);
	hero->setObjectName(QStringLiteral("gfxClHero"));
	auto *hl = new QHBoxLayout(hero);
	hl->setContentsMargins(22, 18, 22, 18);
	hl->setSpacing(14);
	auto *mark = new QLabel(hero);
	mark->setFixedSize(54, 54);
	mark->setPixmap(theme::brandMark(54, dlg.devicePixelRatioF()));
	auto *titles = new QVBoxLayout();
	titles->setSpacing(2);
	auto *title = new QLabel(QStringLiteral("What's new"), hero);
	title->setObjectName(QStringLiteral("gfxClTitle"));
	auto *sub = new QLabel(QStringLiteral("GoalForgeX for OBS %1").arg(QString::fromLatin1(PLUGIN_VERSION)), hero);
	sub->setObjectName(QStringLiteral("gfxClSub"));
	titles->addWidget(title);
	titles->addWidget(sub);
	hl->addWidget(mark);
	hl->addLayout(titles, 1);
	if (fresh > 0) {
		auto *badge = new QLabel(fresh == 1 ? QStringLiteral("✨  1 new update")
						    : QStringLiteral("✨  %1 new updates").arg(fresh),
					 hero);
		badge->setObjectName(QStringLiteral("gfxClNew"));
		hl->addWidget(badge, 0, Qt::AlignVCenter);
	}
	// Look switcher
	auto *looks = new QHBoxLayout();
	looks->setSpacing(6);
	auto *lookGroup = new QButtonGroup(&dlg);
	lookGroup->setExclusive(true);
	const QList<QPair<QString, QString>> lookList = {{QStringLiteral("aurora"), QStringLiteral("🌌 Aurora")},
							 {QStringLiteral("terminal"), QStringLiteral("💻 Terminal")},
							 {QStringLiteral("paper"), QStringLiteral("📄 Paper")},
							 {QStringLiteral("dock"), QStringLiteral("🎛 My dock")}};
	QList<QPushButton *> lookBtns;
	for (const auto &l : lookList) {
		auto *b = new QPushButton(l.second, hero);
		b->setObjectName(QStringLiteral("gfxClLook"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		b->setProperty("gfxLook", l.first);
		b->setChecked(settings().changelogTheme == l.first);
		b->setToolTip(QStringLiteral("Show What's new in the %1 look")
				      .arg(l.second.mid(l.second.indexOf(QLatin1Char(' ')) + 1)));
		lookGroup->addButton(b);
		looks->addWidget(b);
		lookBtns << b;
	}
	hl->addLayout(looks);
	root->addWidget(hero);

	// ── Timeline + notes ──
	auto *mid = new QHBoxLayout();
	mid->setContentsMargins(0, 0, 0, 0);
	mid->setSpacing(0);
	auto *side = new QFrame(&dlg);
	side->setObjectName(QStringLiteral("gfxClSide"));
	side->setFixedWidth(190);
	auto *sl = new QVBoxLayout(side);
	sl->setContentsMargins(12, 14, 10, 10);
	sl->setSpacing(8);
	auto *sideHead = new QLabel(QStringLiteral("TIMELINE"), side);
	sideHead->setObjectName(QStringLiteral("gfxClSideHead"));
	auto *timeline = new QListWidget(side);
	timeline->setObjectName(QStringLiteral("gfxClTimeline"));
	timeline->setFocusPolicy(Qt::NoFocus);
	timeline->setCursor(Qt::PointingHandCursor);
	timeline->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	for (const Release &r : kReleases) {
		const QString v = QString::fromLatin1(r.version);
		const bool isNew = !since.isEmpty() && compareVersions(v, since) > 0;
		auto *item = new QListWidgetItem(QStringLiteral("%1  %2\n      %3")
							 .arg(isNew ? QStringLiteral("✦") : QStringLiteral("•"), v,
							      QString::fromLatin1(r.date)),
						 timeline);
		item->setData(Qt::UserRole, v);
		if (isNew) {
			QFont f = item->font();
			f.setBold(true);
			item->setFont(f);
		}
	}
	sl->addWidget(sideHead);
	sl->addWidget(timeline, 1);
	auto *right = new QVBoxLayout();
	right->setContentsMargins(18, 14, 18, 0);
	right->setSpacing(10);
	auto *search = new QLineEdit(&dlg);
	search->setObjectName(QStringLiteral("gfxClSearch"));
	search->setPlaceholderText(QStringLiteral("🔍  Search every update — try “TikTok”, “emotes” or “themes”"));
	search->setClearButtonEnabled(true);
	auto *body = new QTextBrowser(&dlg);
	body->setObjectName(QStringLiteral("gfxClBody"));
	body->setOpenLinks(false);
	body->document()->setDocumentMargin(4);
	right->addWidget(search);
	right->addWidget(body, 1);
	mid->addWidget(side);
	mid->addLayout(right, 1);
	root->addLayout(mid, 1);

	// ── Footer ──
	auto *foot = new QFrame(&dlg);
	foot->setObjectName(QStringLiteral("gfxClFoot"));
	auto *fl = new QHBoxLayout(foot);
	fl->setContentsMargins(18, 10, 18, 10);
	auto *footNote =
		new QLabel(QStringLiteral("Shows after every update · open it any time from ⚙ → What's new"), foot);
	footNote->setObjectName(QStringLiteral("gfxClFootNote"));
	auto *close = new QPushButton(QStringLiteral("Let's go"), foot);
	close->setObjectName(QStringLiteral("gfxClClose"));
	close->setCursor(Qt::PointingHandCursor);
	close->setDefault(true);
	fl->addWidget(footNote, 1);
	fl->addWidget(close);
	root->addWidget(foot);

	const auto render = [&dlg, body, search, since] {
		const Look k = lookFor(settings().changelogTheme);
		dlg.setStyleSheet(dialogSheet(k));
		const int keep = body->verticalScrollBar()->value();
		body->setHtml(notesHtml(since, search->text().trimmed(), k));
		body->verticalScrollBar()->setValue(keep);
	};
	for (QPushButton *b : lookBtns)
		QObject::connect(b, &QPushButton::clicked, &dlg, [b, render] {
			settings().changelogTheme = b->property("gfxLook").toString();
			saveSettings();
			render();
		});
	QObject::connect(search, &QLineEdit::textChanged, &dlg, [render, body](const QString &) {
		render();
		body->verticalScrollBar()->setValue(0);
	});
	QObject::connect(timeline, &QListWidget::itemClicked, &dlg, [body, search](QListWidgetItem *item) {
		if (!search->text().isEmpty())
			search->clear();
		body->scrollToAnchor(QStringLiteral("v") + item->data(Qt::UserRole).toString());
	});
	QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
	render();
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
