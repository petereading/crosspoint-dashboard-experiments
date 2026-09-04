#pragma once

#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/Activity.h"
#include "network/HttpDownloader.h"

// Generic externally-rendered dashboard. The device only owns transport,
// validation, display, and timed sleep; dashboard generation stays off-device.
class RemoteImageDashboardActivity final : public Activity {
 public:
  // slot indexes CrossPointSettings::lockScreenCardUrl. Every card is a URL and
  // an interval; nothing else distinguishes one from another.
  explicit RemoteImageDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint8_t slot,
                                        bool autoRefresh = false)
      : Activity("RemoteImageDashboard", renderer, mappedInput), slot(slot), autoRefresh(autoRefresh) {
    buildCachePaths();
  }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == State::Connecting || state == State::Fetching; }
  bool preventAutoSleep() override { return autoRefresh || state != State::Failed; }

 private:
  enum class State { Connecting, Fetching, Showing, Failed };

  static constexpr unsigned long WIFI_TIMEOUT_MS = 45000;
  static constexpr unsigned long FETCH_TOTAL_TIMEOUT_MS = 25000;
  static constexpr unsigned long FETCH_FIRST_ATTEMPT_MS = 9000;
  static constexpr unsigned long FETCH_OPERATION_TIMEOUT_MS = 3000;
  static constexpr unsigned long WIFI_RETRY_TIMEOUT_MS = 5000;
  static constexpr unsigned long DISPLAY_GRACE_INTERACTIVE_MS = 20000;

  const uint8_t slot;
  const bool autoRefresh;
  // Per-slot cache filenames, built once. These used to be a switch returning
  // string literals per card kind; with numbered slots they are just formatted.
  char imagePathBuf[40] = {};
  char tempPathBuf[40] = {};
  char backupPathBuf[40] = {};
  char titleBuf[32] = {};
  State state = State::Connecting;
  bool wifiUsed = false;
  bool cachedImageAvailable = false;
  bool powerInputArmed = false;
  bool powerExitRequested = false;
  bool powerInterruptAttached = false;
  unsigned long cycleStartMs = 0;
  unsigned long wifiConnectStart = 0;
  unsigned long sleepAt = 0;
  const char* errorMessage = nullptr;
  // Why the last fetch failed, in terms that separate the three faults the
  // single "Image download failed" string used to hide: a bad HTTP status, a
  // timeout, and a failure to write the temp file to SD.
  char failureDetail[48] = {};
  int lastHttpStatus = 0;
  size_t lastBytesReceived = 0;

  void buildCachePaths();
  void promptUrl();
  void beginUpdate();
  void startDirectWifiConnect();
  void runFetch();
  HttpDownloader::DownloadError downloadDashboardImage();
  bool reconnectWifiForRetry(unsigned long timeoutMs);
  void startPowerLatch();
  void stopPowerLatch();
  bool powerLatchTriggered();
  void returnToUser();
  void goToSleepAndPoll();
  void exitDashboardMode();

  // The URL is used exactly as configured -- no route, device or orientation is
  // appended. Anything the image needs to know travels in the URL itself.
  std::string dashboardUrl() const { return SETTINGS.lockScreenCardUrl[slot]; }
  const char* configuredUrl() const { return SETTINGS.lockScreenCardUrl[slot]; }
  char* configuredUrlBuffer() const { return SETTINGS.lockScreenCardUrl[slot]; }
  size_t configuredUrlCapacity() const { return CrossPointSettings::LOCK_SCREEN_CARD_URL_LEN; }
  const char* imagePath() const { return imagePathBuf; }
  const char* tempPath() const { return tempPathBuf; }
  const char* backupPath() const { return backupPathBuf; }
  const char* title() const { return titleBuf; }
  const char* urlLabel() const;
  uint8_t refreshMinutes() const { return SETTINGS.cardRefreshMinutes(slot); }
  uint8_t activeDashboardMode() const { return CrossPointState::DASHBOARD_CARD_BASE + slot; }

  void recoverInterruptedSwap();
  bool validateImageFile(const char* path) const;
  bool promoteDownloadedImage();
  bool renderCachedImage() const;
  void renderDefaultSleepScreen() const;
  void renderMessage(const char* message) const;
};
