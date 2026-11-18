// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/ayu_database.h"

#include "ayu/data/entities.h"
#include "ayu/libs/sqlite/sqlite_orm.h"
#include "ayu/security/secure_database.h"

#include <openssl/crypto.h>
#include <QDir>
#include <QFile>

#include <mutex>

using namespace sqlite_orm;

std::mutex databaseMutex;

namespace {

struct PrivateSetting {
	std::string name;
	std::vector<char> value;
};

struct ArchivedMedia {
	ID userId = 0;
	ID mediaId = 0;
	int mediaType = 0;
	std::vector<char> bytes;
};

auto makeStorage(const std::string &path) {
	return make_storage(
	path,
	make_table<PrivateSetting>(
		"PrivateSetting",
		make_column("name", &PrivateSetting::name, primary_key()),
		make_column("value", &PrivateSetting::value)
	),
	make_table<ArchivedMedia>(
		"ArchivedMedia",
		make_column("userId", &ArchivedMedia::userId),
		make_column("mediaId", &ArchivedMedia::mediaId),
		make_column("mediaType", &ArchivedMedia::mediaType),
		make_column("bytes", &ArchivedMedia::bytes),
		primary_key(&ArchivedMedia::userId, &ArchivedMedia::mediaId,
			&ArchivedMedia::mediaType)
	),
	make_table<SchemaVersion>(
		"SchemaVersion",
		make_column("id", &SchemaVersion::id, primary_key()),
		make_column("version", &SchemaVersion::version)
	),
	make_index("idx_deleted_message_userId_dialogId_topicId_messageId",
			   column<DeletedMessage>(&DeletedMessage::userId),
			   column<DeletedMessage>(&DeletedMessage::dialogId),
			   column<DeletedMessage>(&DeletedMessage::topicId),
			   column<DeletedMessage>(&DeletedMessage::messageId)),
	make_index("idx_edited_message_userId_dialogId_messageId",
			   column<EditedMessage>(&EditedMessage::userId),
			   column<EditedMessage>(&EditedMessage::dialogId),
			   column<EditedMessage>(&EditedMessage::messageId)),
	make_table<DeletedMessage>(
		"DeletedMessage",
		make_column("fakeId", &DeletedMessage::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedMessage::userId),
		make_column("dialogId", &DeletedMessage::dialogId),
		make_column("groupedId", &DeletedMessage::groupedId),
		make_column("peerId", &DeletedMessage::peerId),
		make_column("fromId", &DeletedMessage::fromId),
		make_column("topicId", &DeletedMessage::topicId),
		make_column("messageId", &DeletedMessage::messageId),
		make_column("date", &DeletedMessage::date),
		make_column("flags", &DeletedMessage::flags),
		make_column("editDate", &DeletedMessage::editDate),
		make_column("views", &DeletedMessage::views),
		make_column("fwdFlags", &DeletedMessage::fwdFlags),
		make_column("fwdFromId", &DeletedMessage::fwdFromId),
		make_column("fwdName", &DeletedMessage::fwdName),
		make_column("fwdDate", &DeletedMessage::fwdDate),
		make_column("fwdPostAuthor", &DeletedMessage::fwdPostAuthor),
		make_column("replyFlags", &DeletedMessage::replyFlags),
		make_column("replyMessageId", &DeletedMessage::replyMessageId),
		make_column("replyPeerId", &DeletedMessage::replyPeerId),
		make_column("replyTopId", &DeletedMessage::replyTopId),
		make_column("replyForumTopic", &DeletedMessage::replyForumTopic),
		make_column("replySerialized", &DeletedMessage::replySerialized),
		make_column("entityCreateDate", &DeletedMessage::entityCreateDate),
		make_column("text", &DeletedMessage::text),
		make_column("textEntities", &DeletedMessage::textEntities),
		make_column("mediaPath", &DeletedMessage::mediaPath),
		make_column("hqThumbPath", &DeletedMessage::hqThumbPath),
		make_column("documentType", &DeletedMessage::documentType),
		make_column("documentSerialized", &DeletedMessage::documentSerialized),
		make_column("thumbsSerialized", &DeletedMessage::thumbsSerialized),
		make_column("documentAttributesSerialized", &DeletedMessage::documentAttributesSerialized),
		make_column("mimeType", &DeletedMessage::mimeType)
	),
	make_table<EditedMessage>(
		"EditedMessage",
		make_column("fakeId", &EditedMessage::fakeId, primary_key().autoincrement()),
		make_column("userId", &EditedMessage::userId),
		make_column("dialogId", &EditedMessage::dialogId),
		make_column("groupedId", &EditedMessage::groupedId),
		make_column("peerId", &EditedMessage::peerId),
		make_column("fromId", &EditedMessage::fromId),
		make_column("topicId", &EditedMessage::topicId),
		make_column("messageId", &EditedMessage::messageId),
		make_column("date", &EditedMessage::date),
		make_column("flags", &EditedMessage::flags),
		make_column("editDate", &EditedMessage::editDate),
		make_column("views", &EditedMessage::views),
		make_column("fwdFlags", &EditedMessage::fwdFlags),
		make_column("fwdFromId", &EditedMessage::fwdFromId),
		make_column("fwdName", &EditedMessage::fwdName),
		make_column("fwdDate", &EditedMessage::fwdDate),
		make_column("fwdPostAuthor", &EditedMessage::fwdPostAuthor),
		make_column("replyFlags", &EditedMessage::replyFlags),
		make_column("replyMessageId", &EditedMessage::replyMessageId),
		make_column("replyPeerId", &EditedMessage::replyPeerId),
		make_column("replyTopId", &EditedMessage::replyTopId),
		make_column("replyForumTopic", &EditedMessage::replyForumTopic),
		make_column("replySerialized", &EditedMessage::replySerialized),
		make_column("entityCreateDate", &EditedMessage::entityCreateDate),
		make_column("text", &EditedMessage::text),
		make_column("textEntities", &EditedMessage::textEntities),
		make_column("mediaPath", &EditedMessage::mediaPath),
		make_column("hqThumbPath", &EditedMessage::hqThumbPath),
		make_column("documentType", &EditedMessage::documentType),
		make_column("documentSerialized", &EditedMessage::documentSerialized),
		make_column("thumbsSerialized", &EditedMessage::thumbsSerialized),
		make_column("documentAttributesSerialized", &EditedMessage::documentAttributesSerialized),
		make_column("mimeType", &EditedMessage::mimeType)
	),
	make_table<DeletedDialog>(
		"DeletedDialog",
		make_column("fakeId", &DeletedDialog::fakeId, primary_key().autoincrement()),
		make_column("userId", &DeletedDialog::userId),
		make_column("dialogId", &DeletedDialog::dialogId),
		make_column("peerId", &DeletedDialog::peerId),
		make_column("folderId", &DeletedDialog::folderId),
		make_column("topMessage", &DeletedDialog::topMessage),
		make_column("lastMessageDate", &DeletedDialog::lastMessageDate),
		make_column("flags", &DeletedDialog::flags),
		make_column("entityCreateDate", &DeletedDialog::entityCreateDate)
	),
	make_table<RegexFilter>(
		"RegexFilter",
		make_column("id", &RegexFilter::id, primary_key()),
		make_column("text", &RegexFilter::text),
		make_column("enabled", &RegexFilter::enabled),
		make_column("reversed", &RegexFilter::reversed),
		make_column("caseInsensitive", &RegexFilter::caseInsensitive),
		make_column("dialogId", &RegexFilter::dialogId)
	),
	make_table<RegexFilterGlobalExclusion>(
		"RegexFilterGlobalExclusion",
		make_column("fakeId", &RegexFilterGlobalExclusion::fakeId, primary_key().autoincrement()),
		make_column("dialogId", &RegexFilterGlobalExclusion::dialogId),
		make_column("filterId", &RegexFilterGlobalExclusion::filterId)
	),
	make_table<SpyMessageRead>(
		"SpyMessageRead",
		make_column("fakeId", &SpyMessageRead::fakeId, primary_key().autoincrement()),
		make_column("userId", &SpyMessageRead::userId),
		make_column("dialogId", &SpyMessageRead::dialogId),
		make_column("messageId", &SpyMessageRead::messageId),
		make_column("entityCreateDate", &SpyMessageRead::entityCreateDate)
	),
	make_table<SpyMessageContentsRead>(
		"SpyMessageContentsRead",
		make_column("fakeId", &SpyMessageContentsRead::fakeId, primary_key().autoincrement()),
		make_column("userId", &SpyMessageContentsRead::userId),
		make_column("dialogId", &SpyMessageContentsRead::dialogId),
		make_column("messageId", &SpyMessageContentsRead::messageId),
		make_column("entityCreateDate", &SpyMessageContentsRead::entityCreateDate)
	)
);

}

using ArchiveStorage = decltype(makeStorage(std::string()));

struct ArchiveState {
	std::unique_ptr<ArchiveStorage> database;
	QByteArray key;
	bool unlocked = false;

