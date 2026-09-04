#include "RemoteImageDashboardActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
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
#include "images/Logo120.h"
#include "network/HttpDownloader.h"

namespace {
volatile uint32_t remotePowerInterruptFired = 0;

void IRAM_ATTR remotePowerInterruptHandler() { remotePowerInterruptFired = 1; }

std::string urlEncode(const char* value) {
  static constexpr char kHexDigits[] = "0123456789ABCDEF";
  std::string encoded;
  for (const unsigned char ch : std::string(value ? value : "")) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      encoded.push_back(static_cast<char>(ch));
    } else {
      encoded.push_back('%');
      encoded.push_back(kHexDigits[ch >> 4]);
      encoded.push_back(kHexDigits[ch & 0x0f]);
    }
  }
  return encoded;
}

std::string workerBaseUrl(const char* value) { return RemoteImageValidation::workerBaseUrl(value ? value : ""); }
}  // namespace

void RemoteImageDashboardActivity::onEnter() {
  Activity::onEnter();

  if (card != Card::CustomImage && SETTINGS.dashboardWorkerUrl[0] != '\0') {
    const std::string normalized = workerBaseUrl(SETTINGS.dashboardWorkerUrl);
    if (normalized != SETTINGS.dashboardWorkerUrl) {
      strncpy(SETTINGS.dashboardWorkerUrl, normalized.c_str(), sizeof(SETTINGS.dashboardWorkerUrl) - 1);
      SETTINGS.dashboardWorkerUrl[sizeof(SETTINGS.dashboardWorkerUrl) - 1] = '\0';
      SETTINGS.saveToFile();
    }
  }

  maxAllocEnterKb = static_cast<uint16_t>(ESP.getMaxAllocHeap() / 1024);
  cycleStartMs = millis();
  powerInputArmed = false;
  powerExitRequested = false;
  if (autoRefresh) startPowerLatch();

  recoverInterruptedSwap();
  cachedImageAvailable = validateImageFile(imagePath());
  snprintf(statusDetail, sizeof(statusDetail), "starting");

  // Paint the last known-good dashboard immediately when opening an interactive
  // preview, and when entering the configured sleep screen for the first time.
  // A timer wake already has the cached dashboard retained on the e-ink panel,
  // so avoid repainting the same image before its scheduled refresh begins.
  if (cachedImageAvailable && (!autoRefresh || APP_STATE.activeDashboardMode != activeDashboardMode())) {
    requestUpdateAndWait();
  }

  if (configuredUrl()[0] == '\0') {
    if (autoRefresh) {
      state = State::Failed;
      errorMessage = tr(STR_REMOTE_IMAGE_HTTPS_REQUIRED);
      snprintf(statusDetail, sizeof(statusDetail), "no worker URL set");
      requestUpdate();
      return;
    }
    promptUrl();
    return;
  }

  if (!RemoteImageValidation::isHttpsUrl(dashboardUrl()) ||
      (card == Card::Rss && !RemoteImageValidation::isHttpsUrl(SETTINGS.rssFeedUrl))) {
    state = State::Failed;
    errorMessage = tr(STR_REMOTE_IMAGE_HTTPS_REQUIRED);
    snprintf(statusDetail, sizeof(statusDetail), "URL must be https");
    requestUpdate();
    return;
  }

  beginUpdate();
}

void RemoteImageDashboardActivity::onExit() {
  stopPowerLatch();
  Activity::onExit();
  if (wifiUsed) shutdownWifi();
}

