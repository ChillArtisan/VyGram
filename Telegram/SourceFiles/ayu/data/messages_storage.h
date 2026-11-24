// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/data/entities.h"

namespace Main { class Session; }

namespace AyuMessages {

[[nodiscard]] MTPMessageMedia restoreMedia(
	const AyuMessageBase &message,
	not_null<Main::Session*> session,
	std::shared_ptr<void> &mediaView,
	bool chatSnapshot = false);

void addEditedMessage(not_null<HistoryItem *> item);
std::vector<AyuMessageBase> getEditedMessages(not_null<HistoryItem*> item, ID minId, ID maxId, int totalLimit);
bool hasRevisions(not_null<HistoryItem*> item);

[[nodiscard]] bool isChatArchiveEnabled(not_null<PeerData*> peer);
[[nodiscard]] bool setChatArchiveEnabled(not_null<PeerData*> peer, bool enabled);
void archiveChatMessage(not_null<HistoryItem*> item);

void addDeletedMessage(not_null<HistoryItem*> item);
std::vector<AyuMessageBase> getDeletedMessages(not_null<PeerData*> peer, ID topicId, ID minId, ID maxId, int totalLimit, const QString &searchQuery = QString());
bool hasDeletedMessages(not_null<PeerData*> peer, ID topicId);
void removeDeletedMessage(not_null<HistoryItem*> item);
void clearDeletedMessages(not_null<PeerData*> peer, ID topicId);

}
