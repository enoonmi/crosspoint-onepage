// Symbol definitions for the host stubs in stubs/Arduino.h.
// These exist only so the IO layer can be linked on the host to prove it has no
// missing symbols; the behaviour is trivially wrong on purpose (no real network,
// no real SD card). Never link this into firmware.

#include <Arduino.h>

#include <cstdio>
#include <cstring>
#include <functional>

#include "Epub.h"

EspStub ESP;

namespace anxlog {
void logImpl(const char* level, const char* module, const char* fmt, ...) {
  std::fprintf(stderr, "[%s][%s] ", level, module);
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fputc('\n', stderr);
}
}  // namespace anxlog

HalStorage& HalStorage::getInstance() {
  static HalStorage instance;
  return instance;
}

bool HalStorage::exists(const char* path) {
  static_cast<void>(path);
  return false;
}

bool HalStorage::remove(const char* path) {
  static_cast<void>(path);
  return true;
}

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  static_cast<void>(oldPath);
  static_cast<void>(newPath);
  return true;
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  static_cast<void>(path);
  return true;
}

bool HalStorage::writeFile(const char* path, const String& content) {
  static_cast<void>(path);
  static_cast<void>(content);
  return true;
}

String HalStorage::readFile(const char* path) {
  static_cast<void>(path);
  return String("");
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  static_cast<void>(moduleName);
  static_cast<void>(path);
  static_cast<void>(file);
  return true;
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  static_cast<void>(moduleName);
  static_cast<void>(path);
  static_cast<void>(file);
  return true;
}

// ---- esp_http_client: every call fails, which keeps the host build honest
// ---- about the fact that no network is exercised.

namespace {
struct FakeClient {
  int status = 0;
  int64_t contentLength = 0;
  esp_http_client_method_t method = HTTP_METHOD_GET;
  esp_err_t (*handler)(esp_http_client_event_t*) = nullptr;
  void* userData = nullptr;
};
}  // namespace

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
  auto* client = new FakeClient();
  client->method = config->method;
  client->handler = config->event_handler;
  client->userData = config->user_data;
  return client;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char* key, const char* value) {
  static_cast<void>(client);
  static_cast<void>(key);
  static_cast<void>(value);
  return ESP_OK;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t client, int content_length) {
  static_cast<void>(client);
  static_cast<void>(content_length);
  // Report a connection failure: better an obvious error than a silent "sync
  // succeeded" during a host smoke test.
  return ESP_FAIL;
}

int esp_http_client_write(esp_http_client_handle_t client, const char* buffer, int len) {
  static_cast<void>(client);
  static_cast<void>(buffer);
  return len;
}

int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) {
  static_cast<void>(client);
  return 0;
}

int esp_http_client_read(esp_http_client_handle_t client, char* buffer, int len) {
  static_cast<void>(client);
  static_cast<void>(buffer);
  static_cast<void>(len);
  return 0;
}

int esp_http_client_read_response(esp_http_client_handle_t client, char* buffer, int len) {
  static_cast<void>(client);
  static_cast<void>(buffer);
  static_cast<void>(len);
  return 0;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
  return static_cast<FakeClient*>(client)->status;
}

int64_t esp_http_client_get_content_length(esp_http_client_handle_t client) {
  return static_cast<FakeClient*>(client)->contentLength;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
  delete static_cast<FakeClient*>(client);
  return ESP_OK;
}

// ---------------- Epub ----------------

// See stubs/Epub.h: load() fails on purpose so a host run cannot look like a
// successful position scan. The cache path mirrors the real constructor so the
// string this stub produces would match, which keeps the code under test honest
// about the path it builds.
Epub::Epub(std::string path, const std::string& cacheDir) : filepath(std::move(path)) {
  cachePath = cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(filepath));
}

bool Epub::load(bool buildIfMissing, bool skipLoadingCss) {
  static_cast<void>(buildIfMissing);
  static_cast<void>(skipLoadingCss);
  return false;
}

int Epub::getSpineItemsCount() const { return 0; }

size_t Epub::getCumulativeSpineItemSize(int spineIndex) const {
  static_cast<void>(spineIndex);
  return 0;
}
