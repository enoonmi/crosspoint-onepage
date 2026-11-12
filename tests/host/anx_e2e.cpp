#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "AnxPathMap.h"
#include "AnxPropfindParser.h"
#include "AnxSyncPlan.h"

// End-to-end driver: takes a PROPFIND body that was captured over real HTTP
// from tools/mock_webdav.py, then runs the production parse -> plan pipeline on
// it. This is how the host build proves the modules work together on data that
// actually came off the wire, rather than only on hand-written literals.
//
// Usage:
//     anx_e2e <propfind-body-file> [--expect-download N] [--expect-skip N]
//
// Exit code 0 on success, 1 on any mismatch or parse failure, 2 on usage error.

namespace {

struct Expectations {
  int download = -1;
  int replace = -1;
  int skip = -1;
  int ignored = -1;
};

bool readWholeFile(const char* path, std::string& out) {
  std::FILE* handle = std::fopen(path, "rb");
  if (handle == nullptr) return false;
  char buffer[4096];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), handle)) > 0) {
    out.append(buffer, got);
  }
  std::fclose(handle);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: %s <propfind-body.xml> [--expect-download N] [--expect-skip N] "
                 "[--expect-replace N] [--expect-ignored N]\n",
                 argv[0]);
    return 2;
  }

  Expectations expect;
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string flag = argv[i];
    const int value = std::atoi(argv[i + 1]);
    if (flag == "--expect-download") expect.download = value;
    else if (flag == "--expect-replace") expect.replace = value;
    else if (flag == "--expect-skip") expect.skip = value;
    else if (flag == "--expect-ignored") expect.ignored = value;
    else {
      std::fprintf(stderr, "unknown flag: %s\n", flag.c_str());
      return 2;
    }
  }

  std::string body;
  if (!readWholeFile(argv[1], body)) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }

  std::vector<anx::PropfindEntry> entries;
  if (!anx::PropfindParser::parse(body, entries)) {
    std::fprintf(stderr, "PARSE FAILED: body is not a WebDAV multistatus response\n");
    return 1;
  }

  std::printf("parsed %zu entries\n", entries.size());

  std::vector<anx::RemoteFile> remote;
  for (const anx::PropfindEntry& entry : entries) {
    std::printf("  %-8s %-60s size=%-10lld mtime=%s\n", entry.isCollection ? "[dir]" : "[file]",
                entry.name.c_str(), static_cast<long long>(entry.contentLength),
                entry.hasLastModified ? "yes" : "no");
    if (entry.isCollection) continue;
    anx::RemoteFile file;
    file.name = entry.name;
    file.size = entry.contentLength;
    file.lastModified = entry.lastModified;
    file.hasLastModified = entry.hasLastModified;
    remote.push_back(file);
  }

  // No local files: every accepted book should be scheduled for download.
  const anx::SyncPlan plan = anx::buildSyncPlan(remote, {});

  std::printf("\nplan: %d to download, %d to replace, %d to skip, %d ignored, %lld bytes\n",
              plan.countOf(anx::SyncAction::Download), plan.countOf(anx::SyncAction::Replace),
              plan.countOf(anx::SyncAction::Skip), static_cast<int>(plan.ignored.size()),
              static_cast<long long>(plan.bytesToDownload()));

  for (const anx::SyncPlanItem& item : plan.items) {
    if (item.action == anx::SyncAction::Download || item.action == anx::SyncAction::Replace) {
      std::printf("  %-8s %s -> %s (%lld bytes)\n", anx::toString(item.action), item.remoteName.c_str(),
                  item.localPath.c_str(), static_cast<long long>(item.size));
    }
  }
  for (const std::string& name : plan.ignored) {
    std::printf("  ignored  %s\n", name.c_str());
  }

  int failures = 0;
  const struct {
    const char* label;
    int expected;
    int actual;
  } checks[] = {
      {"download", expect.download, plan.countOf(anx::SyncAction::Download)},
      {"replace", expect.replace, plan.countOf(anx::SyncAction::Replace)},
      {"skip", expect.skip, plan.countOf(anx::SyncAction::Skip)},
      {"ignored", expect.ignored, static_cast<int>(plan.ignored.size())},
  };

  for (const auto& check : checks) {
    if (check.expected < 0) continue;
    if (check.expected != check.actual) {
      std::fprintf(stderr, "MISMATCH %s: expected %d, got %d\n", check.label, check.expected, check.actual);
      ++failures;
    }
  }

  if (failures == 0) {
    std::printf("\nE2E OK\n");
    return 0;
  }
  std::printf("\nE2E FAILED (%d mismatches)\n", failures);
  return 1;
}
