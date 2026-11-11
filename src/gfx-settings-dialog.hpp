/*
GoalForgeX for OBS
Copyright (C) 2026 GoalForgeX <support@goalforgex.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/
#pragma once

#include "gfx-settings.hpp"

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>

class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class QVBoxLayout;
class QWidget;

// The ⚙ Settings window (2.14): a sidebar of pages with search, cards of
// switches, sliders and colour pickers, and a live chat preview. Every change
// is written straight into gfx::settings() and previewed on the docks at once;
// Cancel (or closing the window) puts the settings back exactly as they were.
// The caller saves and applies the side effects after Save (exec() == Accepted).
class GfxSettingsDialog : public QDialog {
public:
	struct Options {
		QString startPage;                   // page title to open on, e.g. "MultiChat"
		QString noticesNote;                 // extra line under Event notices (missing permissions)
		std::function<void()> preview;       // re-apply the look to every dock (debounced)
		std::function<void()> openChangelog; // About → What's new
	};
	GfxSettingsDialog(QWidget *parent, Options options);

	void reject() override;

	// Page titles, in order (the startPage values that work).
	static QStringList pageTitles();

private:
	// One searchable line of a page.
	struct Row {
		int page = 0;
		QWidget *widget = nullptr;
		QFrame *card = nullptr;
		QString hay;                 // lower-case search text
		std::function<void()> reset; // back to the default (Reset page)
	};
	struct Card {
		int page = 0;
		QFrame *frame = nullptr;
	};

	void buildPages();
	QVBoxLayout *addPage(const QString &icon, const QString &title, const QString &blurb);
	QVBoxLayout *addCard(QVBoxLayout *page, const QString &icon, const QString &title,
			     const QString &sub = QString());
	QWidget *rowFrame(QVBoxLayout *card, const QString &title, const QString &hint, QWidget *control,
			  bool controlBelow = false);
	void addRow(QWidget *row, const QString &title, const QString &hint, std::function<void()> reset);
	void addToggle(QVBoxLayout *card, const QString &title, const QString &hint, bool gfx::Settings::*member,
		       bool indent = false);
	void addSlider(QVBoxLayout *card, const QString &title, const QString &hint, int lo, int hi, int step,
		       std::function<int()> get, std::function<void(int)> set, std::function<QString(int)> fmt,
		       int defaultValue);
	void addSlider(QVBoxLayout *card, const QString &title, const QString &hint, int gfx::Settings::*member, int lo,
		       int hi, int step, std::function<QString(int)> fmt);
	void addColor(QVBoxLayout *card, const QString &title, const QString &hint, QString gfx::Settings::*member,
		      const QString &badge, const QString &badgeColor, const QString &sampleName);
	void addChoice(QVBoxLayout *card, const QString &title, const QString &hint, QString gfx::Settings::*member,
		       const QList<QPair<QString, QString>> &choices);
	void addIntChoice(QVBoxLayout *card, const QString &title, const QString &hint, int gfx::Settings::*member,
			  const QList<QPair<int, QString>> &choices);
	void addNote(QVBoxLayout *card, const QString &text);
	QWidget *buildThemeGrid(QWidget *parent);
	QWidget *buildAccentRow(QWidget *parent);

	void changed();
	void restyle();
	void renderPreview();
	void applySearch(const QString &text);
	void resetCurrentPage();
	void selectPage(int index);

	Options options_;
	gfx::Settings before_;
	QList<Row> rows_;
	QList<Card> cards_;
	QList<std::function<void()>> syncs_; // re-read settings into widgets (after a reset)
	int buildingPage_ = 0;
	QFrame *buildingCard_ = nullptr;

	QFrame *hero_ = nullptr;
	QLabel *heroMark_ = nullptr;
	QLineEdit *search_ = nullptr;
	QListWidget *nav_ = nullptr;
	QStackedWidget *stack_ = nullptr;
	QList<QLabel *> emptyNotes_; // "Nothing on this page matches" per page
	QTextBrowser *preview_ = nullptr;
	QTimer *previewTimer_ = nullptr;
	QLabel *footNote_ = nullptr;
	QString styleKey_; // theme + accent + text size the window was last styled for
};
