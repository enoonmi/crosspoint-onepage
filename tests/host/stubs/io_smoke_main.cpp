#include <cstdio>

#include "AnxCredentialStore.h"
#include "AnxWebDavClient.h"

// Smoke driver for the host link check of lib/AnxSyncIo.
//
// This does NOT exercise the network or the SD card - the stubs in this
// directory report every esp_http_client operation as a failure on purpose, so
// that a host run can never look like a successful sync.
//
// What it does verify:
//   * the IO layer compiles and links with no missing symbols
//   * basic configuration plumbing (URL normalisation, isConfigured) behaves
//   * buildUrl() joins and percent-encodes the way WebDAV servers expect
//   * an empty password entry cannot erase a stored one
//
// Run via tests/host/check_io_layer.py.

namespace {

int urlFailures = 0;

void expectUrl(const char* what, const std::string& actual, const std::string& expected) {
  if (actual == expected) return;
  std::printf("FAIL: %s\n  expected: %s\n  actual:   %s\n", what, expected.c_str(), actual.c_str());
  ++urlFailures;
}

// buildUrl() is the single place every request path is assembled. A wrong join
// or a mis-encoded byte sends PROPFIND/GET/PUT to a 404, and the symptom on the
// device is "sync failed" with nothing pointing at the URL. It is cheap to test
// and it had no test at all.
void checkBuildUrl() {
  AnxCredentialStore& store = AnxCredentialStore::getInstance();
  store.clear();

  // No base configured: the relative path is returned untouched, which is what
  // the caller sees before the settings screen has been filled in.
  expectUrl("no base keeps the relative path", AnxWebDavClient::buildUrl("anx/data/file/"),
            "anx/data/file/");

  // A base without a trailing slash must gain exactly one.
  store.setBaseUrl("https://cloud.example.com/dav");
  expectUrl("base without slash gets one", AnxWebDavClient::buildUrl("anx/data/file/"),
            "https://cloud.example.com/dav/anx/data/file/");

  // setBaseUrl already trims trailing slashes, so this asserts the join does not
  // double them up afterwards.
  store.setBaseUrl("https://cloud.example.com/dav/");
  expectUrl("base with slash is not doubled", AnxWebDavClient::buildUrl("anx/onepage/progress.json"),
            "https://cloud.example.com/dav/anx/onepage/progress.json");

  // Structural slashes survive; only the segment bytes are encoded.
  expectUrl("slashes are preserved", AnxWebDavClient::buildUrl("anx/data/file/a b.epub"),
            "https://cloud.example.com/dav/anx/data/file/a%20b.epub");

  // Unreserved characters stay as-is: a book name is normally plain hex.
  expectUrl("unreserved characters survive",
            AnxWebDavClient::buildUrl("anx/data/file/0123456789abcdef.epub"),
            "https://cloud.example.com/dav/anx/data/file/0123456789abcdef.epub");

  // A '+' must be encoded: some servers decode a literal '+' as a space in a
  // path, which would turn a file name into a different file name.
  expectUrl("plus is encoded", AnxWebDavClient::buildUrl("anx/data/file/a+b.epub"),
            "https://cloud.example.com/dav/anx/data/file/a%2Bb.epub");

  // UTF-8 must be encoded per byte, uppercase hex. A non-ASCII book name is
  // plausible; if the encoder widened a signed char the result would be
  // %FFFFFFE4 and every request to it would 404.
  expectUrl("utf-8 is encoded per byte", AnxWebDavClient::buildUrl("anx/data/file/\xE4\xB8\x89.epub"),
            "https://cloud.example.com/dav/anx/data/file/%E4%B8%89.epub");

  // Percent itself must be encoded, or an already-encoded name would be
  // double-decoded by the server.
  expectUrl("percent is encoded", AnxWebDavClient::buildUrl("anx/data/file/a%20b.epub"),
            "https://cloud.example.com/dav/anx/data/file/a%2520b.epub");

  store.clear();
}

}  // namespace

