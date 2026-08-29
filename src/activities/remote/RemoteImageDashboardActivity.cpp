#include "RemoteImageDashboardActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
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

namespace {
volatile uint32_t remotePowerInterruptFired = 0;

void IRAM_ATTR remotePowerInterruptHandler() { remotePowerInterruptFired = 1; }
}  // namespace

void RemoteImageDashboardActivity::onEnter() {
  Activity::onEnter();

  cycleStartMs = millis();
  powerInputArmed = false;
  powerExitRequested = false;
  partialRefreshTestPending = autoRefresh && gpio.deviceIsX3();
  if (autoRefresh) startPowerLatch();

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
  stopPowerLatch();
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
  // The device returns to deep sleep immediately after the fetch, so modem
  // sleep saves little here and can add multi-second latency to short TLS
  // transfers on marginal links.
  WiFi.setSleep(false);
  WiFi.begin(cred->ssid.c_str(), cred->password.empty() ? nullptr : cred->password.c_str());
  wifiConnectStart = millis();
}

void RemoteImageDashboardActivity::loop() {
  if (autoRefresh) {
    // The press that entered the lock screen may still be held when this
    // activity starts. Do not interpret that same physical press as an exit;
    // arm only after it has first been released.
    if (!powerInputArmed) {
      if (!mappedInput.isPressed(MappedInputManager::Button::Power)) powerInputArmed = true;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Power)) {
      LOG_INF("REMOTE", "Power pressed during unattended refresh; returning to normal use");
      powerExitRequested = true;
    }

    if (powerLatchTriggered()) {
      returnToUser();
      return;
    }
  }

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
        if (powerLatchTriggered()) {
          returnToUser();
          return;
        }
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
    if (powerLatchTriggered()) {
      returnToUser();
      return;
    }
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
  const auto downloadResult = downloadDashboardImage();
  if (downloadResult == HttpDownloader::ABORTED && autoRefresh && powerLatchTriggered()) {
    LOG_INF("REMOTE", "Dashboard download cancelled by power button");
    returnToUser();
    return;
  }
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

  if (autoRefresh && powerLatchTriggered()) {
    returnToUser();
    return;
  }

  requestUpdateAndWait();
  if (autoRefresh) {
    if (powerLatchTriggered()) {
      returnToUser();
      return;
    }
    goToSleepAndPoll();
  } else if (state == State::Showing) {
    sleepAt = millis() + DISPLAY_GRACE_INTERACTIVE_MS;
  }
}

HttpDownloader::DownloadError RemoteImageDashboardActivity::downloadDashboardImage() {
  const unsigned long fetchStartedAt = millis();
  const auto cancelled = [this]() { return autoRefresh && powerLatchTriggered(); };
  const auto remainingBudget = [&]() -> unsigned long {
    const unsigned long elapsed = millis() - fetchStartedAt;
    return elapsed < FETCH_TOTAL_TIMEOUT_MS ? FETCH_TOTAL_TIMEOUT_MS - elapsed : 0;
  };
  const auto fetchOnce = [&](unsigned long budgetMs) {
    HttpDownloader::DownloadOptions options;
    options.operationTimeoutMs = std::min(FETCH_OPERATION_TIMEOUT_MS, budgetMs);
    options.overallTimeoutMs = budgetMs;
    options.bypassCache = true;
    options.cancelRequested = cancelled;
    return HttpDownloader::downloadToFile(SETTINGS.remoteImageUrl, TEMP_PATH, options);
  };

  const unsigned long firstBudget = std::min(FETCH_FIRST_ATTEMPT_MS, remainingBudget());
  auto result = fetchOnce(firstBudget);
  if (result == HttpDownloader::OK || result == HttpDownloader::ABORTED || cancelled()) return result;

  LOG_INF("REMOTE", "First image fetch failed (%d); reconnecting once", static_cast<int>(result));
  const unsigned long reconnectBudget = std::min(WIFI_RETRY_TIMEOUT_MS, remainingBudget());
  if (reconnectBudget == 0 || !reconnectWifiForRetry(reconnectBudget)) {
    return cancelled() ? HttpDownloader::ABORTED : result;
  }

  const unsigned long retryBudget = remainingBudget();
  if (retryBudget == 0) return HttpDownloader::TIMED_OUT;
  LOG_INF("REMOTE", "Retrying dashboard image fetch with %lu ms remaining", retryBudget);
  return fetchOnce(retryBudget);
}

