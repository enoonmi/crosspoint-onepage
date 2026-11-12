#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "AnxProgressJson.h"

namespace anx {

/**
 * Translates between the reader's on-disk position and the progress document
 * exchanged with Anx Reader.
 *
 * The two sides do not agree on what "position" means, and that mismatch is the
 * whole difficulty of this feature:
 *
 *   reader  : (spineIndex, pageNumber) plus the per-item page count, persisted
 *             in `.crosspoint/epub_<hash>/progress.bin`
 *   Anx      : `reading_percentage` (a float) and `last_read_position` (an EPUB
 *             CFI produced by foliate-js)
 *
 * Only the percentage crosses the wire. A device without a DOM cannot produce a
 * CFI, and writing one we invented would destroy the phone's precise position,
 * so `last_read_position` is never touched - see docs/01 in the integration repo.
 *
 * What this class does NOT do
 * ---------------------------
 * It never opens a file, never touches the SD card and never calls into Epub or
 * GfxRenderer. Reading `progress.bin` stays with the reader activities and the
 * byte-level coordinate arithmetic lives in ProgressLocator. Keeping this
 * translation pure is what makes it testable.
 *
 * Book identity
 * -------------
 * `file_path` (e.g. "file/<hash>.epub") is the join key Anx Reader uses in
 * `tb_books`, so the bridge works in those terms. Getting from a local
 * `/Books/<hash>.epub` path to that form is a separate, trivially testable step
 * (`localPathToAnxFilePath`).
 */
class ProgressBridge {
 public:
  /** What the device currently knows about a book's position. */
  struct DevicePosition {
    int spineIndex = 0;
    int pageNumber = 0;
    /** Pages in the current spine item; <= 1 means "no interior". */
    int totalPages = 0;

    bool operator==(const DevicePosition& other) const {
      return spineIndex == other.spineIndex && pageNumber == other.pageNumber && totalPages == other.totalPages;
    }
  };

  /**
   * Convert a device position into the percentage stored in progress.json.
   *
   * @param cumulative  running spine sizes from Epub::getCumulativeSpineItemSize
   * @param position    current position
   * @return book-wide progress in [0,1]; 0 when the table is unusable
   */
  static double positionToPercentage(const std::vector<uint32_t>& cumulative, const DevicePosition& position);

  /**
   * Convert a percentage from progress.json back into a device position.
   *
   * @param cumulative     running spine sizes
   * @param percentage     value from the phone, clamped to [0,1]
   * @param pagesPerSpine  page count per spine item as the *current* render
   *                       settings produce them. Indexed by spine index; a
   *                       missing entry (or a non-positive one) means the item
   *                       has not been laid out yet and a single page is assumed.
   * @param wasFallback    set true when the percentage could not be resolved
   */
  static DevicePosition percentageToPosition(const std::vector<uint32_t>& cumulative, double percentage,
                                             const std::vector<int>& pagesPerSpine,
                                             bool* wasFallback = nullptr);

  /**
   * Translate a local book path into Anx Reader's `tb_books.file_path` form.
   *
   * "/Books/9f2c1a....epub" -> "file/9f2c1a....epub"
   *
   * Returns false (leaving `out` untouched) when the path is not a local book,
   * so a caller cannot accidentally key a progress entry on something Anx
   * Reader will never match.
   */
  static bool localPathToAnxFilePath(const std::string& localPath, std::string& out);

  /**
   * Pick which side's position wins.
   *
   * Both sides can have moved since the last sync. The tie-breaks are ordered so
   * that the outcome is deterministic and explainable:
   *
   *   1. If the phone has no entry for the book, the device wins.
   *   2. If the device has no entry, the phone wins.
   *   3. A strictly newer timestamp wins.
   *   4. On equal timestamps, the phone wins. It is the primary reading device for
   *      most users, and preferring it means a device that woke up stale cannot
   *      drag the phone backwards.
   *
   * A position that differs by less than `epsilon` counts as the same, so a
   * device that rounds slightly differently does not cause an endless ping-pong.
   */
  enum class Winner { Device, Phone, Same };

  static Winner resolveConflict(bool hasDeviceEntry, double devicePercentage, int64_t deviceUpdatedAt,
                                bool hasPhoneEntry, double phonePercentage, int64_t phoneUpdatedAt,
                                double epsilon = 0.001);

  /**
   * Merge phone-side progress into a local device store.
   *
   * Applies `resolveConflict` per book. Books the device knows about but the
   * phone does not are left alone (they are side-loaded, or the phone deleted
   * them - either way the device must not invent an entry for them).
   *
   * @param outWinners  receives the file paths whose device position should change
   * @return number of books to update
   */
  static int mergeFromPhone(const ProgressDocument& document, const std::vector<BookProgress>& deviceEntries,
                            std::vector<std::string>& outWinners);

  /**
   * Build the document the device uploads.
   *
   * Only books whose device entry is strictly newer than the phone's are
   * included, so the upload carries just the deltas this device is authoritative
   * about. Everything else is left to the phone.
   */
  static ProgressDocument buildUploadDocument(const std::vector<BookProgress>& deviceEntries,
                                              const ProgressDocument& phoneDocument, const std::string& deviceName,
                                              int64_t now);
};

}  // namespace anx
