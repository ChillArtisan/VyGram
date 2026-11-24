// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_infra.h"

#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/ayu_worker.h"
#include "ayu/data/ayu_database.h"
#include "ayu/ui/ayu_logo.h"
#include "features/translator/ayu_translator.h"
#include "ui/chat/chat_style_radius.h"
#include "utils/rc_manager.h"
#include "window/themes/window_theme.h"

#include <QtGui/QFontDatabase>
#include <QApplication>
#include <QWidget>

#ifdef Q_OS_WIN
#include "ayu/utils/windows_utils.h"
#endif

namespace AyuInfra {

void initUiSettings() {
	Window::Theme::InitializeFreshProfile();
	const auto fontId = QFontDatabase::addApplicationFont(
		u":/gui/fonts/JetBrainsMono-Regular.ttf"_q);
	if (fontId < 0) {
		LOG(("AyuGram: could not load the bundled Terminal font."));
	}
	applyProtectedUiSettings();
}

void applyProtectedUiSettings() {
	const auto &settings = AyuSettings::getInstance();
	AyuUiSettings::setMonoFont(settings.monoFont());
	AyuUiSettings::setWideMultiplier(settings.wideMultiplier());
	AyuUiSettings::setMaterialSwitches(settings.materialSwitches());
	AyuUiSettings::setAvatarCorners(settings.avatarCorners());
	Ui::SetAppliedBubbleRadius(settings.messageBubbleRadius());
	if (QApplication::instance()) {
		for (const auto widget : QApplication::allWidgets()) {
			widget->update();
		}
	}
}

void initDatabase() {
	AyuDatabase::initialize();
}

void initWorker() {
	AyuWorker::initialize();
}

void initRCManager() {
	RCManager::getInstance().start();
}

void initTranslator() {
	Ayu::Translator::TranslateManager::init();
}

void initIcon() {
#ifdef Q_OS_WIN
	AyuAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif
}

void init() {
	initDatabase();
	initUiSettings();
	initIcon();
	initWorker();
	initRCManager();
	initTranslator();
}

}