	void clear() {
		unlocked = false;
		database.reset();
		if (!key.isEmpty()) {
			OPENSSL_cleanse(key.data(), key.size());
			key.clear();
		}
	}

	~ArchiveState() {
		clear();
	}
};

ArchiveState archive;

ArchiveStorage &openedStorage() {
	if (!archive.unlocked || !archive.database) {
		throw std::runtime_error("Secure archive is locked.");
	}
	return *archive.database;
}

} // namespace

namespace AyuMigrations {

void migrateToV1(ArchiveStorage &storage) {
	try {
		storage.sync_schema(true);
		LOG(("Migration to V1 successful."));
	} catch (...) {
		LOG(("Migration to V1 failed."));
		throw;
	}
}

}

void runMigrations(ArchiveStorage &storage) {
	constexpr int kLatestVersion = 1;

	const std::map<int, Fn<void(ArchiveStorage &)>> migrations = {
		{1, AyuMigrations::migrateToV1},
	};

	int currentVersion = 0;
	try {
		if (auto versionRow = storage.get_pointer<SchemaVersion>(1)) {
			currentVersion = versionRow->version;
		} else {
			storage.insert(SchemaVersion{1, 0});
		}
	} catch (...) {
		LOG(("No SchemaVersion, assuming 0"));
		storage.insert(SchemaVersion{1, 0});
	}

	if (currentVersion >= kLatestVersion) {
		LOG(("Database is ok"));
		return;
	}

	LOG(("Database version: %1. Latest version: %2.").arg(currentVersion).arg(kLatestVersion));

	for (int v = currentVersion + 1; v <= kLatestVersion; ++v) {
		if (migrations.contains(v)) {
			try {
				LOG(("Migration for version: %1").arg(v));
				storage.begin_transaction();

				migrations.at(v)(storage);

				storage.update_all(set(c(&SchemaVersion::version) = v), where(c(&SchemaVersion::id) == 1));
				storage.commit();
				LOG(("Applied migration for version: %1.").arg(v));
			} catch (...) {
				storage.rollback();
				LOG(("Failed to apply migration for version: %1.").arg(v));
				throw;
			}
		}
	}
}

