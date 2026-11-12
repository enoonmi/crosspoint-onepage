#pragma once

#include <cstdint>
#include <vector>

namespace anx {

/**
 * Pure arithmetic behind mapping a book-wide reading percentage onto a spine
 * item and an intra-chapter offset.
 *
 * Why this exists separately
 * --------------------------
 * `ProgressMapper::toCrossPoint()` does this inside a function that also needs a
 * live `Epub` and `GfxRenderer`, so it can only be exercised on hardware or in
 * the SDL simulator. The arithmetic is the part most likely to be wrong (off-by-
 * one on the cumulative table, division by zero on a single-item book, percentage
 * landing exactly on a boundary), and none of the surrounding object graph is
 * needed to check it. Extracting it here makes it host-testable, and
 * `ProgressMapper` can call it when that becomes convenient.
 *
 * Conventions match CrossPoint's `getCumulativeSpineItemSize`: the table holds a
 * running total of decoded sizes, so `cumulative[i]` is the end offset of spine
 * item `i` and `cumulative[spineCount - 1]` is the whole book.
 */
class ProgressLocator {
 public:
  /** Where a book-wide percentage lands. */
  struct Location {
    /** Spine item index, clamped into [0, spineCount-1]. */
    int spineIndex = 0;
    /** Progress within that spine item, in [0.0, 1.0]. */
    float intraSpine = 0.0f;
    /**
     * True when the percentage could not be resolved from the book size (empty
     * book, zero total, non-finite input) and what came back is just the first
     * page. Callers should treat this as "unknown position" rather than "start".
     */
    bool isFallback = false;
  };

  /**
   * Locate `percentage` (0.0-1.0) within a book described by `cumulative`.
   *
   * @param cumulative  running totals from getCumulativeSpineItemSize(), one per
   *                    spine item. May be empty.
   * @param percentage  book-wide progress; outside [0,1] is clamped.
   *
   * Edge cases handled explicitly:
   *   - empty table or non-positive total  -> fallback, spine 0, intra 0
   *   - NaN / infinite percentage          -> fallback
   *   - percentage 1.0                     -> last spine item, intra 1.0
   *   - percentage 0.0                     -> first spine item, intra 0.0
   *   - zero-size spine item               -> skipped, never divides by zero
   */
  static Location locate(const std::vector<uint32_t>& cumulative, float percentage);

  /**
   * Reverse of locate(): the book-wide percentage for a position.
   *
   * @param cumulative  same table
   * @param spineIndex  spine item index (clamped)
   * @param intraSpine  progress within that item, clamped to [0,1]
   * @return book-wide progress in [0,1]; 0 when the table is unusable.
   */
  static float toPercentage(const std::vector<uint32_t>& cumulative, int spineIndex, float intraSpine);

  /**
   * Map a per-item page number onto intra-item progress.
   *
   * A single-page item reports 0.0 (there is nowhere to be within it); the
   * `totalPages - 1` divisor is what makes "last page of the item" land on 1.0
   * instead of overshooting.
   */
  static float pageToIntraSpine(int pageNumber, int totalPages);

  /**
   * Map intra-item progress back onto a page index for an item known to have
   * `totalPages` pages. Result is clamped to [0, totalPages-1].
   */
  static int intraSpineToPage(float intraSpine, int totalPages);

 private:
  static float clamp01(float value);
};

}  // namespace anx
