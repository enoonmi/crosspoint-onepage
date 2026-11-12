#include "AnxProgressStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

#include <Epub.h>

#include "AnxPathMap.h"
#include "ProgressFile.h"

// NOTE: not host-compiled - depends on HalStorage and the reader's ProgressFile.
// It IS compile-checked against tests/host/stubs by tests/host/check_io_layer.py.
//
// The reader's own progress.bin layout (6 bytes, little-endian spine/page/pageCount)
// is documented in src/activities/reader/EpubReaderUtils.h and written through
// ProgressFile::writeAtomic, so this class reuses both rather than reimplementing
// the format. Getting that format wrong would silently corrupt the saved position.
//
// Both directions of the progress sync live here:
//   scanReaderPositions()  progress.bin -> journal -> upload
//   applyPhonePositions()  phone document -> progress.bin
// Both need Epub for the cumulative spine sizes; progress.bin stores a position,
// not a percentage, so the translation cannot be done without them.

namespace {

// Journal file magic + version, so a future change can be detected instead of
// misread as garbage.
constexpr uint8_t kJournalMagic[4] = {'A', 'N', 'X', 'P'};
constexpr uint8_t kJournalVersion = 1;
constexpr size_t kJournalHeaderSize = 5;  // magic + version

// A book path longer than this is not something Anx Reader produces; refusing it
// keeps a corrupt journal from allocating wildly.
constexpr size_t kMaxPathLen = 255;

uint16_t readU16(const uint8_t* data) { return static_cast<uint16_t>(data[0] | (data[1] << 8)); }

void appendU16(std::string& out, uint16_t value) {
  out.push_back(static_cast<char>(value & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void appendU32(std::string& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
}

void appendI64(std::string& out, int64_t value) {
  const uint64_t bits = static_cast<uint64_t>(value);
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
}

uint32_t readU32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

int64_t readI64(const uint8_t* data) {
  uint64_t bits = 0;
  for (int i = 0; i < 8; ++i) bits |= static_cast<uint64_t>(data[i]) << (8 * i);
  return static_cast<int64_t>(bits);
}

}  // namespace

AnxProgressStore& AnxProgressStore::getInstance() {
  static AnxProgressStore instance;
  return instance;
}

const char* AnxProgressStore::journalPath() { return "/.crosspoint/anx_progress.bin"; }

AnxProgressStore::Entry* AnxProgressStore::findMutable(const std::string& filePath) {
  for (Entry& entry : journal) {
    if (entry.filePath == filePath) return &entry;
  }
  return nullptr;
}

void AnxProgressStore::clear() { journal.clear(); }

bool AnxProgressStore::record(const std::string& filePath, double percentage, int64_t now) {
  if (filePath.empty()) return false;
  const double clamped = anx::ProgressJson::clampPercentage(percentage);

  Entry* existing = findMutable(filePath);
  if (existing == nullptr) {
    journal.push_back(Entry{filePath, clamped, now});
    return true;
  }

  // Only bump the timestamp when the position actually moved. Without this, every
  // wake-up and every idle page-turn would mark the whole library as changed and
  // the upload would resend everything on each sync.
  constexpr double kSamePosition = 0.001;
  if (std::fabs(existing->percentage - clamped) < kSamePosition) {
    return false;
  }
  existing->percentage = clamped;
  existing->updatedAt = now;
  return true;
}

bool AnxProgressStore::readReaderPosition(const std::string& cachePath, int& spineIndex, int& pageNumber,
                                          int& totalPages) const {
  const std::string path = cachePath + "/progress.bin";
  if (!Storage.exists(path.c_str())) return false;

  HalFile file;
  if (!Storage.openFileForRead("AnxSync", path, file)) return false;

  uint8_t data[6] = {0};
  const int read = file.read(data, sizeof(data));
  if (read != static_cast<int>(sizeof(data))) {
    LOG_ERR("AnxSync", "progress.bin short read for %s (%d bytes)", cachePath.c_str(), read);
    return false;
  }

  spineIndex = readU16(data + 0);
  pageNumber = readU16(data + 2);
  totalPages = readU16(data + 4);
  return true;
}

bool AnxProgressStore::save() const {
  if (!Storage.ready()) {
    LOG_ERR("AnxSync", "save journal: storage not ready");
    return false;
  }
  if (!Storage.exists("/.crosspoint")) {
    if (!Storage.ensureDirectoryExists("/.crosspoint")) {
      LOG_ERR("AnxSync", "save journal: cannot create /.crosspoint");
      return false;
    }
  }

  std::string blob;
  blob.reserve(kJournalHeaderSize + journal.size() * 64);
  blob.append(reinterpret_cast<const char*>(kJournalMagic), sizeof(kJournalMagic));
  blob.push_back(static_cast<char>(kJournalVersion));
  appendU16(blob, static_cast<uint16_t>(journal.size()));

  for (const Entry& entry : journal) {
    const uint16_t pathLen = static_cast<uint16_t>(std::min(entry.filePath.size(), kMaxPathLen));
    appendU16(blob, pathLen);
    const uint8_t flags = 0;  // reserved for future use
    blob.push_back(static_cast<char>(flags));
    appendI64(blob, entry.updatedAt);
    // Percentage is stored as a 32-bit float scaled to [0,1]; four bytes is plenty
    // for a position that is later quantised to a page anyway.
    const float asFloat = static_cast<float>(entry.percentage);
    uint32_t bits = 0;
    std::memcpy(&bits, &asFloat, sizeof(bits));
    appendU32(blob, bits);
    blob.append(entry.filePath.data(), pathLen);
  }

  const std::string finalPath = journalPath();
  const std::string tmpPath = finalPath + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite("AnxSync", tmpPath, file)) {
      LOG_ERR("AnxSync", "save journal: cannot open temp file");
      return false;
    }
    const size_t written = file.write(blob.data(), blob.size());
    if (written != blob.size()) {
      LOG_ERR("AnxSync", "save journal: short write %u/%u", static_cast<unsigned>(written),
              static_cast<unsigned>(blob.size()));
      return false;
    }
    file.flush();
  }

  // Same crash-safety argument as ProgressFile::writeAtomic: the canonical file is
  // only ever replaced by a fully written one.
  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("AnxSync", "save journal: rename failed");
    return false;
  }
  LOG_DBG("AnxSync", "journal saved (%u entries)", static_cast<unsigned>(journal.size()));
  return true;
}

