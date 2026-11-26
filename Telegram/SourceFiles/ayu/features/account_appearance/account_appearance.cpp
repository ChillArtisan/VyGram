#include "ayu/features/account_appearance/account_appearance.h"

#include "ayu/ayu_infra.h"
#include "ayu/ayu_settings.h"
#include "ayu/ayu_ui_settings.h"
#include "ayu/data/ayu_database.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/chat/chat_style_radius.h"
#include "window/themes/window_theme.h"

#include <QtCore/QBuffer>
#include <QtCore/QJsonDocument>

namespace Ayu::AccountAppearance {
namespace {

constexpr auto kSharedSlot = uint64(0);
constexpr auto kLookVersion = 1;
constexpr auto kJpegQuality = 92;
constexpr auto kMaxFontNameLength = 128;
constexpr auto kCurrentSlotKey = "vygram.appearance.current";

template <typename Value>
using Variables = base::flat_map<
	uint64,
	std::unique_ptr<rpl::variable<Value>>>;

struct State {
	bool ready = false;
	std::optional<uint64> pendingSlot;
	rpl::lifetime sessionWait;
	Variables<bool> own;
	Variables<bool> wallpapers;
	Variables<std::optional<QColor>> accents;
	base::flat_map<
		std::pair<uint64, uint64>,
		std::unique_ptr<rpl::variable<bool>>> chats;
};

struct StoredLook {
	Window::Theme::LookSnapshot look;
	QJsonObject options;
};

struct OptionFlag {
	QString key;
	bool (AyuSettings::*get)() const;
	void (AyuSettings::*set)(bool);
};

struct OptionRange {
	QString key;
	int min = 0;
	int max = 0;
	int (AyuSettings::*get)() const;
	void (AyuSettings::*set)(int);
};

[[nodiscard]] State &GlobalState() {
	static const auto result = new State();
	return *result;
}

[[nodiscard]] const std::vector<OptionFlag> &OptionFlags() {
	static const auto result = std::vector<OptionFlag>{
		{ u"graphiteNavLabels"_q, &AyuSettings::graphiteNavLabels, &AyuSettings::setGraphiteNavLabels },
		{ u"graphiteCompactNav"_q, &AyuSettings::graphiteCompactNav, &AyuSettings::setGraphiteCompactNav },
		{ u"graphiteForumMessages"_q, &AyuSettings::graphiteForumMessages, &AyuSettings::setGraphiteForumMessages },
		{ u"graphiteMessageIds"_q, &AyuSettings::graphiteMessageIds, &AyuSettings::setGraphiteMessageIds },
		{ u"removeMessageTail"_q, &AyuSettings::removeMessageTail, &AyuSettings::setRemoveMessageTail },
	};
	return result;
}

[[nodiscard]] const std::vector<OptionRange> &OptionRanges() {
	static const auto result = std::vector<OptionRange>{
		{ u"avatarCorners"_q, 0, AyuUiSettings::kMaxAvatarCorners, &AyuSettings::avatarCorners, &AyuSettings::setAvatarCorners },
		{ u"messageBubbleRadius"_q, 0, 16, &AyuSettings::messageBubbleRadius, &AyuSettings::setMessageBubbleRadius },
		{ u"graphiteGrainOpacity"_q, 0, 10, &AyuSettings::graphiteGrainOpacity, &AyuSettings::setGraphiteGrainOpacity },
		{ u"graphiteChatSurface"_q, 0, 5, &AyuSettings::graphiteChatSurface, &AyuSettings::setGraphiteChatSurface },
		{ u"graphiteNavSurface"_q, 0, 5, &AyuSettings::graphiteNavSurface, &AyuSettings::setGraphiteNavSurface },
		{ u"graphiteListSurface"_q, 0, 5, &AyuSettings::graphiteListSurface, &AyuSettings::setGraphiteListSurface },
		{ u"graphiteAvatarSize"_q, 32, 56, &AyuSettings::graphiteAvatarSize, &AyuSettings::setGraphiteAvatarSize },
		{ u"graphiteMessageSize"_q, 12, 20, &AyuSettings::graphiteMessageSize, &AyuSettings::setGraphiteMessageSize },
	};
	return result;
}

[[nodiscard]] std::string Key(const char *name, uint64 id) {
	return std::string("vygram.") + name + '.' + std::to_string(id);
}

[[nodiscard]] std::string ChatKey(not_null<PeerData*> peer) {
	return Key("chat_wallpaper_hidden", peer->session().uniqueId())
		+ '.'
		+ std::to_string(peer->id.value);
}

[[nodiscard]] bool LoadFlag(const std::string &key) {
	const auto value = AyuDatabase::tryLoadPrivateSetting(key);
	return value && (*value == "1");
}

bool StoreFlag(const std::string &key, bool value) {
	return AyuDatabase::storePrivateSetting(
		key,
		value ? QByteArray("1") : QByteArray());
}

template <typename MapKey>
[[nodiscard]] rpl::variable<bool> &CachedFlag(
		base::flat_map<
			MapKey,
			std::unique_ptr<rpl::variable<bool>>> &map,
		const MapKey &id,
		const std::string &storageKey) {
	auto i = map.find(id);
	if (i == end(map)) {
		i = map.emplace(
			id,
			std::make_unique<rpl::variable<bool>>(LoadFlag(storageKey))
		).first;
	}
	return *i->second;
}

[[nodiscard]] rpl::variable<bool> &OwnFlag(uint64 id) {
	return CachedFlag(GlobalState().own, id, Key("appearance.own", id));
}

[[nodiscard]] rpl::variable<bool> &WallpapersFlag(uint64 id) {
	return CachedFlag(
		GlobalState().wallpapers,
		id,
		Key("appearance.wallpapers_hidden", id));
}

[[nodiscard]] rpl::variable<bool> &ChatFlag(not_null<PeerData*> peer) {
	const auto id = std::make_pair(
		peer->session().uniqueId(),
		peer->id.value);
	return CachedFlag(GlobalState().chats, id, ChatKey(peer));
}

[[nodiscard]] rpl::variable<std::optional<QColor>> &AccentVariable(
		uint64 id) {
	auto &map = GlobalState().accents;
	auto i = map.find(id);
	if (i == end(map)) {
		auto color = std::optional<QColor>();
		const auto value = AyuDatabase::tryLoadPrivateSetting(
			Key("appearance.accent", id));
		if (value && !value->isEmpty()) {
			const auto parsed = QColor(QString::fromLatin1(*value));
			if (parsed.isValid()) {
				color = parsed;
			}
		}
		i = map.emplace(
			id,
			std::make_unique<rpl::variable<std::optional<QColor>>>(color)
		).first;
	}
	return *i->second;
}

[[nodiscard]] uint64 SlotFor(not_null<Main::Session*> session) {
	return HasOwn(session) ? session->uniqueId() : kSharedSlot;
}

[[nodiscard]] uint64 CurrentSlot() {
	const auto value = AyuDatabase::tryLoadPrivateSetting(kCurrentSlotKey);
	return value ? value->toULongLong() : kSharedSlot;
}

[[nodiscard]] bool ActiveInMainWindow(not_null<Main::Session*> session) {
	const auto account = &session->account();
	return Core::App().domain().started()
		&& (&Core::App().domain().active() == account)
		&& !Core::App().separateWindowFor(account);
}

[[nodiscard]] QByteArray EncodeImage(const QImage &image, bool pattern) {
	if (image.isNull()) {
		return QByteArray();
	}
	auto result = QByteArray();
	auto buffer = QBuffer(&result);
	if (!buffer.open(QIODevice::WriteOnly)) {
		return QByteArray();
	}
	const auto saved = pattern
		? image.save(&buffer, "PNG")
		: image.save(&buffer, "JPG", kJpegQuality);
	return saved ? result : QByteArray();
}

[[nodiscard]] QString Base64(const QByteArray &bytes) {
	return QString::fromLatin1(bytes.toBase64());
}

[[nodiscard]] QByteArray FromBase64(const QJsonObject &object, const QString &key) {
	return QByteArray::fromBase64(object.value(key).toString().toLatin1());
}

[[nodiscard]] QByteArray SerializeLook(
		const Window::Theme::LookSnapshot &look) {
	const auto &theme = look.theme;
	const auto cloud = QJsonObject{
		{ u"id"_q, QString::number(theme.cloud.id) },
		{ u"accessHash"_q, QString::number(theme.cloud.accessHash) },
		{ u"slug"_q, theme.cloud.slug },
		{ u"title"_q, theme.cloud.title },
		{ u"documentId"_q, QString::number(theme.cloud.documentId) },
		{ u"createdBy"_q, QString::number(theme.cloud.createdBy.bare) },
	};
	const auto object = QJsonObject{
		{ u"version"_q, kLookVersion },
		{ u"night"_q, look.nightMode },
		{ u"themePath"_q, theme.pathAbsolute },
		{ u"themeRelative"_q, theme.pathRelative },
		{ u"themeContent"_q, Base64(theme.content) },
		{ u"cloud"_q, cloud },
		{ u"paper"_q, Base64(look.paper.serialize()) },
		{ u"image"_q, Base64(EncodeImage(look.image, look.paper.isPattern())) },
		{ u"tile"_q, look.tile },
		{ u"options"_q, CaptureOptions() },
	};
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

[[nodiscard]] std::optional<StoredLook> LoadLook(uint64 slot) {
	const auto bytes = AyuDatabase::tryLoadPrivateSetting(
		Key("appearance.slot", slot));
	if (!bytes || bytes->isEmpty()) {
		return std::nullopt;
	}
	const auto object = QJsonDocument::fromJson(*bytes).object();
	if (object.value(u"version"_q).toInt() != kLookVersion) {
		return std::nullopt;
	}
	const auto paper = Data::WallPaper::FromSerialized(
		FromBase64(object, u"paper"_q));
	if (!paper) {
		return std::nullopt;
	}
	auto result = StoredLook();
	auto &look = result.look;
	auto &theme = look.theme;
	const auto cloud = object.value(u"cloud"_q).toObject();
	const auto number = [&](const QString &key) {
		return cloud.value(key).toString().toULongLong();
	};
	look.nightMode = object.value(u"night"_q).toBool();
	theme.pathAbsolute = object.value(u"themePath"_q).toString();
	theme.pathRelative = object.value(u"themeRelative"_q).toString();
	theme.content = FromBase64(object, u"themeContent"_q);
	theme.cloud.id = number(u"id"_q);
	theme.cloud.accessHash = number(u"accessHash"_q);
	theme.cloud.slug = cloud.value(u"slug"_q).toString();
	theme.cloud.title = cloud.value(u"title"_q).toString();
	theme.cloud.documentId = number(u"documentId"_q);
	theme.cloud.createdBy = UserId(number(u"createdBy"_q));
	look.paper = *paper;
	const auto image = QImage::fromData(FromBase64(object, u"image"_q));
	if (!image.isNull()) {
		look.image = image.convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	look.tile = object.value(u"tile"_q).toBool();
	result.options = object.value(u"options"_q).toObject();
	return result;
}

bool StoreCurrentLook(uint64 slot) {
	const auto look = Window::Theme::Background()->captureLook();
	return look && AyuDatabase::storePrivateSetting(
		Key("appearance.slot", slot),
		SerializeLook(*look));
}

void SwitchTo(uint64 slot) {
	auto &state = GlobalState();
	if (!state.ready || !AyuDatabase::isUnlocked()) {
		state.pendingSlot = slot;
		return;
	}
	state.pendingSlot = std::nullopt;
	const auto current = CurrentSlot();
	if (current == slot) {
		return;
	} else if (!StoreCurrentLook(current)) {
		LOG(("VyGram: could not save appearance slot %1.").arg(current));
		return;
	}
	if (const auto stored = LoadLook(slot)) {
		if (!Window::Theme::Background()->applyLook(stored->look)) {
			LOG(("VyGram: could not apply appearance slot %1.").arg(slot));
			return;
		} else if (ValidateOptions(stored->options)) {
			ApplyOptions(stored->options);
		}
	}
	const auto saved = AyuDatabase::storePrivateSetting(
		kCurrentSlotKey,
		QByteArray::number(qulonglong(slot)));
	if (!saved) {
		LOG(("VyGram: could not store the current appearance slot."));
	}
}

} // namespace

void Activating(not_null<Main::Account*> account) {
	auto &state = GlobalState();
	state.sessionWait.destroy();
	if (Core::App().separateWindowFor(account)) {
		return;
	}
	account->sessionValue(
	) | rpl::filter([](Main::Session *session) {
		return (session != nullptr);
	}) | rpl::take(1) | rpl::on_next([](Main::Session *session) {
		SwitchTo(SlotFor(session));
	}, state.sessionWait);
}

void BackgroundReady() {
	auto &state = GlobalState();
	state.ready = true;
	if (const auto slot = base::take(state.pendingSlot)) {
		SwitchTo(*slot);
	}
}

bool HasOwn(not_null<Main::Session*> session) {
	return OwnFlag(session->uniqueId()).current();
}

rpl::producer<bool> HasOwnValue(not_null<Main::Session*> session) {
	return OwnFlag(session->uniqueId()).value();
}

bool SetOwn(not_null<Main::Session*> session, bool own) {
	const auto id = session->uniqueId();
	if (!StoreFlag(Key("appearance.own", id), own)) {
		return false;
	}
	OwnFlag(id) = own;
	if (ActiveInMainWindow(session)) {
		SwitchTo(SlotFor(session));
	}
	return true;
}

const std::vector<QColor> &AccentChoices() {
	static const auto result = std::vector<QColor>{
		QColor(0xE0, 0xA0, 0x48),
		QColor(0xD9, 0x5F, 0x5F),
		QColor(0x5E, 0xB5, 0x6E),
		QColor(0x4F, 0x9C, 0xDB),
		QColor(0x9C, 0x6E, 0xD8),
		QColor(0xD8, 0x6E, 0xAB),
		QColor(0x4E, 0xC3, 0xC0),
		QColor(0xB4, 0xBA, 0xC2),
	};
	return result;
}

std::optional<QColor> Accent(uint64 sessionUniqueId) {
	return AccentVariable(sessionUniqueId).current();
}

rpl::producer<std::optional<QColor>> AccentValue(uint64 sessionUniqueId) {
	return AccentVariable(sessionUniqueId).value();
}

bool SetAccent(uint64 sessionUniqueId, std::optional<QColor> color) {
	const auto stored = AyuDatabase::storePrivateSetting(
		Key("appearance.accent", sessionUniqueId),
		color ? color->name(QColor::HexRgb).toLatin1() : QByteArray());
	if (!stored) {
		return false;
	}
	AccentVariable(sessionUniqueId) = color;
	return true;
}

bool WallpapersHidden(not_null<Main::Session*> session) {
	return WallpapersFlag(session->uniqueId()).current();
}

rpl::producer<bool> WallpapersHiddenValue(not_null<Main::Session*> session) {
	return WallpapersFlag(session->uniqueId()).value();
}

bool SetWallpapersHidden(not_null<Main::Session*> session, bool hidden) {
	const auto id = session->uniqueId();
	if (!StoreFlag(Key("appearance.wallpapers_hidden", id), hidden)) {
		return false;
	}
	WallpapersFlag(id) = hidden;
	return true;
}

bool ChatWallpaperHidden(not_null<PeerData*> peer) {
	return ChatFlag(peer).current();
}

rpl::producer<bool> ChatWallpaperHiddenValue(not_null<PeerData*> peer) {
	return ChatFlag(peer).value();
}

bool SetChatWallpaperHidden(not_null<PeerData*> peer, bool hidden) {
	if (!StoreFlag(ChatKey(peer), hidden)) {
		return false;
	}
	ChatFlag(peer) = hidden;
	return true;
}

QJsonObject CaptureOptions() {
	const auto &settings = AyuSettings::getInstance();
	auto result = QJsonObject();
	for (const auto &flag : OptionFlags()) {
		result.insert(flag.key, (settings.*flag.get)());
	}
	for (const auto &range : OptionRanges()) {
		result.insert(range.key, (settings.*range.get)());
	}
	result.insert(u"monoFont"_q, settings.monoFont());
	return result;
}

bool ValidateOptions(const QJsonObject &data) {
	for (const auto &flag : OptionFlags()) {
		if (!data.value(flag.key).isBool()) {
			return false;
		}
	}
	for (const auto &range : OptionRanges()) {
		// Earlier v1 profiles lacked the optional Graphite appearance controls.
		if (!data.contains(range.key) && range.key.startsWith(u"graphite"_q)) {
			continue;
		}
		const auto value = data.value(range.key).toInt(-1);
		if (value < range.min || value > range.max) {
			return false;
		}
	}
	const auto font = data.value(u"monoFont"_q);
	return font.isString() && (font.toString().size() <= kMaxFontNameLength);
}

void ApplyOptions(const QJsonObject &data) {
	auto &settings = AyuSettings::getInstance();
	for (const auto &flag : OptionFlags()) {
		(settings.*flag.set)(data.value(flag.key).toBool());
	}
	for (const auto &range : OptionRanges()) {
		if (data.contains(range.key)) {
			(settings.*range.set)(data.value(range.key).toInt());
		}
	}
	settings.setMonoFont(data.value(u"monoFont"_q).toString());
	Ui::SetAppliedBubbleRadius(settings.messageBubbleRadius());
	AyuInfra::applyProtectedUiSettings();
}

} // namespace Ayu::AccountAppearance
