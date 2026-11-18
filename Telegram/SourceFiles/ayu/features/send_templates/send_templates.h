#pragma once

#include "ui/chat/attach/attach_prepare.h"

#include <QDateTime>

namespace Ayu::SendTemplates {

enum class Type {
	Round,
	Voice,
	Video,
	Photo,
};

struct Entry {
	QString id;
	QString path;
	QString name;
	QString caption;
	Type type = Type::Round;
};

enum class Error {
	None,
	File,
	Size,
	Format,
};

struct Prepared {
	Error error = Error::None;
	QByteArray bytes;
	Ui::PreparedList list;
	QDateTime modified;
	crl::time duration = 0;
};

inline constexpr auto kMaxEntries = 128;
inline constexpr auto kMaxNameLength = 128;
inline constexpr auto kMaxCaptionLength = 1024;
inline constexpr auto kMinDelay = 1;
inline constexpr auto kMaxDelay = 60;

[[nodiscard]] std::optional<std::vector<Entry>> Load(uint64 accountId);
[[nodiscard]] bool Put(uint64 accountId, const Entry &entry);
[[nodiscard]] bool Remove(uint64 accountId, const QString &id);
[[nodiscard]] Prepared Prepare(const Entry &entry);
[[nodiscard]] bool RefreshRoundMetadata(Prepared &prepared);
[[nodiscard]] bool Unchanged(const Entry &entry, const Prepared &prepared);

} // namespace Ayu::SendTemplates