bool AnxProgressStore::load() {
  journal.clear();
  if (!Storage.ready()) return false;

  const std::string path = journalPath();
  if (!Storage.exists(path.c_str())) return false;  // nothing synced yet

  HalFile file;
  if (!Storage.openFileForRead("AnxSync", path, file)) return false;
  const size_t size = file.fileSize();
  if (size < kJournalHeaderSize) {
    LOG_ERR("AnxSync", "journal too small (%u bytes)", static_cast<unsigned>(size));
    return false;
  }

  std::string blob(size, '\0');
  if (file.read(&blob[0], size) != static_cast<int>(size)) {
    LOG_ERR("AnxSync", "journal short read");
    return false;
  }

  const uint8_t* data = reinterpret_cast<const uint8_t*>(blob.data());
  if (std::memcmp(data, kJournalMagic, sizeof(kJournalMagic)) != 0) {
    LOG_ERR("AnxSync", "journal magic mismatch");
    return false;
  }
  if (data[4] != kJournalVersion) {
    LOG_ERR("AnxSync", "journal version %u unsupported", data[4]);
    return false;
  }

  size_t offset = kJournalHeaderSize;
  const uint16_t count = (offset + 2 <= size) ? readU16(data + offset) : 0;
  offset += 2;

  journal.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    if (offset + 2 + 1 + 8 + 4 > size) {
      LOG_ERR("AnxSync", "journal truncated at entry %u", i);
      break;
    }
    const uint16_t pathLen = readU16(data + offset);
    offset += 2;
    offset += 1;  // flags
    const int64_t updatedAt = readI64(data + offset);
    offset += 8;
    const uint32_t bits = readU32(data + offset);
    offset += 4;

    if (offset + pathLen > size) {
      LOG_ERR("AnxSync", "journal path overflow at entry %u", i);
      break;
    }
    float asFloat = 0.0f;
    std::memcpy(&asFloat, &bits, sizeof(asFloat));

    Entry entry;
    entry.filePath.assign(blob.data() + offset, pathLen);
    entry.percentage = anx::ProgressJson::clampPercentage(asFloat);
    entry.updatedAt = updatedAt;
    offset += pathLen;
    journal.push_back(entry);
  }

  LOG_DBG("AnxSync", "journal loaded (%u entries)", static_cast<unsigned>(journal.size()));
  return true;
}

