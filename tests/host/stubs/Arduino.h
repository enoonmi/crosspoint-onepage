// Minimal stand-ins for the Arduino / ESP-IDF / crosspoint headers that
// lib/AnxSyncIo depends on.
//
// Purpose: let a plain MSVC build syntax-check and link AnxCredentialStore.cpp,
// AnxWebDavClient.cpp and AnxProgressStore.cpp on the host, so "written but
// never compiled" is not the best we can do. This is NOT a faithful emulation of
// the ESP32: it does not model real HTTP, real TLS, or the real SD card. It only
// provides enough of the API surface to prove the code parses, type-checks, and
// has no missing symbols.
//
// Note the simulator/ directory in this repo already contains a much more
// faithful esp_http_client shim backed by libcurl; this one is deliberately
// smaller and dependency-free so the check runs anywhere MSVC exists.

#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// ---------------- Arduino ----------------

class String {
 public:
  String() = default;
  String(const char* s) : value(s != nullptr ? s : "") {}  // NOLINT(google-explicit-constructor)
  String(const std::string& s) : value(s) {}               // NOLINT(google-explicit-constructor)
  const char* c_str() const { return value.c_str(); }
  bool isEmpty() const { return value.empty(); }
  size_t length() const { return value.size(); }
  String& operator+=(const String& other) {
    value += other.value;
    return *this;
  }
  friend String operator+(const String& a, const String& b) { return String(a.value + b.value); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.value); }
  bool operator==(const String& other) const { return value == other.value; }

 private:
  std::string value;
};

// ---------------- ESP-IDF bits ----------------

struct EspStub {
  uint32_t getFreeHeap() const { return 200000; }
};

struct esp_http_client_event_t {
  int event_id;
  void* user_data;
  void* data;
  int data_len;
};

typedef int esp_err_t;
static constexpr int ESP_OK = 0;
static constexpr int HTTP_EVENT_ON_DATA = 1;

struct esp_http_client_config_t {
  const char* url = nullptr;
  const char* method = nullptr;
  const char* username = nullptr;
  const char* password = nullptr;
  int auth_type = 0;
  bool skip_cert_common_name_check = false;
  void* crt_bundle_attach = nullptr;
  void (*event_handler)(esp_http_client_event_t*) = nullptr;
  void* user_data = nullptr;
  int timeout_ms = 5000;
  bool disable_auto_redirect = false;
};

typedef void* esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char* key, const char* value);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int content_length);
int esp_http_client_write(esp_http_client_handle_t client, const char* buffer, int len);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_read(esp_http_client_handle_t client, char* buffer, int len);
int esp_http_client_read_response(esp_http_client_handle_t client, char* buffer, int len);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int64_t esp_http_client_get_content_length(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);

// The stub implementations deliberately fail every network call: a host run must
// never look like a successful sync. See Arduino_stubs.cpp.
static esp_err_t stubFail(const char* what) {
  static_cast<void>(what);
  return -1;
}

// ---------------- logging ----------------

namespace anxlog {
void logImpl(const char* level, const char* module, const char* fmt, ...);
}

#define LOG_ERR(module, fmt, ...) anxlog::logImpl("E", module, fmt, ##__VA_ARGS__)
#define LOG_DBG(module, fmt, ...) \
  do {                            \
  } while (0)

// ---------------- HalStorage ----------------

typedef int oflag_t;
static constexpr int O_RDONLY = 0;

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  int read(void* buf, size_t count) {
    static_cast<void>(buf);
    static_cast<void>(count);
    return 0;
  }
  // The real HalFile also has the single-byte form and the Print override.
  // Neither is used by AnxSyncIo today, but omitting them made the stub a
  // narrower contract than the header it stands in for.
  int read() { return -1; }
  size_t write(const void* buf, size_t count) {
    static_cast<void>(buf);
    return count;
  }
  size_t write(uint8_t b) {
    static_cast<void>(b);
    return 1;
  }
  void flush() {}
  bool close() { return true; }
  bool isOpen() const { return true; }
  operator bool() const { return true; }
  size_t fileSize() { return 0; }
  bool seek(size_t pos) {
    static_cast<void>(pos);
    return true;
  }
};

class HalStorage {
 public:
  static HalStorage& getInstance();
  bool ready() const { return true; }
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  bool ensureDirectoryExists(const char* path);
  bool writeFile(const char* path, const String& content);
  String readFile(const char* path);

  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);

  // The real lib/hal/HalStorage.h also provides std::string overloads; the IO
  // layer uses them, so the stub must match or the code would compile on device
  // while failing here, which is the opposite of what a stub is for.
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
    return openFileForWrite(moduleName, path.c_str(), file);
  }
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
    return openFileForRead(moduleName, path.c_str(), file);
  }
  // The real HalStorage declares three overloads each: const char*, std::string
  // and Arduino String. The String ones matter: Arduino's String has no implicit
  // conversion to std::string, so code that passes a String compiles here only if
  // this overload exists. Leaving them out made the stub *more permissive* than
  // the real header, which is the opposite of what a stub should be.
  bool openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
    return openFileForWrite(moduleName, path.c_str(), file);
  }
  bool openFileForRead(const char* moduleName, const String& path, HalFile& file) {
    return openFileForRead(moduleName, path.c_str(), file);
  }
};

#define Storage HalStorage::getInstance()

// The firmware passes a version string through build flags; tests do not have it.
#ifndef CROSSPOINT_VERSION
#define CROSSPOINT_VERSION "host-stub"
#endif
