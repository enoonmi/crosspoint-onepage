#include "AnxProgressLocator.h"

#include <cmath>

namespace anx {

namespace {

bool isFinite(float value) { return std::isfinite(value); }

}  // namespace

float ProgressLocator::clamp01(float value) {
  if (!isFinite(value)) return 0.0f;
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

ProgressLocator::Location ProgressLocator::locate(const std::vector<uint32_t>& cumulative, float percentage) {
  Location result;

  // An empty table means the book metadata is not loaded yet; an unknown total
  // means the same. Distinguish that from "start of book" so callers do not
  // overwrite real progress with a zero.
  if (cumulative.empty() || cumulative.back() == 0) {
    result.isFallback = true;
    return result;
  }
  if (!isFinite(percentage)) {
    result.isFallback = true;
    return result;
  }

  const float clamped = clamp01(percentage);
  const float total = static_cast<float>(cumulative.back());
  const float target = total * clamped;

  // Attribute the target to the item that contains it.
  //
  // A strict `>` comparison is used deliberately. A percentage that lands exactly
  // on a boundary is produced both by "last page of item N" (intra = 1.0) and by
  // "first page of item N+1" (intra = 0.0) -- the two are the same point in the
  // book, and the resolution of a single float cannot tell them apart. Choosing
  // the item that *starts* at the boundary makes the round-trip stable in the
  // direction that matters: restoring a position lands on the first page of a
  // chapter rather than the last page of the previous one, so a reader never
  // opens a book showing the wrong chapter.
  //
  // The final item still receives the very end of the book: when target equals
  // total, no cumulative entry is strictly greater than it, so the loop falls
  // through to the last index.
  int index = static_cast<int>(cumulative.size()) - 1;
  for (size_t i = 0; i < cumulative.size(); ++i) {
    if (static_cast<float>(cumulative[i]) > target) {
      index = static_cast<int>(i);
      break;
    }
  }

  const uint32_t end = cumulative[static_cast<size_t>(index)];
  const uint32_t start = (index > 0) ? cumulative[static_cast<size_t>(index - 1)] : 0u;
  const uint32_t span = end - start;

  result.spineIndex = index;
  // A zero-length spine item cannot express a position; report the start of it
  // rather than dividing by zero.
  if (span == 0) {
    result.intraSpine = 0.0f;
    return result;
  }

  float intra = (target - static_cast<float>(start)) / static_cast<float>(span);
  // Snap to the item edges. Without this, a percentage that names an item edge
  // comes back as 0.99999994 after the float round-trip, and the caller then
  // restores one page short of where it should. Anything within a thousandth of
  // an edge is on that edge as far as a page-based reader is concerned.
  constexpr float kEdgeSnap = 1.0f / 1000.0f;
  if (intra > 1.0f - kEdgeSnap) {
    intra = 1.0f;
  } else if (intra < kEdgeSnap) {
    intra = 0.0f;
  }

  result.intraSpine = clamp01(intra);
  return result;
}

float ProgressLocator::toPercentage(const std::vector<uint32_t>& cumulative, int spineIndex, float intraSpine) {
  if (cumulative.empty() || cumulative.back() == 0) return 0.0f;
  if (spineIndex < 0) spineIndex = 0;
  const int last = static_cast<int>(cumulative.size()) - 1;
  if (spineIndex > last) spineIndex = last;

  const uint32_t end = cumulative[static_cast<size_t>(spineIndex)];
  const uint32_t start = (spineIndex > 0) ? cumulative[static_cast<size_t>(spineIndex - 1)] : 0u;
  const uint32_t span = end - start;

  const float total = static_cast<float>(cumulative.back());
  const float intra = clamp01(intraSpine);
  const float offset = static_cast<float>(start) + intra * static_cast<float>(span);
  return clamp01(offset / total);
}

float ProgressLocator::pageToIntraSpine(int pageNumber, int totalPages) {
  // "Last page" must map to 1.0, so the divisor is totalPages - 1. A one-page
  // item has no interior, hence 0.0.
  if (totalPages <= 1) return 0.0f;
  if (pageNumber < 0) pageNumber = 0;
  if (pageNumber > totalPages - 1) pageNumber = totalPages - 1;
  return static_cast<float>(pageNumber) / static_cast<float>(totalPages - 1);
}

int ProgressLocator::intraSpineToPage(float intraSpine, int totalPages) {
  if (totalPages <= 1) return 0;
  const float intra = clamp01(intraSpine);
  // Round to the nearest page rather than truncating: truncation biases every
  // restore one page early, which is visible when bouncing between devices.
  const int page = static_cast<int>(std::lround(intra * static_cast<float>(totalPages - 1)));
  if (page < 0) return 0;
  if (page > totalPages - 1) return totalPages - 1;
  return page;
}

}  // namespace anx
