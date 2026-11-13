#include <functional>
#include <string>
#include <vector>

#include "AnxPathMap.h"
#include "AnxSyncPlan.h"
#include "test_harness.h"

using anx::LocalFile;
using anx::PathMap;
using anx::RemoteFile;
using anx::SyncAction;
using anx::SyncPlan;
using anx::buildSyncPlan;

namespace {

void testAcceptableNames() {
  anxtest::beginSuite("PathMap: acceptable book names");

  ANX_CHECK(PathMap::isAcceptableBookName("9f2c1ab34d5e6f708192a3b4c5d6e7f8.epub"));
  ANX_CHECK(PathMap::isAcceptableBookName("BOOK.EPUB"));
  ANX_CHECK(PathMap::isAcceptableBookName("中文书名.epub"));

  // Rejected: not a book, hidden, unsafe, or degenerate.
  ANX_CHECK(!PathMap::isAcceptableBookName(""));
  ANX_CHECK(!PathMap::isAcceptableBookName("."));
  ANX_CHECK(!PathMap::isAcceptableBookName(".."));
  ANX_CHECK(!PathMap::isAcceptableBookName(".epub"));
  ANX_CHECK(!PathMap::isAcceptableBookName(".hidden.epub"));
  ANX_CHECK(!PathMap::isAcceptableBookName("../../etc/passwd.epub"));
  ANX_CHECK(!PathMap::isAcceptableBookName("a/b.epub"));
  ANX_CHECK(!PathMap::isAcceptableBookName("a\\b.epub"));
  ANX_CHECK(!PathMap::isAcceptableBookName("book.pdf"));
  ANX_CHECK(!PathMap::isAcceptableBookName("book.mobi"));
  ANX_CHECK(!PathMap::isAcceptableBookName("book.txt"));
  ANX_CHECK(!PathMap::isAcceptableBookName("epub"));

  anxtest::endSuite("PathMap: acceptable book names");
}

void testLocalPaths() {
  anxtest::beginSuite("PathMap: local path mapping");

  std::string path;
  ANX_CHECK(PathMap::localBookPath("abc.epub", path));
  ANX_CHECK_EQ_STR("/Books/abc.epub", path);

  ANX_CHECK(PathMap::localTempBookPath("abc.epub", path));
  ANX_CHECK_EQ_STR("/Books/.abc.epub.part", path);

  // The backup name holds the previous copy while a replacement lands. Its whole
  // point is that it is NOT a book name: if it were, scanLocalBooks() would list
  // it and the next sync would try to re-download a book that only exists as a
  // leftover. Asserting both halves stops a future suffix change from quietly
  // turning the backup into a phantom book.
  ANX_CHECK(PathMap::localBackupBookPath("abc.epub", path));
  ANX_CHECK_EQ_STR("/Books/.abc.epub.old", path);
  ANX_CHECK(!PathMap::isAcceptableBookName(".abc.epub.old"));

  ANX_CHECK(!PathMap::localBookPath("../evil.epub", path));
  ANX_CHECK(!PathMap::localTempBookPath("book.pdf", path));

  anxtest::endSuite("PathMap: local path mapping");
}

void testStripBaseUrl() {
  anxtest::beginSuite("PathMap: stripBaseUrl");

  ANX_CHECK_EQ_STR("/dav/anx/data/file/x.epub",
                   PathMap::stripBaseUrl("https://cloud.example.com/dav/anx/data/file/x.epub"));
  ANX_CHECK_EQ_STR("/anx/data/file/x.epub", PathMap::stripBaseUrl("/anx/data/file/x.epub"));
  ANX_CHECK_EQ_STR("anx/data/file/x.epub", PathMap::stripBaseUrl("anx/data/file/x.epub"));
  ANX_CHECK_EQ_STR("/", PathMap::stripBaseUrl("https://host"));

  anxtest::endSuite("PathMap: stripBaseUrl");
}

void testIsInsideRoot() {
  anxtest::beginSuite("PathMap: isInsideRoot");

  ANX_CHECK(PathMap::isInsideRoot("/Books", "/Books/a.epub"));
  ANX_CHECK(PathMap::isInsideRoot("/Books", "/Books/sub/a.epub"));
  ANX_CHECK(PathMap::isInsideRoot("/Books", "/Books/./a.epub"));
  ANX_CHECK(PathMap::isInsideRoot("/Books", "/Books/x/../a.epub"));

  ANX_CHECK(!PathMap::isInsideRoot("/Books", "/Books/../../etc/passwd"));
  ANX_CHECK(!PathMap::isInsideRoot("/Books", "/Other/a.epub"));
  ANX_CHECK(!PathMap::isInsideRoot("/Books", "Books/a.epub"));
  ANX_CHECK(!PathMap::isInsideRoot("/Books", "/Books"));

  anxtest::endSuite("PathMap: isInsideRoot");
}

void testPlanBasic() {
  anxtest::beginSuite("SyncPlan: basic decisions");

  std::vector<RemoteFile> remote = {
      {"new.epub", 1000, 1700000000, true},
      {"same.epub", 2000, 1700000100, true},
      {"bigger.epub", 3000, 1700000200, true},
      {"note.pdf", 500, 1700000300, true},
      {".partial.epub", 10, 0, false},
  };

  std::vector<LocalFile> local = {
      {"same.epub", 2000, 1700000100},
      {"bigger.epub", 2999, 1700000200},  // size differs
      {"stale.epub", 4000, 1700000400},   // local only
  };

  const SyncPlan plan = buildSyncPlan(remote, local);

  ANX_CHECK_EQ_INT(1, plan.countOf(SyncAction::Download));
  ANX_CHECK_EQ_INT(1, plan.countOf(SyncAction::Replace));
  ANX_CHECK_EQ_INT(1, plan.countOf(SyncAction::Skip));

  ANX_CHECK_EQ_INT(2, plan.ignored.size());  // note.pdf + .partial.epub
  ANX_CHECK_EQ_STR("note.pdf", plan.ignored[0]);

  ANX_CHECK_EQ_INT(1, plan.localOnly.size());
  ANX_CHECK_EQ_STR("stale.epub", plan.localOnly[0]);

  ANX_CHECK(plan.hasWork());
  // Download (1000) + Replace (3000)
  ANX_CHECK_EQ_INT(4000, plan.bytesToDownload());

  anxtest::endSuite("SyncPlan: basic decisions");
}

void testPlanNewerTimestampWins() {
  anxtest::beginSuite("SyncPlan: timestamp comparison");

  // Same size but the remote timestamp moved: the book was replaced server-side
  // with an identical-length edit, so it must be re-fetched.
  std::vector<RemoteFile> remote = {{"edited.epub", 5000, 1700009999, true}};
  std::vector<LocalFile> local = {{"edited.epub", 5000, 1700000000}};

  SyncPlan plan = buildSyncPlan(remote, local);
  ANX_CHECK_EQ_INT(1, plan.countOf(SyncAction::Replace));
  ANX_CHECK_EQ_STR("remote timestamp is newer", plan.items[0].reason);

  // Identical timestamp: nothing to do.
  remote[0].lastModified = 1700000000;
  plan = buildSyncPlan(remote, local);
  ANX_CHECK_EQ_INT(1, plan.countOf(SyncAction::Skip));

  anxtest::endSuite("SyncPlan: timestamp comparison");
}

void testPlanWithoutRemoteTimestamps() {
  anxtest::beginSuite("SyncPlan: servers that omit getlastmodified");

  std::vector<RemoteFile> remote = {
      {"a.epub", 1234, 0, false},
      {"b.epub", 0, 0, false},
  };
  std::vector<LocalFile> local = {
      {"a.epub", 1234, 1700000000},
      {"b.epub", 999, 1700000000},
  };

  const SyncPlan plan = buildSyncPlan(remote, local);

  // Both are already present. "a" has equal size -> Skip. "b" reports size 0
  // from the server, which cannot be compared, so it also stays untouched.
  ANX_CHECK_EQ_INT(2, plan.countOf(SyncAction::Skip));
  ANX_CHECK_EQ_INT(0, plan.countOf(SyncAction::Download));
  ANX_CHECK_EQ_INT(0, plan.countOf(SyncAction::Replace));

  anxtest::endSuite("SyncPlan: servers that omit getlastmodified");
}

void testPlanEmptyAndNoWork() {
  anxtest::beginSuite("SyncPlan: empty and no-op cases");

  SyncPlan plan = buildSyncPlan({}, {});
  ANX_CHECK_EQ_INT(0, plan.items.size());
  ANX_CHECK(!plan.hasWork());
  ANX_CHECK_EQ_INT(0, plan.bytesToDownload());

  // Everything already present.
  std::vector<RemoteFile> remote = {{"a.epub", 10, 100, true}};
  std::vector<LocalFile> local = {{"a.epub", 10, 100}};
  plan = buildSyncPlan(remote, local);
  ANX_CHECK_EQ_INT(1, plan.countOf(SyncAction::Skip));
  ANX_CHECK(!plan.hasWork());

  // Empty remote must NOT schedule deletions of local books.
  std::vector<LocalFile> onlyLocal = {{"mine.epub", 10, 100}};
  plan = buildSyncPlan({}, onlyLocal);
  ANX_CHECK_EQ_INT(0, plan.items.size());
  ANX_CHECK_EQ_INT(1, plan.localOnly.size());
  ANX_CHECK(!plan.hasWork());

  anxtest::endSuite("SyncPlan: empty and no-op cases");
}

void testPlanIgnoresUnsafeRemoteNames() {
  anxtest::beginSuite("SyncPlan: unsafe remote names are ignored");

  std::vector<RemoteFile> remote = {
      {"../../etc/passwd.epub", 10, 0, false},
      {"a/b.epub", 10, 0, false},
      {"good.epub", 10, 0, false},
  };

  const SyncPlan plan = buildSyncPlan(remote, {});
  ANX_CHECK_EQ_INT(2, plan.ignored.size());
  ANX_CHECK_EQ_INT(1, plan.countOf(SyncAction::Download));
  ANX_CHECK_EQ_STR("good.epub", plan.items[0].remoteName);
  ANX_CHECK_EQ_STR("/Books/good.epub", plan.items[0].localPath);

  anxtest::endSuite("SyncPlan: unsafe remote names are ignored");
}

// The reader's cache-path contract.
//
// progress.bin stores a position, not a book name, so the ONLY way to know which
// book a given progress.bin belongs to is to rebuild the directory name the reader
// used. If PathMap::readerCachePathFor() ever stops matching Epub's constructor,
// every progress scan finds nothing - and "no progress.bin" is a normal state, so
// there is no error to notice. Both progress directions would simply stop working.
//
// This locks the formula. It cannot compare against the real Epub (that class is
// not host-buildable), so it rebuilds the same expression here and compares.
void testReaderCachePath() {
  anxtest::beginSuite("PathMap: reader cache path");

  ANX_CHECK_EQ_STR("/.crosspoint", PathMap::cacheDir());

  // Independently rebuild the string the way Epub's constructor does, and compare.
  //
  // This is the assertion that actually protects the contract. Checking only the
  // prefix and uniqueness was not enough: injecting `hash + 1` into the
  // implementation kept every one of those checks passing, because the shape was
  // unchanged and the mapping was still one-to-one. Recomputing here means any
  // drift in the formula fails, even though the hash VALUE is unknowable in
  // advance (std::hash is unspecified across standard libraries).
  const std::string bookPath = "/Books/abc.epub";
  const std::string expected = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(bookPath));
  ANX_CHECK_EQ_STR(expected, PathMap::readerCachePathFor(bookPath));

