// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/utils/ayu_mapper.h"

#include "data/data_document.h"
#include "data/data_photo.h"

#include <cstring>

#include "apiwrap.h"
#include "api/api_text_entities.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "mtproto/connection_abstract.h"
#include "mtproto/details/mtproto_dump_to_text.h"

namespace AyuMapper {

constexpr auto kMessageFlagUnread = 0x00000001;
constexpr auto kMessageFlagOut = 0x00000002;
constexpr auto kMessageFlagForwarded = 0x00000004;
constexpr auto kMessageFlagReply = 0x00000008;
constexpr auto kMessageFlagMention = 0x00000010;
constexpr auto kMessageFlagContentUnread = 0x00000020;
constexpr auto kMessageFlagHasMarkup = 0x00000040;
constexpr auto kMessageFlagHasEntities = 0x00000080;
constexpr auto kMessageFlagHasFromId = 0x00000100;
constexpr auto kMessageFlagHasMedia = 0x00000200;
constexpr auto kMessageFlagHasViews = 0x00000400;
constexpr auto kMessageFlagHasBotId = 0x00000800;
constexpr auto kMessageFlagIsSilent = 0x00001000;
constexpr auto kMessageFlagIsPost = 0x00004000;
constexpr auto kMessageFlagEdited = 0x00008000;
constexpr auto kMessageFlagHasPostAuthor = 0x00010000;
constexpr auto kMessageFlagIsGrouped = 0x00020000;
constexpr auto kMessageFlagFromScheduled = 0x00040000;
constexpr auto kMessageFlagHasReactions = 0x00100000;
constexpr auto kMessageFlagHideEdit = 0x00200000;
constexpr auto kMessageFlagRestricted = 0x00400000;
constexpr auto kMessageFlagHasReplies = 0x00800000;
constexpr auto kMessageFlagIsPinned = 0x01000000;
constexpr auto kMessageFlagHasTTL = 0x02000000;
constexpr auto kMessageFlagInvertMedia = 0x08000000;
constexpr auto kMessageFlagHasSavedPeer = 0x10000000;

template<typename MTPObject>
std::vector<char> serializeObject(MTPObject object) {
	mtpBuffer buffer;
	object.write(buffer);

	const auto from = reinterpret_cast<char*>(buffer.data());
	const auto end = from + buffer.size() * sizeof(mtpPrime);

	std::vector<char> entities(from, end);
	return entities;
}

template<typename MTPObject>
MTPObject deserializeObject(std::vector<char> serialized) {
	gsl::span<char> span(serialized.data(), serialized.size());

	auto from = reinterpret_cast<const mtpPrime*>(span.data());
	const auto end = from + span.size() / sizeof(mtpPrime);

	MTPObject data;
	if (!data.read(from, end)) {
		LOG(("AyuMapper: Failed to deserialize object"));
	}
	return data;
}

std::pair<std::string, std::vector<char>> serializeTextWithEntities(not_null<HistoryItem*> item) {
	if (item->emptyText()) {
		return std::make_pair("", std::vector<char>());
	}
	auto textWithEntities = item->originalText();


	std::vector<char> entities;
	if (!textWithEntities.entities.empty()) {
		const auto mtpEntities = Api::EntitiesToMTP(
			&item->history()->session(),
			textWithEntities.entities,
			Api::ConvertOption::WithLocal);

		entities = serializeObject(mtpEntities);
	}

	return std::make_pair(textWithEntities.text.toStdString(), entities);
}

MTPVector<MTPMessageEntity> deserializeTextWithEntities(std::vector<char> serialized) {
	if (serialized.empty()) {
		return MTP_vector<MTPMessageEntity>();
	}
	return deserializeObject<MTPVector<MTPMessageEntity>>(serialized);
}

std::vector<char> serializeMedia(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return {};
	}
	if (const auto photo = media->photo()) {
		const auto input = photo->mtpInput();
		if (input.type() != mtpc_inputPhoto) {
			return {};
		}
		const auto &remote = input.c_inputPhoto();
		const auto data = MTP_photo(MTP_flags(0), MTP_long(photo->id),
			remote.vaccess_hash(), remote.vfile_reference(), MTP_int(photo->date()),
			MTP_vector<MTPPhotoSize>(1, MTP_photoSize(MTP_string("y"),
				MTP_int(photo->width()), MTP_int(photo->height()),
				MTP_int(photo->imageByteSize(Data::PhotoSize::Large)))),
			MTPVector<MTPVideoSize>(), MTP_int(photo->getDC()));
		return serializeObject(MTPMessageMedia(MTP_messageMediaPhoto(
			MTP_flags(MTPDmessageMediaPhoto::Flag::f_photo), data,
			MTPint(), MTPDocument())));
	}
	if (const auto document = media->document()) {
		const auto input = document->mtpInput();
		if (input.type() != mtpc_inputDocument) {
			return {};
		}
		QVector<MTPDocumentAttribute> attributes;
		attributes.push_back(MTP_documentAttributeFilename(MTP_string(document->filename())));
		if (document->isVideoFile() || document->isVideoMessage()) {
			using Flag = MTPDdocumentAttributeVideo::Flag;
			attributes.push_back(MTP_documentAttributeVideo(
				MTP_flags(document->isVideoMessage() ? Flag::f_round_message : Flag()),
				MTP_double(document->duration() / 1000.),
				MTP_int(document->dimensions.width()), MTP_int(document->dimensions.height()),
				MTPint(), MTPdouble(), MTPstring()));
		} else if (document->isAudioFile() || document->isVoiceMessage()) {
			using Flag = MTPDdocumentAttributeAudio::Flag;
			attributes.push_back(MTP_documentAttributeAudio(
				MTP_flags(document->isVoiceMessage() ? Flag::f_voice : Flag()),
				MTP_int(document->duration() / 1000), MTPstring(), MTPstring(), MTPbytes()));
		} else if (!document->dimensions.isEmpty()) {
			attributes.push_back(MTP_documentAttributeImageSize(
				MTP_int(document->dimensions.width()), MTP_int(document->dimensions.height())));
		}
		if (document->isAnimation()) {
			attributes.push_back(MTP_documentAttributeAnimated());
		}
		const auto &remote = input.c_inputDocument();
		const auto data = MTP_document(MTP_flags(0), MTP_long(document->id),
			remote.vaccess_hash(), remote.vfile_reference(), MTP_int(document->date),
			MTP_string(document->mimeString()), MTP_long(document->size),
			MTPVector<MTPPhotoSize>(), MTPVector<MTPVideoSize>(),
			MTP_int(document->getDC()), MTP_vector<MTPDocumentAttribute>(attributes));
		return serializeObject(MTPMessageMedia(MTP_messageMediaDocument(
			MTP_flags(MTPDmessageMediaDocument::Flag::f_document), data,
			MTPVector<MTPDocument>(), MTPPhoto(), MTPint(), MTPint())));
	}
	return {};
}

