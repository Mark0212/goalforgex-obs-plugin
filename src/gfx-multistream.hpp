/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include <QHash>
#include <cstdint>
#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

// "GoalForgeX Multistream" — go live on several platforms at once from OBS.
// Each destination is an extra RTMP(S) output next to OBS's own stream:
//  • "Same as main stream" reuses the encoders OBS is already running, so an
//    extra platform costs upload bandwidth only (it runs while the main stream
//    is live);
//  • "Own encoder" encodes again at its own bitrate (more CPU/GPU, but can run
//    on its own).
// GoalForgeX extras (Pro): stream keys filled in from the connected Twitch /
// Kick accounts, and one title / category pushed to every platform.
// Destinations and keys are saved per OBS install, keys encrypted (DPAPI).
class GfxMultistreamDock : public QWidget {
public:
	explicit GfxMultistreamDock(QWidget *parent = nullptr);
	~GfxMultistreamDock() override;

	// Settings → Appearance changed.
	void applyAppearance();
	// OBS's own stream started / is stopping (from the frontend event callback).
	void mainStreamStarted();
	void mainStreamStopping();
	// OBS is closing: stop everything now.
	void shutdown();
	// Output signals (start / stop / reconnect), marshalled onto the UI thread.
	void onOutputSignal(const QString &id, const QString &what, int code);

	struct Destination {
		QString id;
		QString name;
		QString platform; // twitch | kick | youtube | tiktok | custom
		QString server;
		QString key;                 // in memory only; saved encrypted
		bool withMain = true;        // go live / stop together with OBS's own stream
		bool ownEncoder = false;     // false = share the main stream's encoders
		int bitrate = 6000;          // kbps, own encoder only
		bool keyFromAccount = false; // filled in from GoalForgeX (refreshed before each start)
	};

private:
	struct Runner; // one live output

	void buildUi();
	void load();
	void save();
	void renderList();
	void renderStatus();
	// The "Main stream" row: what's set in OBS → Settings → Stream, live stats, Start/Stop.
	void renderMain();
	void editDestination(const QString &id); // empty = new
	void startDest(const QString &id, bool quiet);
	void stopDest(const QString &id);
	void startAll();
	void stopAll();
	void releaseRunner(const QString &id, bool force);
	void tick();
	void setMsg(const QString &text, bool error = false);
	Destination *find(const QString &id);
	// Pro extras (need the main GoalForgeX dock connected).
	void fetchKeys(std::function<void(const QJsonObject &keys, const QString &error)> done);
	void loadStreamInfo();
	void pushStreamInfo();
	void reconnectTwitch();

	QList<Destination> dests_;
	QHash<QString, Runner *> runners_;
	QTimer tick_;
	QTimer mainRefresh_; // re-reads OBS → Settings → Stream while visible
	bool mainLive_ = false;

	struct Row {
		QFrame *box = nullptr;
		QLabel *state = nullptr;
		QLabel *stats = nullptr;
		QPushButton *toggle = nullptr;
	};
	QHash<QString, Row> rows_;

	// Main stream row
	QLabel *mainIcon_ = nullptr;
	QLabel *mainName_ = nullptr;
	QLabel *mainState_ = nullptr;
	QLabel *mainStats_ = nullptr;
	QLabel *mainKey_ = nullptr;
	QPushButton *mainBtn_ = nullptr;
	QString mainPlatform_;
	qint64 mainStartMs_ = 0;
	uint64_t mainLastBytes_ = 0;
	qint64 mainLastMs_ = 0;
	int mainKbps_ = 0;
	QString mainKeyChecked_; // the main key last compared with the connected Twitch account
	QString mainKeyCheck_;   // "" | match | mismatch

	QLabel *mark_ = nullptr;
	QLabel *summary_ = nullptr;
	QLabel *bandwidth_ = nullptr;
	QVBoxLayout *list_ = nullptr;
	QLabel *empty_ = nullptr;
	QPushButton *allBtn_ = nullptr;
	QLabel *msg_ = nullptr;
	QTimer msgClear_;
	// Title & category
	QFrame *infoCard_ = nullptr;
	QLineEdit *title_ = nullptr;
	QLineEdit *category_ = nullptr;
	QCheckBox *toTwitch_ = nullptr;
	QCheckBox *toKick_ = nullptr;
	QPushButton *pushBtn_ = nullptr;
	QLabel *infoNote_ = nullptr;
	bool infoLoaded_ = false;
};
