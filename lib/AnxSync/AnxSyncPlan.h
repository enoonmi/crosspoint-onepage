#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anx {

/** What the sync should do with one remote book. */
enum class SyncAction {
  /** Not present locally: download it. */
  Download,
  /** Present but the remote copy is newer or a different size: download again. */
  Replace,
  /** Present and identical by size+mtime: nothing to do. */
  Skip,
  /** Not a book we handle (wrong extension, hidden, unsafe name): ignore. */
  Ignore,
};

const char* toString(SyncAction action);

/** A local file as seen by the sync planner. */
struct LocalFile {
  std::string name;
  int64_t size = 0;
  /** Modification time in Unix seconds; 0 when unknown. */
  int64_t lastModified = 0;
};

/** A remote entry as produced by the PROPFIND parser. */
struct RemoteFile {
  std::string name;
  int64_t size = 0;
  /** Modification time in Unix seconds; 0 when the server did not report one. */
  int64_t lastModified = 0;
  bool hasLastModified = false;
};

/** One planned operation. */
struct SyncPlanItem {
  SyncAction action = SyncAction::Ignore;
  std::string remoteName;
  std::string localPath;
  int64_t size = 0;
  /** Human-readable reason, for logging and for the on-device progress screen. */
  std::string reason;
};

/** Aggregated plan for one sync pass. */
struct SyncPlan {
  std::vector<SyncPlanItem> items;
  /** Files that exist locally but not remotely. Reported, never deleted. */
  std::vector<std::string> localOnly;
  /** Remote entries rejected by the name rules. */
  std::vector<std::string> ignored;

  int countOf(SyncAction action) const;
  int64_t bytesToDownload() const;
  bool hasWork() const;
};

/**
 * Decide what to download for one sync pass.
 *
 * Rules
 * -----
 *  - A remote entry that fails the name rules is reported in `ignored`, never
 *    downloaded.
 *  - Missing locally            -> Download
 *  - Same size AND same mtime   -> Skip
 *  - Different size OR mtime    -> Replace
 *  - When the server omits getlastmodified, size alone decides: equal size is
 *    treated as Skip, because re-downloading every book on every sync would be
 *    both slow and pointless.
 *  - Local files with no remote counterpart are reported in `localOnly` but
 *    never deleted. Deleting the user's books is not this feature's job, and
 *    Anx Reader itself prunes remote files - a device that also deleted
 *    locally would make an accidental server-side wipe unrecoverable.
 */
SyncPlan buildSyncPlan(const std::vector<RemoteFile>& remote, const std::vector<LocalFile>& local);

}  // namespace anx
