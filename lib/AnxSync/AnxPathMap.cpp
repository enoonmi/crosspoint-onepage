#include "AnxPathMap.h"

#include <cctype>
#include <functional>
#include <string>
#include <vector>

namespace anx {

namespace {

bool hasUnsafeComponent(const std::string& name) {
  if (name.empty()) return true;
  if (name == "." || name == "..") return true;
  for (char c : name) {
    // Separators and NUL would let a crafted name escape /Books.
    if (c == '/' || c == '\\' || c == '\0') return true;
  }
  // A leading dot means a hidden or partial file (".davtmp", ".DS_Store").
  if (name[0] == '.') return true;
  return false;
}

bool endsWithNoCase(const std::string& value, const char* suffix) {
  const size_t n = std::char_traits<char>::length(suffix);
  if (value.size() < n) return false;
  const size_t offset = value.size() - n;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(value[offset + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

/** Split on '/' and drop empty segments. */
std::vector<std::string> splitPath(const std::string& path) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : path) {
    if (c == '/' || c == '\\') {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

}  // namespace

const char* PathMap::booksDir() { return "/Books"; }

const char* PathMap::remoteBooksDir() { return "anx/data/file"; }

const char* PathMap::remoteProgressPath() { return "anx/onepage/progress.json"; }

bool PathMap::isAcceptableBookName(const std::string& name) {
  if (hasUnsafeComponent(name)) return false;
  if (!endsWithNoCase(name, ".epub")) return false;
  // Reject degenerate names that are only (or nearly only) the extension.
  if (name.size() <= 5) return false;
  return true;
}

bool PathMap::localBookPath(const std::string& name, std::string& out) {
  if (!isAcceptableBookName(name)) return false;
  out = std::string(booksDir()) + "/" + name;
  return true;
}

bool PathMap::localTempBookPath(const std::string& name, std::string& out) {
  if (!isAcceptableBookName(name)) return false;
  out = std::string(booksDir()) + "/." + name + ".part";
  return true;
}

bool PathMap::localBackupBookPath(const std::string& name, std::string& out) {
  if (!isAcceptableBookName(name)) return false;
  out = std::string(booksDir()) + "/." + name + ".old";
  return true;
}

const char* PathMap::cacheDir() { return "/.crosspoint"; }

std::string PathMap::readerCachePathFor(const std::string& bookPath) {
  // Byte-for-byte the expression in Epub's constructor:
  //   cachePath = cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(filepath));
  // Any difference here and the scan looks in a directory that does not exist,
  // which reads as "this book was never opened" rather than as an error.
  return std::string(cacheDir()) + "/epub_" +
         std::to_string(std::hash<std::string>{}(bookPath));
}

std::string PathMap::stripBaseUrl(const std::string& href) {
  // Look for "://" and take everything from the first '/' after the authority.
  const size_t schemeEnd = href.find("://");
  if (schemeEnd == std::string::npos) return href;

  const size_t authorityStart = schemeEnd + 3;
  const size_t pathStart = href.find('/', authorityStart);
  if (pathStart == std::string::npos) return "/";
  return href.substr(pathStart);
}

bool PathMap::isInsideRoot(const std::string& root, const std::string& path) {
  const std::vector<std::string> rootParts = splitPath(root);
  const std::vector<std::string> pathParts = splitPath(path);

  if (rootParts.empty()) return false;

  // A path must live *strictly inside* the root: the root itself is not a file
  // location, and a relative path ("Books/a.epub") is ambiguous with respect to
  // the current directory, so both are rejected.
  if (!path.empty() && path[0] != '/') return false;
  if (pathParts.size() <= rootParts.size()) return false;

  size_t depth = 0;
  for (const std::string& part : pathParts) {
    if (part == "..") {
      if (depth == 0) return false;  // climbed above the filesystem root
      --depth;
      continue;
    }
    if (part == ".") continue;
    if (depth < rootParts.size()) {
      // Must match the root prefix exactly while we are still inside it.
      if (part != rootParts[depth]) return false;
    }
    ++depth;
  }

  return depth >= rootParts.size();
}

}  // namespace anx
