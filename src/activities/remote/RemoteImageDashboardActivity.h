#pragma once

#include "activities/Activity.h"
#include "network/HttpDownloader.h"

// Generic externally-rendered dashboard. The device only owns transport,
// validation, display, and timed sleep; dashboard generation stays off-device.
class RemoteImageDashboardActivity final : public Activity {
 public:
  explicit RemoteImageDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        bool autoRefresh = false)
      : Activity("RemoteImageDashboard", renderer, mappedInput), autoRefresh(autoRefresh) {}

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
  static constexpr const char* IMAGE_PATH = "/.crosspoint/remote-image.bmp";
  static constexpr const char* TEMP_PATH = "/.crosspoint/remote-image.tmp";
  static constexpr const char* BACKUP_PATH = "/.crosspoint/remote-image.bak";

  const bool autoRefresh;
  State state = State::Connecting;
  bool wifiUsed = false;
  bool cachedImageAvailable = false;
  bool powerInputArmed = false;
  bool powerExitRequested = false;
  bool powerInterruptAttached = false;
  mutable bool partialRefreshTestPending = false;
  unsigned long cycleStartMs = 0;
  unsigned long wifiConnectStart = 0;
  unsigned long sleepAt = 0;
  const char* errorMessage = nullptr;

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

  void recoverInterruptedSwap();
  bool validateImageFile(const char* path) const;
  bool promoteDownloadedImage();
  bool renderCachedImage() const;
  void runPartialRefreshTest() const;
  void renderMessage(const char* message) const;
};
