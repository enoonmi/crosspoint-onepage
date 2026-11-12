#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <AnxProgressBridge.h>
#include <AnxProgressJson.h>

/**
 * Device-side persistence for Anx Reader progress sync.
 *
 * Responsibilities:
 *   - read the per-book position the reader saved in
 *     `.crosspoint/epub_<hash>/progress.bin`
 *   - write a merged position back into that same file
 *   - keep a sync-side journal so a book that was read but not uploaded yet is
 *     not lost across a reboot
 *   - turn the journal plus the phone's document into the upload payload, and
 *     apply phone-side updates back onto the journal
 *
 * Deliberately NOT responsible for writing `progress.bin` as part of reading:
 * that file belongs to the reader activities (`EpubReaderUtils::saveProgress`),
 * and having two writers would risk corrupting it. This class reads it during a
 * scan, and writes it only through the same crash-safe
 * `ProgressFile::writeAtomic` the reader uses, when the phone's position wins.
 *
 * NOT host-tested: depends on HalStorage and Epub. It IS compile-checked against
 * tests/host/stubs by tests/host/check_io_layer.py. The translation logic it
 * delegates to (ProgressBridge, ProgressLocator) is host-tested.
 */
class AnxProgressStore {
 public:
  /** One book's last known position from this device's point of view. */
  struct Entry {
    /** Anx Reader join key, e.g. "file/<hash>.epub". */
    std::string filePath;
    /** Book-wide progress in [0,1]. */
    double percentage = 0.0;
    /** Unix seconds when this device recorded it. */
    int64_t updatedAt = 0;
  };

  static AnxProgressStore& getInstance();

  /** Load the journal from `/.crosspoint/anx_progress.bin`. Missing file is fine. */
  bool load();

  /** Persist the journal. */
  bool save() const;

  const std::vector<Entry>& entries() const { return journal; }

  /**
   * Record that this device read `filePath` to `percentage`.
   *
   * Only bumps `updatedAt` when the percentage actually moved (beyond the epsilon
   * used by ProgressBridge), so idle page-turns and repeated wake-ups do not make
   * every book look modified and flood the upload.
   *
   * @return true when the journal changed and should be persisted
   */
  bool record(const std::string& filePath, double percentage, int64_t now);

  /**
   * Read a book's position from its reader-side `progress.bin`.
   *
   * The file holds a 6-byte little-endian record (spine, page, pageCount) written
   * by `EpubReaderUtils::saveProgress`. Returns false when the book has never
   * been opened on this device.
   */
  bool readReaderPosition(const std::string& cachePath, int& spineIndex, int& pageNumber, int& totalPages) const;

  /**
   * Walk /Books, read each book's saved reader position, and record it.
   *
   * This is the step that makes the device -> phone direction work at all. The
   * reader writes `/<cacheDir>/epub_<hash>/progress.bin`; nothing else on the
   * device knows which book a given progress.bin belongs to, because the file
   * stores only spine/page/pageCount. So the mapping is rebuilt here: for each
   * book path, the same cache key the reader uses
   * (`cacheDir + "/epub_" + std::hash<std::string>{}(bookPath)`) is derived, and
   * that directory's progress.bin is read.
   *
   * Without a call to this, the journal stays empty, buildUpload() returns an
   * empty document, and the upload phase silently does nothing - the progress
   * direction only ever runs phone -> device.
   *
   * @param now  timestamp stamped on entries whose position actually moved
   * @return how many entries changed, i.e. what the next upload will send
   */
  int scanReaderPositions(int64_t now);

  /**
   * Write the phone's winning positions back into the reader's progress.bin files.
   *
   * This is the last missing link in the phone -> device direction. Merge the
   * document into the journal, then call this: for every book the phone has, the
   * percentage is converted back to (spine, page) and written with
   * ProgressFile::writeAtomic, which the reader itself uses.
   *
   * Books missing locally are skipped, as is a book whose stored position already
   * matches, so a sync that changes nothing does not rewrite the card.
   *
   * @param phone  the phone's document
   * @return how many progress.bin files were rewritten
   */
  int applyPhonePositions(const anx::ProgressDocument& phone);

  /** Build the upload payload from the journal. */
  anx::ProgressDocument buildUpload(const anx::ProgressDocument& phone, const std::string& deviceName,
                                    int64_t now) const;

  /**
   * Apply the phone's document to the journal.
   *
   * Books the phone wins on are updated; books the device is ahead on are kept.
   * Returns the number of entries changed.
   */
  int mergeFromPhone(const anx::ProgressDocument& phone);

  /** Journal path on the SD card. */
  static const char* journalPath();

  /** Drop everything (used by a "forget server" / reset action). */
  void clear();

 private:
  AnxProgressStore() = default;

  std::vector<Entry> journal;

  /** Find an entry by file path; nullptr when absent. */
  Entry* findMutable(const std::string& filePath);
};
