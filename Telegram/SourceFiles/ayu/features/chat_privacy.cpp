#include "ayu/features/chat_privacy.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "data/data_peer.h"
#include "main/main_session.h"

namespace Ayu::ChatPrivacy {

std::optional<IndicatorMode> LoadIndicators(not_null<PeerData*> peer) {
	const auto value = AyuDatabase::tryLoadPrivateSetting(
		IndicatorKey(peer->session().uniqueId(), peer->id.value));
	if (!value) {
		return std::nullopt;
	} else if (*value == "allow") {
		return IndicatorMode::Allow;
	} else if (*value == "hide") {
		return IndicatorMode::Hide;
	} else if (value->isEmpty() || *value == "inherit") {
		return IndicatorMode::Inherit;
	}
	return std::nullopt;
}

bool SaveIndicators(not_null<PeerData*> peer, IndicatorMode mode) {
	const auto value = (mode == IndicatorMode::Allow)
		? "allow"
		: (mode == IndicatorMode::Hide)
		? "hide"
		: "inherit";
	return AyuDatabase::storePrivateSetting(
		IndicatorKey(peer->session().uniqueId(), peer->id.value),
		QByteArray(value));
}

bool SendIndicators(not_null<PeerData*> peer) {
	const auto mode = LoadIndicators(peer);
	return mode && IndicatorsEnabled(
		*mode,
		AyuSettings::ghost(&peer->session()).sendUploadProgress());
}

} // namespace Ayu::ChatPrivacy