void RemoteImageDashboardActivity::promptUrl() {
  char* urlBuffer = configuredUrlBuffer();
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, urlLabel(), urlBuffer,
                                                                 configuredUrlCapacity() - 1, InputType::Url),
                         [this, urlBuffer](const ActivityResult& result) {
                           if (result.isCancelled) {
                             if (urlBuffer[0] == '\0') {
                               finish();
                             } else {
                               if (state == State::Showing) scheduleNextInteractiveRefresh();
                               requestUpdate();
                             }
                             return;
                           }

                           const auto& kb = std::get<KeyboardResult>(result.data);
                           if (kb.text.empty()) {
                             urlBuffer[0] = '\0';
                             SETTINGS.saveToFile();
                             finish();
                             return;
                           }
                           const bool isWorkerUrl = urlBuffer == SETTINGS.dashboardWorkerUrl;
                           const std::string savedUrl = isWorkerUrl ? workerBaseUrl(kb.text.c_str()) : kb.text;
                           if (!RemoteImageValidation::isHttpsUrl(savedUrl)) {
                             state = State::Failed;
                             errorMessage = tr(STR_REMOTE_IMAGE_HTTPS_REQUIRED);
                             requestUpdate();
                             return;
                           }

                           strncpy(urlBuffer, savedUrl.c_str(), configuredUrlCapacity() - 1);
                           urlBuffer[configuredUrlCapacity() - 1] = '\0';
                           SETTINGS.saveToFile();
                           if (configuredUrl()[0] == '\0') {
                             promptUrl();
                             return;
                           }
                           beginUpdate();
                         });
}

void RemoteImageDashboardActivity::beginUpdate() {
  state = State::Connecting;
  errorMessage = nullptr;
  nextInteractiveRefreshAt = 0;

  // The radio cannot be brought up at the idle clock: WiFi.mode() never returns
  // at LOW_POWER_FREQ (see enterLockScreenSleep in main.cpp). An interactive card
  // lets the clock drop between refreshes, so restore full speed here — before
  // any WiFi call, not at the end of the loop iteration where skipLoopDelay acts.
  powerManager.setPowerSaving(false);

  if (wifiReady()) {
    state = State::Fetching;
    snprintf(statusDetail, sizeof(statusDetail), "fetching");
    if (!autoRefresh && !cachedImageAvailable) requestUpdate();
    return;
  }

  wifiUsed = true;
  snprintf(statusDetail, sizeof(statusDetail), "connecting");
  startDirectWifiConnect();
}

void RemoteImageDashboardActivity::startDirectWifiConnect() {
  {
    RenderLock lock(*this);
    WIFI_STORE.loadFromFile();
  }

  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const WifiCredential* cred = lastSsid.empty() ? nullptr : WIFI_STORE.findCredential(lastSsid);
  if (!cred) {
    if (!autoRefresh) {
      // First interactive use, or the saved network was forgotten: let the user
      // pick one. Later refreshes reconnect to it directly, without the picker.
      promptWifiSelection();
      return;
    }
    LOG_ERR("REMOTE", "No saved WiFi network for unattended refresh");
    state = State::Failed;
    errorMessage = tr(STR_DASHBOARD_WIFI_FAILED);
    return;
  }

  LOG_INF("REMOTE", "Connecting to %s", cred->ssid.c_str());
  // Mirror WifiSelectionActivity::attemptConnection(). Credentials belong to
  // WifiCredentialStore, so the SDK's own NVS copy must not drive an
  // auto-connect: entering STA mode with it enabled starts a connect attempt of
  // its own, and a begin() issued while that is in flight is dropped. Clearing
  // it first is what makes a connect without the picker reliable.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  // The radio is powered down again as soon as the fetch ends (deep sleep when
  // unattended, shutdownWifi() when interactive), so modem sleep saves little
  // here and can add multi-second latency to short TLS transfers on marginal
  // links.
  WiFi.setSleep(false);
  WiFi.begin(cred->ssid.c_str(), cred->password.empty() ? nullptr : cred->password.c_str());
  wifiConnectStart = millis();
}