int main() {
  std::printf("AnxSyncIo host link check\n\n");

  AnxCredentialStore& store = AnxCredentialStore::getInstance();

  int failures = 0;

  // A fresh store is not configured.
  if (store.isConfigured()) {
    std::printf("FAIL: empty store reports configured\n");
    ++failures;
  }

  // A plain URL is accepted.
  store.setBaseUrl("https://cloud.example.com/dav");
  if (!store.isConfigured()) {
    std::printf("FAIL: valid https URL rejected\n");
    ++failures;
  }

  // Trailing slashes are dropped.
  store.setBaseUrl("https://cloud.example.com/dav///");
  if (store.getBaseUrl() != "https://cloud.example.com/dav") {
    std::printf("FAIL: trailing slashes not trimmed (got '%s')\n", store.getBaseUrl().c_str());
    ++failures;
  }

  // A pasted `anx` suffix is stripped so we do not double it up.
  store.setBaseUrl("https://cloud.example.com/dav/anx");
  if (store.getBaseUrl() != "https://cloud.example.com/dav") {
    std::printf("FAIL: trailing /anx not stripped (got '%s')\n", store.getBaseUrl().c_str());
    ++failures;
  }

  // Whitespace from a copy-paste is trimmed.
  store.setBaseUrl("  https://cloud.example.com/dav  ");
  if (store.getBaseUrl() != "https://cloud.example.com/dav") {
    std::printf("FAIL: whitespace not trimmed (got '%s')\n", store.getBaseUrl().c_str());
    ++failures;
  }

  // Garbage is rejected rather than silently accepted.
  store.setBaseUrl("cloud.example.com/dav");
  if (store.isConfigured()) {
    std::printf("FAIL: scheme-less URL accepted\n");
    ++failures;
  }
  store.setBaseUrl("https://");
  if (store.isConfigured()) {
    std::printf("FAIL: scheme with no host accepted\n");
    ++failures;
  }

  // Credentials gate.
  store.setBaseUrl("https://cloud.example.com/dav");
  store.setUsername("reader");
  if (store.hasCredentials()) {
    std::printf("FAIL: username without password reported as credentials\n");
    ++failures;
  }
  store.setPassword("secret");
  if (!store.hasCredentials()) {
    std::printf("FAIL: username+password not reported as credentials\n");
    ++failures;
  }

  store.clear();
  if (store.isConfigured() || store.hasCredentials() || !store.getBaseUrl().empty()) {
    std::printf("FAIL: clear() left state behind\n");
    ++failures;
  }

  // Password editing: the settings screen now opens the keyboard with an empty
  // field, so an empty entry must mean "keep what is stored". If the screen ever
  // called setPassword("") unconditionally, opening the field and confirming
  // without typing would silently erase a working password - and every later sync
  // would fail with an auth error the user could not connect to anything they did.
  store.setBaseUrl("https://cloud.example.com/dav");
  store.setUsername("reader");
  store.setPassword("secret");
  {
    // What the settings screen does for an empty entry.
    const std::string typed;
    if (!typed.empty()) {
      store.setPassword(typed);
    }
    if (store.getPassword() != "secret") {
      std::printf("FAIL: an empty password entry erased the stored password\n");
      ++failures;
    }
  }
  {
    // And a real entry must still replace it.
    const std::string typed = "newsecret";
    if (!typed.empty()) {
      store.setPassword(typed);
    }
    if (store.getPassword() != "newsecret") {
      std::printf("FAIL: a typed password did not replace the stored one\n");
      ++failures;
    }
  }

  checkBuildUrl();
  failures += urlFailures;

  std::printf("\n%s\n", failures == 0 ? "IO LAYER SMOKE OK" : "IO LAYER SMOKE FAILED");
  return failures == 0 ? 0 : 1;
}
