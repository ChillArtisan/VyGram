/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/assertion.h"
#include "base/debug_log.h"

namespace Logs {

void SetDebugEnabled(bool enabled);
bool DebugEnabled();
[[nodiscard]] bool WritingEntry();

void start();
bool started();
void finish();

bool instanceChecked();
void multipleInstances();

void closeMain();

void writeMain(const QString &v);
void writeDebug(const QString &v);
void writeMtp(int32 dc, const QString &v);

QString full();

inline const char *b(bool v) {
	return v ? "[TRUE]" : "[FALSE]";
}

struct MemoryBuffer {
	MemoryBuffer(const void *, uint32) {
	}
	QString str() const {
		return QString();
	}

};

inline MemoryBuffer mb(const void *ptr, uint32 size) {
	return MemoryBuffer(ptr, size);
}

} // namespace Logs

// Protocol bodies must not be evaluated or retained by this private fork.
#define MTP_LOG(dc, msg) do { } while (false)
