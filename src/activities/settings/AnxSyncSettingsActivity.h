#pragma once

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
 *   Server URL   -> AnxCredentialStore::setBaseUrl (normalises trailing slash and
 *                   a pasted trailing /anx)
 *   Username
 *   Password
 *   Sync on Wi-Fi only     toggle
 *   Sync on wake          toggle
 *   Sync now              opens AnxSyncActivity
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
  bool showSaveError = false;

  static constexpr int kMenuItemCount = 6;

  void handleSelection();
  void save();
  /** Load credentials from the SD card once, on first entry. */
  void ensureLoaded();

  bool loaded = false;
};
