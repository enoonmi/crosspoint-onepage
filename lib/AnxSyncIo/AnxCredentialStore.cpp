#include "AnxCredentialStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <string>

// NOTE: not host-compiled: depends on HalStorage (SD card). It *is* compile-checked
// against tests/host/stubs by tests/host/check_io_layer.py, together with a smoke
// test of the URL normalisation and credential gating below.
//
// Password handling matches the bar the rest of this firmware sets: stored as
// written, protected only by whatever protects the SD card. OpdsServerStore goes
// one step further and XORs with the device MAC; doing that here would need the
// MAC, which pulls in WiFi headers, so it is left as a follow-up rather than
// shipped half done.

namespace {

std::string escapeForJson(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

/**
 * Pull a string field out of a flat JSON object.
 *
 * Deliberately tiny: this file is written by this class and holds four fields.
 * The general-purpose parser in lib/JsonParser is streaming and callback based,
 * which is more machinery than the job needs.
 */
bool extractString(const std::string& json, const char* key, std::string& out) {
  const std::string needle = std::string("\"") + key + "\"";
  const size_t keyPos = json.find(needle);
  if (keyPos == std::string::npos) return false;
  size_t pos = json.find(':', keyPos + needle.size());
  if (pos == std::string::npos) return false;
  pos = json.find('"', pos);
  if (pos == std::string::npos) return false;
  ++pos;

  out.clear();
  while (pos < json.size()) {
    const char c = json[pos];
    if (c == '"') return true;
    if (c == '\\' && pos + 1 < json.size()) {
      switch (json[pos + 1]) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        default: out.push_back(json[pos + 1]); break;
      }
      pos += 2;
      continue;
    }
    out.push_back(c);
    ++pos;
  }
  return false;  // unterminated string
}

bool extractBool(const std::string& json, const char* key, bool& out) {
  const std::string needle = std::string("\"") + key + "\"";
  const size_t keyPos = json.find(needle);
  if (keyPos == std::string::npos) return false;
  const size_t colon = json.find(':', keyPos + needle.size());
  if (colon == std::string::npos) return false;
  const size_t truePos = json.find("true", colon);
  const size_t falsePos = json.find("false", colon);
  if (truePos != std::string::npos && (falsePos == std::string::npos || truePos < falsePos)) {
    out = true;
    return true;
  }
  if (falsePos != std::string::npos) {
    out = false;
    return true;
  }
  return false;
}

}  // namespace

AnxCredentialStore& AnxCredentialStore::getInstance() {
  static AnxCredentialStore instance;
  return instance;
}

const char* AnxCredentialStore::settingsPath() { return "/.crosspoint/anx.json"; }

void AnxCredentialStore::setBaseUrl(const std::string& url) {
  std::string normalized = url;

  // Trim surrounding whitespace: a pasted URL routinely carries a trailing
  // space or newline, which would otherwise corrupt every request path.
  size_t begin = 0;
  while (begin < normalized.size() &&
         (normalized[begin] == ' ' || normalized[begin] == '\t' || normalized[begin] == '\r' ||
          normalized[begin] == '\n')) {
    ++begin;
  }
  size_t end = normalized.size();
  while (end > begin && (normalized[end - 1] == ' ' || normalized[end - 1] == '\t' ||
                         normalized[end - 1] == '\r' || normalized[end - 1] == '\n')) {
    --end;
  }
  normalized = normalized.substr(begin, end - begin);

  // Drop trailing slashes.
  while (!normalized.empty() && normalized.back() == '/') normalized.pop_back();

  // Drop a trailing "anx" segment. Anx Reader's own settings hint asks for the
  // writable directory and creates `anx/` underneath, but plenty of people paste
  // the sync folder itself. Without this, requests would go to
  // `.../anx/anx/data/file/` and 404 on every sync.
  const std::string suffix = "/anx";
  if (normalized.size() > suffix.size() &&
      normalized.compare(normalized.size() - suffix.size(), suffix.size(), suffix) == 0) {
    normalized.erase(normalized.size() - suffix.size());
  }

  baseUrl = normalized;
}

bool AnxCredentialStore::isConfigured() const {
  if (baseUrl.empty()) return false;

  const bool http = baseUrl.rfind("http://", 0) == 0;
  const bool https = baseUrl.rfind("https://", 0) == 0;
  if (!http && !https) return false;

  // "https://" with no host is not usable.
  const size_t schemeLen = https ? 8 : 7;
  return baseUrl.size() > schemeLen;
}

bool AnxCredentialStore::hasCredentials() const { return !username.empty() && !password.empty(); }

void AnxCredentialStore::clear() {
  baseUrl.clear();
  username.clear();
  password.clear();
  onlySyncOnWifi = true;
  autoSyncOnWake = false;
}

bool AnxCredentialStore::saveToFile() const {
  std::string json = "{\n";
  json += "  \"base_url\": \"" + escapeForJson(baseUrl) + "\",\n";
  json += "  \"username\": \"" + escapeForJson(username) + "\",\n";
  json += "  \"password\": \"" + escapeForJson(password) + "\",\n";
  json += std::string("  \"only_sync_on_wifi\": ") + (onlySyncOnWifi ? "true" : "false") + ",\n";
  json += std::string("  \"auto_sync_on_wake\": ") + (autoSyncOnWake ? "true" : "false") + "\n";
  json += "}\n";

  if (!Storage.ready()) {
    LOG_ERR("AnxSync", "saveToFile: storage not ready");
    return false;
  }
  // The settings file lives beside the EPUB cache. HalStorage::writeFile does not
  // create parents, so make sure the directory exists first.
  if (!Storage.exists("/.crosspoint")) {
    if (!Storage.ensureDirectoryExists("/.crosspoint")) {
      LOG_ERR("AnxSync", "saveToFile: cannot create /.crosspoint");
      return false;
    }
  }
  if (!Storage.writeFile(settingsPath(), String(json.c_str()))) {
    LOG_ERR("AnxSync", "saveToFile: write failed for %s", settingsPath());
    return false;
  }
  LOG_DBG("AnxSync", "settings saved (%u bytes)", static_cast<unsigned>(json.size()));
  return true;
}

bool AnxCredentialStore::loadFromFile() {
  if (!Storage.ready()) {
    LOG_ERR("AnxSync", "loadFromFile: storage not ready");
    return false;
  }
  if (!Storage.exists(settingsPath())) {
    // Not an error: a device that has never been configured simply has no file.
    LOG_DBG("AnxSync", "no settings file at %s", settingsPath());
    return false;
  }

  const String raw = Storage.readFile(settingsPath());
  if (raw.isEmpty()) {
    LOG_ERR("AnxSync", "loadFromFile: empty file at %s", settingsPath());
    return false;
  }

  const std::string json(raw.c_str());
  std::string url;
  if (extractString(json, "base_url", url)) {
    setBaseUrl(url);  // normalise again: the file may predate a rule change
  }
  std::string user;
  if (extractString(json, "username", user)) username = user;
  std::string pass;
  if (extractString(json, "password", pass)) password = pass;

  bool wifiOnly = onlySyncOnWifi;
  if (extractBool(json, "only_sync_on_wifi", wifiOnly)) onlySyncOnWifi = wifiOnly;
  bool autoSync = autoSyncOnWake;
  if (extractBool(json, "auto_sync_on_wake", autoSync)) autoSyncOnWake = autoSync;

  LOG_DBG("AnxSync", "settings loaded: url=%s configured=%d credentials=%d", baseUrl.c_str(),
          isConfigured() ? 1 : 0, hasCredentials() ? 1 : 0);
  return true;
}
