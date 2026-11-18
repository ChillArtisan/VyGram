// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/messages_storage.h"

#include "ayu/data/ayu_database.h"
#include "ayu/utils/ayu_mapper.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/unixtime.h"
#include "base/weak_ptr.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_forum_topic.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "storage/file_download.h"

#include <QFile>
#include <QImage>

namespace AyuMessages {

namespace {

constexpr auto kPhotoMedia = 1;
constexpr auto kDocumentMedia = 2;

auto mediaDownloadEvents(not_null<Main::Session*> session) {
	return rpl::merge(
		session->downloaderTaskFinished() | rpl::map_to(true),
		session->data().sessionDataAboutToBeCleared() | rpl::map_to(false)
	) | rpl::take_while([](bool alive) { return alive; });
}

void archiveMedia(not_null<HistoryItem*> item, AyuMessageBase &message) {
	message.mediaPath.clear();
	message.documentType = 0;
	message.documentSerialized = AyuMapper::serializeMedia(item);
	if (message.documentSerialized.empty()) {
		return;
	}
	const auto session = &item->history()->session();
	const auto weak = base::make_weak(session);
	const auto userId = message.userId;
	const auto origin = Data::FileOrigin(item->fullId());
	if (const auto photo = item->media()->photo()) {
		message.documentType = kPhotoMedia;
		const auto view = photo->createMediaView();
		const auto id = ID(photo->id);
		const auto save = [=] {
			if (!weak) {
				return;
			}
			const auto bytes = view->imageBytes(Data::PhotoSize::Large);
			if (!bytes.isEmpty()) {
				if (!AyuDatabase::storeMedia(userId, id, kPhotoMedia, bytes)) {
					LOG(("AyuMessages: Failed to commit archived photo"));
				}
			}
		};
		if (!view->imageBytes(Data::PhotoSize::Large).isEmpty()) {
			save();
		} else {
			// The view, not a HistoryItem pointer, survives asynchronous loading.
			mediaDownloadEvents(session)
				| rpl::filter([=] { return !weak || !view->imageBytes(Data::PhotoSize::Large).isEmpty()
					|| !photo->loading(Data::PhotoSize::Large); })
				| rpl::take(1)
				| rpl::on_next(save, session->lifetime());
			view->wanted(Data::PhotoSize::Large, origin);
		}
	} else if (const auto document = item->media()->document()) {
		message.documentType = kDocumentMedia;
		message.mimeType = document->mimeString().toStdString();
		const auto view = document->createMediaView();
		const auto id = ID(document->id);
		if (view->bytes().isEmpty() && document->size > 0
			&& document->size <= 256 * 1024 * 1024) {
			// Import an existing user download; the archive never creates one.
			const auto &location = document->location(true);
			if (!location.isEmpty() && location.accessEnable()) {
				QFile file(location.name());
				if (file.open(QIODevice::ReadOnly) && file.size() == document->size) {
					const auto bytes = file.readAll();
					if (bytes.size() == document->size) view->setBytes(bytes);
				}
				location.accessDisable();
			}
		}
		const auto save = [=] {
			if (!weak) {
				return;
			}
			const auto bytes = view->bytes();
			if (!bytes.isEmpty() && bytes.size() == document->size) {
				if (!AyuDatabase::storeMedia(userId, id, kDocumentMedia, bytes)) {
					LOG(("AyuMessages: Failed to commit archived document"));
				}
			}
		};
		if (!view->bytes().isEmpty()) {
			save();
		} else if (document->size > 0 && document->size < Storage::kMaxFileInMemory) {
			// WHY: Upstream's memory-only loader has a 10 MiB limit. Never let it
			// fall back to creating an unencrypted file for larger documents.
			document->forceToCache(true);
			mediaDownloadEvents(session)
				| rpl::filter([=] { return !weak || !view->bytes().isEmpty() || !document->loading(); })
				| rpl::take(1)
				| rpl::on_next(save, session->lifetime());
			document->save(origin, QString());
		}
	}
}

} // namespace

MTPMessageMedia restoreMedia(
		const AyuMessageBase &message,
		not_null<Main::Session*> session,
		std::shared_ptr<void> &mediaView) {
	const ID userId = session->userId().bare & PeerId::kChatTypeMask;
	if (message.userId != userId) {
		return MTP_messageMediaEmpty();
	}
	const auto media = AyuMapper::deserializeMedia(message.documentSerialized);
	if (media.type() == mtpc_messageMediaPhoto) {
		const auto photo = media.c_messageMediaPhoto().vphoto();
		if (!photo || photo->type() != mtpc_photo) {
			return MTP_messageMediaEmpty();
		}
		const auto bytes = AyuDatabase::loadMedia(userId, ID(photo->c_photo().vid().v), kPhotoMedia);
		auto image = QImage::fromData(bytes);
		if (bytes.isEmpty() || image.isNull()) {
			return MTP_messageMediaEmpty();
		}
		const auto data = session->data().processPhoto(*photo);
		const auto view = data->createMediaView();
		view->set(Data::PhotoSize::Large, Data::PhotoSize::Large, std::move(image), bytes);
		mediaView = view;
		return media;
	} else if (media.type() == mtpc_messageMediaDocument) {
		const auto document = media.c_messageMediaDocument().vdocument();
		if (!document || document->type() != mtpc_document) {
			return MTP_messageMediaEmpty();
		}
		const auto bytes = AyuDatabase::loadMedia(userId, ID(document->c_document().vid().v), kDocumentMedia);
		if (bytes.isEmpty() || bytes.size() != document->c_document().vsize().v) {
			return MTP_messageMediaEmpty();
		}
		const auto data = session->data().processDocument(*document);
		const auto view = data->createMediaView();
		view->setBytes(bytes);
		mediaView = view;
		return media;
	}
	return MTP_messageMediaEmpty();
}

template<typename DerivedMessage>
std::vector<AyuMessageBase> convertToBase(const std::vector<DerivedMessage> &messages) {
	std::vector<AyuMessageBase> based;
	based.reserve(messages.size());
	for (const auto &msg : messages) {
		based.push_back(static_cast<AyuMessageBase>(msg));
	}
	return based;
}

void map(not_null<HistoryItem*> item, AyuMessageBase &message) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;

