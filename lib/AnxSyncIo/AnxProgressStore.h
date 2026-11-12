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
 *   - keep a sync-side journal so a book that was read but not uploaded yet is
 *     not lost across a reboot
 *   - turn the journal plus the phone's document into the upload payload, and
 *     apply phone-side updates back onto the journal
 *
 * Deliberately NOT responsible for writing `progress.bin` itself: that file
 * belongs to the reader activities (`EpubReaderUtils::saveProgress`), and having
 * two writers would risk corrupting it. This class only reads it, and keeps its
 * own journal at `/.crosspoint/anx_progress.bin`.
 *
 * NOT host-tested: depends on HalStorage. It IS compile-checked against
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
