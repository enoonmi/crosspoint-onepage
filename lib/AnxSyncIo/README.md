# AnxSyncIo — device-dependent half of the Anx Reader sync

`lib/AnxSync/` holds the pure-logic half: parsing, path mapping, diffing, progress
JSON. Everything there is host-compiled and unit-tested.

This directory holds the half that needs the device:

| File | Purpose | Verified |
|---|---|---|
| `AnxCredentialStore.{h,cpp}` | WebDAV URL / credentials / sync toggles, persisted to `/.crosspoint/anx.json` | compiles + config smoke test |
| `AnxWebDavClient.{h,cpp}` | PROPFIND listing, streaming book download, progress PUT | compiles only |

## What "verified" means here

`tests/host/check_io_layer.py` compiles both translation units against the stubs
in `tests/host/stubs/`, links them, and runs a small smoke driver that checks URL
normalisation and credential gating.

That catches typos, missing declarations, wrong types, namespace mistakes and
missing symbols — it caught several while this was written (unqualified `PathMap`
and `RemoteFile`, a malformed array allocation helper). It does **not** prove the
sync works:

* the `esp_http_client` stubs report every operation as a failure on purpose, so
  a host run can never look like a successful sync;
* there is no real TLS, no real WebDAV server and no real SD card.

Both `.cpp` files carry a header comment saying they are not host-tested, so the
distinction survives outside this README.

## Why it is a separate library directory

PlatformIO compiles every `.cpp` under a library directory. Keeping the IO code
out of `lib/AnxSync/` is what lets `tests/host/run_tests.ps1` build the
pure-logic modules without dragging in Arduino headers. Splitting them was not
cosmetic: with both in one directory the host tests stopped compiling.

## Dependency notes

* `AnxWebDavClient` follows the `esp_http_client` usage in
  `lib/KOReaderSync/KOReaderSyncClient.cpp` (client creation, Basic auth,
  `perform()` for small bodies) and `src/network/HttpDownloader.cpp` (the manual
  `open` / `read` loop for streaming a large file to the SD card).
* `AnxCredentialStore` follows `OpdsServerStore` for persistence shape.
* The password is currently stored in plain text in `/.crosspoint/anx.json`.
  `OpdsServerStore` XORs with the device MAC and base64s it; that is a known
  follow-up, noted in the source rather than silently skipped.

## Safety rules encoded here

1. Nothing is ever written under `anx/data/file/` or `anx/data/cover/`. Anx
   Reader deletes remote files there that are not in its database.
2. `anx/database*.db` is never written. Anx Reader treats its database as
   authoritative and would wipe the phone's notes and library.
3. Progress is PUT to `anx/onepage/progress.json`, which Anx Reader ignores.
4. Downloads land on `/Books/.<name>.part` and are renamed only after the byte
   count matches `Content-Length`, so an interrupted sync cannot leave a
   truncated EPUB that the reader would try to parse.
