#pragma once

#include "ayu/features/chat_privacy_policy.h"

#include <optional>

class PeerData;

namespace Ayu::ChatPrivacy {

std::optional<IndicatorMode> LoadIndicators(not_null<PeerData*> peer);
bool SaveIndicators(not_null<PeerData*> peer, IndicatorMode mode);
bool SendIndicators(not_null<PeerData*> peer);

} // namespace Ayu::ChatPrivacy
