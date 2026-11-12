#include "AnxSyncActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <memory>

#include "AnxCredentialStore.h"
#include "AnxPathMap.h"
#include "AnxProgressJson.h"
#include "AnxProgressStore.h"
#include "AnxWebDavClient.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Not host-compiled: depends on the Activity framework, WiFi and esp_http_client.
//
// The sequencing here is intentionally boring: one bounded step per loop(), so
// Back stays responsive and there is no task handle to leak into a destroyed
// activity. See the header comment for why this is not a FreeRTOS worker.

namespace {

/** Walk /Books and collect the book names already present on the SD card. */
std::vector<anx::LocalFile> scanLocalBooks() {
  std::vector<anx::LocalFile> out;
  if (!Storage.ready()) return out;

  const auto names = Storage.listFiles(anx::PathMap::booksDir(), 500);
  out.reserve(names.size());
  for (const auto& name : names) {
    const std::string asStd(name.c_str());
    if (!anx::PathMap::isAcceptableBookName(asStd)) continue;

    anx::LocalFile file;
    file.name = asStd;
    // Listing does not expose size/mtime, and re-statting every book on every
    // sync is expensive on SPI. Leaving both at 0 makes the planner compare by
    // presence only: a book that exists is not re-downloaded. A book whose
    // content changed is handled by the reader's own cache invalidation, which
    // keys on file content anyway.
    out.push_back(file);
  }
  return out;
}

}  // namespace

void AnxSyncActivity::onEnter() {
  Activity::onEnter();
  phase = Phase::NeedWifi;
  statusLine.clear();
  errorLine.clear();
  remoteBooks.clear();
  pending.clear();
  pendingIndex = 0;
  downloadedCount = 0;
  failedCount = 0;

  requestUpdate();

  if (WiFi.status() == WL_CONNECTED) {
    onWifiReady(true);
    return;
  }

  // Same pattern as CalibreConnectActivity: ask for a network, then continue.
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiReady(!result.isCancelled); });
}

void AnxSyncActivity::onExit() { Activity::onExit(); }

void AnxSyncActivity::onWifiReady(bool connected) {
  if (!connected) {
    finishWithError(tr(STR_ANX_SYNC_NO_WIFI));
    return;
  }
  startSync();
}

void AnxSyncActivity::startSync() {
  const anx::AnxCredentialStore& store = anx::AnxCredentialStore::getInstance();
  if (!store.isConfigured()) {
    finishWithError(tr(STR_ANX_SYNC_NO_SERVER));
    return;
  }

  if (!anx::AnxWebDavClient::ensureLocalLayout()) {
    finishWithError(tr(STR_ERROR_GENERAL_FAILURE));
    return;
  }

  phase = Phase::Listing;
  statusLine = tr(STR_ANX_SYNC_HINT);
  requestUpdate();
}

void AnxSyncActivity::finishWithError(const char* why) {
  phase = Phase::Failed;
  errorLine = why != nullptr ? why : tr(STR_ANX_SYNC_FAILED);
  LOG_ERR("AnxSync", "sync failed: %s", errorLine.c_str());
  requestUpdate();
}