MTPMessageMedia deserializeMedia(const std::vector<char> &serialized) {
	if (serialized.empty() || (serialized.size() % sizeof(mtpPrime)) != 0) {
		return MTP_messageMediaEmpty();
	}
	mtpBuffer buffer(serialized.size() / sizeof(mtpPrime));
	memcpy(buffer.data(), serialized.data(), serialized.size());
	const auto start = buffer.constData();
	auto from = start;
	const auto end = from + buffer.size();
	MTPMessageMedia result;
	try {
		if (result.read(from, end) && from == end) {
			return result;
		}
	} catch (...) {
		LOG(("AyuMapper: Invalid archived media metadata"));
	}
	return MTP_messageMediaEmpty();
}

int mapItemFlagsToMTPFlags(not_null<HistoryItem*> item) {
	int flags = 0;

	const auto thread = item->topic()
							? reinterpret_cast<Data::Thread*>(item->topic())
							: item->history();
	if (item->unread(thread)) {
		flags |= kMessageFlagUnread;
	}

	if (item->out()) {
		flags |= kMessageFlagOut;
	}

	if (item->Get<HistoryMessageForwarded>()) {
		flags |= kMessageFlagForwarded;
	}

	if (item->Get<HistoryMessageReply>()) {
		flags |= kMessageFlagReply;
	}

	if (item->mentionsMe()) {
		flags |= kMessageFlagMention;
	}

	if (item->hasUnreadMediaFlag()) {
		flags |= kMessageFlagContentUnread;
	}

	if (item->definesReplyKeyboard()) {
		flags |= kMessageFlagHasMarkup;
	}

	if (!item->originalText().entities.empty()) {
		flags |= kMessageFlagHasEntities;
	}

	if (item->displayFrom()) {
		// todo: maybe wrong
		flags |= kMessageFlagHasFromId;
	}

	if (item->media()) {
		flags |= kMessageFlagHasMedia;
	}

	if (item->hasViews()) {
		flags |= kMessageFlagHasViews;
	}

	if (item->viaBot()) {
		flags |= kMessageFlagHasBotId;
	}

	if (item->isSilent()) {
		flags |= kMessageFlagIsSilent;
	}

	if (item->isPost()) {
		flags |= kMessageFlagIsPost;
	}

	if (item->Get<HistoryMessageEdited>()) {
		flags |= kMessageFlagEdited;
	}

	if (item->Get<HistoryMessageSigned>()) {
		flags |= kMessageFlagHasPostAuthor;
	}

	if (item->groupId()) {
		flags |= kMessageFlagIsGrouped;
	}

	if (item->isScheduled()) {
		flags |= kMessageFlagFromScheduled;
	}

	if (!item->reactions().empty()) {
		flags |= kMessageFlagHasReactions;
	}

	if (item->hideEditedBadge()) {
		flags |= kMessageFlagHideEdit;
	}

	if (item->hasPossibleRestrictions()) {
		flags |= kMessageFlagRestricted;
	}

	if (item->repliesCount() > 0) {
		flags |= kMessageFlagHasReplies;
	}

	if (item->isPinned()) {
		flags |= kMessageFlagIsPinned;
	}

	if (item->ttlDestroyAt() > 0) {
		flags |= kMessageFlagHasTTL;
	}

	if (item->invertMedia()) {
		flags |= kMessageFlagInvertMedia;
	}

	if (item->savedFromSender()) {
		// todo: maybe wrong
		flags |= kMessageFlagHasSavedPeer;
	}

	return flags;
}

}