namespace AyuDatabase {

void initialize() {
	std::lock_guard lock(databaseMutex);
	if (archive.database) {
		return;
	}
	const auto path = cWorkingDir() + u"tdata/ayudata.secure.db"_q;
	archive.database = std::make_unique<ArchiveStorage>(makeStorage(path.toStdString()));
	archive.database->on_open = [](sqlite3 *db) {
		Ayu::Security::Configure(db, archive.key.constData(), archive.key.size());
	};
}

bool unlock(const QByteArray &profileKey) {
	std::lock_guard lock(databaseMutex);
	if (archive.unlocked) {
		return archive.key == profileKey;
	}
	if (profileKey.size() != 256) {
		return false;
	}
	archive.key = QByteArray(profileKey.constData(), profileKey.size());
	try {
		const auto directory = cWorkingDir() + u"tdata/"_q;
		if (!QDir().mkpath(directory)) {
			throw std::runtime_error("Could not open archive directory.");
		}
		const auto backups = QDir(directory).entryList(
			{ u"ayudata_*.db"_q, u"ayudata_*.db-*"_q },
			QDir::Files | QDir::Hidden | QDir::System);
		if (!backups.isEmpty()) {
			LOG(("Legacy archive backups require explicit encrypted recovery."));
			throw std::runtime_error("Legacy archive backups require recovery.");
		}
		const auto path = directory + u"ayudata.secure.db"_q;
		const auto legacy = directory + u"ayudata.db"_q;
		if (QFile::exists(legacy)) {
			if (QFile::exists(path)) {
				throw std::runtime_error("Legacy archive requires recovery.");
			}
			const auto staging = path + u".migrating"_q;
			if (QFile::exists(staging)) {
				throw std::runtime_error("Archive migration requires recovery.");
			}
			Ayu::Security::ExportLegacy(legacy.toStdString(), staging.toStdString(),
				archive.key.constData(), archive.key.size());
			if (!QFile::rename(staging, path) || !QFile::remove(legacy)) {
				throw std::runtime_error("Could not finalize archive migration.");
			}
			for (const auto &suffix : { u"-wal"_q, u"-shm"_q, u"-journal"_q }) {
				const auto sidecar = legacy + suffix;
				if (QFile::exists(sidecar) && !QFile::remove(sidecar)) {
					throw std::runtime_error("Could not remove legacy archive sidecar.");
				}
			}
		}
		for (const auto &suffix : { u"-wal"_q, u"-shm"_q, u"-journal"_q }) {
			if (QFile::exists(legacy + suffix)) {
				throw std::runtime_error("Legacy archive sidecar requires recovery.");
			}
		}
		archive.database = std::make_unique<ArchiveStorage>(makeStorage(path.toStdString()));
		archive.database->on_open = [](sqlite3 *db) {
			Ayu::Security::Configure(db, archive.key.constData(), archive.key.size());
			Ayu::Security::Verify(db);
		};
		archive.database->open_forever();
		archive.database->sync_schema(true);
		runMigrations(*archive.database);
		archive.database->sync_schema(true);
		archive.unlocked = true;
		return true;
	} catch (...) {
		archive.clear();
		LOG(("Encrypted archive could not be opened; existing data was preserved."));
		return false;
	}
}

bool isUnlocked() {
	std::lock_guard lock(databaseMutex);
	return archive.unlocked;
}

void shutdown() {
	std::lock_guard lock(databaseMutex);
	archive.clear();
}

bool storePrivateSetting(const std::string &name, const QByteArray &value) {
	std::lock_guard lock(databaseMutex);
	if (value.isEmpty()) {
		return false;
	}
	try {
		openedStorage().replace(PrivateSetting{ name,
			std::vector<char>(value.constData(), value.constData() + value.size()) });
		return true;
	} catch (...) {
		LOG(("Failed to store encrypted private setting."));
		return false;
	}
}

QByteArray loadPrivateSetting(const std::string &name) {
	return tryLoadPrivateSetting(name).value_or(QByteArray());
}

std::optional<QByteArray> tryLoadPrivateSetting(const std::string &name) {
	std::lock_guard lock(databaseMutex);
	try {
		const auto row = openedStorage().get_pointer<PrivateSetting>(name);
		return row ? QByteArray(row->value.data(), row->value.size()) : QByteArray();
	} catch (...) {
		LOG(("Failed to load encrypted private setting."));
		return std::nullopt;
	}
}

bool storeMedia(ID userId, ID mediaId, int mediaType, const QByteArray &bytes) {
	std::lock_guard lock(databaseMutex);
	if (bytes.isEmpty()) {
		return false;
	}
	try {
		openedStorage().replace(ArchivedMedia{ userId, mediaId, mediaType,
			std::vector<char>(bytes.constData(), bytes.constData() + bytes.size()) });
		return true;
	} catch (...) {
		LOG(("Failed to store encrypted media."));
		return false;
	}
}

QByteArray loadMedia(ID userId, ID mediaId, int mediaType) {
	std::lock_guard lock(databaseMutex);
	try {
		const auto rows = openedStorage().get_all<ArchivedMedia>(where(
			c(&ArchivedMedia::userId) == userId
			and c(&ArchivedMedia::mediaId) == mediaId
			and c(&ArchivedMedia::mediaType) == mediaType), limit(1));
		return rows.empty() ? QByteArray() : QByteArray(
			rows.front().bytes.data(), rows.front().bytes.size());
	} catch (...) {
		LOG(("Failed to load encrypted media."));
		return {};
	}
}

void addEditedMessage(const EditedMessage &message) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().begin_transaction();
		openedStorage().insert(message);
		openedStorage().commit();
	} catch (std::exception &ex) {
		try {
			openedStorage().rollback();
		} catch (...) {
		}
		LOG(("Failed to save edited message for some reason: %1").arg(ex.what()));
	}
}

