#pragma once

#include "ayu/libs/sqlite/sqlite3.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace Ayu::Security {

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
	if (!key || keySize != 256) {
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

inline void ExportLegacy(
		const std::string &source,
		const std::string &destination,
		const void *key,
		int keySize) {
	if (!key || keySize != 256) {
		throw std::runtime_error("Secure archive is locked.");
	}
	{
		auto destinationFile = Open(destination,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
		Configure(destinationFile.get(), key, keySize);
	}
	auto db = Open(source, SQLITE_OPEN_READWRITE);
	Execute(db.get(), "PRAGMA temp_store = MEMORY");
	{
		auto attach = Prepare(db.get(),
			"ATTACH DATABASE ? AS encrypted KEY ?");
		RequireSqlite(sqlite3_bind_text(attach.get(), 1,
			destination.c_str(), -1, SQLITE_TRANSIENT));
		RequireSqlite(sqlite3_bind_blob(attach.get(), 2,
			key, keySize, SQLITE_TRANSIENT));
		if (sqlite3_step(attach.get()) != SQLITE_DONE) {
			throw std::runtime_error("Could not create encrypted archive.");
		}
	}
	Execute(db.get(), "PRAGMA encrypted.cipher_memory_security = ON");
	Execute(db.get(), "SELECT sqlcipher_export('encrypted')");
	Execute(db.get(), "DETACH DATABASE encrypted");
	auto encrypted = Open(destination, SQLITE_OPEN_READONLY);
	Configure(encrypted.get(), key, keySize);
	Verify(encrypted.get());
}

} // namespace Ayu::Security
