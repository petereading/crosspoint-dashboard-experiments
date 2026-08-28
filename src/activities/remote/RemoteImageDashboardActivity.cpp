#include "RemoteImageDashboardActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DashboardSleep.h"
#include "MappedInputManager.h"
#include "RemoteImageValidation.h"
#include "SilentRestart.h"
#include "WifiCredentialStore.h"
#include "activities/dashboard/DashboardUI.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

void RemoteImageDashboardActivity::onEnter() {
  Activity::onEnter();

  recoverInterruptedSwap();
  cachedImageAvailable = validateImageFile(IMAGE_PATH);

  // When Remote Image is entered as the configured sleep screen, paint the
  // last known-good dashboard immediately instead of replacing the reader page
  // with a blocking "Downloading image..." screen. A timer wake already has
  // the cached dashboard retained on the e-ink panel, so avoid needlessly
  // repainting the same image before the scheduled refresh begins.
  if (autoRefresh && cachedImageAvailable && APP_STATE.activeDashboardMode != CrossPointState::DASHBOARD_REMOTE_IMAGE) {
    requestUpdate();
  }

  if (SETTINGS.remoteImageUrl[0] == '\0') {
    if (autoRefresh) {
      state = State::Failed;
      errorMessage = tr(STR_REMOTE_IMAGE_HTTPS_REQUIRED);
      requestUpdate();
      return;
    }
    promptUrl();
    return;
  }

  if (!RemoteImageValidation::isHttpsUrl(SETTINGS.remoteImageUrl)) {
    state = State::Failed;
    errorMessage = tr(STR_REMOTE_IMAGE_HTTPS_REQUIRED);
    requestUpdate();
    return;
  }

  beginUpdate();
}

void RemoteImageDashboardActivity::onExit() {
  Activity::onExit();
  if (wifiUsed && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void RemoteImageDashboardActivity::promptUrl() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_REMOTE_IMAGE_URL), SETTINGS.remoteImageUrl,
                                              sizeof(SETTINGS.remoteImageUrl) - 1, InputType::Url),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          if (SETTINGS.remoteImageUrl[0] == '\0') {
            finish();
          } else {
            if (state == State::Showing) sleepAt = millis() + DISPLAY_GRACE_INTERACTIVE_MS;
            requestUpdate();
          }
          return;
        }

        const auto& kb = std::get<KeyboardResult>(result.data);
        if (kb.text.empty()) {
          SETTINGS.remoteImageUrl[0] = '\0';
          SETTINGS.saveToFile();
          finish();
          return;
        }
        if (!RemoteImageValidation::isHttpsUrl(kb.text)) {
          state = State::Failed;
          errorMessage = tr(STR_REMOTE_IMAGE_HTTPS_REQUIRED);
          requestUpdate();
          return;
        }

        strncpy(SETTINGS.remoteImageUrl, kb.text.c_str(), sizeof(SETTINGS.remoteImageUrl) - 1);
        SETTINGS.remoteImageUrl[sizeof(SETTINGS.remoteImageUrl) - 1] = '\0';
        SETTINGS.saveToFile();
        beginUpdate();
      });
}

void RemoteImageDashboardActivity::beginUpdate() {
  state = State::Connecting;
  errorMessage = nullptr;
  sleepAt = 0;

  if (WiFi.status() == WL_CONNECTED) {
    state = State::Fetching;
    if (!autoRefresh) requestUpdate();
    return;
  }

  wifiUsed = true;
  if (autoRefresh) {
    startDirectWifiConnect();
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
                             finish();
                             return;
                           }
                           state = State::Fetching;
                           requestUpdate();
                         });
}

