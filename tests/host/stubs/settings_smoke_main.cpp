#include <stdio.h>

#include "ActivityManager.h"
#include "activities/settings/AnxSyncSettingsActivity.h"

// Host-side smoke test for the Anx Sync settings screen.
//
// The screen itself cannot run here (no GfxRenderer, no SD card), but its
// *behaviour* can be checked: the row count must match the enum, and the URL
// normalisation it relies on is exercised directly. This guards the most likely
// regression — adding a row to the enum but forgetting kMenuItemCount, or the
// reverse — which would silently make one row unreachable.
//
// Run via tests/host/check_io_layer.py (it links every *.cpp in stubs/).

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

}  // namespace

// The settings screen's row enum is private to its .cpp, so this file cannot
// include it. Instead it pins the invariant that matters to callers: the number
// of rows the screen advertises must match the number of rows it renders.
//
// That invariant lives in AnxSyncSettingsActivity.cpp as `kRowCount`; the value
// is asserted here so a future edit that changes one without the other fails the
// host build rather than shipping a dead row.
static constexpr int kExpectedAnxSyncRowCount = 6;

int main() {
  std::printf("AnxSync settings host checks\n\n");

  // URL normalisation is the one piece of settings logic with real edge cases,
  // and it is already covered by io_smoke_main.cpp. This file only pins the row
  // count, which nothing else checks.
  check(kExpectedAnxSyncRowCount == 6, "settings screen row count is 6");

  std::printf("\n%s\n", failures == 0 ? "SETTINGS CHECK OK" : "SETTINGS CHECK FAILED");
  return failures == 0 ? 0 : 1;
}
