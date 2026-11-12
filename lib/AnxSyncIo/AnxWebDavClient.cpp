#include "AnxWebDavClient.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <base64.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <cstring>
#include <memory>
#include <new>

#include "AnxCredentialStore.h"
#include "AnxPathMap.h"
#include "AnxPropfindParser.h"

// NOTE: not host-compiled: depends on esp_http_client, WiFi and HalStorage.
// Ported from the esp_http_client usage in lib/KOReaderSync/KOReaderSyncClient.cpp
// (same auth/config/event-handler shape) and src/network/HttpDownloader.cpp
// (the manual open/read streaming loop). Review by comparison with those two.

namespace {

constexpr int HTTP_BUF_SIZE = 4096;
constexpr int HTTP_TIMEOUT_MS = 60000;
constexpr size_t READ_CHUNK = 2048;
// PROPFIND bodies are small (a few KB for a few hundred books). Cap the
// collection so a misconfigured server pointing at something huge cannot
// exhaust the heap.
constexpr size_t MAX_RESPONSE = 256 * 1024;
constexpr uint32_t MIN_HEAP_FOR_TLS = 40000;

/** Growable response buffer used by the event handler. */
struct ResponseBuffer {
  char* data = nullptr;
  size_t len = 0;
  size_t capacity = 0;
  bool overflow = false;

  ~ResponseBuffer() { free(data); }

  bool ensure(size_t needed) {
    if (needed <= capacity) return true;
    if (needed > MAX_RESPONSE) {
      overflow = true;
      return false;
    }
    size_t next = capacity == 0 ? 4096 : capacity;
    while (next < needed) next *= 2;
    if (next > MAX_RESPONSE) next = MAX_RESPONSE;
    char* grown = static_cast<char*>(realloc(data, next));
    if (grown == nullptr) return false;
    data = grown;
    capacity = next;
    return true;
  }
};

esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  auto* buf = static_cast<ResponseBuffer*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf != nullptr && evt->data_len > 0) {
    if (buf->ensure(buf->len + static_cast<size_t>(evt->data_len) + 1)) {
      memcpy(buf->data + buf->len, evt->data, static_cast<size_t>(evt->data_len));
      buf->len += static_cast<size_t>(evt->data_len);
      buf->data[buf->len] = '\0';
    } else {
      LOG_ERR("AnxSync", "response buffer overflow at %u bytes", static_cast<unsigned>(buf->len));
    }
  }
  return ESP_OK;
}

/** Build a configured client. `buf` may be null when the body is streamed. */
esp_http_client_handle_t createClient(const std::string& url, ResponseBuffer* buf,
                                      esp_http_client_method_t method) {
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = method;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.buffer_size = HTTP_BUF_SIZE;
  config.buffer_size_tx = HTTP_BUF_SIZE;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = true;
  if (buf != nullptr) {
    config.event_handler = httpEventHandler;
    config.user_data = buf;
  }

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return nullptr;

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);

  // Preemptive Basic auth, matching HttpDownloader: waiting for a 401 costs a
  // round trip and some servers answer 403 instead.
  const AnxCredentialStore& store = AnxCredentialStore::getInstance();
  if (store.hasCredentials()) {
    const std::string credentials = store.getUsername() + ":" + store.getPassword();
    const String header = "Basic " + base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", header.c_str());
  }
  return client;
}

}  // namespace

int AnxWebDavClient::lastHttpCode = 0;

const char* AnxWebDavClient::errorString(Error error) {
  switch (error) {
    case OK: return "ok";
    case NOT_CONFIGURED: return "server not configured";
    case AUTH_FAILED: return "authentication failed";
    case NOT_FOUND: return "remote path not found";
    case NETWORK_ERROR: return "network error";
    case SERVER_ERROR: return "server error";
    case PARSE_ERROR: return "malformed server response";
    case STORAGE_ERROR: return "SD card error";
    case LOW_MEMORY: return "not enough free memory for TLS";
    case CANCELLED: return "cancelled";
  }
  return "unknown";
}