void RemoteImageDashboardActivity::startDirectWifiConnect() {
  {
    RenderLock lock(*this);
    WIFI_STORE.loadFromFile();
  }

  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const WifiCredential* cred = lastSsid.empty() ? nullptr : WIFI_STORE.findCredential(lastSsid);
  if (!cred) {
    LOG_ERR("REMOTE", "No saved WiFi network for unattended refresh");
    state = State::Failed;
    errorMessage = tr(STR_DASHBOARD_WIFI_FAILED);
    return;
  }

  LOG_INF("REMOTE", "Unattended refresh: connecting to %s", cred->ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(cred->ssid.c_str(), cred->password.empty() ? nullptr : cred->password.c_str());
  wifiConnectStart = millis();
}

void RemoteImageDashboardActivity::loop() {
  switch (state) {
    case State::Connecting:
      if (!autoRefresh) return;
      if (WiFi.status() == WL_CONNECTED) {
        state = State::Fetching;
        return;
      }
      if (millis() - wifiConnectStart >= WIFI_TIMEOUT_MS) {
        LOG_ERR("REMOTE", "Unattended WiFi connect timed out");
        state = State::Failed;
        errorMessage = tr(STR_DASHBOARD_WIFI_FAILED);
        requestUpdateAndWait();
        goToSleepAndPoll();
      }
      return;

    case State::Fetching:
      if (!autoRefresh) requestUpdateAndWait();
      runFetch();
      return;

    case State::Showing:
    case State::Failed:
      break;
  }

  if (autoRefresh) {
    requestUpdateAndWait();
    goToSleepAndPoll();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    exitDashboardMode();
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    sleepAt = 0;
    promptUrl();
    return;
  }
  if (state == State::Showing && sleepAt != 0 && millis() >= sleepAt) {
    goToSleepAndPoll();
  }
}

void RemoteImageDashboardActivity::runFetch() {
  Storage.mkdir("/.crosspoint");

  LOG_INF("REMOTE", "Downloading configured dashboard image");
  const auto downloadResult = HttpDownloader::downloadToFile(SETTINGS.remoteImageUrl, TEMP_PATH);
  if (downloadResult != HttpDownloader::OK) {
    LOG_ERR("REMOTE", "Image download failed: %d", static_cast<int>(downloadResult));
    state = State::Failed;
    errorMessage = tr(STR_REMOTE_IMAGE_FETCH_FAILED);
  } else if (!validateImageFile(TEMP_PATH)) {
    Storage.remove(TEMP_PATH);
    state = State::Failed;
    errorMessage = tr(STR_REMOTE_IMAGE_INVALID);
  } else if (!promoteDownloadedImage()) {
    Storage.remove(TEMP_PATH);
    state = State::Failed;
    errorMessage = tr(STR_REMOTE_IMAGE_FETCH_FAILED);
  } else {
    cachedImageAvailable = true;
    state = State::Showing;
  }

  requestUpdateAndWait();
  if (autoRefresh) {
    goToSleepAndPoll();
  } else if (state == State::Showing) {
    sleepAt = millis() + DISPLAY_GRACE_INTERACTIVE_MS;
  }
}

void RemoteImageDashboardActivity::goToSleepAndPoll() {
  APP_STATE.activeDashboardMode = CrossPointState::DASHBOARD_REMOTE_IMAGE;
  APP_STATE.saveToFile();
  const uint32_t intervalS = SETTINGS.remoteImageRefreshMinutes * 60u;
  LOG_INF("REMOTE", "Dashboard armed, sleeping for %u s", static_cast<unsigned>(intervalS));
  enterDashboardSleep(intervalS);
}

void RemoteImageDashboardActivity::exitDashboardMode() {
  if (APP_STATE.activeDashboardMode == CrossPointState::DASHBOARD_REMOTE_IMAGE) {
    APP_STATE.activeDashboardMode = CrossPointState::DASHBOARD_NONE;
    APP_STATE.saveToFile();
  }
}

void RemoteImageDashboardActivity::recoverInterruptedSwap() {
  if (!Storage.exists(IMAGE_PATH) && Storage.exists(BACKUP_PATH)) {
    if (Storage.rename(BACKUP_PATH, IMAGE_PATH)) {
      LOG_INF("REMOTE", "Recovered previous dashboard image after interrupted update");
    }
  } else if (Storage.exists(IMAGE_PATH) && Storage.exists(BACKUP_PATH)) {
    Storage.remove(BACKUP_PATH);
  }
  if (Storage.exists(TEMP_PATH)) Storage.remove(TEMP_PATH);
}

bool RemoteImageDashboardActivity::validateImageFile(const char* path) const {
  HalFile file;
  if (!Storage.openFileForRead("REMOTE", path, file)) return false;

  std::array<uint8_t, 54> header = {};
  const uint64_t fileSize = file.fileSize64();
  const int bytesRead = file.read(header.data(), header.size());
  file.close();
  if (bytesRead != static_cast<int>(header.size())) return false;

  RemoteImageValidation::BmpInfo info;
  const auto result = RemoteImageValidation::validateBmp(header.data(), header.size(), fileSize, &info);
  if (result != RemoteImageValidation::BmpError::Ok) {
    LOG_ERR("REMOTE", "BMP validation failed: %s", RemoteImageValidation::errorToString(result));
    return false;
  }

  HalFile bitmapFile;
  if (!Storage.openFileForRead("REMOTE", path, bitmapFile)) return false;
  Bitmap bitmap(bitmapFile, true);
  const auto bitmapResult = bitmap.parseHeaders();
  bitmapFile.close();
  if (bitmapResult != BmpReaderError::Ok) {
    LOG_ERR("REMOTE", "BMP renderer rejected image: %s", Bitmap::errorToString(bitmapResult));
    return false;
  }

  LOG_INF("REMOTE", "Validated %ldx%ld %u-bpp BMP", static_cast<long>(info.width), static_cast<long>(info.height),
          static_cast<unsigned>(info.bitsPerPixel));
  return true;
}

bool RemoteImageDashboardActivity::promoteDownloadedImage() {
  if (Storage.exists(BACKUP_PATH) && !Storage.remove(BACKUP_PATH)) return false;

  const bool hadPrevious = Storage.exists(IMAGE_PATH);
  if (hadPrevious && !Storage.rename(IMAGE_PATH, BACKUP_PATH)) {
    LOG_ERR("REMOTE", "Could not stage previous dashboard image");
    return false;
  }

  if (Storage.rename(TEMP_PATH, IMAGE_PATH)) {
    if (hadPrevious) Storage.remove(BACKUP_PATH);
    return true;
  }

  LOG_ERR("REMOTE", "Could not promote downloaded dashboard image");
  if (hadPrevious && !Storage.rename(BACKUP_PATH, IMAGE_PATH)) {
    LOG_ERR("REMOTE", "Could not restore previous dashboard image");
  }
  return false;
}

void RemoteImageDashboardActivity::render(RenderLock&&) {
  switch (state) {
    case State::Connecting:
    case State::Fetching:
      // During unattended sleep-screen refreshes, leave the last known-good
      // dashboard visible while WiFi and HTTPS run. Only first use (no cache)
      // needs the explicit updating screen.
      if (!autoRefresh || !cachedImageAvailable || !renderCachedImage()) {
        renderMessage(tr(STR_REMOTE_IMAGE_UPDATING));
      }
      break;
    case State::Failed:
      if (!cachedImageAvailable || !renderCachedImage()) {
        renderMessage(errorMessage ? errorMessage : tr(STR_REMOTE_IMAGE_FETCH_FAILED));
      }
      break;
    case State::Showing:
      if (!renderCachedImage()) renderMessage(tr(STR_REMOTE_IMAGE_INVALID));
      break;
  }
}

bool RemoteImageDashboardActivity::renderCachedImage() const {
  HalFile file;
  if (!Storage.openFileForRead("REMOTE", IMAGE_PATH, file)) return false;

  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return false;
  }

  const auto originalOrientation = renderer.getOrientation();
  renderer.setOrientation(bitmap.getHeight() >= bitmap.getWidth()
                              ? GfxRenderer::Orientation::Portrait
                              : GfxRenderer::Orientation::LandscapeCounterClockwise);
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const float scale = std::min(
      {1.0f, static_cast<float>(pageWidth) / bitmap.getWidth(), static_cast<float>(pageHeight) / bitmap.getHeight()});
  const int renderedWidth = static_cast<int>(std::floor(bitmap.getWidth() * scale));
  const int renderedHeight = static_cast<int>(std::floor(bitmap.getHeight() * scale));
  const int x = (pageWidth - renderedWidth) / 2;
  const int y = (pageHeight - renderedHeight) / 2;

  renderer.clearScreen();
  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  renderer.setOrientation(originalOrientation);
  file.close();
  return true;
}

void RemoteImageDashboardActivity::renderMessage(const char* message) const {
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 30, tr(STR_REMOTE_IMAGE_DASHBOARD), true,
                            EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 5, message);

  if (!autoRefresh && state == State::Failed) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_REMOTE_IMAGE_CHANGE_URL), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
