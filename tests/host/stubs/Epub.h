#pragma once

#include <string>

// Host stub for lib/Epub/Epub.h.
//
// Only the members AnxProgressStore::scanReaderPositions() needs. The real header
// pulls in SdFat, the zip reader, BookMetadataCache and the CSS parser, none of
// which build on a desktop.
//
// `load()` returns false on purpose, matching the other stubs in this directory:
// a host run must never look like it successfully mapped a book's position. That
// keeps IO LAYER SMOKE OK meaning "it compiles and links", not "the sync worked".
//
// The cache-path expression in the constructor matches the real Epub, so the
// string the code under test builds is the string the device would build - that
// part is genuinely exercised even though loading is not.

class Epub {
 public:
  explicit Epub(std::string filepath, const std::string& cacheDir);

  bool load(bool buildIfMissing = true, bool skipLoadingCss = false);

  const std::string& getCachePath() const { return cachePath; }
  const std::string& getPath() const { return filepath; }

  int getSpineItemsCount() const;
  size_t getCumulativeSpineItemSize(int spineIndex) const;

 private:
  std::string filepath;
  std::string cachePath;
};