int AnxProgressStore::scanReaderPositions(int64_t now) {
  if (!Storage.ready()) return 0;

  // The cache directory the reader uses. Epub builds its per-book path as
  // cacheDir + "/epub_" + std::hash<std::string>{}(filepath), so the mapping from
  // a book to its progress.bin exists only in that hash. Rebuilding it here with
  // the same expression is what makes the progress direction work at all: nothing
  // in progress.bin says which book it belongs to.
  static constexpr const char* kCacheDir = "/.crosspoint";

  const auto names = Storage.listFiles(anx::PathMap::booksDir(), 500);
  int changed = 0;

  for (const auto& name : names) {
    const std::string bookName(name.c_str());
    if (!anx::PathMap::isAcceptableBookName(bookName)) continue;

    std::string bookPath;
    if (!anx::PathMap::localBookPath(bookName, bookPath)) continue;

    const std::string cachePath =
        std::string(kCacheDir) + "/epub_" + std::to_string(std::hash<std::string>{}(bookPath));

    int spineIndex = 0;
    int pageNumber = 0;
    int totalPages = 0;
    if (!readReaderPosition(cachePath, spineIndex, pageNumber, totalPages)) {
      // Never opened in the reader, or the position was cleared. Not an error.
      continue;
    }
    if (totalPages <= 0) continue;

    // The percentage is book-wide, so the spine table is needed. Opening the EPUB
    // costs a metadata read (not the whole file), and a book the reader has
    // touched has already been parsed once, so its cache is warm. A book that
    // fails to open is skipped rather than aborting the sync: one unreadable
    // file must not stop the progress of every other book.
    Epub epub(bookPath, kCacheDir);
    if (!epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true)) {
      LOG_ERR("AnxSync", "cannot open %s for progress mapping", bookName.c_str());
      continue;
    }

    const int spineCount = epub.getSpineItemsCount();
    if (spineIndex < 0 || spineIndex >= spineCount) continue;

    std::vector<uint32_t> cumulative;
    cumulative.reserve(static_cast<size_t>(spineCount));
    for (int i = 0; i < spineCount; ++i) {
      cumulative.push_back(static_cast<uint32_t>(epub.getCumulativeSpineItemSize(i)));
    }

    const anx::ProgressBridge::DevicePosition position{spineIndex, pageNumber, totalPages};
    const double percentage = anx::ProgressBridge::positionToPercentage(cumulative, position);

    // record() only reports a change when the position actually moved, so a book
    // nobody read this session does not mark itself as modified.
    if (record("file/" + bookName, percentage, now)) ++changed;
  }

  LOG_DBG("AnxSync", "scanned positions: %d changed", changed);
  return changed;
}

anx::ProgressDocument AnxProgressStore::buildUpload(const anx::ProgressDocument& phone,
                                                    const std::string& deviceName, int64_t now) const {
  std::vector<anx::BookProgress> asBookProgress;
  asBookProgress.reserve(journal.size());
  for (const Entry& entry : journal) {
    asBookProgress.push_back(anx::BookProgress{entry.filePath, entry.percentage, entry.updatedAt});
  }
  return anx::ProgressBridge::buildUploadDocument(asBookProgress, phone, deviceName, now);
}

int AnxProgressStore::mergeFromPhone(const anx::ProgressDocument& phone) {
  std::vector<anx::BookProgress> asBookProgress;
  asBookProgress.reserve(journal.size());
  for (const Entry& entry : journal) {
    asBookProgress.push_back(anx::BookProgress{entry.filePath, entry.percentage, entry.updatedAt});
  }

  std::vector<std::string> winners;
  anx::ProgressBridge::mergeFromPhone(phone, asBookProgress, winners);

  int changed = 0;
  for (const std::string& filePath : winners) {
    const anx::BookProgress* phoneEntry = anx::ProgressJson::find(phone, filePath);
    if (phoneEntry == nullptr) continue;

    Entry* existing = findMutable(filePath);
    if (existing == nullptr) {
      journal.push_back(Entry{filePath, phoneEntry->percentage, phoneEntry->updatedAt});
      ++changed;
      continue;
    }
    // Adopt both the value and the phone's timestamp, so the next upload does not
    // immediately echo it back.
    existing->percentage = phoneEntry->percentage;
    existing->updatedAt = phoneEntry->updatedAt;
    ++changed;
  }
  return changed;
}

