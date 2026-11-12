#pragma once

#include <string>

/**
 * Credentials and endpoint for the Anx Reader WebDAV sync.
 *
 * Deliberately matches the shape of `OpdsServerStore` / `KOReaderCredentialStore`
 * so it plugs into the existing settings persistence pattern and the settings UI
 * without inventing a new mechanism.
 *
 * NOT host-tested: this depends on SD storage, so it can only be exercised on
 * hardware. It *is* compile-checked against tests/host/stubs by
 * tests/host/check_io_layer.py.
 */
class AnxCredentialStore {
 public:
  AnxCredentialStore(const AnxCredentialStore&) = delete;
  AnxCredentialStore& operator=(const AnxCredentialStore&) = delete;

  static AnxCredentialStore& getInstance();

  /**
   * Load from `<sd>/.crosspoint/anx.json`.
   * A missing file is not an error: the store stays empty and
   * isConfigured() returns false.
   */
  bool loadFromFile();

  /** Persist to `<sd>/.crosspoint/anx.json`. */
  bool saveToFile() const;

  // ---- accessors ----

  /** Base URL the user entered, WITHOUT the trailing `anx/` (we append it). */
  const std::string& getBaseUrl() const { return baseUrl; }
  const std::string& getUsername() const { return username; }
  const std::string& getPassword() const { return password; }

  /** Sync only while associated with a WiFi network. Default true. */
  bool getOnlySyncOnWifi() const { return onlySyncOnWifi; }

  /** Pull books automatically on wake. Default false (manual trigger only). */
  bool getAutoSyncOnWake() const { return autoSyncOnWake; }

  /**
   * Base URL normalisation: strips whitespace, trailing slashes and a trailing
   * `anx` segment so a user who pastes the sync folder itself does not end up
   * requesting `.../anx/anx/data/file/`.
   */
  void setBaseUrl(const std::string& url);
  void setUsername(const std::string& value) { username = value; }
  void setPassword(const std::string& value) { password = value; }
  void setOnlySyncOnWifi(bool value) { onlySyncOnWifi = value; }
  void setAutoSyncOnWake(bool value) { autoSyncOnWake = value; }

  /**
   * Usable configuration: an http(s) URL with a host. Anx Reader itself also
   * requires username+password, but some self-hosted WebDAV setups are
   * unauthenticated, so credentials are reported separately by
   * hasCredentials().
   */
  bool isConfigured() const;

  /** True when a username and password are both present. */
  bool hasCredentials() const;

  /** Clear everything (used by a "forget server" action). */
  void clear();

  /** Path of the settings file on the SD card. */
  static const char* settingsPath();

 private:
  AnxCredentialStore() = default;

  std::string baseUrl;
  std::string username;
  std::string password;
  bool onlySyncOnWifi = true;
  bool autoSyncOnWake = false;
};
