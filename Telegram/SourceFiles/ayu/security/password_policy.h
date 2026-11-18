#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace Ayu::Security {

[[nodiscard]] inline bool IsStrongLocalPassword(std::u32string_view password) {
	if (password.size() < 12) {
		return false;
	}
	const auto fold = [](char32_t ch) {
		return (ch >= U'A' && ch <= U'Z') ? ch + (U'a' - U'A') : ch;
	};
	const auto space = [](char32_t ch) {
		return (ch >= 0x09 && ch <= 0x0D)
			|| ch == 0x20 || ch == 0x85 || ch == 0xA0 || ch == 0x1680
			|| (ch >= 0x2000 && ch <= 0x200A)
			|| ch == 0x2028 || ch == 0x2029 || ch == 0x202F
			|| ch == 0x205F || ch == 0x3000;
	};
	auto distinct = std::array<char32_t, 5>();
	auto count = std::size_t(0);
	for (const auto ch : password) {
		if (ch > 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF)) {
			return false;
		}
		if (count != distinct.size() && !space(ch)) {
			const auto end = distinct.begin() + count;
			if (std::find(distinct.begin(), end, fold(ch)) == end) {
				distinct[count++] = fold(ch);
			}
		}
	}
	if (count < distinct.size()) {
		return false;
	}
	for (auto period = std::size_t(1); period <= password.size() / 2; ++period) {
		auto repeated = true;
		for (auto i = period; i != password.size(); ++i) {
			if (fold(password[i]) != fold(password[i % period])) {
				repeated = false;
				break;
			}
		}
		if (repeated) {
			return false;
		}
	}
	for (const auto sequence : {
		std::u32string_view(U"01234567890123456789"),
		std::u32string_view(U"12345678901234567890"),
		std::u32string_view(U"abcdefghijklmnopqrstuvwxyz"),
		std::u32string_view(U"qwertyuiopasdfghjklzxcvbnm") }) {
		if (std::search(sequence.begin(), sequence.end(),
				password.begin(), password.end(),
				[&](char32_t a, char32_t b) { return a == fold(b); })
				!= sequence.end()
			|| std::search(sequence.rbegin(), sequence.rend(),
				password.begin(), password.end(),
				[&](char32_t a, char32_t b) { return a == fold(b); })
				!= sequence.rend()) {
			return false;
		}
	}
	for (const auto common : {
		std::u32string_view(U"password1234"),
		std::u32string_view(U"password12345"),
		std::u32string_view(U"password123456"),
		std::u32string_view(U"qwerty123456"),
		std::u32string_view(U"qwerty123456789") }) {
		if (std::equal(password.begin(), password.end(),
				common.begin(), common.end(),
				[&](char32_t a, char32_t b) { return fold(a) == b; })) {
			return false;
		}
	}
	return true;
}

} // namespace Ayu::Security