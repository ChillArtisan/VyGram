#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace Ayu::SendTemplates::Mp4 {
namespace details {

struct Edit {
	enum class Kind { Time32, Time64, FreeType, Erase };
	size_t offset = 0;
	size_t size = 0;
	Kind kind = Kind::Erase;
};

inline uint64_t Read(std::span<const unsigned char> bytes) {
	auto result = uint64_t(0);
	for (const auto byte : bytes) {
		result = (result << 8) | byte;
	}
	return result;
}

class Inspector {
public:
	explicit Inspector(std::span<const unsigned char> bytes) : _bytes(bytes) {
	}

	std::optional<std::vector<Edit>> inspect() {
		if (!scan(0, _bytes.size(), {}, 0)
			|| !_movies || !_tracks || !_media) {
			return std::nullopt;
		}
		return std::move(_edits);
	}

private:
	bool scan(size_t start, size_t end, std::string_view parent, int depth) {
		if (depth > 12) {
			return false;
		}
		for (auto pos = start; pos < end;) {
			if (++_boxes > 10000 || end - pos < 8) {
				return false;
			}
			auto size = Read(_bytes.subspan(pos, 4));
			auto header = size_t(8);
			if (size == 1) {
				if (end - pos < 16) {
					return false;
				}
				size = Read(_bytes.subspan(pos + 8, 8));
				header = 16;
			} else if (!size) {
				size = end - pos;
			}
			if (size < header || size > end - pos) {
				return false;
			}
			const auto type = std::string_view(
				reinterpret_cast<const char*>(_bytes.data() + pos + 4), 4);
			const auto payload = pos + header;
			const auto payloadSize = size_t(size) - header;
			const auto movie = (type == "mvhd" && parent == "moov");
			const auto track = (type == "tkhd" && parent == "trak");
			const auto media = (type == "mdhd" && parent == "mdia");
			if (movie || track || media) {
				if (payloadSize < 4 || _bytes[payload] > 1) {
					return false;
				}
				const auto wide = (_bytes[payload] == 1);
				const auto minimum = movie ? 100 : track ? 84 : 24;
				if (payloadSize < minimum + (wide ? 12 : 0)) {
					return false;
				}
				_edits.push_back({ payload + 4, wide ? size_t(16) : size_t(8),
					wide ? Edit::Kind::Time64 : Edit::Kind::Time32 });
				_movies += movie;
				_tracks += track;
				_media += media;
			} else if (type == "udta" || type == "meta" || type == "uuid"
				|| type == "free" || type == "skip") {
				// Equal-size empty atoms remove capture tags without shifting media.
				_edits.push_back({ pos + 4, 4, Edit::Kind::FreeType });
				_edits.push_back({ payload, payloadSize, Edit::Kind::Erase });
			} else if (type == "moov" || type == "trak" || type == "mdia"
				|| type == "minf" || type == "stbl" || type == "edts"
				|| type == "dinf" || type == "mvex" || type == "moof"
				|| type == "traf" || type == "mfra") {
				if (!scan(payload, pos + size_t(size), type, depth + 1)) {
					return false;
				}
			}
			pos += size_t(size);
		}
		return true;
	}

	std::span<const unsigned char> _bytes;
	std::vector<Edit> _edits;
	int _boxes = 0;
	int _movies = 0;
	int _tracks = 0;
	int _media = 0;
};

} // namespace details

inline bool CanRefreshMetadata(std::span<const unsigned char> bytes) {
	return details::Inspector(bytes).inspect().has_value();
}

// Refresh UTC dates in the outgoing snapshot only; validate before any writes.
inline bool RefreshMetadata(std::span<unsigned char> bytes, uint64_t unixTime) {
	constexpr auto epochOffset = uint64_t(2082844800);
	if (unixTime > std::numeric_limits<uint64_t>::max() - epochOffset) {
		return false;
	}
	const auto time = unixTime + epochOffset;
	const auto edits = details::Inspector(bytes).inspect();
	if (!edits) {
		return false;
	}
	using Kind = details::Edit::Kind;
	for (const auto &edit : *edits) {
		if (edit.kind == Kind::Time32
			&& time > std::numeric_limits<uint32_t>::max()) {
			return false;
		}
	}
	for (const auto &edit : *edits) {
		auto target = bytes.subspan(edit.offset, edit.size);
		if (edit.kind == Kind::FreeType) {
			std::copy_n("free", 4, target.begin());
		} else if (edit.kind == Kind::Erase) {
			std::fill(target.begin(), target.end(), static_cast<unsigned char>(0));
		} else {
			const auto width = edit.size / 2;
			for (auto field = size_t(0); field != 2; ++field) {
				for (auto i = size_t(0); i != width; ++i) {
					target[field * width + i] = static_cast<unsigned char>(
						time >> ((width - i - 1) * 8));
				}
			}
		}
	}
	return true;
}

} // namespace Ayu::SendTemplates::Mp4
