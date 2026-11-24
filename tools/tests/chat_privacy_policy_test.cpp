#include "ayu/features/chat_privacy_policy.h"

#include <cassert>

int main() {
	using namespace Ayu::ChatPrivacy;
	assert(IndicatorsEnabled(IndicatorMode::Inherit, true));
	assert(!IndicatorsEnabled(IndicatorMode::Inherit, false));
	assert(IndicatorsEnabled(IndicatorMode::Allow, false));
	assert(!IndicatorsEnabled(IndicatorMode::Hide, true));
	assert(IndicatorKey(1, 23) != IndicatorKey(12, 3));
	assert(IndicatorKey(1, 2) != IndicatorKey(2, 2));
}
