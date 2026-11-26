#pragma once

#include <QtCore/QJsonObject>
#include <QtGui/QColor>

#include <optional>
#include <vector>

class PeerData;

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Ayu::AccountAppearance {

void Activating(not_null<Main::Account*> account);
void BackgroundReady();

[[nodiscard]] bool HasOwn(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<bool> HasOwnValue(
	not_null<Main::Session*> session);
bool SetOwn(not_null<Main::Session*> session, bool own);

[[nodiscard]] const std::vector<QColor> &AccentChoices();
[[nodiscard]] std::optional<QColor> Accent(uint64 sessionUniqueId);
[[nodiscard]] rpl::producer<std::optional<QColor>> AccentValue(
	uint64 sessionUniqueId);
bool SetAccent(uint64 sessionUniqueId, std::optional<QColor> color);

[[nodiscard]] bool WallpapersHidden(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<bool> WallpapersHiddenValue(
	not_null<Main::Session*> session);
bool SetWallpapersHidden(not_null<Main::Session*> session, bool hidden);

[[nodiscard]] bool ChatWallpaperHidden(not_null<PeerData*> peer);
[[nodiscard]] rpl::producer<bool> ChatWallpaperHiddenValue(
	not_null<PeerData*> peer);
bool SetChatWallpaperHidden(not_null<PeerData*> peer, bool hidden);

[[nodiscard]] QJsonObject CaptureOptions();
[[nodiscard]] bool ValidateOptions(const QJsonObject &data);
void ApplyOptions(const QJsonObject &data);

} // namespace Ayu::AccountAppearance