std::vector<EditedMessage> getEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit) {
	std::lock_guard lock(databaseMutex);
	try {
		return openedStorage().get_all<EditedMessage>(
			where(
				column<EditedMessage>(&EditedMessage::userId) == userId and
				column<EditedMessage>(&EditedMessage::dialogId) == dialogId and
				column<EditedMessage>(&EditedMessage::messageId) == messageId and
				(column<EditedMessage>(&EditedMessage::fakeId) > minId or minId == 0) and
				(column<EditedMessage>(&EditedMessage::fakeId) < maxId or maxId == 0)
			),
			order_by(column<EditedMessage>(&EditedMessage::fakeId)).desc(),
			limit(totalLimit)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get edited messages: %1").arg(ex.what()));
		return {};
	}
}

bool hasRevisions(ID userId, ID dialogId, ID messageId) {
	std::lock_guard lock(databaseMutex);
	try {
		return !openedStorage().select(
			columns(column<EditedMessage>(&EditedMessage::messageId)),
			where(
				column<EditedMessage>(&EditedMessage::userId) == userId and
				column<EditedMessage>(&EditedMessage::dialogId) == dialogId and
				column<EditedMessage>(&EditedMessage::messageId) == messageId
			),
			limit(1)
		).empty();
	} catch (std::exception &ex) {
		LOG(("Failed to check if message has revisions: %1").arg(ex.what()));
		return false;
	}
}

void addDeletedMessage(const DeletedMessage &message) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().begin_transaction();
		const auto previous = openedStorage().get_all<DeletedMessage>(where(
			column<DeletedMessage>(&DeletedMessage::userId) == message.userId
			and column<DeletedMessage>(&DeletedMessage::dialogId) == message.dialogId
			and column<DeletedMessage>(&DeletedMessage::messageId) == message.messageId),
			order_by(column<DeletedMessage>(&DeletedMessage::fakeId)).desc(), limit(1));
		if (previous.empty()) {
			openedStorage().insert(message);
		} else {
			auto updated = message;
			updated.fakeId = previous.front().fakeId;
			openedStorage().replace(updated);
			openedStorage().remove_all<DeletedMessage>(where(
				column<DeletedMessage>(&DeletedMessage::userId) == message.userId
				and column<DeletedMessage>(&DeletedMessage::dialogId) == message.dialogId
				and column<DeletedMessage>(&DeletedMessage::messageId) == message.messageId
				and column<DeletedMessage>(&DeletedMessage::fakeId) != updated.fakeId));
		}
		openedStorage().commit();
	} catch (std::exception &ex) {
		try {
			openedStorage().rollback();
		} catch (...) {
		}
		LOG(("Failed to save edited message for some reason: %1").arg(ex.what()));
	}
}

