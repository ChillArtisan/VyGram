#include "ayu/security/secure_database.h"
#include "ayu/libs/sqlite/sqlite_orm.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

void Check(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

std::string Read(const std::filesystem::path &path) {
	auto file = std::ifstream(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(file), {} };
}

struct PrivateSetting {
	std::string name;
	std::vector<char> value;
};

struct Media {
	long long userId;
	long long mediaId;
	int mediaType;
	std::vector<char> bytes;
};

} // namespace

int main(int argc, char **argv) {
	try {
		Check(argc == 2, "Pass an isolated temporary test directory.");
		const auto directory = std::filesystem::path(argv[1]);
		Check(!std::filesystem::exists(directory), "Test directory must be new.");
		std::filesystem::create_directories(directory);
		auto key = std::array<unsigned char, 256>();
		for (auto i = 0; i != key.size(); ++i) {
			key[i] = static_cast<unsigned char>(i);
		}
		const auto source = (directory / "legacy.db").string();
		const auto output = (directory / "secure.db").string();
		const auto sentinel = std::string("PRIVATE_ARCHIVE_MESSAGE_123456789");
		{
			auto legacy = Ayu::Security::Open(source,
				SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
			Ayu::Security::Execute(legacy.get(),
				"CREATE TABLE messages(id INTEGER PRIMARY KEY, text TEXT, media BLOB);"
				"INSERT INTO messages VALUES(1,'PRIVATE_ARCHIVE_MESSAGE_123456789',"
				"x'000102ff00564944454f');");
		}
		const auto legacyBytes = Read(source);
		Ayu::Security::ExportLegacy(source, output, key.data(), key.size());
		Check(Read(source) == legacyBytes, "Migration changed source data.");
		Check(Read(output).find(sentinel) == std::string::npos,
			"Plaintext message found in encrypted output.");
		Check(Read(output).substr(0, 16) != std::string("SQLite format 3\0", 16),
			"Encrypted database has a plaintext SQLite header.");
		{
			auto db = Ayu::Security::Open(output, SQLITE_OPEN_READWRITE);
			Ayu::Security::Configure(db.get(), key.data(), key.size());
			Ayu::Security::Verify(db.get());
			auto query = Ayu::Security::Prepare(db.get(),
				"SELECT text, hex(media) FROM messages WHERE id = 1");
			Check(sqlite3_step(query.get()) == SQLITE_ROW, "Message missing.");
			Check(std::string(reinterpret_cast<const char*>(
				sqlite3_column_text(query.get(), 0))) == sentinel, "Message changed.");
			Check(std::string(reinterpret_cast<const char*>(
				sqlite3_column_text(query.get(), 1))) == "000102FF00564944454F",
				"Binary media changed during migration.");
		}
		{
			using namespace sqlite_orm;
			auto storage = make_storage(output,
				make_table<PrivateSetting>("PrivateSetting",
					make_column("name", &PrivateSetting::name, primary_key()),
					make_column("value", &PrivateSetting::value)),
				make_table<Media>("ArchivedMedia",
					make_column("userId", &Media::userId),
					make_column("mediaId", &Media::mediaId),
					make_column("mediaType", &Media::mediaType),
					make_column("bytes", &Media::bytes),
					primary_key(&Media::userId, &Media::mediaId, &Media::mediaType)));
			storage.on_open = [&](sqlite3 *db) {
				Ayu::Security::Configure(db, key.data(), key.size());
			};
			storage.open_forever();
			storage.sync_schema(true);
			const auto settings = std::string("PRIVATE_SETTINGS_SENTINEL_987654321");
			storage.replace(PrivateSetting{ "private-settings",
				std::vector<char>(settings.begin(), settings.end()) });
			Check(storage.get<PrivateSetting>("private-settings").value
				== std::vector<char>(settings.begin(), settings.end()),
				"Private setting roundtrip failed.");
			storage.replace(Media{ 1, 10, 1, { 'p', '\0', 'h' } });
			storage.replace(Media{ 2, 10, 1, { 'a', '\0', 'b' } });
			storage.replace(Media{ 1, 10, 2, { 'v', '\0', '\xff' } });
			Check(storage.get<Media>(1, 10, 1).bytes
				== std::vector<char>({ 'p', '\0', 'h' }), "Photo changed.");
			Check(storage.get<Media>(2, 10, 1).bytes
				== std::vector<char>({ 'a', '\0', 'b' }), "Account isolation failed.");
			Check(storage.get<Media>(1, 10, 2).bytes
				== std::vector<char>({ 'v', '\0', '\xff' }), "Type isolation failed.");
		}
		{
			auto db = Ayu::Security::Open(output, SQLITE_OPEN_READONLY);
			Ayu::Security::Configure(db.get(), key.data(), key.size());
			Ayu::Security::Verify(db.get());
			auto query = Ayu::Security::Prepare(db.get(),
				"SELECT COUNT(*) FROM ArchivedMedia");
			Check(sqlite3_step(query.get()) == SQLITE_ROW
				&& sqlite3_column_int(query.get(), 0) == 3, "BLOBs lost after reopen.");
			auto privateValue = Ayu::Security::Prepare(db.get(),
				"SELECT value FROM PrivateSetting WHERE name='private-settings'");
			Check(sqlite3_step(privateValue.get()) == SQLITE_ROW
				&& std::string(reinterpret_cast<const char*>(
					sqlite3_column_blob(privateValue.get(), 0)),
					sqlite3_column_bytes(privateValue.get(), 0))
					== "PRIVATE_SETTINGS_SENTINEL_987654321",
				"Private settings changed after reopen.");
			auto video = Ayu::Security::Prepare(db.get(),
				"SELECT hex(bytes) FROM ArchivedMedia "
				"WHERE userId=1 AND mediaId=10 AND mediaType=2");
			Check(sqlite3_step(video.get()) == SQLITE_ROW
				&& std::string(reinterpret_cast<const char*>(
					sqlite3_column_text(video.get(), 0))) == "7600FF",
				"Binary video bytes changed after reopen.");
		}
		Check(Read(output).find("PRIVATE_SETTINGS_SENTINEL_987654321")
			== std::string::npos, "Private settings leaked into plaintext.");
		const auto beforeWrongKey = Read(output);
		{
			auto rejected = false;
			try {
				auto wrong = key;
				wrong[0] ^= 1;
				auto db = Ayu::Security::Open(output, SQLITE_OPEN_READWRITE);
				Ayu::Security::Configure(db.get(), wrong.data(), wrong.size());
				Ayu::Security::Verify(db.get());
			} catch (...) {
				rejected = true;
			}
			Check(rejected, "Wrong key accepted.");
			Check(Read(output) == beforeWrongKey, "Wrong key modified archive.");
		}
		const auto tampered = (directory / "tampered.db").string();
		std::filesystem::copy_file(output, tampered);
		{
			auto file = std::fstream(tampered,
				std::ios::in | std::ios::out | std::ios::binary);
			file.seekp(100);
			file.put(static_cast<char>(beforeWrongKey[100] ^ 0x80));
		}
		{
			auto rejected = false;
			try {
				auto db = Ayu::Security::Open(tampered, SQLITE_OPEN_READONLY);
				Ayu::Security::Configure(db.get(), key.data(), key.size());
				Ayu::Security::Verify(db.get());
			} catch (...) {
				rejected = true;
			}
			Check(rejected, "Tampered archive accepted.");
		}
		{
			const auto broken = (directory / "broken-legacy.db").string();
			std::ofstream(broken, std::ios::binary) << "CORRUPT_LEGACY_PRESERVE_ME";
			const auto original = Read(broken);
			auto rejected = false;
			try {
				Ayu::Security::ExportLegacy(broken,
					(directory / "failed-export.db").string(), key.data(), key.size());
			} catch (...) {
				rejected = true;
			}
			Check(rejected, "Corrupted legacy archive accepted.");
			Check(Read(broken) == original, "Failed migration changed legacy archive.");
		}
		Check(std::filesystem::exists(source), "Legacy source was lost.");
		std::cout << "PASS: encrypted export, reopen, private settings, media BLOB isolation, wrong key, tamper\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