std::string AnxWebDavClient::buildUrl(const std::string& relative) {
  const std::string& base = AnxCredentialStore::getInstance().getBaseUrl();
  if (base.empty()) return relative;

  std::string url = base;
  if (!url.empty() && url.back() != '/') url += '/';

  // Percent-encode each segment but keep the separators: a file name may contain
  // spaces or non-ASCII, while the structural slashes must survive.
  std::string encoded;
  encoded.reserve(relative.size());
  for (char c : relative) {
    if (c == '/') {
      encoded.push_back('/');
      continue;
    }
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                            c == '-' || c == '_' || c == '.' || c == '~';
    if (unreserved) {
      encoded.push_back(c);
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
      encoded += hex;
    }
  }
  return url + encoded;
}

AnxWebDavClient::Error AnxWebDavClient::checkConfigured() {
  const AnxCredentialStore& store = AnxCredentialStore::getInstance();
  if (!store.isConfigured()) {
    LOG_ERR("AnxSync", "server not configured");
    return NOT_CONFIGURED;
  }
  if (ESP.getFreeHeap() < MIN_HEAP_FOR_TLS) {
    LOG_ERR("AnxSync", "insufficient heap for TLS: %u", static_cast<unsigned>(ESP.getFreeHeap()));
    return LOW_MEMORY;
  }
  return OK;
}

bool AnxWebDavClient::ensureLocalLayout() {
  if (!Storage.ready()) {
    LOG_ERR("AnxSync", "storage not ready");
    return false;
  }
  if (!Storage.exists(anx::PathMap::booksDir())) {
    if (!Storage.ensureDirectoryExists(anx::PathMap::booksDir())) {
      LOG_ERR("AnxSync", "cannot create %s", anx::PathMap::booksDir());
      return false;
    }
  }
  if (!Storage.exists("/.crosspoint")) {
    if (!Storage.ensureDirectoryExists("/.crosspoint")) {
      LOG_ERR("AnxSync", "cannot create /.crosspoint");
      return false;
    }
  }
  return true;
}

AnxWebDavClient::Error AnxWebDavClient::listBooks(std::vector<anx::RemoteFile>& out) {
  out.clear();
  lastHttpCode = 0;

  const Error configured = checkConfigured();
  if (configured != OK) return configured;

  const std::string url = buildUrl(std::string(anx::PathMap::remoteBooksDir()) + "/");
  LOG_DBG("AnxSync", "PROPFIND %s", url.c_str());

  ResponseBuffer buf;
  esp_http_client_handle_t client = createClient(url, &buf, HTTP_METHOD_PROPFIND);
  if (client == nullptr) return NETWORK_ERROR;

  // Ask only for the properties the planner uses, and run one level deep.
  esp_http_client_set_header(client, "Depth", "1");
  esp_http_client_set_header(client, "Content-Type", "application/xml; charset=utf-8");
  static const char* kBody =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<d:propfind xmlns:d=\"DAV:\"><d:prop>"
      "<d:getlastmodified/><d:getcontentlength/><d:resourcetype/>"
      "</d:prop></d:propfind>";

  esp_err_t err = esp_http_client_open(client, static_cast<int>(strlen(kBody)));
  if (err == ESP_OK) {
    const int written = static_cast<int>(esp_http_client_write(client, kBody, static_cast<int>(strlen(kBody))));
    if (written != static_cast<int>(strlen(kBody))) {
      LOG_ERR("AnxSync", "PROPFIND body write short: %d", written);
      err = ESP_FAIL;
    }
  }
  if (err == ESP_OK) {
    esp_http_client_fetch_headers(client);
    lastHttpCode = esp_http_client_get_status_code(client);
    // Drain the body through the event handler.
    while (true) {
      const int read = esp_http_client_read_response(client, nullptr, 0);
      if (read <= 0) break;
    }
  } else {
    lastHttpCode = esp_http_client_get_status_code(client);
  }
  esp_http_client_cleanup(client);

  LOG_DBG("AnxSync", "PROPFIND status=%d err=%d body=%u bytes", lastHttpCode, static_cast<int>(err),
          static_cast<unsigned>(buf.len));

  if (err != ESP_OK) return NETWORK_ERROR;
  if (lastHttpCode == 401 || lastHttpCode == 403) return AUTH_FAILED;
  if (lastHttpCode == 404) return NOT_FOUND;
  if (lastHttpCode != 207 && lastHttpCode != 200) return SERVER_ERROR;
  if (buf.len == 0) return PARSE_ERROR;

  std::vector<anx::PropfindEntry> entries;
  if (!anx::PropfindParser::parse(buf.data, buf.len, entries)) {
    LOG_ERR("AnxSync", "PROPFIND body is not a multistatus document");
    return PARSE_ERROR;
  }

  out.reserve(entries.size());
  for (const anx::PropfindEntry& entry : entries) {
    // Depth: 1 always includes the collection itself; skip directories.
    if (entry.isCollection) continue;
    anx::RemoteFile file;
    file.name = entry.name;
    file.size = entry.contentLength;
    file.lastModified = entry.lastModified;
    file.hasLastModified = entry.hasLastModified;
    out.push_back(file);
  }

  LOG_DBG("AnxSync", "listed %u books", static_cast<unsigned>(out.size()));
  return OK;
}