bool RemoteImageDashboardActivity::reconnectWifiForRetry(unsigned long timeoutMs) {
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const WifiCredential* cred = lastSsid.empty() ? nullptr : WIFI_STORE.findCredential(lastSsid);
  if (!cred) return false;

  WiFi.disconnect(false);
  delay(50);
  if (powerLatchTriggered()) return false;
  WiFi.begin(cred->ssid.c_str(), cred->password.empty() ? nullptr : cred->password.c_str());

  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    if (powerLatchTriggered()) return false;
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

void RemoteImageDashboardActivity::startPowerLatch() {
  if (!autoRefresh || powerInterruptAttached) return;

  const auto& input = BoardConfig::ACTIVE.input;
  if (input.power < 0) return;

  remotePowerInterruptFired = 0;
  const int interruptMode = input.powerActiveHigh ? RISING : FALLING;
  attachInterrupt(digitalPinToInterrupt(input.power), remotePowerInterruptHandler, interruptMode);
  powerInterruptAttached = true;
}

void RemoteImageDashboardActivity::stopPowerLatch() {
  if (!powerInterruptAttached) return;

  const auto& input = BoardConfig::ACTIVE.input;
  detachInterrupt(digitalPinToInterrupt(input.power));
  powerInterruptAttached = false;
  if (remotePowerInterruptFired != 0) powerExitRequested = true;
}

bool RemoteImageDashboardActivity::powerLatchTriggered() {
  if (remotePowerInterruptFired != 0) powerExitRequested = true;
  return powerExitRequested;
}

void RemoteImageDashboardActivity::returnToUser() {
  stopPowerLatch();
  if (Storage.exists(TEMP_PATH)) Storage.remove(TEMP_PATH);
  exitDashboardMode();

  // Match a normal power-button wake from dashboard sleep: return to the book
  // when sleep began in the reader, otherwise return home. The silent restart
  // also clears WiFi/TLS heap fragmentation before normal use resumes.
  if (APP_STATE.lastSleepFromReader && !APP_STATE.openEpubPath.empty()) {
    silentRestartToReader();
  } else {
    silentRestart();
  }
}

void RemoteImageDashboardActivity::goToSleepAndPoll() {
  stopPowerLatch();
  if (powerExitRequested) {
    returnToUser();
    return;
  }

  APP_STATE.activeDashboardMode = CrossPointState::DASHBOARD_REMOTE_IMAGE;
  APP_STATE.saveToFile();
  const uint32_t intervalS = SETTINGS.remoteImageRefreshMinutes * 60u;
  const uint32_t intervalMs = intervalS * 1000u;
  const uint32_t cycleElapsedMs = millis() - cycleStartMs;
  const uint32_t sleepMs = intervalMs == 0 ? 1000u : intervalMs - (cycleElapsedMs % intervalMs);
  const uint32_t sleepS = std::max<uint32_t>(1u, (sleepMs + 999u) / 1000u);
  LOG_INF("REMOTE", "Dashboard armed after %lu ms, sleeping for %u s", cycleElapsedMs, static_cast<unsigned>(sleepS));
  enterDashboardSleep(sleepS);
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
  runPartialRefreshTest();
  renderer.setOrientation(originalOrientation);
  file.close();
  return true;
}

void RemoteImageDashboardActivity::runPartialRefreshTest() const {
  if (!partialRefreshTestPending) return;
  partialRefreshTestPending = false;

  // Standalone X3 hardware experiment: prove that the UC8253 can update a
  // small RAM window without repainting the cached dashboard or involving the
  // synchronous HTTPS path. The background bytes are restored exactly before
  // the second partial update.
  constexpr int DOT_WINDOW_SIZE = 24;
  constexpr int DOT_MARGIN = 12;
  constexpr unsigned long DOT_HOLD_MS = 1000;
  constexpr size_t MAX_SAVED_BYTES = 128;
  const int dotX = renderer.getScreenWidth() - DOT_MARGIN - DOT_WINDOW_SIZE;
  const int dotY = DOT_MARGIN;
  const size_t savedSize = renderer.getRegionByteSize(dotX, dotY, DOT_WINDOW_SIZE, DOT_WINDOW_SIZE);
  std::array<uint8_t, MAX_SAVED_BYTES> saved = {};
  if (savedSize == 0 || savedSize > saved.size() ||
      !renderer.copyRegionToBuffer(dotX, dotY, DOT_WINDOW_SIZE, DOT_WINDOW_SIZE, saved.data(), saved.size())) {
    LOG_ERR("REMOTE", "Could not save busy-dot partial window");
    return;
  }

  LOG_INF("REMOTE", "X3 partial test: showing %dx%d busy dot", DOT_WINDOW_SIZE, DOT_WINDOW_SIZE);
  renderer.fillRect(dotX, dotY, DOT_WINDOW_SIZE, DOT_WINDOW_SIZE, false);
  renderer.fillRoundedRect(dotX + 4, dotY + 4, DOT_WINDOW_SIZE - 8, DOT_WINDOW_SIZE - 8, (DOT_WINDOW_SIZE - 8) / 2,
                           Color::Black);
  renderer.displayWindow(dotX, dotY, DOT_WINDOW_SIZE, DOT_WINDOW_SIZE);
  delay(DOT_HOLD_MS);

  if (!renderer.copyBufferToRegion(dotX, dotY, DOT_WINDOW_SIZE, DOT_WINDOW_SIZE, saved.data(), savedSize)) {
    LOG_ERR("REMOTE", "Could not restore busy-dot partial window");
    return;
  }
  renderer.displayWindow(dotX, dotY, DOT_WINDOW_SIZE, DOT_WINDOW_SIZE);
  LOG_INF("REMOTE", "X3 partial test: busy dot removed");
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
