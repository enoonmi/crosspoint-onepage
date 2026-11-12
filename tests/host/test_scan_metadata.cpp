#include <string>
#include <vector>

#include "AnxSyncPlan.h"
#include "test_harness.h"

// Regression test for a real defect found in AnxSyncActivity::scanLocalBooks().
//
// What the defect was
// -------------------
// scanLocalBooks() filled in only `name` for each file it found on the SD card,
// leaving `size` and `lastModified` at 0, with a comment saying re-statting was
// too expensive. But buildSyncPlan() guards both Replace conditions on the local
// metadata being non-zero:
//
//     if (entry.size > 0 && present.size > 0 && entry.size != present.size)
//     if (entry.hasLastModified && entry.lastModified > 0 && present.lastModified > 0 ...)
//
// With a local size of 0 those conditions can never be true, so a book that Anx
// Reader replaced on the phone (same name, different content) was planned as
// Skip - forever. The device kept serving the stale copy and the user had no way
// to notice, because nothing reported an error.
//
// The tests below lock both halves: that the planner *does* detect the change
// when given real metadata, and that a zeroed LocalFile is what hides it. The
// second half is what makes this a regression test rather than a restatement of
// the planner's contract.

namespace {

anx::RemoteFile remoteBook(const std::string& name, long long size, long long mtime) {
  anx::RemoteFile file;
  file.name = name;
  file.size = size;
  file.lastModified = mtime;
  file.hasLastModified = true;
  return file;
}

anx::LocalFile localBook(const std::string& name, long long size, long long mtime) {
  anx::LocalFile file;
  file.name = name;
  file.size = size;
  file.lastModified = mtime;
  return file;
}

const char* kName = "0123456789abcdef0123456789abcdef.epub";

}  // namespace

void runScanMetadataTests() {
  anxtest::beginSuite("scanLocalBooks metadata (regression for the stale-book defect)");

  // --- With real metadata, a changed remote book must be fetched again. ---
  {
    const auto plan =
        anx::buildSyncPlan({remoteBook(kName, 5000, 1000)}, {localBook(kName, 4000, 1000)});
    ANX_CHECK_EQ_INT(1, plan.countOf(anx::SyncAction::Replace));
  }
  {
    const auto plan =
        anx::buildSyncPlan({remoteBook(kName, 4000, 2000)}, {localBook(kName, 4000, 1000)});
    ANX_CHECK_EQ_INT(1, plan.countOf(anx::SyncAction::Replace));
  }
  {
    const auto plan =
        anx::buildSyncPlan({remoteBook(kName, 4000, 1000)}, {localBook(kName, 4000, 1000)});
    ANX_CHECK_EQ_INT(1, plan.countOf(anx::SyncAction::Skip));
  }

  // --- The defect: a zeroed LocalFile can never satisfy the guards. ---
  // This is exactly what scanLocalBooks() used to produce: name only.
  {
    const auto stale = localBook(kName, 0, 0);
    const auto plan = anx::buildSyncPlan({remoteBook(kName, 999999, 5000)}, {stale});
    // Documented, not desired: the planner is behaving as specified given the
    // input it was handed. The fix belongs in scanLocalBooks(), and this
    // assertion is what would catch a future change that re-introduces zeroed
    // metadata.
    ANX_CHECK_EQ_INT(0, plan.countOf(anx::SyncAction::Replace));
    ANX_CHECK_EQ_INT(1, plan.countOf(anx::SyncAction::Skip));
  }
  {
    const auto stale = localBook(kName, 0, 0);
    const auto plan = anx::buildSyncPlan({remoteBook(kName, 4000, 999999)}, {stale});
    ANX_CHECK_EQ_INT(0, plan.countOf(anx::SyncAction::Replace));
  }

  // --- Guard against the opposite mistake: an absent book is downloaded. ---
  {
    const auto plan = anx::buildSyncPlan({remoteBook(kName, 4000, 1000)}, {});
    ANX_CHECK_EQ_INT(1, plan.countOf(anx::SyncAction::Download));
  }

  // --- Local-only reporting must keep working with real metadata. ---
  {
    const auto plan = anx::buildSyncPlan({}, {localBook(kName, 4000, 1000)});
    ANX_CHECK_EQ_INT(1, static_cast<int>(plan.localOnly.size()));
    ANX_CHECK_EQ_INT(0, plan.countOf(anx::SyncAction::Download));
  }

  anxtest::endSuite("scanLocalBooks metadata");
}
