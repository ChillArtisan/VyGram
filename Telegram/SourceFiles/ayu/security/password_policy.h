#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace Ayu::Security {

inline constexpr auto kMinimumLocalPasswordLength = std::size_t(12);

// The local passcode is the only secret between an attacker holding a copy
// of tdata and the archive, so it is rated by how many guesses a smart
// offline attacker needs (dictionary words, names, keyboard walks, leet
// substitutions, dates, repeats, sequences), not by its length alone.
// 50 bits keeps even a GPU farm against the 64 MiB scrypt busy for years.
inline constexpr auto kMinimumLocalPasswordBits = 50.;

namespace details {

// Rough frequency order: a word's guess cost is log2 of its position.
// Lowercase, with "ё" written as "е" (see Fold()).
inline constexpr std::u32string_view kCommonTokens[] = {
	U"password", U"qwerty", U"iloveyou", U"admin", U"welcome", U"monkey",
	U"dragon", U"master", U"letmein", U"login", U"princess", U"qwertyuiop",
	U"sunshine", U"shadow", U"football", U"baseball", U"superman",
	U"batman", U"trustno", U"michael", U"jennifer", U"hunter", U"freedom",
	U"whatever", U"starwars", U"pokemon", U"minecraft", U"computer",
	U"internet", U"secret", U"killer", U"hello", U"charlie", U"donald",
	U"mustang", U"access", U"flower", U"cookie", U"matrix", U"ninja",
	U"azerty", U"love", U"lovely", U"angel", U"soccer", U"hockey",
	U"ranger", U"buster", U"thomas", U"tigger", U"robert", U"daniel",
	U"jordan", U"harley", U"pepper", U"ginger", U"passw", U"pass", U"pwd",
	U"asdf", U"asdfgh", U"asdfghjkl", U"zxcv", U"zxcvbn", U"zxcvbnm",
	U"qazwsx", U"qazwsxedc", U"1qaz2wsx", U"1q2w3e", U"1q2w3e4r",
	U"1q2w3e4r5t", U"1q2w3e4r5t6y", U"zaq12wsx", U"zaq1xsw2", U"1qazxsw2",
	U"qweasd", U"qweasdzxc", U"123qwe", U"qwe123", U"qwer", U"qwert",
	U"poiuytrewq", U"mnbvcxz", U"lkjhgfdsa", U"147258369", U"159753",
	U"147852369", U"789456123", U"741852963", U"159357", U"123321",
	U"summer", U"winter", U"spring", U"autumn", U"fall",
	U"monday", U"tuesday", U"wednesday", U"thursday", U"friday",
	U"saturday", U"sunday", U"january", U"february", U"march", U"april",
	U"may", U"june", U"july", U"august", U"september", U"october",
	U"november", U"december",
	U"red", U"blue", U"green", U"black", U"white", U"yellow", U"orange",
	U"purple", U"silver", U"golden", U"gold", U"pink", U"brown", U"grey",
	U"gray", U"cat", U"dog", U"kitty", U"puppy", U"tiger", U"lion",
	U"bear", U"wolf", U"eagle", U"horse", U"fish", U"bird", U"snake",
	U"rabbit", U"bunny", U"panda", U"fox", U"shark", U"dolphin",
	U"apple", U"banana", U"cherry", U"lemon", U"mango", U"chocolate",
	U"coffee", U"pizza", U"cake", U"candy", U"sugar", U"butter",
	U"money", U"family", U"friend", U"friends", U"happy", U"smile",
	U"life", U"live", U"forever", U"always", U"heart", U"baby", U"sweet",
	U"honey", U"darling", U"beautiful", U"pretty", U"girl", U"boy",
	U"king", U"queen", U"prince", U"god", U"jesus", U"christ", U"lucky",
	U"magic", U"star", U"stars", U"moon", U"sun", U"sky", U"ocean",
	U"sea", U"river", U"lake", U"mountain", U"forest", U"fire", U"water",
	U"earth", U"world", U"house", U"home", U"school", U"college",
	U"student", U"teacher", U"music", U"guitar", U"rock", U"metal",
	U"game", U"games", U"gamer", U"player", U"test", U"tester", U"user",
	U"guest", U"root", U"system", U"server", U"windows", U"linux",
	U"google", U"yahoo", U"facebook", U"telegram", U"whatsapp", U"iphone",
	U"samsung", U"android", U"nokia", U"mercedes", U"ferrari", U"porsche",
	U"bmw", U"audi", U"toyota", U"honda", U"london", U"paris", U"moscow",
	U"berlin", U"newyork", U"america", U"russia", U"ukraine", U"england",
	U"germany", U"china", U"india", U"canada", U"john", U"james",
	U"david", U"william", U"richard", U"joseph", U"alex", U"alexander",
	U"andrew", U"anna", U"maria", U"mary", U"elena", U"olga", U"natasha",
	U"sergey", U"dmitry", U"ivan", U"nikita", U"vladimir", U"maxim",
	U"artem", U"kate", U"julia", U"sasha", U"masha", U"dasha", U"katya",
	U"sophia", U"emma", U"olivia", U"isabella", U"liam", U"noah",
	U"oliver", U"jack", U"harry", U"george", U"peter", U"paul", U"mark",
	U"steven", U"kevin", U"brian", U"jason", U"justin", U"ashley",
	U"jessica", U"amanda", U"sarah", U"nicole", U"michelle", U"lisa",
	U"secure", U"security", U"private", U"changeme",
	U"default", U"hacker", U"hack", U"crypto", U"bitcoin", U"wallet",
	U"archive", U"backup", U"phone", U"mobile", U"office", U"work",
	U"company", U"business", U"power", U"energy", U"light", U"dark",
	U"night", U"day", U"time", U"today", U"tomorrow", U"winner",
	U"champion", U"warrior", U"soldier", U"knight", U"wizard", U"legend",
	U"hero", U"devil", U"demon", U"ghost", U"zombie", U"vampire",
	U"spider", U"spiderman", U"ironman", U"thor", U"hulk", U"naruto",
	U"anime", U"disney", U"mickey", U"barbie", U"lego", U"fortnite",
	U"roblox", U"steam", U"valve", U"counter", U"strike", U"warcraft",
	U"diablo", U"zelda", U"mario", U"sonic", U"genius", U"crazy",
	U"cool", U"super", U"best", U"good", U"great", U"nice", U"big",
	U"little", U"small", U"new", U"old", U"first", U"last", U"one",
	U"two", U"three", U"four", U"five", U"six", U"seven", U"eight",
	U"nine", U"ten", U"hundred", U"thousand", U"million",
	// Transliterated Russian and Russian typed on a Latin layout.
	U"parol", U"privet", U"lubov", U"lyubov", U"solnce", U"solnyshko",
	U"kotik", U"zaika", U"zayka", U"rossiya", U"moskva", U"piter",
	U"zenit", U"spartak", U"dinamo", U"cska", U"nastya", U"vova",
	U"misha", U"pasha", U"dima", U"kolya", U"natali", U"svetlana",
	U"sveta", U"tatyana", U"tanya", U"irina", U"marina", U"oksana",
	U"anya", U"lena", U"yulia", U"ivanov", U"petrov", U"smirnov",
	U"mama", U"papa", U"lublu", U"tebya", U"kiska", U"sobaka",
	U"gfhjkm", U"ghbdtn", U"qwertyu", U"ytrewq",
	// Russian.
	U"пароль", U"привет", U"любовь", U"солнце", U"солнышко", U"котик",
	U"зайка", U"зайчик", U"россия", U"москва", U"питер", U"наташа",
	U"маша", U"саша", U"даша", U"катя", U"настя", U"лена", U"оля",
	U"таня", U"света", U"ирина", U"марина", U"анна", U"мария", U"елена",
	U"ольга", U"наталья", U"татьяна", U"светлана", U"александр",
	U"алексей", U"сергей", U"дмитрий", U"андрей", U"иван", U"максим",
	U"владимир", U"михаил", U"никита", U"артем", U"денис", U"евгений",
	U"мама", U"папа", U"люблю", U"тебя", U"кот", U"кошка", U"собака",
	U"зима", U"весна", U"лето", U"осень", U"январь", U"февраль", U"март",
	U"апрель", U"май", U"июнь", U"июль", U"август", U"сентябрь",
	U"октябрь", U"ноябрь", U"декабрь", U"понедельник", U"вторник",
	U"среда", U"четверг", U"пятница", U"суббота", U"воскресенье",
	U"красный", U"синий", U"зеленый", U"черный", U"белый", U"желтый",
	U"счастье", U"жизнь", U"дом", U"мир", U"друг", U"друзья", U"семья",
	U"деньги", U"бог", U"ангел", U"король", U"королева", U"принцесса",
	U"игра", U"футбол", U"хоккей", U"спартак", U"зенит", U"динамо",
	U"работа", U"школа", U"секрет", U"админ", U"компьютер", U"телеграм",
	U"небо", U"море", U"река", U"звезда", U"луна", U"огонь",
	U"вода", U"земля", U"тигр", U"волк", U"медведь", U"лиса", U"заяц",
	U"кролик", U"рыба", U"птица", U"цветок", U"роза", U"яблоко",
	U"пока", U"спасибо", U"хорошо", U"класс", U"супер",
	U"йцукен", U"йцукенгшщз", U"фыва", U"фывапр", U"фывапролдж",
	U"ячсмить", U"ячсмит", U"цуке",
};

[[nodiscard]] inline char32_t Fold(char32_t ch) {
	if (ch >= U'A' && ch <= U'Z') {
		return ch + (U'a' - U'A');
	} else if (ch >= 0x0410 && ch <= 0x042F) {
		return ch + 0x20;
	} else if (ch == 0x0401 || ch == 0x0451) {
		return 0x0435;
	}
	return ch;
}

[[nodiscard]] inline bool IsUpper(char32_t ch) {
	return (ch >= U'A' && ch <= U'Z')
		|| (ch >= 0x0410 && ch <= 0x042F)
		|| ch == 0x0401;
}

[[nodiscard]] inline bool IsLower(char32_t ch) {
	return (ch >= U'a' && ch <= U'z')
		|| (ch >= 0x0430 && ch <= 0x044F)
		|| ch == 0x0451;
}

[[nodiscard]] inline bool IsDigit(char32_t ch) {
	return ch >= U'0' && ch <= U'9';
}

[[nodiscard]] inline bool IsSpace(char32_t ch) {
	return (ch >= 0x09 && ch <= 0x0D)
		|| ch == 0x20 || ch == 0x85 || ch == 0xA0 || ch == 0x1680
		|| (ch >= 0x2000 && ch <= 0x200A)
		|| ch == 0x2028 || ch == 0x2029 || ch == 0x202F
		|| ch == 0x205F || ch == 0x3000;
}

// Common leet substitutions, applied to a folded character.
[[nodiscard]] inline bool Unleets(char32_t ch, char32_t letter) {
	switch (ch) {
	case U'4': case U'@': return letter == U'a';
	case U'8': return letter == U'b';
	case U'(': return letter == U'c';
	case U'3': return letter == U'e';
	case U'6': case U'9': return letter == U'g';
	case U'1': case U'!': case U'|': return letter == U'i' || letter == U'l';
	case U'0': return letter == U'o';
	case U'5': case U'$': return letter == U's';
	case U'7': case U'+': return letter == U't';
	case U'2': return letter == U'z';
	}
	return false;
}

// Guessing cost of a character an attacker has to brute force.
[[nodiscard]] inline double BruteBits(char32_t folded) {
	if (folded >= U'a' && folded <= U'z') {
		return 4.70; // log2(26)
	} else if (IsDigit(folded)) {
		return 3.32; // log2(10)
	} else if (folded >= 0x0430 && folded <= 0x044F) {
		return 5.04; // log2(33)
	} else if (folded < 0x80) {
		return 5.04; // ASCII symbols and space, log2(33)
	}
	return 7.;
}

[[nodiscard]] inline double Binomial(int n, int k) {
	auto result = 1.;
	for (auto i = 1; i <= k; ++i) {
		result = result * (n - k + i) / i;
	}
	return result;
}

// Extra guesses for the capitalization of a dictionary word.
[[nodiscard]] inline double CaseBits(std::u32string_view token) {
	auto upper = 0;
	auto lower = 0;
	for (const auto ch : token) {
		if (IsUpper(ch)) {
			++upper;
		} else if (IsLower(ch)) {
			++lower;
		}
	}
	if (!upper) {
		return 0.;
	} else if (!lower
		|| (upper == 1 && (IsUpper(token.front()) || IsUpper(token.back())))) {
		return 1.;
	}
	auto variants = 0.;
	for (auto i = 1; i <= std::min(upper, lower); ++i) {
		variants += Binomial(upper + lower, i);
	}
	return std::log2(variants);
}

[[nodiscard]] inline int Number(const std::u32string &s, std::size_t from, std::size_t count) {
	auto result = 0;
	for (auto i = from; i != from + count; ++i) {
		result = result * 10 + int(s[i] - U'0');
	}
	return result;
}

[[nodiscard]] inline bool ValidDate(int day, int month) {
	return day >= 1 && day <= 31 && month >= 1 && month <= 12;
}

// Guess cost of a date at s[from, from + count), or infinity.
[[nodiscard]] inline double DateBits(
		const std::u32string &s,
		std::size_t from,
		std::size_t count) {
	constexpr auto kNone = std::numeric_limits<double>::infinity();
	for (auto i = from; i != from + count; ++i) {
		if (!IsDigit(s[i])) {
			return kNone;
		}
	}
	const auto year = [](int value) { return value >= 1900 && value <= 2039; };
	if (count == 4) {
		return year(Number(s, from, 4)) ? 6.9 : kNone; // log2(140)
	} else if (count == 6) {
		const auto a = Number(s, from, 2);
		const auto b = Number(s, from + 2, 2);
		const auto c = Number(s, from + 4, 2);
		return (ValidDate(a, b) || ValidDate(b, a) || ValidDate(c, b))
			? 15.2 // log2(31 * 12 * 100)
			: kNone;
	} else if (count == 8) {
		const auto a = Number(s, from, 2);
		const auto b = Number(s, from + 2, 2);
		const auto tail = Number(s, from + 4, 4);
		const auto head = Number(s, from, 4);
		const auto c = Number(s, from + 4, 2);
		const auto d = Number(s, from + 6, 2);
		return ((year(tail) && (ValidDate(a, b) || ValidDate(b, a)))
				|| (year(head) && ValidDate(d, c)))
			? 15.7 // log2(31 * 12 * 140)
			: kNone;
	}
	return kNone;
}

[[nodiscard]] inline int SequenceClass(char32_t folded) {
	if (folded >= U'a' && folded <= U'z') {
		return 1;
	} else if (IsDigit(folded)) {
		return 2;
	} else if (folded >= 0x0430 && folded <= 0x044F) {
		return 3;
	}
	return 0;
}

// Minimal total guess cost, in bits, over all ways to split the password
// into known patterns and brute-forced characters.
[[nodiscard]] inline double EstimateBits(
		std::u32string_view original,
		bool detectRepeats) {
	const auto n = original.size();
	auto s = std::u32string(n, U'\0');
	std::transform(original.begin(), original.end(), s.begin(), Fold);

	auto best = std::vector<double>(
		n + 1,
		std::numeric_limits<double>::infinity());
	best[0] = 0.;
	const auto relax = [&](std::size_t from, std::size_t till, double bits) {
		best[till] = std::min(best[till], best[from] + bits);
	};
	for (auto i = std::size_t(0); i != n; ++i) {
		relax(i, i + 1, BruteBits(s[i]));

		// Dictionary words, forwards and reversed, with leet substitutions.
		auto rank = 0;
		for (const auto word : kCommonTokens) {
			++rank;
			const auto length = word.size();
			if (length < 3 || length > n - i) {
				continue;
			}
			for (const auto reversed : { false, true }) {
				auto substitutions = 0;
				auto matched = true;
				for (auto k = std::size_t(0); k != length; ++k) {
					const auto expected = reversed
						? word[length - 1 - k]
						: word[k];
					const auto actual = s[i + k];
					if (actual == expected) {
						continue;
					} else if (Unleets(actual, expected)) {
						++substitutions;
					} else {
						matched = false;
						break;
					}
				}
				if (matched) {
					relax(i, i + length, std::log2(double(rank))
						+ CaseBits(original.substr(i, length))
						+ substitutions
						+ (reversed ? 1. : 0.));
				}
			}
		}

		// Dates and years.
		for (const auto length : { 4, 6, 8 }) {
			if (std::size_t(length) <= n - i) {
				const auto bits = DateBits(s, i, length);
				if (bits < std::numeric_limits<double>::infinity()) {
					relax(i, i + length, bits);
				}
			}
		}

		// Sequences: abc, 987, гдеж.
		const auto type = SequenceClass(s[i]);
		if (type && i + 1 < n) {
			const auto delta = int(s[i + 1]) - int(s[i]);
			if (delta == 1 || delta == -1) {
				const auto start = s[i];
				const auto obvious = (start == U'a' || start == U'z'
					|| start == U'0' || start == U'1' || start == U'9'
					|| start == 0x0430 || start == 0x044F);
				const auto base = obvious
					? 2.
					: (type == 2) ? 3.32 : 4.70;
				for (auto j = i + 1; j != n; ++j) {
					if (SequenceClass(s[j]) != type
						|| int(s[j]) - int(s[j - 1]) != delta) {
						break;
					}
					const auto length = j + 1 - i;
					if (length >= 3) {
						relax(i, j + 1, base
							+ std::log2(double(length))
							+ (delta < 0 ? 1. : 0.));
					}
				}
			}
		}

		// Repeats: abcabc, !!!!, lovelove.
		if (detectRepeats) {
			for (auto period = std::size_t(1); period * 2 <= n - i; ++period) {
				auto count = std::size_t(1);
				while (i + period * (count + 1) <= n
					&& std::equal(
						s.begin() + i,
						s.begin() + i + period,
						s.begin() + i + period * count)) {
					++count;
				}
				if (count < 2) {
					continue;
				}
				const auto base = EstimateBits(
					original.substr(i, period),
					false);
				for (auto times = std::size_t(2); times <= count; ++times) {
					relax(i, i + period * times, base + std::log2(double(times)));
				}
			}
		}
	}
	return best[n];
}

} // namespace details

// Estimated guessing entropy. Only the first 64 characters are analysed,
// which can only underestimate longer passwords.
[[nodiscard]] inline double EstimateLocalPasswordBits(
		std::u32string_view password) {
	return details::EstimateBits(password.substr(0, 64), true);
}

[[nodiscard]] inline bool IsStrongLocalPassword(std::u32string_view password) {
	if (password.size() < kMinimumLocalPasswordLength) {
		return false;
	}
	auto distinct = std::vector<char32_t>();
	for (const auto ch : password) {
		if (ch > 0x10FFFF || (ch >= 0xD800 && ch <= 0xDFFF)) {
			return false;
		}
		const auto folded = details::Fold(ch);
		if (!details::IsSpace(ch)
			&& distinct.size() < 5
			&& std::find(distinct.begin(), distinct.end(), folded)
				== distinct.end()) {
			distinct.push_back(folded);
		}
	}
	return distinct.size() >= 5
		&& EstimateLocalPasswordBits(password) >= kMinimumLocalPasswordBits;
}

} // namespace Ayu::Security