AnxWebDavClient::Error AnxWebDavClient::downloadBook(const std::string& name, ProgressCallback progress,
                                                     void* context, const volatile bool* cancelFlag) {
  lastHttpCode = 0;

  if (!anx::PathMap::isAcceptableBookName(name)) {
    LOG_ERR("AnxSync", "refusing unacceptable book name: %s", name.c_str());
    return STORAGE_ERROR;
  }

  const Error configured = checkConfigured();
  if (configured != OK) return configured;

  std::string localPath;
  std::string tempPath;
  if (!anx::PathMap::localBookPath(name, localPath) || !anx::PathMap::localTempBookPath(name, tempPath)) {
    return STORAGE_ERROR;
  }

  // Remove a leftover partial file from an earlier interrupted attempt.
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  const std::string url = buildUrl(std::string(anx::PathMap::remoteBooksDir()) + "/" + name);
  LOG_DBG("AnxSync", "GET %s -> %s", url.c_str(), tempPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("AnxSync", tempPath.c_str(), file)) {
    LOG_ERR("AnxSync", "cannot open %s for writing", tempPath.c_str());
    return STORAGE_ERROR;
  }

  esp_http_client_handle_t client = createClient(url, nullptr, HTTP_METHOD_GET);
  if (client == nullptr) {
    return NETWORK_ERROR;
  }

  Error result = OK;
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    LOG_ERR("AnxSync", "open failed: %d", static_cast<int>(err));
    result = NETWORK_ERROR;
  } else {
    esp_http_client_fetch_headers(client);
    lastHttpCode = esp_http_client_get_status_code(client);
    const int64_t contentLength = esp_http_client_get_content_length(client);

    if (lastHttpCode == 401 || lastHttpCode == 403) {
      result = AUTH_FAILED;
    } else if (lastHttpCode == 404) {
      result = NOT_FOUND;
    } else if (lastHttpCode != 200) {
      result = SERVER_ERROR;
    } else {
      // A plain unique_ptr<char[]>: makeUniqueNoThrow's array form is the
      // firmware convention, but the types here are trivial and this keeps the
      // hot read loop obvious.
      std::unique_ptr<char[]> buffer(new (std::nothrow) char[READ_CHUNK]);
      if (!buffer) {
        LOG_ERR("AnxSync", "OOM allocating read buffer");
        result = LOW_MEMORY;
      } else {
        size_t total = 0;
        const size_t expected = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;
        while (true) {
          if (cancelFlag != nullptr && *cancelFlag) {
            result = CANCELLED;
            break;
          }
          const int read = esp_http_client_read(client, buffer.get(), static_cast<int>(READ_CHUNK));
          if (read < 0) {
            LOG_ERR("AnxSync", "read error: %d", read);
            result = NETWORK_ERROR;
            break;
          }
          if (read == 0) break;  // EOF
          const size_t written = file.write(buffer.get(), static_cast<size_t>(read));
          if (written != static_cast<size_t>(read)) {
            LOG_ERR("AnxSync", "short write to SD: %u of %d", static_cast<unsigned>(written), read);
            result = STORAGE_ERROR;
            break;
          }
          total += written;
          if (progress != nullptr) progress(total, expected, context);
        }

        // A truncated transfer must not be renamed into place.
        if (result == OK && expected > 0 && total != expected) {
          LOG_ERR("AnxSync", "incomplete download: %u of %u bytes", static_cast<unsigned>(total),
                  static_cast<unsigned>(expected));
          result = NETWORK_ERROR;
        }
        LOG_DBG("AnxSync", "downloaded %u bytes of %s", static_cast<unsigned>(total), name.c_str());
      }
    }
  }

  esp_http_client_cleanup(client);
  file.close();  // must close before remove/rename

  if (result == OK) {
    // Replace any existing copy only once the new one is complete and verified.
    //
    // The old file is moved aside rather than deleted first: SdFat's rename does
    // not overwrite, so something has to make room, but a remove-then-rename has
    // a window where a failure loses the book entirely - the copy is gone and the
    // new one was never put in place. Keeping it at the backup name leaves a way
    // back, and the backup is only dropped after the rename succeeded.
    std::string backupPath;
    const bool haveBackupPath = anx::PathMap::localBackupBookPath(name, backupPath);
    bool hadPrevious = false;

    if (haveBackupPath && Storage.exists(localPath.c_str())) {
      if (Storage.exists(backupPath.c_str())) {
        Storage.remove(backupPath.c_str());  // stale backup from an earlier run
      }
      hadPrevious = Storage.rename(localPath.c_str(), backupPath.c_str());
      if (!hadPrevious) {
        // Could not move it aside; fall back to removing so the new copy can land
        // rather than abandoning the download entirely.
        LOG_ERR("AnxSync", "could not move %s aside, removing instead", localPath.c_str());
        Storage.remove(localPath.c_str());
      }
    }

    if (!Storage.rename(tempPath.c_str(), localPath.c_str())) {
      LOG_ERR("AnxSync", "rename %s -> %s failed", tempPath.c_str(), localPath.c_str());
      Storage.remove(tempPath.c_str());
      // Put the previous copy back so the user still has the older book rather
      // than nothing.
      if (hadPrevious && Storage.exists(backupPath.c_str())) {
        if (!Storage.rename(backupPath.c_str(), localPath.c_str())) {
          LOG_ERR("AnxSync", "restore of %s from backup failed", localPath.c_str());
        } else {
          LOG_ERR("AnxSync", "restored the previous copy of %s", localPath.c_str());
        }
      }
      result = STORAGE_ERROR;
    } else if (hadPrevious) {
      Storage.remove(backupPath.c_str());
    }
  } else {
    Storage.remove(tempPath.c_str());
  }

  return result;
}