std::vector<DeletedMessage> getDeletedMessages(ID userId, ID dialogId, ID topicId, ID minId, ID maxId, int totalLimit, const std::string &searchQuery) {
	std::lock_guard lock(databaseMutex);
	try {
		if (searchQuery.empty()) {
			return openedStorage().get_all<DeletedMessage>(
				where(
					column<DeletedMessage>(&DeletedMessage::userId) == userId and
					column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
					(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) > minId or minId == 0) and
					(column<DeletedMessage>(&DeletedMessage::messageId) < maxId or maxId == 0)
				),
				order_by(column<DeletedMessage>(&DeletedMessage::messageId)).desc(),
				limit(totalLimit)
			);
		}

		std::string escaped;
		escaped.reserve(searchQuery.size());
		for (const auto c : searchQuery) {
			if (c == '%' || c == '_' || c == '\\') {
				escaped += '\\';
			}
			escaped += c;
		}
		const auto pattern = "%" + escaped + "%";
		return openedStorage().get_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0) and
				(column<DeletedMessage>(&DeletedMessage::messageId) > minId or minId == 0) and
				(column<DeletedMessage>(&DeletedMessage::messageId) < maxId or maxId == 0) and
				like(column<DeletedMessage>(&DeletedMessage::text), pattern, "\\")
			),
			order_by(column<DeletedMessage>(&DeletedMessage::messageId)).desc(),
			limit(totalLimit)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get deleted messages: %1").arg(ex.what()));
		return {};
	}
}

