#include <cmath>
#include <vector>

#include "AnxProgressLocator.h"
#include "test_harness.h"

using anx::ProgressLocator;

namespace {

void testEmptyAndDegenerate() {
  anxtest::beginSuite("ProgressLocator: empty and degenerate tables");

  // No metadata: must report a fallback, not "start of book".
  ProgressLocator::Location loc = ProgressLocator::locate({}, 0.5f);
  ANX_CHECK_MSG(loc.isFallback, "empty table is a fallback");
  ANX_CHECK_EQ_INT(0, loc.spineIndex);

  // A table whose total is zero is equally unusable.
  loc = ProgressLocator::locate({0}, 0.5f);
  ANX_CHECK_MSG(loc.isFallback, "zero total is a fallback");

  // Non-finite percentages cannot be located.
  loc = ProgressLocator::locate({100, 200}, std::nanf(""));
  ANX_CHECK_MSG(loc.isFallback, "NaN is a fallback");
  loc = ProgressLocator::locate({100, 200}, INFINITY);
  ANX_CHECK_MSG(loc.isFallback, "infinity is a fallback");

  anxtest::endSuite("ProgressLocator: empty and degenerate tables");
}

void testBoundaries() {
  anxtest::beginSuite("ProgressLocator: boundaries");

  const std::vector<uint32_t> cumulative = {100, 200, 300};

  // 0% is the very start.
  ProgressLocator::Location loc = ProgressLocator::locate(cumulative, 0.0f);
  ANX_CHECK_EQ_INT(0, loc.spineIndex);
  ANX_CHECK(loc.intraSpine == 0.0f);
  ANX_CHECK_MSG(!loc.isFallback, "0% resolves normally");

  // 100% is the end of the last item, not past it.
  loc = ProgressLocator::locate(cumulative, 1.0f);
  ANX_CHECK_EQ_INT(2, loc.spineIndex);
  ANX_CHECK(loc.intraSpine == 1.0f);

  // A percentage landing exactly on an item boundary is attributed to the item
  // that *starts* there. "Last page of chapter 0" and "first page of chapter 1"
  // are the same point in the book, and a single float cannot distinguish them;
  // resolving to the next chapter's first page is the stable choice, because a
  // reader then opens on the correct chapter instead of the tail of the previous
  // one.
  loc = ProgressLocator::locate(cumulative, 100.0f / 300.0f);
  ANX_CHECK_EQ_INT(1, loc.spineIndex);
  ANX_CHECK_MSG(loc.intraSpine < 0.01f, "boundary resolves to the start of item 1");

  // Out-of-range input is clamped rather than rejected.
  loc = ProgressLocator::locate(cumulative, -2.0f);
  ANX_CHECK_EQ_INT(0, loc.spineIndex);
  ANX_CHECK(loc.intraSpine == 0.0f);
  loc = ProgressLocator::locate(cumulative, 5.0f);
  ANX_CHECK_EQ_INT(2, loc.spineIndex);
  ANX_CHECK(loc.intraSpine == 1.0f);

  anxtest::endSuite("ProgressLocator: boundaries");
}

void testMidpoints() {
  anxtest::beginSuite("ProgressLocator: interior positions");

  const std::vector<uint32_t> cumulative = {100, 200, 300};

  // 50% of a {100,200,300} book is byte 150, which sits halfway through item 1
  // (item 1 spans 100..200), not at its end.
  ProgressLocator::Location loc = ProgressLocator::locate(cumulative, 0.5f);
  ANX_CHECK_EQ_INT(1, loc.spineIndex);
  ANX_CHECK_MSG(std::fabs(loc.intraSpine - 0.5f) < 1e-5f, "150 is halfway into item 1");

  // 25% is halfway through item 0.
  loc = ProgressLocator::locate(cumulative, 0.25f);
  ANX_CHECK_EQ_INT(0, loc.spineIndex);
  ANX_CHECK_MSG(std::fabs(loc.intraSpine - 0.75f) < 1e-5f, "25% of 300 = 75 into item 0");

  // 40% is 120 bytes: 20 into item 1.
  loc = ProgressLocator::locate(cumulative, 0.4f);
  ANX_CHECK_EQ_INT(1, loc.spineIndex);
  ANX_CHECK_MSG(std::fabs(loc.intraSpine - 0.2f) < 1e-5f, "40% of 300 = 120, 20 into item 1");

  anxtest::endSuite("ProgressLocator: interior positions");
}

void testUnevenAndZeroSizedItems() {
  anxtest::beginSuite("ProgressLocator: uneven and zero-sized items");

  // Item 1 has zero length (a common artifact of an empty spine entry).
  const std::vector<uint32_t> cumulative = {100, 100, 400};

  // A zero-length item must never cause a division by zero.
  ProgressLocator::Location loc = ProgressLocator::locate(cumulative, 100.0f / 400.0f);
  ANX_CHECK(loc.intraSpine >= 0.0f && loc.intraSpine <= 1.0f);

  // 75% is 300 bytes: 200 into item 2, which spans 100..400 -> 2/3.
  loc = ProgressLocator::locate(cumulative, 0.75f);
  ANX_CHECK_EQ_INT(2, loc.spineIndex);
  ANX_CHECK_MSG(std::fabs(loc.intraSpine - 2.0f / 3.0f) < 1e-4f, "300 of 400 is 2/3 into item 2");

  anxtest::endSuite("ProgressLocator: uneven and zero-sized items");
}

void testRoundTrip() {
  anxtest::beginSuite("ProgressLocator: locate/reverse round-trip");

  const std::vector<uint32_t> cumulative = {1200, 3500, 3510, 9000};

  const float samples[] = {0.0f, 0.05f, 0.3f, 0.5f, 0.77f, 0.999f, 1.0f};
  for (float p : samples) {
    const ProgressLocator::Location loc = ProgressLocator::locate(cumulative, p);
    const float back = ProgressLocator::toPercentage(cumulative, loc.spineIndex, loc.intraSpine);
    ANX_CHECK_MSG(std::fabs(back - p) < 2e-3f, "locate then toPercentage must agree");
  }

  // Direct reverse checks.
  ANX_CHECK(ProgressLocator::toPercentage(cumulative, 0, 0.0f) == 0.0f);
  ANX_CHECK(ProgressLocator::toPercentage(cumulative, 3, 1.0f) == 1.0f);
  // Clamping.
  ANX_CHECK(ProgressLocator::toPercentage(cumulative, -5, 0.0f) == 0.0f);
  ANX_CHECK(ProgressLocator::toPercentage(cumulative, 99, 1.0f) == 1.0f);
  ANX_CHECK(ProgressLocator::toPercentage({}, 0, 0.5f) == 0.0f);

  anxtest::endSuite("ProgressLocator: locate/reverse round-trip");
}

void testPageMapping() {
  anxtest::beginSuite("ProgressLocator: page <-> intra-spine");

  // First and last page.
  ANX_CHECK(ProgressLocator::pageToIntraSpine(0, 10) == 0.0f);
  ANX_CHECK(ProgressLocator::pageToIntraSpine(9, 10) == 1.0f);
  ANX_CHECK_MSG(std::fabs(ProgressLocator::pageToIntraSpine(3, 10) - 3.0f / 9.0f) < 1e-6f, "page 3 of 10");

  // A single-page item has no interior.
  ANX_CHECK(ProgressLocator::pageToIntraSpine(0, 1) == 0.0f);
  ANX_CHECK(ProgressLocator::pageToIntraSpine(5, 1) == 0.0f);
  // Out-of-range pages are clamped, not extrapolated.
  ANX_CHECK(ProgressLocator::pageToIntraSpine(50, 10) == 1.0f);
  ANX_CHECK(ProgressLocator::pageToIntraSpine(-7, 10) == 0.0f);

  // Reverse.
  ANX_CHECK_EQ_INT(0, ProgressLocator::intraSpineToPage(0.0f, 10));
  ANX_CHECK_EQ_INT(9, ProgressLocator::intraSpineToPage(1.0f, 10));
  ANX_CHECK_EQ_INT(0, ProgressLocator::intraSpineToPage(0.5f, 1));
  ANX_CHECK_EQ_INT(4, ProgressLocator::intraSpineToPage(0.44f, 10));
  ANX_CHECK_EQ_INT(5, ProgressLocator::intraSpineToPage(0.5f, 11));

  // Full round-trip over a page range must be exact: restoring a position then
  // saving it again is the common path between two devices, and any drift shows
  // up as "the other device is a page behind".
  for (int total = 2; total <= 40; ++total) {
    for (int page = 0; page < total; ++page) {
      const float intra = ProgressLocator::pageToIntraSpine(page, total);
      const int back = ProgressLocator::intraSpineToPage(intra, total);
      if (back != page) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "page %d of %d round-tripped to %d", page, total, back);
        ANX_CHECK_MSG(false, buf);
      }
    }
  }
  ANX_CHECK_MSG(true, "page round-trip over 2..40 pages");

  anxtest::endSuite("ProgressLocator: page <-> intra-spine");
}

}  // namespace

void runProgressLocatorTests() {
  testEmptyAndDegenerate();
  testBoundaries();
  testMidpoints();
  testUnevenAndZeroSizedItems();
  testRoundTrip();
  testPageMapping();
}
