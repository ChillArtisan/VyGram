// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/data/entities.h"

#include <functional>
#include <optional>
#include <QByteArray>

class SchemaVersion
{
public:
	int id;
	int version;
};

namespace AyuDatabase {

void initialize();

// Whether encrypted archive data exists on disk (complete or mid-rotation).
[[nodiscard]] bool hasArchive();

// Opens the archive with `archiveKey`. Without `allowCreate` it fails
// instead of creating the archive or migrating the plaintext legacy one:
// pass it only once a strong passcode protects `archiveKey` on disk.
[[nodiscard]] bool unlock(const QByteArray &archiveKey, bool allowCreate);
[[nodiscard]] bool isUnlocked();
void shutdown();

enum class RekeyResult {
	Rekeyed,   // The archive is open under the new key.
	Unchanged, // Nothing was persisted; the archive stays under the old key.
	Locked,    // The outcome is settled on the next unlock; archive is shut.
};

// Re-encrypts the unlocked archive under `newKey`. `persist` must durably
// store `newKey` (it runs after a verified copy exists and before that copy
// replaces the archive). If it fails, `rollback` must durably restore the
// previous key and return whether it did.
[[nodiscard]] RekeyResult rekey(
	const QByteArray &newKey,
	const std::function<bool()> &persist,
	const std::function<bool()> &rollback);

[[nodiscard]] bool storePrivateSetting(const std::string &name, const QByteArray &value);
[[nodiscard]] QByteArray loadPrivateSetting(const std::string &name);
[[nodiscard]] std::optional<QByteArray> tryLoadPrivateSetting(
	const std::string &name);

[[nodiscard]] bool storeMedia(ID userId, ID mediaId, int mediaType, const QByteArray &bytes);
[[nodiscard]] QByteArray loadMedia(ID userId, ID mediaId, int mediaType);

struct ArchivedChat {
	ID userId = 0;
	ID dialogId = 0;
	std::string name;
	bool enabled = false;
};

[[nodiscard]] bool setArchivedChatEnabled(
	ID userId, ID dialogId, const std::string &name, bool enabled);
[[nodiscard]] bool isArchivedChatEnabled(ID userId, ID dialogId);
[[nodiscard]] bool archiveChatMessage(const AyuMessageBase &message);
[[nodiscard]] std::vector<ArchivedChat> archivedChats(ID userId);
[[nodiscard]] std::vector<AyuMessageBase> archivedChatMessages(
	ID userId,
	ID dialogId,
	ID beforeId,
	int totalLimit,
	const std::string &searchQuery = {},
	int minDate = 0,
	int maxDate = 0,
	int documentType = -1);
[[nodiscard]] bool storeArchivedChatMedia(
	ID userId, ID dialogId, ID mediaId, int mediaType, const QByteArray &bytes);
[[nodiscard]] QByteArray loadArchivedChatMedia(
	ID userId, ID dialogId, ID mediaId, int mediaType);
[[nodiscard]] bool purgeArchivedChat(ID userId, ID dialogId);

void addEditedMessage(const EditedMessage &message);
std::vector<EditedMessage> getEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit);
bool hasRevisions(ID userId, ID dialogId, ID messageId);

void addDeletedMessage(const DeletedMessage &message);
std::vector<DeletedMessage> getDeletedMessages(ID userId, ID dialogId, ID topicId, ID minId, ID maxId, int totalLimit, const std::string &searchQuery = "");
bool hasDeletedMessages(ID userId, ID dialogId, ID topicId);
void removeDeletedMessage(ID userId, ID dialogId, ID messageId);
void clearDeletedMessages(ID userId, ID dialogId, ID topicId);

std::vector<RegexFilter> getAllRegexFilters();
RegexFilter getById(std::vector<char> id);
std::vector<RegexFilter> getShared();
std::vector<RegexFilter> getByDialogId(ID dialogId);
std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions();
std::vector<RegexFilter> getExcludedByDialogId(ID dialogId);

int getCount();


void addRegexFilter(const RegexFilter &filter);
void addRegexExclusion(const RegexFilterGlobalExclusion &exclusion);

void updateRegexFilter(const RegexFilter &filter);

void deleteFilter(const std::vector<char> &id);
void deleteExclusionsByFilterId(const std::vector<char> &id);
void deleteExclusion(ID dialogId, std::vector<char> filterId);

void deleteAllFilters();
void deleteAllExclusions();

bool hasFilters();
bool hasPerDialogFilters();


}
