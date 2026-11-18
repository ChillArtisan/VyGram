# Protected local storage

Implementation status: native backend tests and the initial full Debug client
compilation passed on 2026-11-18. End-to-end migration/unlock/media tests and an
independent security audit are still required. Do not use
this development build as the only copy of an important profile.

## Storage and password

The existing Telegram profile master key encrypts the core profile and cache.
The same master key now protects AyuGram's SQLCipher archive
`tdata/ayudata.secure.db`, including deleted/edited message records, archived
media payloads, and Ayu settings. The key is not stored in the database.

The global settings record, including the random secret used for the native
theme/background/language encryption key, is sealed inside SQLCipher too.
Legacy public-salt settings files are retained until the encrypted record and
recovery copies pass readback, then removed. Before unlock the client uses a
bundled Terminal palette and defaults. Account startup is deferred until a
strong password is accepted and protected settings are restored. Legacy
per-account copies of weak password wrappers are stripped with synchronous
write and readback before account startup.

A mandatory local password wraps the profile master key. New wrappers use
OpenSSL scrypt (N=65536, r=8, p=1, 64 MiB working memory) and a random salt with
a version marker. Existing wrappers can still be unlocked and are upgraded.
Weak existing passwords must be replaced before opening the archive.
New passwords require at least 12 Unicode characters and reject simple
repeated patterns and common sequences. A long unique password is preferable.
There is no password recovery for this archive.

The interface delays password attempts after three failures: 5, 10, 15, 20,
25, then 30 seconds. The counter is held only in process memory and resets on
restart. It is not a persistent lockout, and someone with copied profile files
can attempt passwords offline without these interface delays. Password entropy
and the scrypt derivation are the protection against that attack; the minimum
length policy alone does not make a predictable password strong.

The password visibility button changes only the field's echo mode. Fields are
masked by default; showing a password deliberately exposes it to a nearby
observer or screen capture. This control does not change the encryption key.

The settings JSON and legacy plaintext database are migrated only after
successful password protection. The encrypted copy is verified before the
source is removed. Failed verification preserves the original and keeps the
archive locked. Interrupted migration artifacts and historical database
backups require explicit recovery; they are not silently discarded.

## Media persistence

Expiration and deletion capture media metadata and available payloads into
the encrypted archive. Captionless media is included. Restored images and
documents use decrypted bytes in memory. Missing files display an explicit
unavailable message. Already downloaded documents up to 256 MiB can be
archived. New memory-only document fetches follow the upstream limit below
10 MiB; large streaming-only videos are not guaranteed to be retained.

## Boundaries

This is at-rest protection for a closed profile. The normal in-app lock hides
the UI but does not unload the running profile, network session, media, or
master key from RAM. Close the client to remove the live session from the
process. Full-disk encryption also protects OS swap, hibernation and other
system-managed copies; this client alone does not provide that protection.

An administrator, malware, screen capture, or someone controlling an already
unlocked process can access plaintext. Old logs, backups, exports, downloaded
files, filesystem snapshots and recoverable remnants of migrated files are
outside the encrypted database. New ordinary application/protocol file logs
and built-in crash dumps are disabled in this fork. OS crash reports and
third-party diagnostic output are separate. Existing files are not silently
deleted.

The archive changes do not add end-to-end encryption to Telegram cloud chats
and do not change server-side retention.

## Verification

`tools/tests/test_secure_database.cpp` exercises actual SQLCipher, legacy
export, reopen, encrypted settings/media, account/type separation, wrong keys,
tampering, and preservation of corrupt legacy input. The password derivation
test checks an independent scrypt vector. Password policy and theme packaging
have separate tests. These tests do not substitute for a complete client build
or an independent security review.
