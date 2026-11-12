#include "AnxSyncSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <string>

#include "AnxCredentialStore.h"
#include "AnxSyncActivity.h"
#include "AnxWebDavClient.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Not host-compiled: depends on the Activity framework, GfxRenderer and the
// keyboard activity. Modeled closely on OpdsSettingsActivity, which is the
// closest existing screen (a URL + credentials edit flow for a remote service).

namespace {
// Row order. Kept as an enum so the render callbacks and handleSelection cannot
// drift apart when a row is inserted.
enum Row {
  kUrl = 0,
  kUsername,
  kPassword,
  kWifiOnly,
  kOnWake,
  kSyncNow,
  kRowCount,
};
}  // namespace

void AnxSyncSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  showSaved = false;
  showSaveError = false;
  ensureLoaded();
  requestUpdate();
}

void AnxSyncSettingsActivity::onExit() { Activity::onExit(); }

void AnxSyncSettingsActivity::ensureLoaded() {
  if (loaded) return;
  // Loading is best-effort: a device that has never synced simply has no file,
  // and the screen still has to render with empty fields.
  AnxCredentialStore::getInstance().loadFromFile();
  loaded = true;
}

void AnxSyncSettingsActivity::save() {
  showSaved = AnxCredentialStore::getInstance().saveToFile();
  showSaveError = !showSaved;
  if (!showSaved) {
    LOG_ERR("AnxSync", "failed to persist settings");
  }
}

void AnxSyncSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % kRowCount;
    showSaved = false;
    showSaveError = false;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + kRowCount - 1) % kRowCount;
    showSaved = false;
    showSaveError = false;
    requestUpdate();
  });
}

void AnxSyncSettingsActivity::handleSelection() {
  AnxCredentialStore& store = AnxCredentialStore::getInstance();

  if (selectedIndex == kUrl) {
    // Prefill with a scheme so the user does not have to type one on an e-ink
    // keyboard; setBaseUrl() strips a bare scheme back to empty.
    const std::string prefill = store.getBaseUrl().empty() ? "https://" : store.getBaseUrl();
    auto handler = [this](const ActivityResult& result) {
      if (result.isCancelled) return;
      const auto& kb = std::get<KeyboardResult>(result.data);
      std::string text = kb.text;
      if (text == "https://" || text == "http://") text.clear();
      AnxCredentialStore::getInstance().setBaseUrl(text);
      save();
      requestUpdate();
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ANX_SYNC_URL), prefill, 127,
                                                InputType::Url),
        handler);
    return;
  }

  if (selectedIndex == kUsername) {
    auto handler = [this](const ActivityResult& result) {
      if (result.isCancelled) return;
      const auto& kb = std::get<KeyboardResult>(result.data);
      AnxCredentialStore::getInstance().setUsername(kb.text);
      save();
      requestUpdate();
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_USERNAME),
                                                                   store.getUsername(), 63, InputType::Text),
                           handler);
    return;
  }

  if (selectedIndex == kPassword) {
    auto handler = [this](const ActivityResult& result) {
      if (result.isCancelled) return;
      const auto& kb = std::get<KeyboardResult>(result.data);
      AnxCredentialStore::getInstance().setPassword(kb.text);
      save();
      requestUpdate();
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD),
                                                                   store.getPassword(), 63, InputType::Password),
                           handler);
    return;
  }

  if (selectedIndex == kWifiOnly) {
    store.setOnlySyncOnWifi(!store.getOnlySyncOnWifi());
    save();
    requestUpdate();
    return;
  }

  if (selectedIndex == kOnWake) {
    store.setAutoSyncOnWake(!store.getAutoSyncOnWake());
    save();
    requestUpdate();
    return;
  }

  if (selectedIndex == kSyncNow) {
    // Refuse early with a specific message rather than letting the sync screen
    // fail after the user has waited for a TLS handshake.
    if (!store.isConfigured()) {
      showSaveError = true;
      requestUpdate();
      return;
    }
    // The sync screen owns the actual run; it loads the journal itself so this
    // screen stays purely about configuration.
    startActivityForResult(std::make_unique<AnxSyncActivity>(renderer, mappedInput),
                           [](const ActivityResult&) { /* nothing to apply on return */ });
    return;
  }
}

void AnxSyncSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_ANX_SYNC));
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    tr(STR_ANX_SYNC_HINT));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + metrics.tabBarHeight;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const AnxCredentialStore& store = AnxCredentialStore::getInstance();

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, kRowCount, static_cast<int>(selectedIndex),
      [](int index) {
        switch (index) {
          case kUrl: return std::string(tr(STR_ANX_SYNC_URL));
          case kUsername: return std::string(tr(STR_USERNAME));
          case kPassword: return std::string(tr(STR_PASSWORD));
          case kWifiOnly: return std::string(tr(STR_ANX_SYNC_WIFI_ONLY));
          case kOnWake: return std::string(tr(STR_ANX_SYNC_ON_WAKE));
          case kSyncNow: return std::string(tr(STR_ANX_SYNC_NOW));
          default: return std::string();
        }
      },
      nullptr, nullptr,
      [&store](int index) {
        switch (index) {
          case kUrl:
            return store.getBaseUrl().empty() ? std::string(tr(STR_NOT_SET)) : store.getBaseUrl();
          case kUsername:
            return store.getUsername().empty() ? std::string(tr(STR_NOT_SET)) : store.getUsername();
          case kPassword:
            return store.getPassword().empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
          case kWifiOnly: return store.getOnlySyncOnWifi() ? std::string("On") : std::string("Off");
          case kOnWake: return store.getAutoSyncOnWake() ? std::string("On") : std::string("Off");
          case kSyncNow: return std::string();
          default: return std::string();
        }
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (showSaveError) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  } else if (showSaved) {
    GUI.drawPopup(renderer, tr(STR_ANX_SYNC_DONE));
  }

  renderer.displayBuffer();
}
