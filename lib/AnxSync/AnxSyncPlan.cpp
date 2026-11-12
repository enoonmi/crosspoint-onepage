#include "AnxSyncPlan.h"

#include <map>

#include "AnxPathMap.h"

namespace anx {

const char* toString(SyncAction action) {
  switch (action) {
    case SyncAction::Download: return "download";
    case SyncAction::Replace: return "replace";
    case SyncAction::Skip: return "skip";
    case SyncAction::Ignore: return "ignore";
  }
  return "unknown";
}

int SyncPlan::countOf(SyncAction action) const {
  int n = 0;
  for (const SyncPlanItem& item : items) {
    if (item.action == action) ++n;
  }
  return n;
}

int64_t SyncPlan::bytesToDownload() const {
  int64_t total = 0;
  for (const SyncPlanItem& item : items) {
    if (item.action == SyncAction::Download || item.action == SyncAction::Replace) {
      total += item.size;
    }
  }
  return total;
}

bool SyncPlan::hasWork() const {
  return countOf(SyncAction::Download) + countOf(SyncAction::Replace) > 0;
}

SyncPlan buildSyncPlan(const std::vector<RemoteFile>& remote, const std::vector<LocalFile>& local) {
  SyncPlan plan;

  std::map<std::string, const LocalFile*> localByName;
  for (const LocalFile& file : local) {
    localByName[file.name] = &file;
  }

  for (const RemoteFile& entry : remote) {
    if (!PathMap::isAcceptableBookName(entry.name)) {
      plan.ignored.push_back(entry.name);
      continue;
    }

    SyncPlanItem item;
    item.remoteName = entry.name;
    item.size = entry.size;
    if (!PathMap::localBookPath(entry.name, item.localPath)) {
      plan.ignored.push_back(entry.name);
      continue;
    }

    const auto found = localByName.find(entry.name);
    if (found == localByName.end()) {
      item.action = SyncAction::Download;
      item.reason = "not present locally";
      plan.items.push_back(item);
      continue;
    }

    const LocalFile& present = *found->second;

    if (entry.size > 0 && present.size > 0 && entry.size != present.size) {
      item.action = SyncAction::Replace;
      item.reason = "size differs";
      plan.items.push_back(item);
      continue;
    }

    if (entry.hasLastModified && entry.lastModified > 0 && present.lastModified > 0 &&
        entry.lastModified != present.lastModified) {
      item.action = SyncAction::Replace;
      item.reason = "remote timestamp is newer";
      plan.items.push_back(item);
      continue;
    }

    item.action = SyncAction::Skip;
    item.reason = entry.hasLastModified ? "up to date" : "same size, no remote timestamp";
    plan.items.push_back(item);
  }

  // Report local books that the remote does not have. Never delete: the user
  // may have side-loaded a book, and Anx Reader prunes the remote side itself.
  for (const LocalFile& file : local) {
    if (!PathMap::isAcceptableBookName(file.name)) continue;
    bool seenRemotely = false;
    for (const RemoteFile& entry : remote) {
      if (entry.name == file.name) {
        seenRemotely = true;
        break;
      }
    }
    if (!seenRemotely) plan.localOnly.push_back(file.name);
  }

  return plan;
}

}  // namespace anx