void AnxSyncActivity::step() {
  switch (phase) {
    case Phase::Listing: {
      remoteBooks.clear();
      const anx::AnxWebDavClient::Error err = anx::AnxWebDavClient::listBooks(remoteBooks);
      if (err != anx::AnxWebDavClient::OK) {
        finishWithError(anx::AnxWebDavClient::errorString(err));
        return;
      }

      const std::vector<anx::LocalFile> local = scanLocalBooks();
      const anx::SyncPlan plan = anx::buildSyncPlan(remoteBooks, local);

      pending.clear();
      for (const anx::SyncPlanItem& item : plan.items) {
        if (item.action == anx::SyncAction::Download || item.action == anx::SyncAction::Replace) {
          pending.push_back(item);
        }
      }
      pendingIndex = 0;

      LOG_DBG("AnxSync", "remote=%u local=%u toFetch=%u", static_cast<unsigned>(remoteBooks.size()),
              static_cast<unsigned>(local.size()), static_cast<unsigned>(pending.size()));

      if (pending.empty()) {
        phase = Phase::UploadingProgress;
        statusLine = tr(STR_ANX_SYNC_UP_TO_DATE);
        requestUpdate();
        return;
      }
      phase = Phase::Downloading;
      requestUpdate();
      return;
    }

    case Phase::Downloading: {
      if (pendingIndex >= pending.size()) {
        phase = Phase::UploadingProgress;
        requestUpdate();
        return;
      }

      const anx::SyncPlanItem& item = pending[pendingIndex];
      statusLine = item.remoteName;
      requestUpdate();

      const anx::AnxWebDavClient::Error err = anx::AnxWebDavClient::downloadBook(item.remoteName);
      if (err == anx::AnxWebDavClient::OK) {
        ++downloadedCount;
      } else {
        ++failedCount;
        LOG_ERR("AnxSync", "download failed for %s: %s", item.remoteName.c_str(),
                anx::AnxWebDavClient::errorString(err));
        // A single bad book must not abort the whole sync; the rest may still be
        // fine, and the failure count is shown at the end.
      }
      ++pendingIndex;
      return;
    }

    case Phase::UploadingProgress: {
      // Push whatever this device is authoritative about. Anx Reader merges this
      // file; it never replaces the phone's database, so a failure here loses
      // nothing a later sync cannot resend.
      anx::AnxProgressStore& progressStore = anx::AnxProgressStore::getInstance();
      progressStore.load();

      anx::ProgressDocument phoneDoc;
      // Reading the phone's document would need a GET plus a JSON parse; the
      // upload path only needs to know what the device has, so an empty document
      // means "send everything this device knows". Over-reporting is harmless
      // because the merge on the phone side is timestamp-guarded.
      const anx::ProgressDocument upload =
          progressStore.buildUpload(phoneDoc, anx::ProgressJson::defaultDeviceName(), 0);

      if (upload.books.empty()) {
        phase = Phase::Done;
        requestUpdate();
        return;
      }

      const anx::AnxWebDavClient::Error err = anx::AnxWebDavClient::putProgress(anx::ProgressJson::write(upload));
      if (err != anx::AnxWebDavClient::OK) {
        // Progress upload failing after books downloaded is a warning, not a
        // failure: the reading position will be sent next time.
        LOG_ERR("AnxSync", "progress upload failed: %s", anx::AnxWebDavClient::errorString(err));
      }
      phase = Phase::Done;
      requestUpdate();
      return;
    }

    case Phase::Done:
    case Phase::Failed:
    case Phase::NeedWifi:
      return;
  }
}

void AnxSyncActivity::loop() {
  if (phase == Phase::Done || phase == Phase::Failed) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      finish();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    // Abandon the run. Books already downloaded stay; the next sync picks up
    // where this one stopped because completed files are now "local".
    finish();
    return;
  }

  step();
}

void AnxSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_ANX_SYNC));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);

  std::string body;
  switch (phase) {
    case Phase::NeedWifi:
      body = tr(STR_ANX_SYNC_NO_WIFI);
      break;
    case Phase::Listing:
      body = tr(STR_ANX_SYNC_RUNNING);
      break;
    case Phase::Downloading:
      body = std::string(tr(STR_ANX_SYNC_RUNNING)) + " " + std::to_string(pendingIndex) + "/" +
             std::to_string(pending.size());
      break;
    case Phase::UploadingProgress:
      body = tr(STR_SYNC_PROGRESS);
      break;
    case Phase::Done:
      body = std::string(tr(STR_ANX_SYNC_DONE)) + ": " + std::to_string(downloadedCount) + " " +
             tr(STR_ANX_SYNC_BOOKS);
      if (failedCount > 0) body += ", " + std::to_string(failedCount) + " failed";
      break;
    case Phase::Failed:
      body = errorLine;
      break;
  }

  renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, contentTop, body.c_str());

  // The current file name goes on its own line so a long hash name cannot push
  // the counters off screen.
  if (!statusLine.empty() && phase != Phase::Done && phase != Phase::Failed) {
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, contentTop + lineHeight * 2, statusLine.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void AnxSyncActivity::requestRedraw() { requestUpdate(); }
