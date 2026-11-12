#include "AnxPropfindParser.h"

#include <cctype>
#include <cstring>

#include "AnxHttpDate.h"

namespace anx {

namespace {

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool ciEqual(const char* a, const char* b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

/** Case-insensitive "does haystack start with needle" over a bounded buffer. */
bool startsWithNoCase(const char* hay, size_t hayLen, const char* needle) {
  const size_t n = std::strlen(needle);
  if (hayLen < n) return false;
  return ciEqual(hay, needle, n);
}

/**
 * Locate the local name of a tag: the part after any namespace prefix.
 * "D:getlastmodified" -> ("getlastmodified", 15)
 */
struct LocalName {
  const char* ptr;
  size_t len;
};

LocalName localNameOf(const char* tag, size_t tagLen) {
  LocalName result{tag, tagLen};
  for (size_t i = 0; i < tagLen; ++i) {
    if (tag[i] == ':') {
      result.ptr = tag + i + 1;
      result.len = tagLen - i - 1;
      break;
    }
  }
  return result;
}

bool localNameEquals(const LocalName& name, const char* expected) {
  const size_t n = std::strlen(expected);
  if (name.len != n) return false;
  return ciEqual(name.ptr, expected, n);
}

/**
 * Advance past the opening tag beginning at `p` (which points at '<').
 * Returns a pointer just past the closing '>' of that tag, or nullptr if the
 * buffer ends first. Handles self-closing tags the same way — the caller checks
 * whether the character before '>' was '/'.
 */
const char* skipTag(const char* p, const char* end, bool& selfClosing) {
  selfClosing = false;
  if (p >= end || *p != '<') return nullptr;
  const char* q = p + 1;
  while (q < end && *q != '>') {
    if (q + 1 < end && q[0] == '/' && q[1] == '>') {
      selfClosing = true;
      return q + 2;
    }
    ++q;
  }
  if (q >= end) return nullptr;
  return q + 1;
}

/** Skip whitespace and XML declaration / comments / PIs before a tag. */
const char* skipMisc(const char* p, const char* end) {
  while (p < end) {
    if (std::isspace(static_cast<unsigned char>(*p)) != 0) {
      ++p;
      continue;
    }
    if (end - p >= 4 && std::strncmp(p, "<!--", 4) == 0) {
      const char* close = p + 4;
      while (close + 2 < end && !(close[0] == '-' && close[1] == '-' && close[2] == '>')) ++close;
      p = (close + 3 <= end) ? close + 3 : end;
      continue;
    }
    if (end - p >= 2 && p[0] == '<' && p[1] == '?') {
      const char* close = p + 2;
      while (close + 1 < end && !(close[0] == '?' && close[1] == '>')) ++close;
      p = (close + 2 <= end) ? close + 2 : end;
      continue;
    }
    break;
  }
  return p;
}

/** Read the text content of the element whose opening tag starts at `p`. */
std::string readElementText(const char* p, const char* end, bool& ok) {
  ok = false;
  bool selfClosing = false;
  const char* afterOpen = skipTag(p, end, selfClosing);
  if (afterOpen == nullptr) return {};
  if (selfClosing) {
    ok = true;
    return {};
  }
  const char* textStart = afterOpen;
  const char* close = afterOpen;
  while (close + 1 < end && !(close[0] == '<' && close[1] == '/')) ++close;
  if (close + 1 >= end) return {};

  std::string text(textStart, static_cast<size_t>(close - textStart));
  // Unescape the XML entities that appear in hrefs and titles.
  std::string unescaped;
  unescaped.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '&') {
      if (text.compare(i, 5, "&amp;") == 0) {
        unescaped.push_back('&');
        i += 4;
        continue;
      }
      if (text.compare(i, 4, "&lt;") == 0) {
        unescaped.push_back('<');
        i += 3;
        continue;
      }
      if (text.compare(i, 4, "&gt;") == 0) {
        unescaped.push_back('>');
        i += 3;
        continue;
      }
      if (text.compare(i, 6, "&quot;") == 0) {
        unescaped.push_back('"');
        i += 5;
        continue;
      }
      if (text.compare(i, 6, "&apos;") == 0) {
        unescaped.push_back('\'');
        i += 5;
        continue;
      }
    }
    unescaped.push_back(text[i]);
  }
  ok = true;
  return unescaped;
}

std::string trimAscii(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
  return s.substr(b, e - b);
}

}  // namespace