bool hasDeletedMessages(ID userId, ID dialogId, ID topicId) {
	std::lock_guard lock(databaseMutex);
	try {
		return !openedStorage().select(
			columns(column<DeletedMessage>(&DeletedMessage::dialogId)),
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0)
			),
			limit(1)
		).empty();
	} catch (std::exception &ex) {
		LOG(("Failed to check if dialog has deleted message: %1").arg(ex.what()));
		return false;
	}
}

void removeDeletedMessage(ID userId, ID dialogId, ID messageId) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().remove_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				column<DeletedMessage>(&DeletedMessage::messageId) == messageId
			)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to remove deleted message: %1").arg(ex.what()));
	}
}

void clearDeletedMessages(ID userId, ID dialogId, ID topicId) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().remove_all<DeletedMessage>(
			where(
				column<DeletedMessage>(&DeletedMessage::userId) == userId and
				column<DeletedMessage>(&DeletedMessage::dialogId) == dialogId and
				(column<DeletedMessage>(&DeletedMessage::topicId) == topicId or topicId == 0)
			)
		);
	} catch (std::exception &) {
	}
}

template<typename T>
std::vector<T> getAllT() {
	std::lock_guard lock(databaseMutex);
	try {
		return openedStorage().get_all<T>();
	} catch (std::exception &ex) {
		LOG(("Failed to get all: %1").arg(ex.what()));
		return {};
	}
}

std::vector<RegexFilter> getAllRegexFilters() {
	return getAllT<RegexFilter>();
}

std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions() {
	return getAllT<RegexFilterGlobalExclusion>();
}

std::vector<RegexFilter> getExcludedByDialogId(ID dialogId) {
	std::lock_guard lock(databaseMutex);
	try {
		return openedStorage().get_all<RegexFilter>(
			where(in(&RegexFilter::id,
					 openedStorage().select(columns(&RegexFilterGlobalExclusion::filterId),
									where(is_equal(&RegexFilterGlobalExclusion::dialogId, dialogId))
					 )
			))
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get excluded by dialog id: %1").arg(ex.what()));
		return {};
	}
}

int getCount() {
	std::lock_guard lock(databaseMutex);
	try {
		return openedStorage().count<RegexFilter>();
	} catch (std::exception &ex) {
		LOG(("Failed to get count: %1").arg(ex.what()));
		return 0;
	}
}

RegexFilter getById(std::vector<char> id) {
	std::lock_guard lock(databaseMutex);
	try {
		return openedStorage().get<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::id) == std::move(id))
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get filters by id: %1").arg(ex.what()));
		return {};
	}
}

