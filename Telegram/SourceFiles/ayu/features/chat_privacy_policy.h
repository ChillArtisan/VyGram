#pragma once

#include <cstdint>
#include <string>

namespace Ayu::ChatPrivacy {

enum class IndicatorMode {
	Inherit,
	Allow,
	Hide,
};

inline std::string IndicatorKey(
		std::uint64_t account,
		std::uint64_t peer) {
	return "chat-indicators/" + std::to_string(account)
		+ "/" + std::to_string(peer);
}

constexpr bool IndicatorsEnabled(IndicatorMode mode, bool inherited) {
	return (mode == IndicatorMode::Inherit)
		? inherited
		: (mode == IndicatorMode::Allow);
}

} // namespace Ayu::ChatPrivacy