bool RemoteImageDashboardActivity::wifiReady() {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void RemoteImageDashboardActivity::promptWifiSelection() {
  // Keep the connect deadline armed across the picker so a resumed Connecting
  // state can never see a stale zero and time out on its first poll.
  wifiConnectStart = millis();
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

void RemoteImageDashboardActivity::shutdownWifi() {
  if (WiFi.getMode() == WIFI_MODE_NULL) return;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  delay(30);
  LOG_DBG("REMOTE", "Radio off until the next refresh");
}

// End of an interactive refresh: arm the next one, and drop the radio if the
// wait is long enough to be worth it. The card stays on screen, and
// allowClockThrottle() lets the CPU idle down, so a long wait costs about as
// much as a static sleep screen.
//
// A short interval is the exception. Bringing the radio up costs ten to twenty
// seconds of association, DHCP and a TLS handshake, so at one minute the device
// would spend a third of its time connecting -- and reconnecting that often
// proved unreliable in practice: the second Clock refresh associated but its
// fetch then ran out its whole budget. Staying associated through a short wait
// is both cheaper and steadier; modem sleep already idles the radio between
// requests.
void RemoteImageDashboardActivity::finishInteractiveCycle() {
  if (refreshMinutes() > KEEP_RADIO_MAX_INTERVAL_MINUTES) shutdownWifi();
  scheduleNextInteractiveRefresh();
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
      if (wifiReady()) {
        state = State::Fetching;
        snprintf(statusDetail, sizeof(statusDetail), "fetching");
        return;
      }
      if (millis() - wifiConnectStart >= WIFI_TIMEOUT_MS) {
        LOG_ERR("REMOTE", "WiFi connect timed out");
        state = State::Failed;
        errorMessage = tr(STR_DASHBOARD_WIFI_FAILED);
        snprintf(statusDetail, sizeof(statusDetail), "WiFi connect timed out");
        requestUpdateAndWait();
        if (!autoRefresh) {
          // Keep the card on screen and retry on its normal interval.
          finishInteractiveCycle();
          return;
        }
        if (powerLatchTriggered()) {
          returnToUser();
          return;
        }
        goToSleepAndPoll();
      }
      return;

    case State::Fetching:
      if (!autoRefresh && !cachedImageAvailable) requestUpdateAndWait();
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
    nextInteractiveRefreshAt = 0;
    promptUrl();
    return;
  }
  if ((state == State::Showing || state == State::Failed) && nextInteractiveRefreshAt != 0 &&
      static_cast<long>(millis() - nextInteractiveRefreshAt) >= 0) {
    cycleStartMs = millis();
    beginUpdate();
  }
}

void RemoteImageDashboardActivity::runFetch() {
  Storage.mkdir("/.crosspoint");

  maxAllocPreFetchKb = static_cast<uint16_t>(ESP.getMaxAllocHeap() / 1024);
  LOG_INF("REMOTE", "Downloading configured dashboard image, max contiguous block %u KB", maxAllocPreFetchKb);
  lastHttpStatus = 0;
  lastBytesReceived = 0;
  lastSlowestWriteMs = 0;
  rssiBeforeFetch = static_cast<int8_t>(WiFi.RSSI());
  const unsigned long fetchStartedAt = millis();
  const auto downloadResult = downloadDashboardImage();
  rssiAfterFetch = static_cast<int8_t>(WiFi.RSSI());
  stillAssociatedAfterFetch = WiFi.status() == WL_CONNECTED;
  if (downloadResult == HttpDownloader::ABORTED && autoRefresh && powerLatchTriggered()) {
    LOG_INF("REMOTE", "Dashboard download cancelled by power button");
    returnToUser();
    return;
  }
  if (downloadResult != HttpDownloader::OK) {
    const unsigned long fetchElapsedS = (millis() - fetchStartedAt + 500) / 1000;
    LOG_ERR("REMOTE", "Image download failed: %d (HTTP %d, %u bytes in %lu s)", static_cast<int>(downloadResult),
            lastHttpStatus, static_cast<unsigned>(lastBytesReceived), fetchElapsedS);
    state = State::Failed;
    errorMessage = tr(STR_REMOTE_IMAGE_FETCH_FAILED);
    switch (downloadResult) {
      case HttpDownloader::TIMED_OUT:
        snprintf(statusDetail, sizeof(statusDetail), "timeout %uB %lus", static_cast<unsigned>(lastBytesReceived),
                 fetchElapsedS);
        break;
      case HttpDownloader::FILE_ERROR:
        snprintf(statusDetail, sizeof(statusDetail), "SD write failed %uB", static_cast<unsigned>(lastBytesReceived));
        break;
      case HttpDownloader::ABORTED:
        snprintf(statusDetail, sizeof(statusDetail), "fetch cancelled");
        break;
      default:
        if (lastHttpStatus > 0) {
          // A status line plus a failed transfer means the body did not
          // complete. The byte count says which: nothing at all, or a stall
          // part-way through.
          snprintf(statusDetail, sizeof(statusDetail), "HTTP %d %uB %lus r%d/%d c%d sd%lu", lastHttpStatus,
                   static_cast<unsigned>(lastBytesReceived), fetchElapsedS, static_cast<int>(rssiBeforeFetch),
                   static_cast<int>(rssiAfterFetch), stillAssociatedAfterFetch ? 1 : 0,
                   static_cast<unsigned long>(lastSlowestWriteMs));
        } else {
          // Never got a status line: DNS, TCP, or the TLS handshake.
          snprintf(statusDetail, sizeof(statusDetail), "no reply %uB %lus", static_cast<unsigned>(lastBytesReceived),
                   fetchElapsedS);
        }
        break;
    }
  } else if (!validateImageFile(tempPath())) {
    Storage.remove(tempPath());
    state = State::Failed;
    errorMessage = tr(STR_REMOTE_IMAGE_INVALID);
    snprintf(statusDetail, sizeof(statusDetail), "bad image: %s", lastBmpError);
  } else if (!promoteDownloadedImage()) {
    Storage.remove(tempPath());
    state = State::Failed;
    errorMessage = tr(STR_REMOTE_IMAGE_FETCH_FAILED);
    snprintf(statusDetail, sizeof(statusDetail), "cache swap failed");
  } else {
    cachedImageAvailable = true;
    state = State::Showing;
    snprintf(statusDetail, sizeof(statusDetail), "ok #%u m%u/%u/%u", static_cast<unsigned>(++cycleCount),
             maxAllocEnterKb, maxAllocPreFetchKb, static_cast<unsigned>(ESP.getMaxAllocHeap() / 1024));
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
  } else {
    finishInteractiveCycle();
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
    options.maxStallMs = FETCH_STALL_MS;
    options.cancelRequested = cancelled;
    options.outHttpStatus = &lastHttpStatus;
    options.outBytesReceived = &lastBytesReceived;
    options.outSlowestWriteMs = &lastSlowestWriteMs;
    return HttpDownloader::downloadToFile(dashboardUrl(), tempPath(), options);
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
  if (Storage.exists(tempPath())) Storage.remove(tempPath());
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

  APP_STATE.activeDashboardMode = activeDashboardMode();
  APP_STATE.saveToFile();
  const uint32_t intervalS = refreshMinutes() * 60u;
  const uint32_t intervalMs = intervalS * 1000u;
  const uint32_t cycleElapsedMs = millis() - cycleStartMs;
  const uint32_t sleepMs = intervalMs == 0 ? 1000u : intervalMs - (cycleElapsedMs % intervalMs);
  const uint32_t sleepS = std::max<uint32_t>(1u, (sleepMs + 999u) / 1000u);
  LOG_INF("REMOTE", "Dashboard armed after %lu ms, sleeping for %u s", cycleElapsedMs, static_cast<unsigned>(sleepS));
  enterDashboardSleep(sleepS);
}

void RemoteImageDashboardActivity::scheduleNextInteractiveRefresh() {
  if (autoRefresh) return;
  const uint32_t intervalMs = std::max<uint32_t>(1u, refreshMinutes()) * 60u * 1000u;
  nextInteractiveRefreshAt = millis() + intervalMs;
}

void RemoteImageDashboardActivity::exitDashboardMode() {
  if (APP_STATE.activeDashboardMode == activeDashboardMode()) {
    APP_STATE.activeDashboardMode = CrossPointState::DASHBOARD_NONE;
    APP_STATE.saveToFile();
  }
}

std::string RemoteImageDashboardActivity::dashboardUrl() const {
  if (card == Card::CustomImage) return SETTINGS.customImageUrl;

  std::string base = workerBaseUrl(SETTINGS.dashboardWorkerUrl);
  if (base.empty()) return {};

  switch (card) {
    case Card::Clock:
      base += "/clock.bmp";
      break;
    case Card::Weather:
      base += "/weather.bmp";
      break;
    case Card::Moon:
      base += "/moon.bmp";
      break;
    case Card::Rss:
      base += "/rss.bmp";
      break;
    case Card::Today:
      base += "/today.bmp";
      break;
    case Card::Quote:
      base += "/quote.bmp";
      break;
    case Card::Bitcoin:
      base += "/bitcoin.bmp";
      break;
    case Card::Solar:
      base += "/solar.bmp";
      break;
    case Card::CustomImage:
      break;
  }
  base += gpio.deviceIsX3() ? "?device=x3" : "?device=x4";
  base += SETTINGS.lockScreenOrientation == CrossPointSettings::LOCK_ORIENT_PORTRAIT ? "&orientation=portrait"
                                                                                     : "&orientation=landscape";
  if (card == Card::Rss) {
    base += "&feed=" + urlEncode(SETTINGS.rssFeedUrl);
    if (SETTINGS.rssFeedTitle[0] != '\0') base += "&title=" + urlEncode(SETTINGS.rssFeedTitle);
  } else if (card == Card::Today) {
    base += "&lang=" + urlEncode(SETTINGS.todayLanguage[0] != '\0' ? SETTINGS.todayLanguage : "en");
  } else if (card == Card::Bitcoin) {
    static constexpr const char* CURRENCIES[] = {"USD", "GBP", "EUR"};
    const uint8_t currency = SETTINGS.bitcoinCurrency < CrossPointSettings::BITCOIN_CURRENCY_COUNT
                                 ? SETTINGS.bitcoinCurrency
                                 : CrossPointSettings::BITCOIN_GBP;
    base += "&currency=";
    base += CURRENCIES[currency];
  }
  return base;
}

const char* RemoteImageDashboardActivity::configuredUrl() const {
  if (card == Card::CustomImage) return SETTINGS.customImageUrl;
  if (SETTINGS.dashboardWorkerUrl[0] == '\0') return SETTINGS.dashboardWorkerUrl;
  return card == Card::Rss ? SETTINGS.rssFeedUrl : SETTINGS.dashboardWorkerUrl;
}

char* RemoteImageDashboardActivity::configuredUrlBuffer() const {
  if (card == Card::CustomImage) return SETTINGS.customImageUrl;
  if (SETTINGS.dashboardWorkerUrl[0] == '\0') return SETTINGS.dashboardWorkerUrl;
  return card == Card::Rss ? SETTINGS.rssFeedUrl : SETTINGS.dashboardWorkerUrl;
}

size_t RemoteImageDashboardActivity::configuredUrlCapacity() const {
  if (card == Card::CustomImage) return sizeof(SETTINGS.customImageUrl);
  if (SETTINGS.dashboardWorkerUrl[0] == '\0') return sizeof(SETTINGS.dashboardWorkerUrl);
  return card == Card::Rss ? sizeof(SETTINGS.rssFeedUrl) : sizeof(SETTINGS.dashboardWorkerUrl);
}

const char* RemoteImageDashboardActivity::imagePath() const {
  switch (card) {
    case Card::Clock:
      return "/.crosspoint/clock-image.bmp";
    case Card::Weather:
      return "/.crosspoint/weather-image.bmp";
    case Card::Moon:
      return "/.crosspoint/moon-image.bmp";
    case Card::Rss:
      return "/.crosspoint/rss-image.bmp";
    case Card::Today:
      return "/.crosspoint/today-image.bmp";
    case Card::Quote:
      return "/.crosspoint/quote-image.bmp";
    case Card::Bitcoin:
      return "/.crosspoint/bitcoin-image.bmp";
    case Card::Solar:
      return "/.crosspoint/solar-image.bmp";
    case Card::CustomImage:
      return "/.crosspoint/custom-image.bmp";
  }
  return "/.crosspoint/custom-image.bmp";
}

const char* RemoteImageDashboardActivity::tempPath() const {
  switch (card) {
    case Card::Clock:
      return "/.crosspoint/clock-image.tmp";
    case Card::Weather:
      return "/.crosspoint/weather-image.tmp";
    case Card::Moon:
      return "/.crosspoint/moon-image.tmp";
    case Card::Rss:
      return "/.crosspoint/rss-image.tmp";
    case Card::Today:
      return "/.crosspoint/today-image.tmp";
    case Card::Quote:
      return "/.crosspoint/quote-image.tmp";
    case Card::Bitcoin:
      return "/.crosspoint/bitcoin-image.tmp";
    case Card::Solar:
      return "/.crosspoint/solar-image.tmp";
    case Card::CustomImage:
      return "/.crosspoint/custom-image.tmp";
  }
  return "/.crosspoint/custom-image.tmp";
}

const char* RemoteImageDashboardActivity::backupPath() const {
  switch (card) {
    case Card::Clock:
      return "/.crosspoint/clock-image.bak";
    case Card::Weather:
      return "/.crosspoint/weather-image.bak";
    case Card::Moon:
      return "/.crosspoint/moon-image.bak";
    case Card::Rss:
      return "/.crosspoint/rss-image.bak";
    case Card::Today:
      return "/.crosspoint/today-image.bak";
    case Card::Quote:
      return "/.crosspoint/quote-image.bak";
    case Card::Bitcoin:
      return "/.crosspoint/bitcoin-image.bak";
    case Card::Solar:
      return "/.crosspoint/solar-image.bak";
    case Card::CustomImage:
      return "/.crosspoint/custom-image.bak";
  }
  return "/.crosspoint/custom-image.bak";
}

const char* RemoteImageDashboardActivity::title() const {
  switch (card) {
    case Card::Clock:
      return tr(STR_CLOCK_DASHBOARD);
    case Card::Weather:
      return tr(STR_WEATHER_DASHBOARD);
    case Card::Moon:
      return tr(STR_MOON_DASHBOARD);
    case Card::Rss:
      return tr(STR_RSS_DASHBOARD);
    case Card::Today:
      return tr(STR_TODAY_DASHBOARD);
    case Card::Quote:
      return tr(STR_QUOTE_DASHBOARD);
    case Card::Bitcoin:
      return tr(STR_BITCOIN_DASHBOARD);
    case Card::Solar:
      return tr(STR_SOLAR_DASHBOARD);
    case Card::CustomImage:
      return tr(STR_CUSTOM_IMAGE_DASHBOARD);
  }
  return tr(STR_CUSTOM_IMAGE_DASHBOARD);
}

const char* RemoteImageDashboardActivity::urlLabel() const {
  if (card == Card::CustomImage) return tr(STR_CUSTOM_IMAGE_URL);
  if (card == Card::Rss && SETTINGS.dashboardWorkerUrl[0] != '\0') return tr(STR_RSS_FEED_URL);
  return tr(STR_DASHBOARD_WORKER_URL);
}

uint8_t RemoteImageDashboardActivity::refreshMinutes() const {
  switch (card) {
    case Card::Clock:
      return SETTINGS.clockRefreshMinutes;
    case Card::Weather:
      return SETTINGS.weatherRefreshMinutes;
    case Card::Moon:
      return SETTINGS.moonRefreshMinutes;
    case Card::Rss:
      return SETTINGS.rssRefreshMinutes;
    case Card::Today:
      return SETTINGS.todayRefreshMinutes;
    case Card::Quote:
      return SETTINGS.quoteRefreshMinutes;
    case Card::Bitcoin:
      return SETTINGS.bitcoinRefreshMinutes;
    case Card::Solar:
      return SETTINGS.solarRefreshMinutes;
    case Card::CustomImage:
      return SETTINGS.customImageRefreshMinutes;
  }
  return SETTINGS.customImageRefreshMinutes;
}

uint8_t RemoteImageDashboardActivity::activeDashboardMode() const {
  switch (card) {
    case Card::Clock:
      return CrossPointState::DASHBOARD_REMOTE_CLOCK;
    case Card::Weather:
      return CrossPointState::DASHBOARD_REMOTE_WEATHER;
    case Card::Moon:
      return CrossPointState::DASHBOARD_REMOTE_MOON;
    case Card::Rss:
      return CrossPointState::DASHBOARD_REMOTE_RSS;
    case Card::Today:
      return CrossPointState::DASHBOARD_REMOTE_TODAY;
    case Card::Quote:
      return CrossPointState::DASHBOARD_REMOTE_QUOTE;
    case Card::Bitcoin:
      return CrossPointState::DASHBOARD_REMOTE_BITCOIN;
    case Card::Solar:
      return CrossPointState::DASHBOARD_REMOTE_SOLAR;
    case Card::CustomImage:
      return CrossPointState::DASHBOARD_CUSTOM_IMAGE;
  }
  return CrossPointState::DASHBOARD_NONE;
}

void RemoteImageDashboardActivity::recoverInterruptedSwap() {
  // Preserve the last image from builds that had one shared Remote Image
  // cache. It belongs to whichever card the settings migration selected.
  const char* legacyImagePath = Storage.exists("/.crosspoint/remote-image.bmp") ? "/.crosspoint/remote-image.bmp"
                                                                                : "/.crosspoint/remote-image.bak";
  if (!Storage.exists(imagePath()) && Storage.exists(legacyImagePath)) {
    if (Storage.rename(legacyImagePath, imagePath())) {
      LOG_INF("REMOTE", "Migrated legacy dashboard image cache");
    }
  }
  if (!Storage.exists(imagePath()) && Storage.exists(backupPath())) {
    if (Storage.rename(backupPath(), imagePath())) {
      LOG_INF("REMOTE", "Recovered previous dashboard image after interrupted update");
    }
  } else if (Storage.exists(imagePath()) && Storage.exists(backupPath())) {
    Storage.remove(backupPath());
  }
  if (Storage.exists(tempPath())) Storage.remove(tempPath());
  if (Storage.exists("/.crosspoint/remote-image.tmp")) Storage.remove("/.crosspoint/remote-image.tmp");
  if (Storage.exists("/.crosspoint/remote-image.bak")) Storage.remove("/.crosspoint/remote-image.bak");
}

bool RemoteImageDashboardActivity::validateImageFile(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("REMOTE", path, file)) return false;

  std::array<uint8_t, 54> header = {};
  const uint64_t fileSize = file.fileSize64();
  const int bytesRead = file.read(header.data(), header.size());
  file.close();
  if (bytesRead != static_cast<int>(header.size())) {
    lastBmpError = "short read";
    return false;
  }

  RemoteImageValidation::BmpInfo info;
  const auto result = RemoteImageValidation::validateBmp(header.data(), header.size(), fileSize, &info);
  if (result != RemoteImageValidation::BmpError::Ok) {
    lastBmpError = RemoteImageValidation::errorToString(result);
    LOG_ERR("REMOTE", "BMP validation failed: %s", lastBmpError);
    return false;
  }

  HalFile bitmapFile;
  if (!Storage.openFileForRead("REMOTE", path, bitmapFile)) return false;
  Bitmap bitmap(bitmapFile, true);
  const auto bitmapResult = bitmap.parseHeaders();
  bitmapFile.close();
  if (bitmapResult != BmpReaderError::Ok) {
    lastBmpError = Bitmap::errorToString(bitmapResult);
    LOG_ERR("REMOTE", "BMP renderer rejected image: %s", lastBmpError);
    return false;
  }

  LOG_INF("REMOTE", "Validated %ldx%ld %u-bpp BMP", static_cast<long>(info.width), static_cast<long>(info.height),
          static_cast<unsigned>(info.bitsPerPixel));
  return true;
}

bool RemoteImageDashboardActivity::promoteDownloadedImage() {
  if (Storage.exists(backupPath()) && !Storage.remove(backupPath())) return false;

  const bool hadPrevious = Storage.exists(imagePath());
  if (hadPrevious && !Storage.rename(imagePath(), backupPath())) {
    LOG_ERR("REMOTE", "Could not stage previous dashboard image");
    return false;
  }

  if (Storage.rename(tempPath(), imagePath())) {
    if (hadPrevious) Storage.remove(backupPath());
    return true;
  }

  LOG_ERR("REMOTE", "Could not promote downloaded dashboard image");
  if (hadPrevious && !Storage.rename(backupPath(), imagePath())) {
    LOG_ERR("REMOTE", "Could not restore previous dashboard image");
  }
  return false;
}

void RemoteImageDashboardActivity::render(RenderLock&&) {
  switch (state) {
    case State::Connecting:
    case State::Fetching:
      // Leave the last known-good dashboard visible while WiFi and HTTPS run.
      // Only first use, when no cache exists, needs an explicit status screen.
      if (!cachedImageAvailable || !renderCachedImage()) {
        if (autoRefresh)
          renderDefaultSleepScreen();
        else
          renderMessage(tr(STR_REMOTE_IMAGE_UPDATING));
      }
      break;
    case State::Failed:
      if (!cachedImageAvailable || !renderCachedImage()) {
        if (autoRefresh) {
          renderDefaultSleepScreen();
        } else {
          renderMessage(errorMessage ? errorMessage : tr(STR_REMOTE_IMAGE_FETCH_FAILED));
        }
      }
      break;
    case State::Showing:
      if (!renderCachedImage()) {
        if (autoRefresh)
          renderDefaultSleepScreen();
        else
          renderMessage(tr(STR_REMOTE_IMAGE_INVALID));
      }
      break;
  }
}

bool RemoteImageDashboardActivity::renderCachedImage() const {
  HalFile file;
  if (!Storage.openFileForRead("REMOTE", imagePath(), file)) return false;

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
  drawStatusFooter(pageWidth, pageHeight);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  renderer.setOrientation(originalOrientation);
  file.close();
  return true;
}

// A card keeps its last good image through every outcome, so without this an
// open card that is connecting, one that cannot reach its worker, and one
// refreshing normally are indistinguishable. Only on an open card: an
// unattended lock screen stays clean, and the same text is in the serial log
// either way. It is diagnostic rather than prose, so deliberately not tr()'d.
// Repainting only happens at the end of a cycle, so the line shows the last
// completed outcome -- a status that never advances past "starting" means the
// refresh never ran, which is the point.
void RemoteImageDashboardActivity::drawStatusFooter(const int pageWidth, const int pageHeight) const {
  if (autoRefresh || statusDetail[0] == '\0') return;

  // Sit above the physical bezel, not at the raw panel edge, or the line is
  // half-hidden behind the case.
  int marginTop = 0, marginRight = 0, marginBottom = 0, marginLeft = 0;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);

  constexpr int BAR_HEIGHT = 18;
  // getOrientedViewableTRBL's bottom margin is not enough on the X3, whose case
  // overlaps further up the panel; lift the line clear of it.
  const int extraBezelClearance = pageHeight / 12;
  const int barTop = pageHeight - marginBottom - extraBezelClearance - BAR_HEIGHT;
  renderer.fillRect(marginLeft, barTop, pageWidth - marginLeft - marginRight, BAR_HEIGHT, false);
  renderer.drawLine(marginLeft, barTop, pageWidth - marginRight, barTop, true);
  renderer.drawCenteredText(SMALL_FONT_ID, barTop + 4, statusDetail);
}

void RemoteImageDashboardActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));
  renderer.invertScreen();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

void RemoteImageDashboardActivity::renderMessage(const char* message) const {
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 30, title(), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 5, message);
  if (statusDetail[0] != '\0') {
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 30, statusDetail);
  }

  if (!autoRefresh && state == State::Failed) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_REMOTE_IMAGE_CHANGE_URL), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
