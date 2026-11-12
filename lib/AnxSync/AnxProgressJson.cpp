#include "AnxProgressJson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "AnxHttpDate.h"

namespace anx {

namespace {

void appendEscaped(std::string& out, const std::string& value) {
  out.push_back('"');
  for (char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
}

std::string formatPercentage(double value) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6f", value);
  std::string text(buf);
  const size_t dot = text.find('.');
  if (dot != std::string::npos) {
    size_t last = text.find_last_not_of('0');
    if (last != std::string::npos && last > dot) {
      text.erase(last + 1);
    } else if (last == dot) {
      text.push_back('0');
    }
  }
  return text;
}

void skipWs(const std::string& s, size_t& i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

bool expectChar(const std::string& s, size_t& i, char expected) {
  skipWs(s, i);
  if (i >= s.size() || s[i] != expected) return false;
  ++i;
  return true;
}

bool parseString(const std::string& s, size_t& i, std::string& out) {
  skipWs(s, i);
  if (i >= s.size() || s[i] != '"') return false;
  ++i;
  out.clear();
  while (i < s.size()) {
    const char c = s[i];
    if (c == '"') {
      ++i;
      return true;
    }
    if (c == '\\') {
      if (i + 1 >= s.size()) return false;
      const char esc = s[i + 1];
      switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'u': {
          if (i + 5 >= s.size()) return false;
          unsigned code = 0;
          for (int k = 0; k < 4; ++k) {
            const char h = s[i + 2 + k];
            unsigned digit = 0;
            if (h >= '0' && h <= '9') digit = static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') digit = static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') digit = static_cast<unsigned>(h - 'A' + 10);
            else return false;
            code = code * 16 + digit;
          }
          i += 4;
          if (code < 0x80) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          break;
        }
        default: return false;
      }
      i += 2;
      continue;
    }
    out.push_back(c);
    ++i;
  }
  return false;
}

bool parseNumber(const std::string& s, size_t& i, double& out) {
  skipWs(s, i);
  if (i < s.size() && s[i] == '"') {
    std::string text;
    if (!parseString(s, i, text)) return false;
    if (text.empty()) return false;
    char* endptr = nullptr;
    const double value = std::strtod(text.c_str(), &endptr);
    if (endptr == text.c_str() || (endptr != nullptr && *endptr != '\0')) return false;
    out = value;
    return true;
  }
  const size_t start = i;
  if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
  bool digits = false;
  while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                          s[i] == '-' || s[i] == '+')) {
    if (s[i] >= '0' && s[i] <= '9') digits = true;
    ++i;
  }
  if (!digits) return false;
  const std::string text = s.substr(start, i - start);
  char* endptr = nullptr;
  const double value = std::strtod(text.c_str(), &endptr);
  if (endptr == text.c_str()) return false;
  out = value;
  return true;
}

bool parseInteger(const std::string& s, size_t& i, int64_t& out) {
  double value = 0;
  if (!parseNumber(s, i, value)) return false;
  out = static_cast<int64_t>(value);
  return true;
}

bool skipValue(const std::string& s, size_t& i) {
  skipWs(s, i);
  if (i >= s.size()) return false;
  const char c = s[i];
  if (c == '"') {
    std::string ignored;
    return parseString(s, i, ignored);
  }
  if (c == '{' || c == '[') {
    const char open = c;
    const char close = (c == '{') ? '}' : ']';
    int depth = 0;
    while (i < s.size()) {
      if (s[i] == open) ++depth;
      else if (s[i] == close) {
        --depth;
        if (depth == 0) {
          ++i;
          return true;
        }
      } else if (s[i] == '"') {
        std::string ignored;
        if (!parseString(s, i, ignored)) return false;
        continue;
      }
      ++i;
    }
    return false;
  }
  const size_t start = i;
  while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' &&
         s[i] != '\r' && s[i] != '\t') {
    ++i;
  }
  return i > start;
}

bool parseBookObject(const std::string& s, size_t& i, BookProgress& out) {
  if (!expectChar(s, i, '{')) return false;

  bool havePath = false;
  bool havePercentage = false;

  skipWs(s, i);
  if (i < s.size() && s[i] == '}') {
    ++i;
    return false;
  }

  while (i < s.size()) {
    std::string key;
    if (!parseString(s, i, key)) return false;
    if (!expectChar(s, i, ':')) return false;

    if (key == "file_path" || key == "filePath") {
      if (!parseString(s, i, out.filePath)) return false;
      havePath = true;
    } else if (key == "reading_percentage" || key == "percentage") {
      double value = 0;
      if (!parseNumber(s, i, value)) return false;
      out.percentage = ProgressJson::clampPercentage(value);
      havePercentage = true;
    } else if (key == "updated_at" || key == "updatedAt") {
      skipWs(s, i);
      if (i < s.size() && s[i] == '"') {
        std::string text;
        if (!parseString(s, i, text)) return false;
        int64_t utc = 0;
        if (HttpDate::parseRfc1123(text.c_str(), utc)) {
          out.updatedAt = utc;
        } else {
          char* endptr = nullptr;
          const long long numeric = std::strtoll(text.c_str(), &endptr, 10);
          out.updatedAt = (endptr != text.c_str()) ? static_cast<int64_t>(numeric) : 0;
        }
      } else {
        if (!parseInteger(s, i, out.updatedAt)) return false;
      }
    } else {
      if (!skipValue(s, i)) return false;
    }

    skipWs(s, i);
    if (i < s.size() && s[i] == ',') {
      ++i;
      continue;
    }
    if (i < s.size() && s[i] == '}') {
      ++i;
      break;
    }
    return false;
  }

  return havePath && havePercentage;
}

}  // namespace

