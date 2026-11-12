#pragma once

#include <string>
#include <vector>

#include <AnxSyncPlan.h>

#include "activities/Activity.h"

/**
 * Runs one Anx Reader sync pass and shows its progress.
 *
 * Why this is a state machine instead of a background task
 * -------------------------------------------------------
 * A TLS handshake plus a multi-megabyte book download takes seconds, so the
 * obvious design is a FreeRTOS worker. But this firmware's Activity lifecycle
 * deletes the activity on exit ("delete currentActivity" in main.cpp), and none
 * of the existing activities spawn tasks that outlive a single screen - the
 * pattern used here and elsewhere is a state machine advanced from loop().
 *
 * Keeping the sync in loop() means:
 *   - no task handle to leak, no wakeup into a destroyed object
 *   - Back is always responsive
 *   - cancellation is just "stop advancing"
 *
 * The cost is that a single chunk of work must stay short. The sync is therefore
 * advanced one book per loop() iteration: a PROPFIND, then one downloadBook()
 * per pass, then the progress upload. If downloading a large book turns out to
 * block the UI for too long on hardware, moving that one call to a task is a
 * contained change - the phase sequencing below does not depend on which thread
 * runs it.
 *
 * NOT host-compiled: depends on the Activity framework, WiFi and esp_http_client.
 */
class AnxSyncActivity final : public Activity {
 public:
  explicit AnxSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AnxSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Phase {
    NeedWifi,  // waiting for the WiFi picker
    Listing,   // PROPFIND in flight
    Downloading,
    UploadingProgress,
    Done,
    Failed,
  };

  Phase phase = Phase::NeedWifi;
  std::string statusLine;
  std::string errorLine;

  // Catalogue pulled from the server, and the subset still to fetch.
  std::vector<anx::RemoteFile> remoteBooks;
  std::vector<anx::SyncPlanItem> pending;
  size_t pendingIndex = 0;
  int downloadedCount = 0;
  int failedCount = 0;

  /** Advance one step. Called from loop(); keeps each call bounded. */
  void step();
  void startSync();
  void finishWithError(const char* why);
  void onWifiReady(bool connected);

  /** Redraw without blocking; render() decides what to show. */
  void requestRedraw();
};
