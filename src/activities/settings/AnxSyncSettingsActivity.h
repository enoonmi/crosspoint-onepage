#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Settings screen for the Anx Reader WebDAV sync.
 *
 * Mirrors OpdsSettingsActivity: one list, each row opening a keyboard prompt, and
 * every edit saved immediately so a partially configured server survives
 * navigation and a power cut. That pattern matters more here than anywhere else
 * in the settings tree - a WebDAV URL typed on an e-ink keyboard is expensive to
 * re-enter.
 *
 * Rows:
 *   WebDAV URL   -> AnxCredentialStore::setBaseUrl (normalises trailing slash and
 *                   a pasted trailing /anx)
 *   Username
 *   Password     -> empty entry keeps the stored password; see the .cpp
 *   Sync on Wi-Fi only     toggle
 *   Sync when waking       toggle
 *   Sync now               opens AnxSyncActivity
 */
class AnxSyncSettingsActivity final : public Activity {
 public:
  explicit AnxSyncSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AnxSyncSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  size_t selectedIndex = 0;
  bool showSaved = false;
  // Set when a popup should appear. The text lives in errorLine because the
  // reason differs: a failed save and a refused "Sync now" need different words,
  // and inferring which from a single bool would get one of them wrong.
  bool showSaveError = false;
  std::string errorLine;

  void handleSelection();
  void save();
  /** Load credentials from the SD card once, on first entry. */
  void ensureLoaded();

  bool loaded = false;
};
