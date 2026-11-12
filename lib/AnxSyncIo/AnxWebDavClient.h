#pragma once

#include <cstdint>
#include <string>
#include <vector>

// RemoteFile comes from the pure-logic library; this header must not pull in
// Arduino / esp_http_client, so it stays includable from tests too.
#include <AnxSyncPlan.h>

/**
 * WebDAV client for Anx Reader's `anx/` layout.
 *
 * Scope: the three operations this feature needs.
 *   - `listBooks()`     PROPFIND anx/data/file/ (Depth: 1) -> remote catalogue
 *   - `downloadBook()`  GET a book, streaming to the SD card
 *   - `putProgress()`   PUT anx/onepage/progress.json
 *
 * It never PUTs anything under anx/data/, and never touches anx/database*.db:
 * Anx Reader treats its database as the whole world and deletes remote book
 * files that are not in it, so a device writing there would destroy the phone's
 * library. See docs/01-anx-webdav-protocol.md in the integration repo.
 *
 * NOT host-compiled: depends on esp_http_client and HalStorage. The parsing and
 * decision logic it calls (AnxPropfindParser, AnxSyncPlan) *is* host-tested.
 */
class AnxWebDavClient {
 public:
  enum Error {
    OK = 0,
    NOT_CONFIGURED,
    AUTH_FAILED,
    NOT_FOUND,
    NETWORK_ERROR,
    SERVER_ERROR,
    PARSE_ERROR,
    STORAGE_ERROR,
    LOW_MEMORY,
    CANCELLED,
  };

  using ProgressCallback = void (*)(size_t done, size_t total, void* context);

  static const char* errorString(Error error);

  /** HTTP status of the most recent request, for diagnostics. 0 if none. */
  static int lastHttpCode;

  /**
   * Ensure the SD layout the sync writes into exists (/Books and
   * /.crosspoint). Returns false if storage is not ready or a directory cannot
   * be created.
   */
  static bool ensureLocalLayout();

  /**
   * PROPFIND `anx/data/file/` with Depth: 1 and fill `out` with the file entries.
   *
   * Directory entries (including the collection itself, which Depth: 1 always
   * returns first) are filtered out here, so callers get books only. Entries
   * with unacceptable names are passed through; the sync planner reports them as
   * ignored so the reason surfaces to the user.
   */
  static Error listBooks(std::vector<anx::RemoteFile>& out);

  /**
   * Download one book to `/Books/<name>`.
   *
   * Writes to `/Books/.<name>.part` first and renames on success, so an
   * interrupted transfer cannot leave a truncated EPUB that the reader would
   * then try to parse. Any leftover `.part` file is removed up front.
   */
  static Error downloadBook(const std::string& name, ProgressCallback progress = nullptr,
                            void* context = nullptr, const volatile bool* cancelFlag = nullptr);

  /**
   * Upload `anx/onepage/progress.json`.
   *
   * That path is outside `anx/data/` so Anx Reader's file sync neither reads nor
   * prunes it, and it is not named `database*.db` so determineSyncDirection()
   * does not mistake it for a database.
   */
  static Error putProgress(const std::string& json);

 private:
  /** Build `baseUrl + "/" + relative`, percent-encoding each path segment. */
  static std::string buildUrl(const std::string& relative);

  /** Validate configuration and free heap before a TLS handshake. */
  static Error checkConfigured();
};