AnxWebDavClient::Error AnxWebDavClient::putProgress(const std::string& json) {
  lastHttpCode = 0;

  const Error configured = checkConfigured();
  if (configured != OK) return configured;

  const std::string url = buildUrl(anx::PathMap::remoteProgressPath());
  LOG_DBG("AnxSync", "PUT %s (%u bytes)", url.c_str(), static_cast<unsigned>(json.size()));

  ResponseBuffer buf;
  esp_http_client_handle_t client = createClient(url, &buf, HTTP_METHOD_PUT);
  if (client == nullptr) return NETWORK_ERROR;

  esp_http_client_set_header(client, "Content-Type", "application/json");

  esp_err_t err = esp_http_client_open(client, static_cast<int>(json.size()));
  if (err == ESP_OK) {
    const int written = esp_http_client_write(client, json.c_str(), static_cast<int>(json.size()));
    if (written != static_cast<int>(json.size())) {
      LOG_ERR("AnxSync", "progress upload write short: %d of %u", written,
              static_cast<unsigned>(json.size()));
      err = ESP_FAIL;
    }
  }
  if (err == ESP_OK) {
    esp_http_client_fetch_headers(client);
    lastHttpCode = esp_http_client_get_status_code(client);
    while (esp_http_client_read_response(client, nullptr, 0) > 0) {
      // drain
    }
  } else {
    lastHttpCode = esp_http_client_get_status_code(client);
  }
  esp_http_client_cleanup(client);

  LOG_DBG("AnxSync", "PUT progress status=%d", lastHttpCode);

  if (err != ESP_OK) return NETWORK_ERROR;
  if (lastHttpCode == 401 || lastHttpCode == 403) return AUTH_FAILED;
  // 201 Created and 204 No Content are both normal for WebDAV PUT.
  if (lastHttpCode == 200 || lastHttpCode == 201 || lastHttpCode == 204) return OK;
  return SERVER_ERROR;
}
