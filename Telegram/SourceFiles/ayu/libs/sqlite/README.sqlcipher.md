# Vendored SQLCipher

The AyuGram archive uses SQLCipher Community Edition in place of the previous
SQLite amalgamation. It links the client's existing OpenSSL dependency; no
external encryption application or service is required.

- Upstream: https://github.com/sqlcipher/sqlcipher
- Release: **v4.20.0**
- Commit: `355d4db92d3404e71faa8088d071a71d767807f4`
- Embedded SQLite: **3.53.4**
- License: [SQLCipher-LICENSE.md](SQLCipher-LICENSE.md)

The generated `sqlite3.c` and `sqlite3.h` are unmodified upstream output.
The adjacent `sqlite_orm.h` is a separate existing dependency.

## Regeneration on Windows

In a Visual Studio x64 Native Tools prompt with Tcl 8.6 available:

```bat
git clone --branch v4.20.0 --depth 1 https://github.com/sqlcipher/sqlcipher.git sqlcipher-source
cd sqlcipher-source
git rev-parse HEAD
nmake /nologo /f Makefile.msc sqlite3.c TCLSH_CMD=C:\path\to\tclsh86.exe TCLVERSION=86
```

Verify the commit above, then copy `sqlite3.c`, `sqlite3.h`, and `LICENSE.md`
(renamed `SQLCipher-LICENSE.md`) into this directory. Tcl is needed only when
regenerating the amalgamation, not to build or run the client afterward.

SHA-256 of the generated files currently vendored:

```text
674a8c2afdc00ca9aaca5af76e3b2a3c6bfb9341d8a88c79d6e3c38869c9a324  sqlite3.c
213d3a43c1cd5eebe43c8c6a56d8b7b9f54c346f34e351f4b562b2c08f8ea1e1  sqlite3.h
```

`Telegram/CMakeLists.txt` enables the codec and OpenSSL provider with
`SQLITE_HAS_CODEC`, `SQLITE_TEMP_STORE=3`, `SQLITE_THREADSAFE=1`,
`SQLCIPHER_CRYPTO_OPENSSL`, `SQLITE_EXTRA_INIT=sqlcipher_extra_init`, and
`SQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown`. The codec definition also
exposes `sqlite3_key()` to the C++ archive code. The archive refuses to open
if `PRAGMA cipher_version` is unavailable.
