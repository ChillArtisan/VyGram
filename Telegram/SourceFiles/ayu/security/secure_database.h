#pragma once

#include "ayu/libs/sqlite/sqlite3.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Ayu::Security {

inline constexpr auto kArchiveKeySize = 256;
inline constexpr auto kArchiveName = "ayudata.secure.db";
inline constexpr auto kLegacyArchiveName = "ayudata.db";
inline constexpr auto kMigrationSuffix = ".migrating";
inline constexpr auto kRekeySuffix = ".rekey";
inline constexpr const char *kSidecarSuffixes[] = { "-journal", "-wal", "-shm" };

inline void RequireSqlite(int result) {
	if (result != SQLITE_OK) {
		throw std::runtime_error("Secure archive operation failed.");
	}
}

inline void Execute(sqlite3 *db, const char *sql) {
	RequireSqlite(sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
using Connection = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;

inline Statement Prepare(sqlite3 *db, const char *sql) {
	auto statement = static_cast<sqlite3_stmt*>(nullptr);
	const auto result = sqlite3_prepare_v2(db, sql, -1, &statement, nullptr);
	auto guard = Statement(statement, sqlite3_finalize);
	RequireSqlite(result);
	return guard;
}

inline Connection Open(const std::string &path, int flags) {
	auto db = static_cast<sqlite3*>(nullptr);
	const auto result = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
	auto guard = Connection(db, sqlite3_close);
	RequireSqlite(result);
	return guard;
}

inline void Configure(sqlite3 *db, const void *key, int keySize) {
	if (!key || keySize != kArchiveKeySize) {
		throw std::runtime_error("Secure archive is locked.");
	}
	RequireSqlite(sqlite3_key(db, key, keySize));
	auto version = Prepare(db, "PRAGMA cipher_version");
	if (sqlite3_step(version.get()) != SQLITE_ROW
		|| sqlite3_column_bytes(version.get(), 0) == 0) {
		throw std::runtime_error("SQLCipher is required.");
	}
	Execute(db, "PRAGMA cipher_memory_security = ON");
	Execute(db, "PRAGMA temp_store = MEMORY");
	Execute(db, "PRAGMA secure_delete = ON");
}

inline void Verify(sqlite3 *db) {
	auto integrity = Prepare(db, "PRAGMA integrity_check");
	if (sqlite3_step(integrity.get()) != SQLITE_ROW
		|| std::string(reinterpret_cast<const char*>(
			sqlite3_column_text(integrity.get(), 0))) != "ok"
		|| sqlite3_step(integrity.get()) != SQLITE_DONE) {
		throw std::runtime_error("Secure archive integrity check failed.");
	}
	auto cipher = Prepare(db, "PRAGMA cipher_integrity_check");
	if (sqlite3_step(cipher.get()) != SQLITE_DONE) {
		throw std::runtime_error("Secure archive authentication failed.");
	}
}

// Paths are UTF-8, the same encoding sqlite3_open_v2 expects.
[[nodiscard]] inline std::filesystem::path FilePath(const std::string &path) {
	return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

// Throws instead of reporting "absent" when the file system cannot answer.
[[nodiscard]] inline bool FileExists(const std::string &path) {
	auto error = std::error_code();
	const auto result = std::filesystem::exists(FilePath(path), error);
	if (error) {
		throw std::runtime_error("Could not inspect an archive file.");
	}
	return result;
}

[[nodiscard]] inline bool HasSidecars(const std::string &path) {
	for (const auto suffix : kSidecarSuffixes) {
		if (FileExists(path + suffix)) {
			return true;
		}
	}
	return false;
}

inline void RemoveFile(const std::string &path) {
	auto error = std::error_code();
	std::filesystem::remove(FilePath(path), error);
	if (error || FileExists(path)) {
		throw std::runtime_error("Could not remove an archive file.");
	}
}

inline void RemoveWithSidecars(const std::string &path) {
	RemoveFile(path);
	for (const auto suffix : kSidecarSuffixes) {
		RemoveFile(path + suffix);
	}
}

// Overwrites the contents before unlinking, so a plaintext archive does not
// stay readable in free space after migration. This is best effort:
// copy-on-write file systems, SSD wear levelling, snapshots and backups can
// still hold older copies of the blocks.
inline void WipeFile(const std::string &path) {
	if (!FileExists(path)) {
		return;
	}
	const auto native = FilePath(path);
	auto error = std::error_code();
	const auto size = std::filesystem::file_size(native, error);
	if (error) {
		throw std::runtime_error("Could not inspect a plaintext archive file.");
	}
	{
		auto raw = static_cast<FILE*>(nullptr);
#ifdef _WIN32
		if (_wfopen_s(&raw, native.c_str(), L"r+b") != 0) {
			raw = nullptr;
		}
#else
		raw = std::fopen(native.c_str(), "r+b");
#endif
		if (!raw) {
			throw std::runtime_error("Could not open a plaintext archive file.");
		}
		const auto file = std::unique_ptr<FILE, decltype(&std::fclose)>(
			raw,
			std::fclose);
		const auto zeros = std::vector<char>(64 * 1024, 0);
		for (auto left = std::uintmax_t(size); left != 0;) {
			const auto chunk = std::size_t(
				std::min<std::uintmax_t>(left, zeros.size()));
			if (std::fwrite(zeros.data(), 1, chunk, raw) != chunk) {
				throw std::runtime_error("Could not wipe a plaintext archive file.");
			}
			left -= chunk;
		}
#ifdef _WIN32
		const auto synced = (std::fflush(raw) == 0) && (_commit(_fileno(raw)) == 0);
#else
		const auto synced = (std::fflush(raw) == 0) && (fsync(fileno(raw)) == 0);
#endif
		if (!synced) {
			throw std::runtime_error("Could not wipe a plaintext archive file.");
		}
	}
	RemoveFile(path);
}

inline void WipeWithSidecars(const std::string &path) {
	WipeFile(path);
	for (const auto suffix : kSidecarSuffixes) {
		WipeFile(path + suffix);
	}
}

// Returns true when `path` is an intact archive encrypted with `key`.
[[nodiscard]] inline bool OpensWith(const std::string &path, const void *key) {
	if (!FileExists(path)) {
		return false;
	}
	try {
		// Read-write, so a hot journal left by a crash can be rolled back.
		auto db = Open(path, SQLITE_OPEN_READWRITE);
		Configure(db.get(), key, kArchiveKeySize);
		Verify(db.get());
		return true;
	} catch (const std::exception &) {
		return false;
	}
}

// Copies `source` (plaintext when `sourceKey` is null) into a new database
// at `destination` encrypted with `key`, then verifies the copy.
inline void Export(
		const std::string &source,
		const void *sourceKey,
		const std::string &destination,
		const void *key) {
	if (!key) {
		throw std::runtime_error("Secure archive is locked.");
	}
	{
		auto destinationFile = Open(destination,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
		Configure(destinationFile.get(), key, kArchiveKeySize);
	}
	auto db = Open(source, SQLITE_OPEN_READWRITE);
	if (sourceKey) {
		Configure(db.get(), sourceKey, kArchiveKeySize);
		Verify(db.get());
	} else {
		Execute(db.get(), "PRAGMA temp_store = MEMORY");
	}
	{
		auto attach = Prepare(db.get(),
			"ATTACH DATABASE ? AS encrypted KEY ?");
		RequireSqlite(sqlite3_bind_text(attach.get(), 1,
			destination.c_str(), -1, SQLITE_TRANSIENT));
		RequireSqlite(sqlite3_bind_blob(attach.get(), 2,
			key, kArchiveKeySize, SQLITE_TRANSIENT));
		if (sqlite3_step(attach.get()) != SQLITE_DONE) {
			throw std::runtime_error("Could not create encrypted archive.");
		}
	}
	Execute(db.get(), "PRAGMA encrypted.cipher_memory_security = ON");
	Execute(db.get(), "SELECT sqlcipher_export('encrypted')");
	Execute(db.get(), "DETACH DATABASE encrypted");
	db.reset();
	auto encrypted = Open(destination, SQLITE_OPEN_READONLY);
	Configure(encrypted.get(), key, kArchiveKeySize);
	Verify(encrypted.get());
}

// Atomically replaces `path` with the completed copy at `staging`.
inline void Promote(const std::string &staging, const std::string &path) {
	// A rollback journal of the database being replaced must never be
	// applied to its replacement.
	for (const auto suffix : kSidecarSuffixes) {
		RemoveFile(path + suffix);
	}
	auto error = std::error_code();
	std::filesystem::rename(FilePath(staging), FilePath(path), error);
	if (error) {
		throw std::runtime_error("Could not replace the archive.");
	}
}

// Key rotation, step 1: write a verified copy of the archive under `newKey`
// next to it. The caller then persists `newKey` and calls PromoteRekey().
// A crash at any point is resolved by RecoverRekey() with whichever key
// ended up persisted.
inline void StageRekey(
		const std::string &path,
		const void *oldKey,
		const void *newKey) {
	const auto staging = path + kRekeySuffix;
	if (!oldKey || !newKey) {
		throw std::runtime_error("Secure archive is locked.");
	} else if (FileExists(staging) || HasSidecars(staging)) {
		throw std::runtime_error("Archive key rotation is already pending.");
	}
	try {
		Export(path, oldKey, staging, newKey);
	} catch (...) {
		try {
			RemoveWithSidecars(staging);
		} catch (...) {
		}
		throw;
	}
}

inline void PromoteRekey(const std::string &path) {
	Promote(path + kRekeySuffix, path);
}

inline void DiscardRekey(const std::string &path) {
	RemoveWithSidecars(path + kRekeySuffix);
}

inline void RecoverRekey(const std::string &path, const void *key) {
	const auto staging = path + kRekeySuffix;
	if (!FileExists(staging) && !HasSidecars(staging)) {
		return;
	}
	if (!HasSidecars(staging) && OpensWith(staging, key)) {
		// The new key was persisted, the swap did not happen.
		Promote(staging, path);
		return;
	}
	// The copy is incomplete or under a key that was never persisted.
	if (FileExists(path) && !OpensWith(path, key)) {
		throw std::runtime_error("Archive key rotation requires recovery.");
	}
	RemoveWithSidecars(staging);
}

// Brings the archive files in `directory` into a consistent state for `key`:
// finishes or discards an interrupted key rotation, migrates (or finishes
// migrating) the plaintext legacy archive and wipes it.
//
// Returns false, touching nothing beyond rotation recovery, when there is no
// encrypted archive yet and `allowCreate` is false. Callers pass
// `allowCreate` only once a strong passcode protects `key` on disk, so an
// archive never exists under a key that is not passcode-protected.
[[nodiscard]] inline bool PrepareArchive(
		const std::string &directory,
		const void *key,
		bool allowCreate) {
	if (!key) {
		throw std::runtime_error("Secure archive is locked.");
	}
	const auto path = directory + kArchiveName;
	const auto legacy = directory + kLegacyArchiveName;
	const auto staging = path + kMigrationSuffix;
	RecoverRekey(path, key);
	if (FileExists(legacy)) {
		if (FileExists(path)) {
			// A previous run promoted the encrypted copy and stopped before
			// the plaintext was removed.
			if (!OpensWith(path, key)) {
				throw std::runtime_error("Legacy archive requires recovery.");
			}
		} else if (!allowCreate) {
			return false;
		} else {
			// An interrupted export leaves the plaintext as the only
			// source of truth: discard the partial copy and start over.
			RemoveWithSidecars(staging);
			Export(legacy, nullptr, staging, key);
			Promote(staging, path);
		}
		RemoveWithSidecars(staging);
		WipeWithSidecars(legacy);
	} else if (HasSidecars(legacy)) {
		throw std::runtime_error("Legacy archive sidecar requires recovery.");
	}
	return allowCreate || FileExists(path);
}

} // namespace Ayu::Security