const char* ProgressJson::defaultDeviceName() { return "onepage-c61"; }

double ProgressJson::clampPercentage(double value) {
  if (std::isnan(value)) return 0.0;
  if (value < 0.0) return 0.0;
  if (value > 1.0) return 1.0;
  return value;
}

std::string ProgressJson::write(const ProgressDocument& document) {
  std::string out;
  out.reserve(256 + document.books.size() * 128);

  out += "{\n";
  out += "  \"schema\": ";
  out += std::to_string(document.schema > 0 ? document.schema : kSchemaVersion);
  out += ",\n";

  out += "  \"device\": ";
  appendEscaped(out, document.device.empty() ? std::string(defaultDeviceName()) : document.device);
  out += ",\n";

  char stamp[40] = {0};
  if (HttpDate::formatRfc1123(document.updatedAt, stamp, sizeof(stamp))) {
    out += "  \"updated_at\": ";
    appendEscaped(out, std::string(stamp));
    out += ",\n";
  }
  out += "  \"updated_at_unix\": ";
  out += std::to_string(static_cast<long long>(document.updatedAt));
  out += ",\n";

  out += "  \"books\": [";
  for (size_t idx = 0; idx < document.books.size(); ++idx) {
    const BookProgress& book = document.books[idx];
    if (idx > 0) out += ",";
    out += "\n    {\"file_path\": ";
    appendEscaped(out, book.filePath);
    out += ", \"reading_percentage\": ";
    out += formatPercentage(clampPercentage(book.percentage));
    out += ", \"updated_at\": ";
    out += std::to_string(static_cast<long long>(book.updatedAt));
    out += "}";
  }
  if (!document.books.empty()) out += "\n  ";
  out += "]\n";
  out += "}\n";
  return out;
}

bool ProgressJson::parse(const std::string& json, ProgressDocument& out) {
  out = ProgressDocument{};

  size_t i = 0;
  if (!expectChar(json, i, '{')) return false;

  skipWs(json, i);
  if (i < json.size() && json[i] == '}') {
    ++i;
    return true;
  }

  while (i < json.size()) {
    std::string key;
    if (!parseString(json, i, key)) return false;
    if (!expectChar(json, i, ':')) return false;

    if (key == "schema") {
      int64_t value = 0;
      if (!parseInteger(json, i, value)) return false;
      out.schema = static_cast<int>(value);
    } else if (key == "device") {
      if (!parseString(json, i, out.device)) return false;
    } else if (key == "updated_at_unix" || key == "updated_at" || key == "updatedAt") {
      skipWs(json, i);
      if (i < json.size() && json[i] == '"') {
        std::string text;
        if (!parseString(json, i, text)) return false;
        int64_t utc = 0;
        if (HttpDate::parseRfc1123(text.c_str(), utc)) {
          if (out.updatedAt == 0) out.updatedAt = utc;
        }
      } else {
        int64_t value = 0;
        if (!parseInteger(json, i, value)) return false;
        out.updatedAt = value;
      }
    } else if (key == "books") {
      if (!expectChar(json, i, '[')) return false;
      skipWs(json, i);
      if (i < json.size() && json[i] == ']') {
        ++i;
      } else {
        while (i < json.size()) {
          BookProgress book;
          if (!parseBookObject(json, i, book)) return false;
          out.books.push_back(book);
          skipWs(json, i);
          if (i < json.size() && json[i] == ',') {
            ++i;
            continue;
          }
          if (i < json.size() && json[i] == ']') {
            ++i;
            break;
          }
          return false;
        }
      }
    } else {
      if (!skipValue(json, i)) return false;
    }

    skipWs(json, i);
    if (i < json.size() && json[i] == ',') {
      ++i;
      continue;
    }
    if (i < json.size() && json[i] == '}') {
      ++i;
      return true;
    }
    return false;
  }

  return false;
}

bool ProgressJson::upsert(ProgressDocument& document, const BookProgress& entry) {
  BookProgress normalized = entry;
  normalized.percentage = clampPercentage(entry.percentage);
  if (normalized.filePath.empty()) return false;

  for (BookProgress& existing : document.books) {
    if (existing.filePath != normalized.filePath) continue;
    if (normalized.updatedAt < existing.updatedAt) {
      return false;
    }
    existing = normalized;
    return true;
  }
  document.books.push_back(normalized);
  return true;
}

const BookProgress* ProgressJson::find(const ProgressDocument& document, const std::string& filePath) {
  for (const BookProgress& book : document.books) {
    if (book.filePath == filePath) return &book;
  }
  return nullptr;
}

}  // namespace anx