	message.userId = userId;
	message.dialogId = getDialogIdFromPeer(item->history()->peer);
	message.groupedId = item->groupId().raw();
	message.peerId = item->history()->peer->id.value & PeerId::kChatTypeMask;
	message.fromId = item->from()->id.value & PeerId::kChatTypeMask;
	if (item->topic()) {
		message.topicId = item->topicRootId().bare;
	} else {
		message.topicId = 0;
	}
	message.messageId = item->id.bare;
	message.date = item->date();
	message.flags = AyuMapper::mapItemFlagsToMTPFlags(item);

	if (const auto edited = item->Get<HistoryMessageEdited>()) {
		message.editDate = edited->date;
	} else {
		message.editDate = base::unixtime::now();
	}

	message.views = item->viewsCount();
	message.fwdFlags = 0;
	message.fwdFromId = 0;
	// message.fwdName
	message.fwdDate = 0;
	// message.fwdPostAuthor
	if (const auto msgsigned = item->Get<HistoryMessageSigned>()) {
		message.postAuthor = msgsigned->author.toStdString();
	}
	message.replyFlags = 0;
	message.replyMessageId = 0;
	message.replyPeerId = 0;
	message.replyTopId = 0;
	message.replyForumTopic = false;
	// message.replySerialized
	// message.replyMarkupSerialized
	message.entityCreateDate = base::unixtime::now();

	auto serializedText = AyuMapper::serializeTextWithEntities(item);
	message.text = serializedText.first;
	message.textEntities = serializedText.second;

	archiveMedia(item, message);
}

void addEditedMessage(not_null<HistoryItem *> item) {
	EditedMessage message{};
	map(item, message);

	if (message.text.empty() && message.documentSerialized.empty()) {
		return;
	}

	AyuDatabase::addEditedMessage(message);
}

std::vector<AyuMessageBase> getEditedMessages(not_null<HistoryItem*> item, ID minId, ID maxId, int totalLimit) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto msgId = item->id.bare;

	return convertToBase(AyuDatabase::getEditedMessages(userId, dialogId, msgId, minId, maxId, totalLimit));
}

bool hasRevisions(not_null<HistoryItem*> item) {
	const ID userId = item->history()->owner().session().userId().bare & PeerId::kChatTypeMask;
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto msgId = item->id.bare;

	return AyuDatabase::hasRevisions(userId, dialogId, msgId);
}

void addDeletedMessage(not_null<HistoryItem*> item) {
	DeletedMessage message{};
	map(item, message);

	if (message.text.empty() && message.documentSerialized.empty()) {
		return;
	}

	AyuDatabase::addDeletedMessage(message);
}

std::vector<AyuMessageBase>
getDeletedMessages(not_null<PeerData*> peer, ID topicId, ID minId, ID maxId, int totalLimit, const QString &searchQuery) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	return convertToBase(
		AyuDatabase::getDeletedMessages(userId, getDialogIdFromPeer(peer), topicId, minId, maxId, totalLimit, searchQuery.toStdString()));
}

bool hasDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	return AyuDatabase::hasDeletedMessages(userId, getDialogIdFromPeer(peer), topicId);
}

void removeDeletedMessage(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	AyuDatabase::removeDeletedMessage(userId, getDialogIdFromPeer(peer), item->id.bare);
}

void clearDeletedMessages(not_null<PeerData*> peer, ID topicId) {
	const ID userId = peer->session().userId().bare & PeerId::kChatTypeMask;
	AyuDatabase::clearDeletedMessages(userId, getDialogIdFromPeer(peer), topicId);
}

}
