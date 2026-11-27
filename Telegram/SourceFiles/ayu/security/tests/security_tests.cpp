// Standalone tests for the local archive protection: password policy and
// the SQLCipher file layer (creation invariant, key rotation and its crash
// recovery, plaintext migration and wiping). No Qt, no app build needed.
//
// Windows, from an "x64 Native Tools Command Prompt", in Telegram/SourceFiles:
//   cl /nologo /std:c++20 /EHsc /MT /O1 /utf-8 /I. /I<openssl>/include ^
//     /DSQLITE_HAS_CODEC /DSQLITE_TEMP_STORE=3 /DSQLITE_THREADSAFE=1 ^
//     /DSQLCIPHER_CRYPTO_OPENSSL /DSQLITE_EXTRA_INIT=sqlcipher_extra_init ^
//     /DSQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown ^
//     ayu/security/tests/security_tests.cpp ayu/libs/sqlite/sqlite3.c ^
//     <openssl>/out/libcrypto.lib ws2_32.lib crypt32.lib advapi32.lib user32.lib
#include "ayu/security/password_policy.h"
#include "ayu/security/secure_database.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

namespace {

using namespace Ayu::Security;
namespace fs = std::filesystem;

int failures = 0;

void Check(bool condition, const char *what) {
	std::printf("%s %s\n", condition ? "  ok  " : "FAILED", what);
	if (!condition) {
		++failures;
	}
}

std::u32string U32(const char *utf8) {
	auto result = std::u32string();
	const auto bytes = reinterpret_cast<const unsigned char*>(utf8);
	for (auto i = 0; bytes[i];) {
		auto ch = char32_t(bytes[i]);
		auto extra = 0;
		if (ch >= 0xF0) {
			ch &= 0x07; extra = 3;
		} else if (ch >= 0xE0) {
			ch &= 0x0F; extra = 2;
		} else if (ch >= 0xC0) {
			ch &= 0x1F; extra = 1;
		}
		++i;
		while (extra--) {
			ch = (ch << 6) | (bytes[i++] & 0x3F);
		}
		result.push_back(ch);
	}
	return result;
}

void TestPasswordPolicy() {
	std::printf("password policy\n");
	for (const auto weak : {
		"Summerautumn1!", "password1234", "Password123!", "P@ssw0rd2024!",
		"qwerty123456", "Qwertyuiop123", "1q2w3e4r5t6y", "Moscow2024!!",
		"aaaaaaaaaaaa", "abcabcabcabc", "123456789012", "iloveyouiloveyou",
		"gfhjkmgfhjkm", "johnsmith1985", "01011990Masha", "Пароль123456",
		"ЛюблюТебя2024", "zxcvbnmasdfgh", "Spartak1922!!", "drowssap1234",
		"qwertyuiopasdfghjklzxcvbnm", "MonkeyDragon99",
	}) {
		const auto text = U32(weak);
		std::printf("        %5.1f bits  %s\n", EstimateLocalPasswordBits(text), weak);
		Check(!IsStrongLocalPassword(text), weak);
	}
	for (const auto strong : {
		"Xk9mPq2vLr7w", "kxqvmtrpzbwl", "tR7#pL9!qZ2$", "violet anchor mango river",
		"кот любит тёплое молоко зимой", "hjX8-vq2N-0pzT", "ведро-кактус-фонарь-шарф",
	}) {
		const auto text = U32(strong);
		std::printf("        %5.1f bits  %s\n", EstimateLocalPasswordBits(text), strong);
		Check(IsStrongLocalPassword(text), strong);
	}
	Check(!IsStrongLocalPassword(U"short1!Xq"), "length below 12 is rejected");
	auto lone = std::u32string(U"Xk9mPq2vLr7w");
	lone.push_back(char32_t(0xD800));
	Check(!IsStrongLocalPassword(lone), "lone surrogate is rejected");
}

struct Key {
	unsigned char bytes[kArchiveKeySize];
};

Key RandomKey() {
	static auto engine = std::mt19937_64(std::random_device()());
	auto key = Key();
	for (auto &byte : key.bytes) {
		byte = static_cast<unsigned char>(engine());
	}
	return key;
}

std::string Utf8(const fs::path &path) {
	const auto value = path.u8string();
	return std::string(value.begin(), value.end());
}

void WriteRows(sqlite3 *db, int from, int count) {
	Execute(db, "CREATE TABLE IF NOT EXISTS Message (id INTEGER, text TEXT)");
	for (auto i = from; i != from + count; ++i) {
		const auto sql = "INSERT INTO Message VALUES (" + std::to_string(i)
			+ ", 'secret message number " + std::to_string(i) + "')";
		Execute(db, sql.c_str());
	}
}

int CountRows(const std::string &path, const Key &key) {
	auto db = Open(path, SQLITE_OPEN_READONLY);
	Configure(db.get(), key.bytes, kArchiveKeySize);
	auto statement = Prepare(db.get(), "SELECT COUNT(*) FROM Message");
	return (sqlite3_step(statement.get()) == SQLITE_ROW)
		? sqlite3_column_int(statement.get(), 0)
		: -1;
}

void CreatePlaintext(const std::string &path, int rows) {
	auto db = Open(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
	WriteRows(db.get(), 0, rows);
}

bool AnyFile(const fs::path &directory) {
	return fs::directory_iterator(directory) != fs::directory_iterator();
}

std::string ReadAll(const fs::path &path) {
	auto file = std::ifstream(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(file), {});
}

void TestArchive(const fs::path &root) {
	std::printf("archive file layer\n");
	const auto directory = root / "tdata";
	fs::create_directories(directory);
	const auto dir = Utf8(directory) + "/";
	const auto path = dir + kArchiveName;
	const auto legacy = dir + kLegacyArchiveName;
	const auto k1 = RandomKey();
	const auto k2 = RandomKey();
	const auto k3 = RandomKey();
	const auto k4 = RandomKey();

	// #2: without a persisted strong passcode nothing is created.
	Check(!PrepareArchive(dir, k1.bytes, false), "no archive: refuses without allowCreate");
	Check(!AnyFile(directory), "no archive: no file created");
	CreatePlaintext(legacy, 5);
	Check(!PrepareArchive(dir, k1.bytes, false), "plaintext only: refuses without allowCreate");
	Check(FileExists(legacy) && !FileExists(path), "plaintext only: not migrated without allowCreate");

	// Migration, including an interrupted earlier export.
	{
		auto junk = std::ofstream(path + kMigrationSuffix, std::ios::binary);
		junk << "partial export";
	}
	Check(PrepareArchive(dir, k1.bytes, true), "migration succeeds over a partial export");
	Check(!FileExists(legacy) && !FileExists(path + kMigrationSuffix), "migration removes plaintext and staging");
	Check(CountRows(path, k1) == 5, "migrated archive keeps the rows");
	Check(!OpensWith(path, k2.bytes), "migrated archive does not open with another key");
	Check(PrepareArchive(dir, k1.bytes, false), "existing archive opens without allowCreate");

	// #4: plaintext left over after the encrypted copy was promoted.
	CreatePlaintext(legacy, 3);
	const auto link = directory / "plaintext-link";
	fs::create_hard_link(FilePath(legacy), link);
	Check(ReadAll(link).find("secret message") != std::string::npos, "leftover plaintext is readable before");
	Check(PrepareArchive(dir, k1.bytes, false), "leftover plaintext with a valid archive is resolved");
	Check(!FileExists(legacy), "leftover plaintext is removed");
	const auto wiped = ReadAll(link);
	Check(!wiped.empty() && wiped.find_first_not_of('\0') == std::string::npos, "leftover plaintext blocks are zeroed");
	fs::remove(link);
	Check(CountRows(path, k1) == 5, "archive untouched by the wipe");

	// #1: rotation.
	StageRekey(path, k1.bytes, k2.bytes);
	auto refused = false;
	try {
		StageRekey(path, k1.bytes, k3.bytes);
	} catch (const std::exception &) {
		refused = true;
	}
	Check(refused, "second rotation refuses while one is pending");
	{
		auto journal = std::ofstream(path + "-journal", std::ios::binary);
		journal << "stale journal of the replaced file";
	}
	PromoteRekey(path);
	Check(!FileExists(path + "-journal"), "promotion drops the replaced file's journal");
	Check(OpensWith(path, k2.bytes) && !OpensWith(path, k1.bytes), "rotated archive opens only with the new key");
	Check(CountRows(path, k2) == 5, "rotation keeps the rows");

	// Crash after the new key was persisted, before the swap.
	StageRekey(path, k2.bytes, k3.bytes);
	Check(PrepareArchive(dir, k3.bytes, false), "recovery with the persisted new key");
	Check(OpensWith(path, k3.bytes) && !FileExists(path + kRekeySuffix), "recovery promotes the staged copy");

	// Crash before the new key was persisted.
	StageRekey(path, k3.bytes, k4.bytes);
	Check(PrepareArchive(dir, k3.bytes, false), "recovery with the old key");
	Check(OpensWith(path, k3.bytes) && !FileExists(path + kRekeySuffix), "recovery discards the unpersisted copy");

	// Crash in the middle of staging.
	{
		auto partial = std::ofstream(path + kRekeySuffix, std::ios::binary);
		partial << "partial copy";
	}
	Check(PrepareArchive(dir, k3.bytes, false), "recovery after a partial staged copy");
	Check(!FileExists(path + kRekeySuffix) && CountRows(path, k3) == 5, "partial staged copy is discarded");

	// A wrong key never destroys data.
	StageRekey(path, k3.bytes, k4.bytes);
	auto wrongKeyFailed = false;
	try {
		(void)PrepareArchive(dir, k1.bytes, true);
	} catch (const std::exception &) {
		wrongKeyFailed = true;
	}
	Check(wrongKeyFailed, "an unrelated key fails recovery");
	Check(FileExists(path + kRekeySuffix) && OpensWith(path, k3.bytes), "an unrelated key leaves both files intact");
	DiscardRekey(path);

	// Leftover plaintext next to an archive the key does not open.
	CreatePlaintext(legacy, 1);
	auto ambiguous = false;
	try {
		(void)PrepareArchive(dir, k1.bytes, true);
	} catch (const std::exception &) {
		ambiguous = true;
	}
	Check(ambiguous && FileExists(legacy), "plaintext is kept when the archive cannot be verified");
}

} // namespace

int main() {
	TestPasswordPolicy();
	const auto root = fs::temp_directory_path()
		/ ("ayu-security-tests-" + std::to_string(std::random_device()()));
	try {
		TestArchive(root);
	} catch (const std::exception &e) {
		std::printf("FAILED exception: %s\n", e.what());
		++failures;
	}
	auto error = std::error_code();
	fs::remove_all(root, error);
	std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
