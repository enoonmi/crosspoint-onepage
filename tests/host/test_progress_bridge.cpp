#include <cmath>
#include <string>
#include <vector>

#include "AnxProgressBridge.h"
#include "test_harness.h"

using anx::BookProgress;
using anx::ProgressBridge;
using anx::ProgressDocument;

namespace {

// A three-chapter book: 100 + 100 + 100 bytes.
const std::vector<uint32_t> kFlat = {100, 200, 300};

void testPositionToPercentage() {
  anxtest::beginSuite("ProgressBridge: position -> percentage");

  // First page of the first chapter is 0%.
  ANX_CHECK(ProgressBridge::positionToPercentage(kFlat, {0, 0, 1}) == 0.0);

  // Single page of chapter 0 sits at its start.
  ANX_CHECK(ProgressBridge::positionToPercentage(kFlat, {0, 0, 10}) == 0.0);

  // Last page of chapter 0 ends at 100/300.
  ANX_CHECK_MSG(std::fabs(ProgressBridge::positionToPercentage(kFlat, {0, 9, 10}) - 1.0 / 3.0) < 1e-6,
                "last page of chapter 0");

  // Last page of the last chapter is the end of the book.
  ANX_CHECK_MSG(std::fabs(ProgressBridge::positionToPercentage(kFlat, {2, 9, 10}) - 1.0) < 1e-6,
                "last page of chapter 2");

  // Middle of chapter 1: 100 + 50 = 150 of 300.
  ANX_CHECK_MSG(std::fabs(ProgressBridge::positionToPercentage(kFlat, {1, 5, 11}) - 0.5) < 1e-6,
                "middle of chapter 1");

  // Unusable table yields 0 rather than garbage.
  ANX_CHECK(ProgressBridge::positionToPercentage({}, {3, 7, 10}) == 0.0);
  ANX_CHECK(ProgressBridge::positionToPercentage({0}, {0, 0, 1}) == 0.0);

  anxtest::endSuite("ProgressBridge: position -> percentage");
}

void testPercentageToPosition() {
  anxtest::beginSuite("ProgressBridge: percentage -> position");

  const std::vector<int> pages = {10, 10, 10};

  bool fallback = true;
  ProgressBridge::DevicePosition pos = ProgressBridge::percentageToPosition(kFlat, 0.0, pages, &fallback);
  ANX_CHECK_MSG(!fallback, "0% resolves");
  ANX_CHECK_EQ_INT(0, pos.spineIndex);
  ANX_CHECK_EQ_INT(0, pos.pageNumber);
  ANX_CHECK_EQ_INT(10, pos.totalPages);

  pos = ProgressBridge::percentageToPosition(kFlat, 1.0, pages, &fallback);
  ANX_CHECK_MSG(!fallback, "100% resolves");
  ANX_CHECK_EQ_INT(2, pos.spineIndex);
  ANX_CHECK_EQ_INT(9, pos.pageNumber);

  // 50% is 150 bytes: halfway into chapter 1.
  pos = ProgressBridge::percentageToPosition(kFlat, 0.5, pages, &fallback);
  ANX_CHECK_EQ_INT(1, pos.spineIndex);
  ANX_CHECK_EQ_INT(5, pos.pageNumber);

  // Out-of-range clamps instead of escaping the book.
  pos = ProgressBridge::percentageToPosition(kFlat, 4.0, pages, &fallback);
  ANX_CHECK_EQ_INT(2, pos.spineIndex);
  pos = ProgressBridge::percentageToPosition(kFlat, -1.0, pages, &fallback);
  ANX_CHECK_EQ_INT(0, pos.spineIndex);

  // An unusable table reports a fallback so the caller does not overwrite a real
  // position with "start of book".
  pos = ProgressBridge::percentageToPosition({}, 0.5, pages, &fallback);
  ANX_CHECK_MSG(fallback, "empty table flags a fallback");
  pos = ProgressBridge::percentageToPosition(kFlat, std::nan(""), pages, &fallback);
  ANX_CHECK_MSG(fallback, "NaN flags a fallback");

  anxtest::endSuite("ProgressBridge: percentage -> position");
}

void testMissingPageCounts() {
  anxtest::beginSuite("ProgressBridge: chapters without a page count");

  // pagesPerSpine shorter than the book: unseen chapters assume one page.
  const std::vector<int> partial = {10};
  bool fallback = true;
  const ProgressBridge::DevicePosition pos = ProgressBridge::percentageToPosition(kFlat, 1.0, partial, &fallback);
  ANX_CHECK_MSG(!fallback, "still resolves");
  ANX_CHECK_EQ_INT(2, pos.spineIndex);
  ANX_CHECK_EQ_INT(1, pos.totalPages);
  ANX_CHECK_EQ_INT(0, pos.pageNumber);

  // A zero entry is treated the same as missing.
  const std::vector<int> zeros = {0, 0, 0};
  const ProgressBridge::DevicePosition zeroPos = ProgressBridge::percentageToPosition(kFlat, 0.5, zeros, &fallback);
  ANX_CHECK_EQ_INT(1, zeroPos.totalPages);

  anxtest::endSuite("ProgressBridge: chapters without a page count");
}

void testRoundTripThroughPercentage() {
  anxtest::beginSuite("ProgressBridge: round-trip");

  const std::vector<int> pages = {12, 30, 7};

  // Every page must survive position -> percentage -> position, either exactly or
  // as the equivalent position at the start of the next chapter.
  //
  // "Last page of chapter N" and "first page of chapter N+1" are the same point
  // in the book, and a single float percentage cannot tell them apart. The
  // restore path resolves a boundary to the next chapter's first page (see
  // ProgressLocator::locate), so chapter-final pages come back one chapter
  // along. That is accepted here on purpose; anything else is a real failure.
  bool anyMismatch = false;
  char detail[200] = {0};
  int boundaryShifted = 0;
  for (size_t spine = 0; spine < pages.size(); ++spine) {
    for (int page = 0; page < pages[spine]; ++page) {
      const ProgressBridge::DevicePosition original{static_cast<int>(spine), page, pages[spine]};
      const double pct = ProgressBridge::positionToPercentage(kFlat, original);
      bool fb = false;
      const ProgressBridge::DevicePosition restored = ProgressBridge::percentageToPosition(kFlat, pct, pages, &fb);

      const bool exact = (restored == original);
      const bool isLastPage = (page == pages[spine] - 1);
      const bool equivalentNextChapter =
          isLastPage && spine + 1 < pages.size() && restored.spineIndex == static_cast<int>(spine) + 1 &&
          restored.pageNumber == 0;

      if (fb || (!exact && !equivalentNextChapter)) {
        anyMismatch = true;
        std::snprintf(detail, sizeof(detail), "spine %zu page %d/%d -> %.4f -> spine %d page %d/%d", spine, page,
                      pages[spine], pct, restored.spineIndex, restored.pageNumber, restored.totalPages);
        break;
      }
      if (equivalentNextChapter) ++boundaryShifted;
    }
    if (anyMismatch) break;
  }
  ANX_CHECK_MSG(!anyMismatch, detail);
  // Only chapter-final pages may shift, and every chapter with a successor must
  // produce exactly one such shift.
  ANX_CHECK_MSG(boundaryShifted == static_cast<int>(pages.size()) - 1, "only chapter-final pages shift");

  anxtest::endSuite("ProgressBridge: round-trip");
}

void testLocalPathMapping() {
  anxtest::beginSuite("ProgressBridge: local path -> Anx file_path");

  std::string out;
  ANX_CHECK(ProgressBridge::localPathToAnxFilePath("/Books/abc.epub", out));
  ANX_CHECK_EQ_STR("file/abc.epub", out);

  // Names with spaces survive unchanged.
  ANX_CHECK(ProgressBridge::localPathToAnxFilePath("/Books/a b.epub", out));
  ANX_CHECK_EQ_STR("file/a b.epub", out);

  // Not a local book path.
  ANX_CHECK(!ProgressBridge::localPathToAnxFilePath("/Books/", out));
  ANX_CHECK(!ProgressBridge::localPathToAnxFilePath("/Books", out));
  ANX_CHECK(!ProgressBridge::localPathToAnxFilePath("/Other/abc.epub", out));
  ANX_CHECK(!ProgressBridge::localPathToAnxFilePath("Books/abc.epub", out));
  ANX_CHECK(!ProgressBridge::localPathToAnxFilePath("", out));
  ANX_CHECK(!ProgressBridge::localPathToAnxFilePath("/Books/sub/abc.epub", out));

  anxtest::endSuite("ProgressBridge: local path -> Anx file_path");
}

void testConflictResolution() {
  anxtest::beginSuite("ProgressBridge: conflict resolution");

  using W = ProgressBridge::Winner;

  // Only one side has the book.
  ANX_CHECK(ProgressBridge::resolveConflict(true, 0.4, 100, false, 0.0, 0) == W::Device);
  ANX_CHECK(ProgressBridge::resolveConflict(false, 0.0, 0, true, 0.4, 100) == W::Phone);

  // Newer wins.
  ANX_CHECK(ProgressBridge::resolveConflict(true, 0.4, 200, true, 0.6, 100) == W::Device);
  ANX_CHECK(ProgressBridge::resolveConflict(true, 0.4, 100, true, 0.6, 200) == W::Phone);

  // Equal timestamps: the phone wins, so a stale device cannot drag it back.
  ANX_CHECK(ProgressBridge::resolveConflict(true, 0.4, 100, true, 0.6, 100) == W::Phone);

  // Positions within epsilon of each other count as the same, regardless of
  // timestamps: this is what stops two devices from ping-ponging forever over a
  // rounding difference.
  ANX_CHECK(ProgressBridge::resolveConflict(true, 0.5000, 100, true, 0.5005, 200) == W::Same);
  ANX_CHECK(ProgressBridge::resolveConflict(true, 0.500, 200, true, 0.5009, 100) == W::Same);

  anxtest::endSuite("ProgressBridge: conflict resolution");
}

void testMergeFromPhone() {
  anxtest::beginSuite("ProgressBridge: merge phone into device");

  ProgressDocument phone;
  phone.books.push_back({"file/a.epub", 0.60, 2000});  // newer than device
  phone.books.push_back({"file/b.epub", 0.20, 1000});  // older than device
  phone.books.push_back({"file/c.epub", 0.90, 3000});  // device has nothing

  std::vector<BookProgress> device = {
      {"file/a.epub", 0.10, 1000},
      {"file/b.epub", 0.30, 2000},
      {"file/d.epub", 0.50, 4000},  // device-only, must be left alone
  };

  std::vector<std::string> winners;
  const int updated = ProgressBridge::mergeFromPhone(phone, device, winners);

  ANX_CHECK_EQ_INT(2, updated);  // a (phone newer) and c (device has none)
  ANX_CHECK_EQ_INT(2, winners.size());
  ANX_CHECK_EQ_STR("file/a.epub", winners[0]);
  ANX_CHECK_EQ_STR("file/c.epub", winners[1]);

  // b is not updated because the device is ahead; d never appears.
  for (const std::string& path : winners) {
    ANX_CHECK_MSG(path != "file/b.epub", "device-newer book must not be overwritten");
    ANX_CHECK_MSG(path != "file/d.epub", "device-only book must not be reported");
  }

  // Empty phone document changes nothing.
  ProgressDocument empty;
  ANX_CHECK_EQ_INT(0, ProgressBridge::mergeFromPhone(empty, device, winners));
  ANX_CHECK_EQ_INT(0, winners.size());

  anxtest::endSuite("ProgressBridge: merge phone into device");
}

void testBuildUploadDocument() {
  anxtest::beginSuite("ProgressBridge: build upload document");

  ProgressDocument phone;
  phone.books.push_back({"file/same.epub", 0.50, 1000});
  phone.books.push_back({"file/phoneNewer.epub", 0.70, 2000});
  phone.books.push_back({"file/deviceNewer.epub", 0.10, 1000});

  std::vector<BookProgress> device = {
      {"file/same.epub", 0.50, 1000},         // identical, skip
      {"file/phoneNewer.epub", 0.40, 1000},   // phone is ahead, skip
      {"file/deviceNewer.epub", 0.80, 2000},  // device is ahead, upload
      {"file/deviceOnly.epub", 0.30, 1500},   // phone has nothing, upload
  };

  const ProgressDocument doc = ProgressBridge::buildUploadDocument(device, phone, "onepage-c61", 9999);

  ANX_CHECK_EQ_INT(1, doc.schema);
  ANX_CHECK_EQ_STR("onepage-c61", doc.device);
  ANX_CHECK_EQ_INT(9999, doc.updatedAt);
  ANX_CHECK_EQ_INT(2, doc.books.size());

  // Only the two the device is authoritative about.
  bool sawDeviceNewer = false;
  bool sawDeviceOnly = false;
  for (const BookProgress& book : doc.books) {
    if (book.filePath == "file/deviceNewer.epub") sawDeviceNewer = true;
    if (book.filePath == "file/deviceOnly.epub") sawDeviceOnly = true;
    ANX_CHECK_MSG(book.filePath != "file/same.epub", "identical book must not be uploaded");
    ANX_CHECK_MSG(book.filePath != "file/phoneNewer.epub", "phone-ahead book must not be uploaded");
  }
  ANX_CHECK(sawDeviceNewer);
  ANX_CHECK(sawDeviceOnly);

  // The document must survive the JSON round-trip used on the wire.
  const std::string json = anx::ProgressJson::write(doc);
  ProgressDocument reparsed;
  ANX_CHECK_MSG(anx::ProgressJson::parse(json, reparsed), "upload document must re-parse");
  ANX_CHECK_EQ_INT(2, reparsed.books.size());

  anxtest::endSuite("ProgressBridge: build upload document");
}

}  // namespace

void runProgressBridgeTests() {
  testPositionToPercentage();
  testPercentageToPosition();
  testMissingPageCounts();
  testRoundTripThroughPercentage();
  testLocalPathMapping();
  testConflictResolution();
  testMergeFromPhone();
  testBuildUploadDocument();
}