int AnxProgressStore::applyPhonePositions(const anx::ProgressDocument& phone) {
  if (!Storage.ready()) return 0;
  if (phone.books.empty()) return 0;

  static constexpr const char* kCacheDir = "/.crosspoint";
  int rewritten = 0;

  for (const anx::BookProgress& entry : phone.books) {
    // file_path is "file/<name>"; the SD card has it at /Books/<name>. Stripping
    // the prefix is all that is needed - localBookPath rebuilds the rest, and the
    // name rules below reject anything that is not a book.
    static constexpr const char* kAnxPrefix = "file/";
    if (entry.filePath.rfind(kAnxPrefix, 0) != 0) continue;
    const std::string bookName = entry.filePath.substr(std::strlen(kAnxPrefix));
    if (!anx::PathMap::isAcceptableBookName(bookName)) continue;

    std::string bookPath;
    if (!anx::PathMap::localBookPath(bookName, bookPath)) continue;
    if (!Storage.exists(bookPath.c_str())) continue;  // book not on this device

    const std::string cachePath =
        std::string(kCacheDir) + "/epub_" + std::to_string(std::hash<std::string>{}(bookPath));

    // The spine table is needed to turn a book-wide percentage back into a
    // (spine, page) pair, exactly as scanReaderPositions() needs it the other way.
    Epub epub(bookPath, kCacheDir);
    if (!epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true)) {
      LOG_ERR("AnxSync", "cannot open %s to apply phone progress", bookName.c_str());
      continue;
    }
    const int spineCount = epub.getSpineItemsCount();
    if (spineCount <= 0) continue;

    std::vector<uint32_t> cumulative;
    cumulative.reserve(static_cast<size_t>(spineCount));
    for (int i = 0; i < spineCount; ++i) {
      cumulative.push_back(static_cast<uint32_t>(epub.getCumulativeSpineItemSize(i)));
    }

    // pageCount for the target spine is unknown without laying the text out, and
    // laying it out here would duplicate the reader's pagination. Passing an empty
    // table makes the locator assume one page per spine item, which resolves to a
    // valid page number; the reader re-paginates on open and clamps the page.
    const std::vector<int> pagesPerSpine;
    const anx::ProgressBridge::DevicePosition target =
        anx::ProgressBridge::percentageToPosition(cumulative, entry.percentage, pagesPerSpine);

    // Skip when the stored position already matches, so a no-op sync does not
    // rewrite the card (and does not wear it).
    int existingSpine = 0;
    int existingPage = 0;
    int existingCount = 0;
    if (readReaderPosition(cachePath, existingSpine, existingPage, existingCount)) {
      if (existingSpine == target.spineIndex && existingPage == target.pageNumber) continue;
    }

    uint8_t data[6];
    data[0] = static_cast<uint8_t>(target.spineIndex & 0xFF);
    data[1] = static_cast<uint8_t>((target.spineIndex >> 8) & 0xFF);
    data[2] = static_cast<uint8_t>(target.pageNumber & 0xFF);
    data[3] = static_cast<uint8_t>((target.pageNumber >> 8) & 0xFF);
    data[4] = static_cast<uint8_t>(target.totalPages & 0xFF);
    data[5] = static_cast<uint8_t>((target.totalPages >> 8) & 0xFF);

    if (!ProgressFile::writeAtomic(cachePath, data, sizeof(data))) {
      LOG_ERR("AnxSync", "cannot write progress.bin for %s", bookName.c_str());
      continue;
    }
    ++rewritten;
  }

  if (rewritten > 0) LOG_DBG("AnxSync", "applied phone progress to %d book(s)", rewritten);
  return rewritten;
}
