#include "ayu/features/send_templates/send_templates.h"

#include "ayu/data/ayu_database.h"
#include "ayu/features/send_templates/send_templates_mp4.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "storage/localimageloader.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <array>
#include <cstring>
#include <string_view>

namespace Ayu::SendTemplates {
namespace {

constexpr auto kMaxLibraryBytes = 2 * 1024 * 1024;
constexpr auto kMaxMediaBytes = 128 * 1024 * 1024;
constexpr auto kMaxSmallMediaBytes = 20 * 1024 * 1024;
constexpr auto kMaxVoiceDuration = crl::time(20 * 60 * 1000);
constexpr auto kMaxRoundDuration = crl::time(60 * 1000);
constexpr auto kMaxPhotoArea = int64(40) * 1000 * 1000;
constexpr auto kMp4Brands = std::array<std::string_view, 10>{
	"isom", "iso2", "iso4", "iso5", "iso6",
	"mp41", "mp42", "avc1", "M4V ", "MSNV",
};

std::string Key(uint64 accountId) {
	return "sendTemplates/" + std::to_string(accountId);
}

bool Valid(const Entry &entry) {
	return !QUuid(entry.id).isNull()
		&& !entry.name.trimmed().isEmpty()
		&& entry.name.size() <= kMaxNameLength
		&& entry.caption.size() <= kMaxCaptionLength
		&& !entry.path.isEmpty()
		&& entry.path.size() <= 4096
		&& QFileInfo(entry.path).isAbsolute()
		&& int(entry.type) >= int(Type::Round)
		&& int(entry.type) <= int(Type::Photo);
}

bool Save(uint64 accountId, const std::vector<Entry> &entries) {
	if (!AyuDatabase::isUnlocked() || entries.size() > kMaxEntries) {
		return false;
	}
	auto array = QJsonArray();
	for (const auto &entry : entries) {
		if (!Valid(entry)) {
			return false;
		}
		array.append(QJsonObject{
			{ u"id"_q, entry.id },
			{ u"path"_q, entry.path },
			{ u"name"_q, entry.name },
			{ u"caption"_q, entry.caption },
			{ u"type"_q, int(entry.type) },
		});
	}
	const auto bytes = QJsonDocument(QJsonObject{
		{ u"version"_q, 1 },
		{ u"entries"_q, array },
	}).toJson(QJsonDocument::Compact);
	return bytes.size() <= kMaxLibraryBytes
		&& AyuDatabase::storePrivateSetting(Key(accountId), bytes);
}

int Read(void *opaque, uint8_t *buffer, int size) {
	const auto result = static_cast<QBuffer*>(opaque)->read(
		reinterpret_cast<char*>(buffer),
		size);
	return result > 0 ? int(result) : AVERROR_EOF;
}

int64_t Seek(void *opaque, int64_t offset, int whence) {
	const auto buffer = static_cast<QBuffer*>(opaque);
	if (whence & AVSEEK_SIZE) {
		return buffer->size();
	}
	whence &= ~AVSEEK_FORCE;
	const auto base = (whence == SEEK_SET)
		? int64(0)
		: (whence == SEEK_CUR)
		? buffer->pos()
		: (whence == SEEK_END)
		? buffer->size()
		: int64(-1);
	if (base < 0 || offset < -base || offset > buffer->size() - base) {
		return AVERROR(EINVAL);
	}
	return buffer->seek(base + offset) ? base + offset : AVERROR(EIO);
}

crl::time Duration(not_null<AVFormatContext*> format, AVStream *stream) {
	const auto value = stream->duration;
	if (value != AV_NOPTS_VALUE && value > 0) {
		return av_rescale_q(value, stream->time_base, { 1, 1000 });
	}
	return (format->duration > 0 && format->duration != AV_NOPTS_VALUE)
		? format->duration / (AV_TIME_BASE / 1000)
		: 0;
}

bool Probe(Type type, Prepared &result) {
	auto buffer = QBuffer(&result.bytes);
	if (!buffer.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto format = FFmpeg::MakeFormatPointer(&buffer, Read, nullptr, Seek);
	if (!format || avformat_find_stream_info(format.get(), nullptr) < 0) {
		return false;
	}
	const auto container = QByteArray(format->iformat->name).split(',');
	auto audio = (AVStream*)nullptr;
	auto video = (AVStream*)nullptr;
	for (auto i = 0U; i != format->nb_streams; ++i) {
		const auto stream = format->streams[i];
		const auto codec = stream->codecpar;
		if (codec->codec_type == AVMEDIA_TYPE_AUDIO && !audio) {
			audio = stream;
		} else if (codec->codec_type == AVMEDIA_TYPE_VIDEO && !video) {
			video = stream;
		} else {
			return false;
		}
	}
	if (type == Type::Voice) {
		if (!container.contains("ogg") || !audio || video) {
			return false;
		}
		const auto codec = audio->codecpar;
		result.duration = Duration(format.get(), audio);
		return codec->codec_id == AV_CODEC_ID_OPUS
			&& codec->sample_rate == 48000
			&& codec->ch_layout.nb_channels == 1
			&& codec->extradata_size >= 19
			&& !memcmp(codec->extradata, "OpusHead", 8)
			&& codec->extradata[8] == 1
			&& codec->extradata[9] == 1
			&& result.duration > 0
			&& result.duration <= kMaxVoiceDuration;
	}
	const auto brand = av_dict_get(format->metadata, "major_brand", nullptr, 0);
	if (!container.contains("mp4")
		|| !brand
		|| ranges::find(kMp4Brands, std::string_view(brand->value))
			== end(kMp4Brands)
		|| !video
		|| video->codecpar->codec_id != AV_CODEC_ID_H264
		|| (audio && audio->codecpar->codec_id != AV_CODEC_ID_AAC)) {
		return false;
	}
	const auto codec = video->codecpar;
	result.duration = Duration(format.get(), video);
	return result.duration > 0
		&& codec->width > 0
		&& codec->height > 0
		&& int64(codec->width) * codec->height <= kMaxPhotoArea
		&& (type != Type::Round
			|| (audio
				&& codec->width == codec->height
				&& codec->width <= 640
				&& !(codec->width % 2)
				&& result.duration <= kMaxRoundDuration));
}

} // namespace

std::optional<std::vector<Entry>> Load(uint64 accountId) {
	if (!AyuDatabase::isUnlocked()) {
		return std::nullopt;
	}
	const auto loaded = AyuDatabase::tryLoadPrivateSetting(Key(accountId));
	if (!loaded) {
		return std::nullopt;
	}
	const auto &bytes = *loaded;
	auto result = std::vector<Entry>();
	if (bytes.isEmpty()) {
		return result;
	} else if (bytes.size() > kMaxLibraryBytes) {
		return std::nullopt;
	}
	const auto document = QJsonDocument::fromJson(bytes);
	const auto object = document.object();
	if (!document.isObject()
		|| object[u"version"_q].toInt() != 1
		|| !object[u"entries"_q].isArray()) {
		return std::nullopt;
	}
	const auto array = object[u"entries"_q].toArray();
	if (array.size() > kMaxEntries) {
		return std::nullopt;
	}
	for (const auto &value : array) {
		const auto item = value.toObject();
		const auto rawType = item[u"type"_q];
		const auto type = rawType.toInt(-1);
		const auto entry = Entry{
			.id = item[u"id"_q].toString(),
			.path = item[u"path"_q].toString(),
			.name = item[u"name"_q].toString(),
			.caption = item[u"caption"_q].toString(),
			.type = Type(type),
		};
		if (!Valid(entry)
			|| !rawType.isDouble()
			|| rawType.toDouble() != type
			|| ranges::find(result, entry.id, &Entry::id) != end(result)) {
			return std::nullopt;
		}
		result.push_back(entry);
	}
	return result;
}

bool Put(uint64 accountId, const Entry &entry) {
	auto entries = Load(accountId);
	if (!entries || !Valid(entry)) {
		return false;
	}
	const auto i = ranges::find(*entries, entry.id, &Entry::id);
	if (i == end(*entries)) {
		entries->push_back(entry);
	} else {
		*i = entry;
	}
	return Save(accountId, *entries);
}

bool Remove(uint64 accountId, const QString &id) {
	auto entries = Load(accountId);
	if (!entries) {
		return false;
	}
	const auto i = ranges::find(*entries, id, &Entry::id);
	if (i != end(*entries)) {
		entries->erase(i);
	}
	return Save(accountId, *entries);
}

Prepared Prepare(const Entry &entry) {
	auto result = Prepared();
	auto file = QFile(entry.path);
	const auto info = QFileInfo(file);
	const auto limit = (entry.type == Type::Video)
		? kMaxMediaBytes
		: kMaxSmallMediaBytes;
	if (!info.isFile() || !file.open(QIODevice::ReadOnly)) {
		result.error = Error::File;
		return result;
	} else if (file.size() <= 0 || file.size() > limit) {
		result.error = Error::Size;
		return result;
	}
	result.modified = info.lastModified();
	result.bytes = file.read(limit + 1);
	if (file.error() != QFile::NoError
		|| result.bytes.size() != info.size()
		|| !Unchanged(entry, result)) {
		result.error = Error::File;
		return result;
	}
	if (entry.type == Type::Photo) {
		auto buffer = QBuffer(&result.bytes);
		buffer.open(QIODevice::ReadOnly);
		auto reader = QImageReader(&buffer);
		const auto size = reader.size();
		const auto format = reader.format();
		if ((format != "jpeg" && format != "png")
			|| size.isEmpty()
			|| int64(size.width()) * size.height() > kMaxPhotoArea
			|| size.width() + size.height() > 10000
			|| std::max(size.width(), size.height())
				> 20 * std::min(size.width(), size.height())
			|| reader.imageCount() > 1) {
			result.error = Error::Format;
			return result;
		}
	} else if (!Probe(entry.type, result)) {
		result.error = Error::Format;
		return result;
	}
	if (entry.type == Type::Photo || entry.type == Type::Video) {
		const auto mime = (entry.type == Type::Photo)
			? (result.bytes.startsWith("\x89PNG")
				? u"image/png"_q
				: u"image/jpeg"_q)
			: u"video/mp4"_q;
		auto information = FileLoadTask::ReadMediaInformation(
			QString(),
			result.bytes,
			mime);
		if (!information) {
			result.error = Error::Format;
			return result;
		}
		if (entry.type == Type::Photo) {
			const auto image = std::get_if<Ui::PreparedFileInformation::Image>(
				&information->media);
			if (!image || image->data.isNull() || image->animated) {
				result.error = Error::Format;
				return result;
			}
		} else {
			const auto video = std::get_if<Ui::PreparedFileInformation::Video>(
				&information->media);
			if (!video || video->thumbnail.isNull() || video->duration <= 0) {
				result.error = Error::Format;
				return result;
			}
			video->modifications.gif = false;
		}
		auto prepared = Ui::PreparedFile(QString());
		prepared.content = result.bytes;
		prepared.size = result.bytes.size();
		prepared.caption = { entry.caption };
		prepared.type = (entry.type == Type::Photo)
			? Ui::PreparedFile::Type::Photo
			: Ui::PreparedFile::Type::Video;
		prepared.information = std::move(information);
		result.list.files.push_back(std::move(prepared));
	} else if (entry.type == Type::Round) {
		if (!Mp4::CanRefreshMetadata({
			reinterpret_cast<const unsigned char*>(result.bytes.constData()),
			size_t(result.bytes.size()) })) {
			result.error = Error::Format;
			return result;
		}
		const auto information = FileLoadTask::ReadMediaInformation(
			QString(),
			result.bytes,
			u"video/mp4"_q);
		const auto video = information
			? std::get_if<Ui::PreparedFileInformation::Video>(&information->media)
			: nullptr;
		if (!video
			|| video->thumbnail.isNull()
			|| video->thumbnail.width() != video->thumbnail.height()
			|| video->isGifv) {
			result.error = Error::Format;
		}
	}
	return result;
}

bool RefreshRoundMetadata(Prepared &prepared) {
	return Mp4::RefreshMetadata({
		reinterpret_cast<unsigned char*>(prepared.bytes.data()),
		size_t(prepared.bytes.size()) },
		uint64(QDateTime::currentSecsSinceEpoch()));
}

bool Unchanged(const Entry &entry, const Prepared &prepared) {
	const auto info = QFileInfo(entry.path);
	return info.isFile()
		&& info.isReadable()
		&& info.size() == prepared.bytes.size()
		&& info.lastModified() == prepared.modified;
}

} // namespace Ayu::SendTemplates
