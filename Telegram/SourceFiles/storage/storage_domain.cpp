/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_domain.h"

#include "core/version.h"
#include "core/application.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "storage/localstorage.h"
#include "mtproto/mtproto_config.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "base/random.h"

// AyuGram includes
#include "ayu/data/ayu_database.h"
#include "ayu/ayu_settings.h"
#include "ayu/security/password_policy.h"
#include "ayu/security/profile_password.h"
#include <openssl/crypto.h>
#include <stdexcept>
#include <cstring>
#include <optional>
#include <QFileInfo>

namespace Storage {
namespace {

using namespace details;

[[nodiscard]] MTP::AuthKeyPtr ProfilePasscodeKey(
		const QByteArray &passcode, const QByteArray &salt) {
	using namespace Ayu::Security;
	const auto data = reinterpret_cast<const unsigned char*>(salt.constData());
	if (!IsScryptProfileSalt(data, salt.size())) {
		return CreateLocalKey(passcode, salt);
	}
	auto key = MTP::AuthKey::Data{};
	if (!DeriveProfilePasswordKey(passcode.constData(), passcode.size(),
		data, salt.size(), reinterpret_cast<unsigned char*>(key.data()), key.size())) {
		throw std::runtime_error("Could not derive the profile password key.");
	}
	const auto wipe = gsl::finally([&] {
		OPENSSL_cleanse(key.data(), key.size());
	});
	return std::make_shared<MTP::AuthKey>(key);
}

[[nodiscard]] MTP::AuthKeyPtr GenerateArchiveKey() {
	auto data = MTP::AuthKey::Data{};
	const auto wipe = gsl::finally([&] {
		OPENSSL_cleanse(data.data(), data.size());
	});
	base::RandomFill(data.data(), data.size());
	return std::make_shared<MTP::AuthKey>(data);
}

[[nodiscard]] QByteArray KeyBytes(const MTP::AuthKeyPtr &key) {
	const auto data = key->data();
	return QByteArray(reinterpret_cast<const char*>(data.data()), data.size());
}

[[nodiscard]] bool UnlockArchive(const MTP::AuthKeyPtr &key, bool allowCreate) {
	auto copy = KeyBytes(key);
	const auto wipe = gsl::finally([&] {
		OPENSSL_cleanse(copy.data(), copy.size());
	});
	try {
		if (!AyuDatabase::unlock(copy, allowCreate)) {
			return false;
		}
		if (!Local::loadProtectedSettings() || !AyuSettings::loadProtected()) {
			AyuDatabase::shutdown();
			return false;
		}
		Core::App().refreshGlobalProxy();
		return true;
	} catch (...) {
		AyuDatabase::shutdown();
		return false;
	}
}

[[nodiscard]] QString BaseGlobalPath() {
	return cWorkingDir() + u"tdata/"_q;
}

[[nodiscard]] QString ComputeKeyName(const QString &dataName) {
	// We dropped old test authorizations when migrated to multi auth.
	//return "key_" + dataName + (cTestMode() ? "[test]" : "");
	return "key_" + dataName;
}

[[nodiscard]] bool WriteProfileWrapper(
		const QString &name,
		const QByteArray &salt,
		const QByteArray &encryptedKey,
		const QByteArray &encryptedInfo,
		bool verify) {
	{
		FileWriteDescriptor output(name, BaseGlobalPath(), verify);
		output.writeData(salt);
		output.writeData(encryptedKey);
		output.writeData(encryptedInfo);
	}
	if (!verify) {
		return true;
	}
	if (!QFileInfo::exists(BaseGlobalPath() + name + u"s"_q)) {
		return false;
	}
	FileReadDescriptor input;
	if (!ReadFile(input, name, BaseGlobalPath())) {
		return false;
	}
	QByteArray readSalt, readKey, readInfo;
	input.stream >> readSalt >> readKey >> readInfo;
	if (!CheckStreamStatus(input.stream) || !input.stream.atEnd()
		|| readSalt != salt || readKey != encryptedKey || readInfo != encryptedInfo) {
		return false;
	}
	for (const auto &suffix : { u"0"_q, u"1"_q }) {
		const auto obsolete = BaseGlobalPath() + name + suffix;
		if (QFileInfo::exists(obsolete)
			&& (!QFile::remove(obsolete) || QFileInfo::exists(obsolete))) {
			return false;
		}
	}
	return true;
}

} // namespace

Domain::Domain(not_null<Main::Domain*> owner, const QString &dataName)
: _owner(owner)
, _dataName(dataName) {
}

Domain::~Domain() = default;

StartResult Domain::start(const QByteArray &passcode) {
	try {
		if (_needsPasscodeSetup) {
			return StartResult::NeedsPasscodeSetup;
		}
		const auto modern = startModern(passcode);
		if (modern == StartModernResult::Success) {
			if (_oldVersion < AppVersion) {
				writeAccounts();
			}
			return StartResult::Success;
		} else if (modern == StartModernResult::NeedsPasscodeSetup) {
			return StartResult::NeedsPasscodeSetup;
		} else if (modern == StartModernResult::IncorrectPasscode) {
			return StartResult::IncorrectPasscode;
		} else if (modern == StartModernResult::Failed
			|| modern == StartModernResult::StorageError) {
			return StartResult::StorageError;
		}
		auto legacy = std::make_unique<Main::Account>(_owner, _dataName, 0);
		const auto result = legacy->legacyStart(passcode);
		if (result == StartResult::Success) {
			_oldVersion = legacy->local().oldMapVersion();
			startWithSingleAccount(passcode, std::move(legacy));
			if (_needsPasscodeSetup) {
				return StartResult::NeedsPasscodeSetup;
			}
		}
		return result;
	} catch (...) {
		_owner->finish();
		AyuDatabase::shutdown();
		_localKey = nullptr;
		_archiveKey = nullptr;
		_passcodeKey = nullptr;
		_passcodeKeySalt.clear();
		_passcodeKeyEncrypted.clear();
		_hasLocalPasscode = false;
		_requiresPasscodeUpgrade = false;
		_needsPasscodeSetup = false;
		_pendingAccountInfo.clear();
		LOG(("Profile storage could not be opened safely."));
		return StartResult::StorageError;
	}
}

void Domain::startAdded(
		not_null<Main::Account*> account,
		std::unique_ptr<MTP::Config> config) {
	Expects(_localKey != nullptr);

	account->prepareToStartAdded(_localKey);
	account->start(std::move(config));
}

void Domain::startWithSingleAccount(
		const QByteArray &passcode,
		std::unique_ptr<Main::Account> account) {
	Expects(account != nullptr);

	const auto legacyKey = account->local().peekLegacyLocalKey();
	if (legacyKey) {
		_localKey = legacyKey;
		applyWrapper(wrapLocalKey(passcode, nullptr));
	} else {
		generateLocalKey();
	}
	EncryptedDescriptor info(sizeof(qint32) * 3);
	info.stream << qint32(1) << qint32(0) << qint32(0);
	_pendingAccountInfo = PrepareEncrypted(info, _localKey);
	if (!_hasLocalPasscode || _requiresPasscodeUpgrade) {
		if (legacyKey && !WriteProfileWrapper(ComputeKeyName(_dataName),
				_passcodeKeySalt, _passcodeKeyEncrypted, _pendingAccountInfo, true)) {
			throw std::runtime_error("Could not preserve the legacy profile key.");
		}
		_needsPasscodeSetup = true;
		_requiresPasscodeUpgrade = true;
		return;
	}
	if (protectWithPasscode(passcode, _pendingAccountInfo)
			!= ProtectResult::Done) {
		throw std::runtime_error("Could not protect the local archive.");
	}
	if (legacyKey) {
		account->start(nullptr);
	} else {
		account->start(account->prepareToStart(_localKey));
	}
	_owner->accountAddedInStorage(Main::Domain::AccountWithIndex{
		.account = std::move(account)
	});
	_pendingAccountInfo.clear();
	_needsPasscodeSetup = false;
	writeAccounts();
}

void Domain::generateLocalKey() {
	Expects(_localKey == nullptr);
	Expects(_passcodeKeySalt.isEmpty());
	Expects(_passcodeKeyEncrypted.isEmpty());

	auto pass = QByteArray(MTP::AuthKey::kSize, Qt::Uninitialized);
	auto salt = QByteArray(LocalEncryptSaltSize, Qt::Uninitialized);
	base::RandomFill(pass.data(), pass.size());
	base::RandomFill(salt.data(), salt.size());
	const auto wipe = gsl::finally([&] {
		OPENSSL_cleanse(pass.data(), pass.size());
	});
	_localKey = CreateLocalKey(pass, salt);

	applyWrapper(wrapLocalKey(QByteArray(), nullptr));
}

Domain::KeyWrapper Domain::wrapLocalKey(
		const QByteArray &passcode,
		MTP::AuthKeyPtr archiveKey) const {
	using namespace Ayu::Security;
	Expects(_localKey != nullptr);
	// Without a passcode the wrapper key is public: never put the archive
	// key there.
	Expects(!archiveKey || !passcode.isEmpty());

	auto salt = QByteArray(passcode.isEmpty()
		? LocalEncryptSaltSize : kProfileSaltSize, Qt::Uninitialized);
	base::RandomFill(salt.data(), salt.size());
	if (!passcode.isEmpty()) {
		std::memcpy(salt.data(), kProfileSaltMarker, kProfileSaltMarkerSize);
	}
	auto key = ProfilePasscodeKey(passcode, salt);
	EncryptedDescriptor passKeyData(
		MTP::AuthKey::kSize * (archiveKey ? 2 : 1));
	const auto wipe = gsl::finally([&] {
		OPENSSL_cleanse(passKeyData.data.data(), passKeyData.data.size());
	});
	_localKey->write(passKeyData.stream);
	if (archiveKey) {
		archiveKey->write(passKeyData.stream);
	}
	auto encrypted = PrepareEncrypted(passKeyData, key);
	const auto hasPasscode = !passcode.isEmpty();
	return KeyWrapper{
		.salt = std::move(salt),
		.encrypted = std::move(encrypted),
		.passcodeKey = std::move(key),
		.archiveKey = std::move(archiveKey),
		.hasPasscode = hasPasscode,
		.requiresUpgrade = hasPasscode && !IsStrongLocalPassword(
			QString::fromUtf8(passcode).toStdU32String()),
	};
}

Domain::KeyWrapper Domain::currentWrapper() const {
	return KeyWrapper{
		.salt = _passcodeKeySalt,
		.encrypted = _passcodeKeyEncrypted,
		.passcodeKey = _passcodeKey,
		.archiveKey = _archiveKey,
		.hasPasscode = _hasLocalPasscode,
		.requiresUpgrade = _requiresPasscodeUpgrade,
	};
}

void Domain::applyWrapper(KeyWrapper &&wrapper) {
	_passcodeKeySalt = std::move(wrapper.salt);
	_passcodeKeyEncrypted = std::move(wrapper.encrypted);
	_passcodeKey = std::move(wrapper.passcodeKey);
	_archiveKey = std::move(wrapper.archiveKey);
	_hasLocalPasscode = wrapper.hasPasscode;
	_requiresPasscodeUpgrade = wrapper.requiresUpgrade;
}

bool Domain::unlockArchive(bool allowCreate) {
	// Invariant: an archive is only ever created under a fresh archive key
	// that a strong passcode protects on disk.
	if (allowCreate
		&& (!_hasLocalPasscode || _requiresPasscodeUpgrade || !_archiveKey)) {
		return false;
	}
	return UnlockArchive(_archiveKey ? _archiveKey : _localKey, allowCreate);
}

// Protects the profile with `passcode` and a freshly generated archive key,
// moving an existing archive to that key. Every passcode change therefore
// also rotates the archive key: an older wrapper of this profile (a leaked
// backup, a weaker passcode or KDF, no passcode at all) reveals _localKey
// at most, never the key of the current archive.
Domain::ProtectResult Domain::protectWithPasscode(
		const QByteArray &passcode,
		const QByteArray &accountsInfo) {
	Expects(!passcode.isEmpty());

	auto next = wrapLocalKey(passcode, GenerateArchiveKey());
	if (next.requiresUpgrade) {
		return ProtectResult::Failed;
	}
	const auto name = ComputeKeyName(_dataName);
	const auto previous = currentWrapper();
	auto persisted = false;
	const auto persist = [&] {
		persisted = WriteProfileWrapper(
			name, next.salt, next.encrypted, accountsInfo, true);
		return persisted;
	};
	const auto rollback = [&] {
		return WriteProfileWrapper(
			name, previous.salt, previous.encrypted, accountsInfo, true);
	};
	if (AyuDatabase::isUnlocked() || AyuDatabase::hasArchive()) {
		if (!AyuDatabase::isUnlocked() && !unlockArchive(false)) {
			return ProtectResult::Failed;
		}
		auto archiveKey = KeyBytes(next.archiveKey);
		const auto wipe = gsl::finally([&] {
			OPENSSL_cleanse(archiveKey.data(), archiveKey.size());
		});
		const auto result = AyuDatabase::rekey(archiveKey, persist, rollback);
		if (result == AyuDatabase::RekeyResult::Unchanged) {
			return ProtectResult::Failed;
		} else if (persisted) {
			applyWrapper(std::move(next));
		}
		return (result == AyuDatabase::RekeyResult::Rekeyed)
			? ProtectResult::Done
			: ProtectResult::Locked;
	}
	if (!persist()) {
		return rollback() ? ProtectResult::Failed : ProtectResult::Locked;
	}
	applyWrapper(std::move(next));
	return unlockArchive(true) ? ProtectResult::Done : ProtectResult::Locked;
}

Domain::StartModernResult Domain::startModern(
		const QByteArray &passcode) {
	const auto name = ComputeKeyName(_dataName);

	FileReadDescriptor keyData;
	if (!ReadFile(keyData, name, BaseGlobalPath())) {
		const auto base = BaseGlobalPath();
		if (QFileInfo::exists(base + name + u's')
			|| QFileInfo::exists(base + name + u'0')
			|| QFileInfo::exists(base + name + u'1')
			|| QFileInfo::exists(base + u"ayudata.secure.db"_q)
			|| QFileInfo::exists(base + u"ayudata.secure.db.migrating"_q)
			|| QFileInfo::exists(base + u"ayudata.secure.db.rekey"_q)) {
			return StartModernResult::StorageError;
		}
		return StartModernResult::Empty;
	}
	LOG(("App Info: reading accounts info..."));

	QByteArray salt, keyEncrypted, infoEncrypted;
	keyData.stream >> salt >> keyEncrypted >> infoEncrypted;
	if (!CheckStreamStatus(keyData.stream)) {
		return StartModernResult::Failed;
	}

	if (salt.size() != LocalEncryptSaltSize
		&& !Ayu::Security::IsScryptProfileSalt(
			reinterpret_cast<const unsigned char*>(salt.constData()), salt.size())) {
		LOG(("App Error: bad salt in info file, size: %1").arg(salt.size()));
		return StartModernResult::Failed;
	}
	_passcodeKey = ProfilePasscodeKey(passcode, salt);

	EncryptedDescriptor keyInnerData, info;
	if (!DecryptLocal(keyInnerData, keyEncrypted, _passcodeKey)) {
		LOG(("App Info: could not decrypt pass-protected key from info file, "
			"maybe bad password..."));
		return StartModernResult::IncorrectPasscode;
	}
	auto key = Serialize::read<MTP::AuthKey::Data>(keyInnerData.stream);
	auto archiveKey = std::optional<MTP::AuthKey::Data>();
	const auto wipe = gsl::finally([&] {
		OPENSSL_cleanse(key.data(), key.size());
		if (archiveKey) {
			OPENSSL_cleanse(archiveKey->data(), archiveKey->size());
		}
		OPENSSL_cleanse(keyInnerData.data.data(), keyInnerData.data.size());
	});
	if (keyInnerData.stream.status() == QDataStream::Ok
		&& !keyInnerData.stream.atEnd()
		&& Ayu::Security::IsScryptProfileSalt(
			reinterpret_cast<const unsigned char*>(salt.constData()),
			salt.size())) {
		archiveKey = Serialize::read<MTP::AuthKey::Data>(keyInnerData.stream);
	}
	if (keyInnerData.stream.status() != QDataStream::Ok
		|| !keyInnerData.stream.atEnd()) {
		LOG(("App Error: could not read pass-protected key from info file"));
		return StartModernResult::Failed;
	}
	_localKey = std::make_shared<MTP::AuthKey>(key);
	_archiveKey = archiveKey
		? std::make_shared<MTP::AuthKey>(*archiveKey)
		: nullptr;

	_passcodeKeyEncrypted = keyEncrypted;
	_passcodeKeySalt = salt;
	_hasLocalPasscode = !passcode.isEmpty();

	if (!DecryptLocal(info, infoEncrypted, _localKey)) {
		LOG(("App Error: could not decrypt info."));
		return StartModernResult::Failed;
	}
	LOG(("App Info: reading encrypted info..."));
	auto count = qint32();
	info.stream >> count;
	if (count <= 0 || count > Main::Domain::kPremiumMaxAccounts) {
		LOG(("App Error: bad accounts count: %1").arg(count));
		return StartModernResult::Failed;
	}
	_requiresPasscodeUpgrade = _hasLocalPasscode
		&& !Ayu::Security::IsStrongLocalPassword(
			QString::fromUtf8(passcode).toStdU32String());
	if (!_hasLocalPasscode || _requiresPasscodeUpgrade) {
		_pendingAccountInfo = infoEncrypted;
		_needsPasscodeSetup = true;
		_requiresPasscodeUpgrade = true;
		return StartModernResult::NeedsPasscodeSetup;
	}
	if (_hasLocalPasscode && !_requiresPasscodeUpgrade) {
		// A wrapper without an archive key predates it, so the archive
		// (if any) is still under _localKey, which older wrappers (PBKDF2
		// or a weaker passcode) could reveal. protectWithPasscode() moves
		// it to a fresh key and, for PBKDF2 wrappers, to scrypt as well.
		if (_archiveKey) {
			if (!WriteProfileWrapper(name, _passcodeKeySalt,
					_passcodeKeyEncrypted, infoEncrypted, true)
				|| !unlockArchive(true)) {
				return StartModernResult::StorageError;
			}
		} else if (protectWithPasscode(passcode, infoEncrypted)
				!= ProtectResult::Done) {
			return StartModernResult::StorageError;
		}
	}

	_oldVersion = keyData.version;

	auto tried = base::flat_set<int>();
	auto sessions = base::flat_set<uint64>();
	auto active = 0;
	for (auto i = 0; i != count; ++i) {
		auto index = qint32();
		info.stream >> index;
		if (index >= 0
			&& index < Main::Domain::kPremiumMaxAccounts
			&& tried.emplace(index).second) {
			auto account = std::make_unique<Main::Account>(
				_owner,
				_dataName,
				index);
			auto config = account->prepareToStart(_localKey);
			const auto sessionId = account->willHaveSessionUniqueId(
				config.get());
			if (!sessions.contains(sessionId)
				&& (sessionId != 0 || (sessions.empty() && i + 1 == count))) {
				if (sessions.empty()) {
					active = index;
				}
				account->start(std::move(config));
				_owner->accountAddedInStorage({
					.index = index,
					.account = std::move(account)
				});
				sessions.emplace(sessionId);
			}
		}
	}
	if (sessions.empty()) {
		LOG(("App Error: no accounts read."));
		return StartModernResult::Failed;
	}

	if (!info.stream.atEnd()) {
		info.stream >> active;
	}
	_owner->activateFromStorage(active);

	_pendingAccountInfo.clear();
	_needsPasscodeSetup = false;
	Ensures(!sessions.empty());
	return StartModernResult::Success;
}

QByteArray Domain::prepareAccountsInfo() const {
	Expects(!_owner->accounts().empty());
	const auto &list = _owner->accounts();
	EncryptedDescriptor info(sizeof(qint32) * (list.size() + 2));
	info.stream << qint32(list.size());
	for (const auto &[index, account] : list) {
		info.stream << qint32(index);
	}
	info.stream << qint32(_owner->activeForStorage());
	return PrepareEncrypted(info, _localKey);
}

void Domain::writeAccounts() {
	if (!WriteProfileWrapper(ComputeKeyName(_dataName), _passcodeKeySalt,
			_passcodeKeyEncrypted, prepareAccountsInfo(), false)) {
		LOG(("Could not schedule profile storage write."));
	}
}

void Domain::startFromScratch() {
	startWithSingleAccount(
		QByteArray(),
		std::make_unique<Main::Account>(_owner, _dataName, 0));
}

bool Domain::checkPasscode(const QByteArray &passcode) const {
	if (_passcodeKeySalt.isEmpty() || !_passcodeKey) {
		return false;
	}
	try {
		return ProfilePasscodeKey(passcode, _passcodeKeySalt)->equals(_passcodeKey);
	} catch (...) {
		return false;
	}
}

bool Domain::setPasscode(const QByteArray &passcode) {
	if (_passcodeKeySalt.isEmpty() || !_localKey) {
		return false;
	}
	try {
		if (!Ayu::Security::IsStrongLocalPassword(
				QString::fromUtf8(passcode).toStdU32String())) {
			return false;
		}
		const auto result = protectWithPasscode(
			passcode,
			_pendingAccountInfo.isEmpty()
				? prepareAccountsInfo()
				: _pendingAccountInfo);
		if (result != ProtectResult::Done) {
			_requiresPasscodeUpgrade = true;
			return false;
		}
		_requiresPasscodeUpgrade = false;
		_needsPasscodeSetup = false;
		_passcodeKeyChanged.fire({});
		return true;
	} catch (...) {
		_requiresPasscodeUpgrade = true;
		return false;
	}
}

int Domain::oldVersion() const {
	return _oldVersion;
}

void Domain::clearOldVersion() {
	_oldVersion = 0;
}

rpl::producer<> Domain::localPasscodeChanged() const {
	return _passcodeKeyChanged.events();
}

bool Domain::hasLocalPasscode() const {
	return _hasLocalPasscode;
}

bool Domain::requiresPasscodeUpgrade() const {
	return _requiresPasscodeUpgrade;
}

bool Domain::needsPasscodeSetup() const {
	return _needsPasscodeSetup;
}

} // namespace Storage
