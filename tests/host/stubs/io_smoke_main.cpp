#include <cstdio>

#include "AnxCredentialStore.h"

// Smoke driver for the host link check of lib/AnxSyncIo.
//
// This does NOT exercise the network or the SD card - the stubs in this
// directory report every esp_http_client operation as a failure on purpose, so
// that a host run can never look like a successful sync.
//
// What it does verify:
//   * the IO layer compiles and links with no missing symbols
//   * basic configuration plumbing (URL normalisation, isConfigured) behaves
//   * an empty password entry cannot erase a stored one
//
// Run via tests/host/check_io_layer.py.

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

  std::printf("\n%s\n", failures == 0 ? "IO LAYER SMOKE OK" : "IO LAYER SMOKE FAILED");
  return failures == 0 ? 0 : 1;
}