  // Same input, same output - twice, so a stateful implementation is caught.
  ANX_CHECK_EQ_STR(PathMap::readerCachePathFor("/Books/abc.epub"),
                   PathMap::readerCachePathFor("/Books/abc.epub"));

  // Two different books must not share a cache directory, or their positions
  // would overwrite each other.
  ANX_CHECK_MSG(PathMap::readerCachePathFor("/Books/abc.epub") !=
                    PathMap::readerCachePathFor("/Books/xyz.epub"),
                "different books must map to different cache directories");

  // And the directory matters too, not just the file name: two books with the
  // same name in different folders are different books.
  ANX_CHECK_MSG(PathMap::readerCachePathFor("/Books/abc.epub") !=
                    PathMap::readerCachePathFor("/Other/abc.epub"),
                "the same name under a different directory is a different book");

  anxtest::endSuite("PathMap: reader cache path");
}

}  // namespace

void runPathAndPlanTests() {
  testAcceptableNames();
  testLocalPaths();
  testStripBaseUrl();
  testIsInsideRoot();
  testPlanBasic();
  testPlanNewerTimestampWins();
  testPlanWithoutRemoteTimestamps();
  testPlanEmptyAndNoWork();
  testPlanIgnoresUnsafeRemoteNames();
  testReaderCachePath();
}
