/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace MTP {
class Config;
class AuthKey;
using AuthKeyPtr = std::shared_ptr<AuthKey>;
} // namespace MTP

namespace Main {
class Account;
class Domain;
} // namespace Main

namespace Storage {

enum class StartResult : uchar {
	Success,
	IncorrectPasscode,
	IncorrectPasscodeLegacy,
	StorageError,
	NeedsPasscodeSetup,
};

class Domain final {
public:
	Domain(not_null<Main::Domain*> owner, const QString &dataName);
	~Domain();

	[[nodiscard]] StartResult start(const QByteArray &passcode);
	void startAdded(
		not_null<Main::Account*> account,
		std::unique_ptr<MTP::Config> config);
	void writeAccounts();
	void startFromScratch();

	[[nodiscard]] bool checkPasscode(const QByteArray &passcode) const;
	[[nodiscard]] bool setPasscode(const QByteArray &passcode);

	[[nodiscard]] int oldVersion() const;
	void clearOldVersion();

	[[nodiscard]] rpl::producer<> localPasscodeChanged() const;
	[[nodiscard]] bool hasLocalPasscode() const;
	[[nodiscard]] bool requiresPasscodeUpgrade() const;
	[[nodiscard]] bool needsPasscodeSetup() const;

private:
	enum class StartModernResult {
		Success,
		IncorrectPasscode,
		Failed,
		Empty,
		StorageError,
		NeedsPasscodeSetup,
	};

	enum class ProtectResult {
		Done,
		Failed, // Nothing changed on disk.
		Locked, // The passcode may be persisted, the archive is shut.
	};
	struct KeyWrapper {
		QByteArray salt;
		QByteArray encrypted;
		MTP::AuthKeyPtr passcodeKey;
		MTP::AuthKeyPtr archiveKey;
		bool hasPasscode = false;
		bool requiresUpgrade = false;
	};

	[[nodiscard]] StartModernResult startModern(const QByteArray &passcode);
	void startWithSingleAccount(
		const QByteArray &passcode,
		std::unique_ptr<Main::Account> account);
	void generateLocalKey();
	[[nodiscard]] KeyWrapper wrapLocalKey(
		const QByteArray &passcode,
		MTP::AuthKeyPtr archiveKey) const;
	[[nodiscard]] KeyWrapper currentWrapper() const;
	void applyWrapper(KeyWrapper &&wrapper);
	[[nodiscard]] ProtectResult protectWithPasscode(
		const QByteArray &passcode,
		const QByteArray &accountsInfo);
	[[nodiscard]] bool unlockArchive(bool allowCreate);
	[[nodiscard]] QByteArray prepareAccountsInfo() const;

	const not_null<Main::Domain*> _owner;
	const QString _dataName;

	MTP::AuthKeyPtr _localKey;
	// Independent random key of the local archive, stored only inside the
	// scrypt-protected wrapper. Null for wrappers written before it existed,
	// whose archive (if any) is still encrypted with _localKey.
	MTP::AuthKeyPtr _archiveKey;
	MTP::AuthKeyPtr _passcodeKey;
	QByteArray _passcodeKeySalt;
	QByteArray _passcodeKeyEncrypted;
	QByteArray _pendingAccountInfo;
	int _oldVersion = 0;

	bool _needsPasscodeSetup = false;
	bool _hasLocalPasscode = false;
	bool _requiresPasscodeUpgrade = false;
	rpl::event_stream<> _passcodeKeyChanged;

};

} // namespace Storage
