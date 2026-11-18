/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "logs.h"

#include "platform/platform_specific.h"
#include "core/crash_reports.h"
#include "core/launcher.h"

// AyuGram includes
#include <atomic>


namespace Logs {
namespace {

std::atomic<bool> LoggingInitialized = false;

[[maybe_unused]] void MoveOldDataFiles(const QString &wasDir) {
	if (wasDir.isEmpty()) {
		return;
	}
	QFile data(wasDir + "data"), dataConfig(wasDir + "data_config"), tdataConfig(wasDir + "tdata/config");
	if (data.exists() && dataConfig.exists() && !QFileInfo::exists(cWorkingDir() + "data") && !QFileInfo::exists(cWorkingDir() + "data_config")) { // move to home dir
		LOG(("Copying data to home dir '%1' from '%2'").arg(cWorkingDir(), wasDir));
		if (data.copy(cWorkingDir() + "data")) {
			LOG(("Copied 'data' to home dir"));
			if (dataConfig.copy(cWorkingDir() + "data_config")) {
				LOG(("Copied 'data_config' to home dir"));
				bool tdataGood = true;
				if (tdataConfig.exists()) {
					tdataGood = false;
					QDir().mkpath(cWorkingDir() + "tdata");
					if (tdataConfig.copy(cWorkingDir() + "tdata/config")) {
						LOG(("Copied 'tdata/config' to home dir"));
						tdataGood = true;
					} else {
						LOG(("Copied 'data' and 'data_config', but could not copy 'tdata/config'!"));
					}
				}
				if (tdataGood) {
					if (data.remove()) {
						LOG(("Removed 'data'"));
					} else {
						LOG(("Could not remove 'data'"));
					}
					if (dataConfig.remove()) {
						LOG(("Removed 'data_config'"));
					} else {
						LOG(("Could not remove 'data_config'"));
					}
					if (!tdataConfig.exists() || tdataConfig.remove()) {
						LOG(("Removed 'tdata/config'"));
					} else {
						LOG(("Could not remove 'tdata/config'"));
					}
					QDir().rmdir(wasDir + "tdata");
				}
			} else {
				LOG(("Copied 'data', but could not copy 'data_config'!!"));
			}
		} else {
			LOG(("Could not copy 'data'!"));
		}
	}
}

} // namespace

void SetDebugEnabled(bool) {
}

bool DebugEnabled() {
	return false;
}

bool WritingEntry() {
	return false;
}

void start() {
	Assert(!LoggingInitialized.load());

	auto &launcher = Core::Launcher::Instance();
	if (!launcher.checkPortableVersionFolder()) {
		return;
	}

	LoggingInitialized = true;
	if (cWorkingDir().isEmpty()) {
#if (!defined Q_OS_WIN && !defined _DEBUG) || defined Q_OS_WINRT || defined OS_WIN_STORE || defined OS_MAC_STORE
		cForceWorkingDir(psAppDataPath());
#else // (!Q_OS_WIN && !_DEBUG) || Q_OS_WINRT || OS_WIN_STORE || OS_MAC_STORE
		cForceWorkingDir(cExeDir());
		if (!QFileInfo(cWorkingDir()).isWritable()) {
			cForceWorkingDir(psAppDataPath());
		}
#endif // (!Q_OS_WIN && !_DEBUG) || Q_OS_WINRT || OS_WIN_STORE || OS_MAC_STORE
	}

	launcher.validateCustomWorkingDir();

#ifndef Q_OS_WINRT
	QDir::setCurrent(cWorkingDir());
#endif // !Q_OS_WINRT

	QDir().mkpath(cWorkingDir() + u"tdata"_q);

	launcher.workingFolderReady();
	CrashReports::StartCatching();

#ifdef Q_OS_WIN
	if (cWorkingDir() == psAppDataPath()) {
		MoveOldDataFiles(psAppDataPathOld());
	}
#elif !defined Q_OS_MAC && !defined _DEBUG
	MoveOldDataFiles(launcher.initialWorkingDir());
#endif
}

void finish() {
	LoggingInitialized = false;
	CrashReports::FinishCatching();
}

bool started() {
	return LoggingInitialized.load();
}

bool instanceChecked() {
	return started();
}

void multipleInstances() {
}

void closeMain() {
}

void writeMain(const QString &) {
}

void writeDebug(const QString &) {
}

void writeMtp(int32, const QString &) {
}

QString full() {
	return QString();
}

} // namespace Logs