std::string PropfindParser::percentDecode(const std::string& in, bool decodeSlashes) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%' && i + 2 < in.size()) {
      const int hi = hexDigit(in[i + 1]);
      const int lo = hexDigit(in[i + 2]);
      if (hi >= 0 && lo >= 0) {
        const char decoded = static_cast<char>((hi << 4) | lo);
        if (decoded == '/' && !decodeSlashes) {
          out += "%2F";  // keep it encoded: it is part of a name, not a separator
        } else {
          out.push_back(decoded);
        }
        i += 2;
        continue;
      }
    }
    out.push_back(in[i]);
  }
  return out;
}

std::string PropfindParser::lastSegment(const std::string& href) {
  // Strip a trailing slash first so "/a/b/" yields "b".
  size_t end = href.size();
  while (end > 0 && href[end - 1] == '/') --end;
  if (end == 0) return {};
  size_t slash = href.rfind('/', end - 1);
  const size_t begin = (slash == std::string::npos) ? 0 : slash + 1;
  return percentDecode(href.substr(begin, end - begin));
}

bool PropfindParser::parse(const std::string& xml, std::vector<PropfindEntry>& out) {
  return parse(xml.data(), xml.size(), out);
}

bool PropfindParser::parse(const char* xml, size_t length, std::vector<PropfindEntry>& out) {
  out.clear();
  if (xml == nullptr || length == 0) return false;

  const char* p = xml;
  const char* end = xml + length;

  p = skipMisc(p, end);
  if (p >= end || *p != '<') return false;

  // Require a multistatus document root. Verifying the root name (rather than
  // accepting any XML) is what distinguishes "empty directory" from
  // "this is a login page / error page that happens to start with '<'".
  {
    const char* rootStart = p + 1;
    const char* rootEnd = rootStart;
    while (rootEnd < end && *rootEnd != '>' && !std::isspace(static_cast<unsigned char>(*rootEnd)) &&
           *rootEnd != '/') {
      ++rootEnd;
    }
    const LocalName rootName = localNameOf(rootStart, static_cast<size_t>(rootEnd - rootStart));
    if (!localNameEquals(rootName, "multistatus")) return false;
  }

  bool sawResponse = false;

  while (p < end) {
    p = skipMisc(p, end);
    if (p >= end || *p != '<') break;

    // Detect <response ...>
    const char* tagStart = p + 1;
    if (tagStart < end && *tagStart == '/') {  // closing tag of an enclosing element
      bool dummy = false;
      p = skipTag(p, end, dummy);
      if (p == nullptr) break;
      continue;
    }

    const char* tagEnd = tagStart;
    while (tagEnd < end && *tagEnd != '>' && !std::isspace(static_cast<unsigned char>(*tagEnd))) ++tagEnd;
    const LocalName tagName = localNameOf(tagStart, static_cast<size_t>(tagEnd - tagStart));

    if (!localNameEquals(tagName, "response")) {
      bool dummy = false;
      const char* next = skipTag(p, end, dummy);
      if (next == nullptr) break;
      p = next;
      continue;
    }

    // ---- Parse one <response> block ----
    sawResponse = true;
    PropfindEntry entry;
    bool inPropstat = false;

    bool selfClosing = false;
    p = skipTag(p, end, selfClosing);
    if (p == nullptr) return false;
    if (selfClosing) continue;  // <response/> carries nothing

    // Walk until the matching </response>.
    while (p < end) {
      p = skipMisc(p, end);
      if (p >= end || *p != '<') break;

      const char* inner = p + 1;
      if (inner < end && *inner == '/') {
        // A closing tag: is it </response>?
        const char* nameStart = inner + 1;
        const char* nameEnd = nameStart;
        while (nameEnd < end && *nameEnd != '>' && !std::isspace(static_cast<unsigned char>(*nameEnd))) ++nameEnd;
        const LocalName closing = localNameOf(nameStart, static_cast<size_t>(nameEnd - nameStart));
        bool dummy = false;
        const char* next = skipTag(p, end, dummy);
        if (next == nullptr) { p = end; break; }
        p = next;
        if (localNameEquals(closing, "response")) break;
        if (localNameEquals(closing, "propstat")) inPropstat = false;
        continue;
      }

      const char* elEnd = inner;
      while (elEnd < end && *elEnd != '>' && !std::isspace(static_cast<unsigned char>(*elEnd))) ++elEnd;
      const LocalName elName = localNameOf(inner, static_cast<size_t>(elEnd - inner));

      if (localNameEquals(elName, "propstat")) {
        inPropstat = true;
        bool sc = false;
        const char* next = skipTag(p, end, sc);
        if (next == nullptr) { p = end; break; }
        p = next;
        continue;
      }

      if (localNameEquals(elName, "href") && entry.href.empty() && !inPropstat) {
        bool ok = false;
        std::string text = readElementText(p, end, ok);
        if (ok) entry.href = trimAscii(text);
        bool sc = false;
        const char* next = skipTag(p, end, sc);
        if (next == nullptr) { p = end; break; }
        p = next;
        if (!sc) {
          // Skip the content and its closing tag.
          while (p + 1 < end && !(p[0] == '<' && p[1] == '/')) ++p;
          bool d2 = false;
          const char* after = skipTag(p, end, d2);
          p = (after == nullptr) ? end : after;
        }
        continue;
      }

      if (localNameEquals(elName, "getcontentlength")) {
        bool ok = false;
        const std::string text = trimAscii(readElementText(p, end, ok));
        bool sc = false;
        const char* next = skipTag(p, end, sc);
        if (next == nullptr) { p = end; break; }
        p = next;
        if (!sc) {
          while (p + 1 < end && !(p[0] == '<' && p[1] == '/')) ++p;
          bool d2 = false;
          const char* after = skipTag(p, end, d2);
          p = (after == nullptr) ? end : after;
        }
        if (ok && !text.empty()) {
          int64_t value = 0;
          bool numeric = true;
          for (char c : text) {
            if (c < '0' || c > '9') { numeric = false; break; }
            value = value * 10 + (c - '0');
          }
          if (numeric) entry.contentLength = value;
        }
        continue;
      }

      if (localNameEquals(elName, "getlastmodified")) {
        bool ok = false;
        const std::string text = readElementText(p, end, ok);
        bool sc = false;
        const char* next = skipTag(p, end, sc);
        if (next == nullptr) { p = end; break; }
        p = next;
        if (!sc) {
          while (p + 1 < end && !(p[0] == '<' && p[1] == '/')) ++p;
          bool d2 = false;
          const char* after = skipTag(p, end, d2);
          p = (after == nullptr) ? end : after;
        }
        int64_t utc = 0;
        if (ok && HttpDate::parseRfc1123(trimAscii(text).c_str(), utc)) {
          entry.lastModified = utc;
          entry.hasLastModified = true;
        }
        continue;
      }

      if (localNameEquals(elName, "resourcetype")) {
        bool sc = false;
        const char* next = skipTag(p, end, sc);
        if (next == nullptr) { p = end; break; }
        p = next;
        if (!sc) {
          // Scan the children for <collection/> (or <collection>).
          const char* scan = p;
          while (scan + 1 < end && !(scan[0] == '<' && scan[1] == '/')) {
            if (*scan == '<') {
              const char* childStart = scan + 1;
              const char* childEnd = childStart;
              while (childEnd < end && *childEnd != '>' && !std::isspace(static_cast<unsigned char>(*childEnd)) &&
                     *childEnd != '/') {
                ++childEnd;
              }
              const LocalName child = localNameOf(childStart, static_cast<size_t>(childEnd - childStart));
              if (localNameEquals(child, "collection")) entry.isCollection = true;
            }
            ++scan;
          }
          while (p + 1 < end && !(p[0] == '<' && p[1] == '/')) ++p;
          bool d2 = false;
          const char* after = skipTag(p, end, d2);
          p = (after == nullptr) ? end : after;
        }
        continue;
      }

      // Uninteresting element: skip over it (and its content, if any).
      bool sc = false;
      const char* next = skipTag(p, end, sc);
      if (next == nullptr) { p = end; break; }
      p = next;
      if (!sc) {
        while (p + 1 < end && !(p[0] == '<' && p[1] == '/')) {
          if (*p == '<') break;  // nested element, let the outer loop handle it
          ++p;
        }
        if (p + 1 < end && p[0] == '<' && p[1] == '/') {
          bool d2 = false;
          const char* after = skipTag(p, end, d2);
          p = (after == nullptr) ? end : after;
        }
      }
    }

    if (entry.href.empty()) {
      // A response without an href is useless to us; skip it rather than emit
      // an entry that would map to an empty path.
      continue;
    }
    entry.name = lastSegment(entry.href);
    out.push_back(entry);
  }

  // Reaching here means the document root was <multistatus>, so the response is
  // structurally valid even when it contained no <response> elements (an empty
  // directory legitimately yields zero entries).
  (void)sawResponse;
  return true;
}

}  // namespace anx