std::vector<RegexFilter> getShared() {
	std::lock_guard lock(databaseMutex);
	try {
		return openedStorage().get_all<RegexFilter>(
			where(is_null(column<RegexFilter>(&RegexFilter::dialogId)))
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get shared filters: %1").arg(ex.what()));
		return {};
	}
}

std::vector<RegexFilter> getByDialogId(ID dialogId) {
	std::lock_guard lock(databaseMutex);
	try {
		return openedStorage().get_all<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::dialogId) == dialogId)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to get filters by dialog id: %1").arg(ex.what()));
		return {};
	}
}

void addRegexFilter(const RegexFilter &filter) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().begin_transaction();
		openedStorage().replace(filter); // we're using replace as we set std::vector<char> as primary key
		openedStorage().commit();
	} catch (std::exception &ex) {
		try {
			openedStorage().rollback();
		} catch (...) {
		}
		LOG(("Failed to save regex filter for some reason: %1").arg(ex.what()));
	}
}

void addRegexExclusion(const RegexFilterGlobalExclusion &exclusion) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().begin_transaction();
		openedStorage().insert(exclusion);
		openedStorage().commit();
	} catch (std::exception &ex) {
		try {
			openedStorage().rollback();
		} catch (...) {
		}
		LOG(("Failed to save regex filter exclusion for some reason: %1").arg(ex.what()));
	}
}

void updateRegexFilter(const RegexFilter &filter) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().update_all(
			set(
				c(&RegexFilter::text) = filter.text,
				c(&RegexFilter::enabled) = filter.enabled,
				c(&RegexFilter::reversed) = filter.reversed,
				c(&RegexFilter::caseInsensitive) = filter.caseInsensitive,
				c(&RegexFilter::dialogId) = filter.dialogId
			),
			where(c(&RegexFilter::id) == filter.id)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to update regex filter for some reason: %1").arg(ex.what()));
	}
}

void deleteFilter(const std::vector<char> &id) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().remove_all<RegexFilter>(
			where(column<RegexFilter>(&RegexFilter::id) == id)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to delete regex filter for some reason: %1").arg(ex.what()));
	}
}

void deleteExclusionsByFilterId(const std::vector<char> &id) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == id)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to delete regex filter exclusion by filter id for some reason: %1").arg(ex.what()));
	}
}

void deleteExclusion(ID dialogId, std::vector<char> filterId) {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().remove_all<RegexFilterGlobalExclusion>(
			where(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::filterId) == filterId and
				column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::dialogId) == dialogId
			)
		);
	} catch (std::exception &ex) {
		LOG(("Failed to delete regex filter exclusion for some reason: %1").arg(ex.what()));
	}
}

void deleteAllFilters() {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().remove_all<RegexFilter>();
	} catch (std::exception &ex) {
		LOG(("Failed to delete all regex filter for some reason: %1").arg(ex.what()));
	}
}

void deleteAllExclusions() {
	std::lock_guard lock(databaseMutex);
	try {
		openedStorage().remove_all<RegexFilterGlobalExclusion>();
	} catch (std::exception &ex) {
		LOG(("Failed to delete all regex filter exclusions for some reason: %1").arg(ex.what()));
	}
}

bool hasFilters() {
	std::lock_guard lock(databaseMutex);
	try {
		return !openedStorage().select(
			columns(column<RegexFilter>(&RegexFilter::id)),
			limit(1)
		).empty();
	} catch (std::exception &ex) {
		LOG(("Failed to check if there's any filters: %1").arg(ex.what()));
		return false;
	}
}

bool hasPerDialogFilters() {
	std::lock_guard lock(databaseMutex);
	try {
		return
			!openedStorage().select(
				columns(column<RegexFilter>(&RegexFilter::id)),
				where(is_not_null(column<RegexFilter>(&RegexFilter::dialogId))),
				limit(1)
			).empty() ||
			!openedStorage().select(
				columns(column<RegexFilterGlobalExclusion>(&RegexFilterGlobalExclusion::fakeId)),
				limit(1)
			).empty();
	} catch (std::exception &ex) {
		LOG(("Failed to check if there's any filters: %1").arg(ex.what()));
		return false;
	}
}

}
