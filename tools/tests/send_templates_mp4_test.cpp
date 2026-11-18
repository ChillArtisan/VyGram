// Standalone: cl /std:c++20 /EHsc /I Telegram/SourceFiles <this file>
#include "ayu/features/send_templates/send_templates_mp4.h"

#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;

Bytes Box(const char *type, Bytes payload, bool extended = false) {
	const auto size = uint64_t(payload.size() + (extended ? 16 : 8));
	auto result = Bytes(extended ? 16 : 8);
	for (auto i = 0; i != 4; ++i) {
		result[i] = static_cast<unsigned char>(extended ? (i == 3 ? 1 : 0) : (size >> (24 - i * 8)));
		result[4 + i] = type[i];
	}
	if (extended) {
		for (auto i = 0; i != 8; ++i) result[8 + i] = static_cast<unsigned char>(size >> (56 - i * 8));
	}
	result.insert(result.end(), payload.begin(), payload.end());
	return result;
}

Bytes Join(Bytes a, const Bytes &b) {
	a.insert(a.end(), b.begin(), b.end());
	return a;
}

Bytes Header(const char *type, int size, int version) {
	auto data = Bytes(size, 0x23);
	data[0] = static_cast<unsigned char>(version);
	return Box(type, data);
}

Bytes Movie(int version, bool extended = false) {
	return Join(Box("ftyp", Bytes{'i','s','o','m',0,0,0,0}),
		Join(Box("moov", Join(Header("mvhd", version ? 112 : 100, version),
			Join(Box("trak", Join(Header("tkhd", version ? 96 : 84, version),
				Box("mdia", Header("mdhd", version ? 36 : 24, version)))),
				Box("udta", Box("meta", Bytes{'o','l','d',' ','d','a','t','e'})))), extended),
		Box("mdat", Bytes{'m','e','d','i','a'})));
}

int main(int argc, char **argv) {
	using Ayu::SendTemplates::Mp4::RefreshMetadata;
	constexpr auto unixTime = uint64_t(1795017600);
	constexpr auto mp4Time = unixTime + 2082844800ULL;
	if (argc == 3) {
		std::ifstream input(argv[1], std::ios::binary);
		assert(input);
		auto data = Bytes(std::istreambuf_iterator<char>(input), {});
		assert(RefreshMetadata(data, unixTime));
		std::ofstream output(argv[2], std::ios::binary);
		output.write(reinterpret_cast<const char*>(data.data()), data.size());
		return output ? 0 : 1;
	}
	for (const auto version : {0, 1}) {
		auto data = Movie(version, true);
		const auto before = data;
		assert(RefreshMetadata(data, unixTime));
		assert(data.size() == before.size());
		const auto text = std::string(data.begin(), data.end());
		assert(text.find("old date") == std::string::npos);
		for (const auto type : {"mvhd", "tkhd", "mdhd"}) {
			const auto position = text.find(type) + 8;
			const auto width = version ? 8 : 4;
			for (auto field = 0; field != 2; ++field) {
				auto value = uint64_t(0);
				for (auto i = 0; i != width; ++i) value = (value << 8) | data[position + field * width + i];
				assert(value == mp4Time);
			}
			// The duration, dimensions, matrix, flags and codec data stay intact.
			const auto end = text.find(type) + (type == std::string("mvhd") ? (version ? 108 : 96) : type == std::string("tkhd") ? (version ? 92 : 80) : (version ? 32 : 20));
			for (auto i = position + 2 * width; i < end; ++i) assert(data[i] == before[i]);
		}
		assert(std::equal(data.end() - 5, data.end(), before.end() - 5));
		assert(RefreshMetadata(data, unixTime + 5));
	}
	for (auto data : {Bytes{}, Box("mdat", Bytes{1,2,3}), Movie(2), Movie(0)}) {
		if (data.size() > 100 && data == Movie(0)) data.pop_back();
		const auto before = data;
		assert(!RefreshMetadata(data, unixTime));
		assert(data == before); // Reject before making any partial changes.
	}
	auto future = Movie(1);
	assert(RefreshMetadata(future, 5000000000ULL));
	auto narrow = Movie(0);
	const auto narrowBefore = narrow;
	assert(!RefreshMetadata(narrow, 5000000000ULL));
	assert(narrow == narrowBefore);

	// Extended-size metadata and size-zero final atoms retain their headers.
	auto extra = Join(Movie(0), Box("uuid", Bytes(40, 0x47), true));
	assert(RefreshMetadata(extra, unixTime));
	assert(extra[extra.size() - 40 - 1] == 56);
	assert(std::all_of(extra.end() - 40, extra.end(), [](auto c) { return c == 0; }));
	auto zeroSize = Join(Movie(0), Bytes{0,0,0,0,'m','d','a','t',1,2,3});
	assert(RefreshMetadata(zeroSize, unixTime));
	assert(zeroSize.back() == 3);

	auto overflow = Movie(0);
	const auto before = overflow;
	assert(!RefreshMetadata(overflow, UINT64_MAX));
	assert(overflow == before);
	auto malformed = Join(Movie(0), Bytes{0,0,0,7,'b','a','d','!'});
	const auto malformedBefore = malformed;
	assert(!RefreshMetadata(malformed, unixTime));
	assert(malformed == malformedBefore);
}
