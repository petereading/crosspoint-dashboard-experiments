#pragma once

#include <string>

#include "activities/Activity.h"
#include "network/HttpDownloader.h"

// Generic externally-rendered dashboard. The device only owns transport,
// validation, display, and timed sleep; dashboard generation stays off-device.
class RemoteImageDashboardActivity final : public Activity {
 public:
  enum class Card { Clock, Weather, CustomImage, Moon, Rss, Today, Quote, Bitcoin, Solar };

  explicit RemoteImageDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Card card,
                                        bool autoRefresh = false)
      : Activity("RemoteImageDashboard", renderer, mappedInput), card(card), autoRefresh(autoRefresh) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == State::Connecting || state == State::Fetching; }
  bool preventAutoSleep() override { return autoRefresh || state != State::Failed; }
  // An open card is a display, not a busy task: between refreshes it holds a
  // static e-ink frame with the radio off, so let the CPU idle down. beginUpdate()
  // restores full speed before the next connect.
  bool allowClockThrottle() override { return !autoRefresh && (state == State::Showing || state == State::Failed); }

 private:
  enum class State { Connecting, Fetching, Showing, Failed };

  static constexpr unsigned long WIFI_TIMEOUT_MS = 45000;
  static constexpr unsigned long FETCH_TOTAL_TIMEOUT_MS = 40000;
  static constexpr unsigned long FETCH_FIRST_ATTEMPT_MS = 18000;
  static constexpr unsigned long FETCH_OPERATION_TIMEOUT_MS = 15000;
  static constexpr unsigned long WIFI_RETRY_TIMEOUT_MS = 7000;

  const Card card;
  const bool autoRefresh;
  State state = State::Connecting;
  bool wifiUsed = false;
  bool cachedImageAvailable = false;
  bool powerInputArmed = false;
  bool powerExitRequested = false;
  bool powerInterruptAttached = false;
  unsigned long cycleStartMs = 0;
  unsigned long wifiConnectStart = 0;
  unsigned long nextInteractiveRefreshAt = 0;
  const char* errorMessage = nullptr;
  // What the last refresh did, in technical terms ("ok #3", "HTTP 404 from
  // worker", "bad image: NotBmp"). A card keeps its last good image whatever
  // happens, so without this an open card that cannot reach its worker, one
  // still connecting, and one refreshing normally all look identical. Shown in
  // a footer on an interactive card only; an unattended lock screen stays
  // clean. Fixed buffer: no allocation on an error path.
  char statusDetail[48] = {};
  uint16_t cycleCount = 0;
  int lastHttpStatus = 0;
  // Points into RemoteImageValidation's static strings, so it needs no storage.
  const char* lastBmpError = "unknown";

  void promptUrl();
  void beginUpdate();
  void startDirectWifiConnect();
  void promptWifiSelection();
  void shutdownWifi();
  void finishInteractiveCycle();
  void runFetch();
  HttpDownloader::DownloadError downloadDashboardImage();
  bool reconnectWifiForRetry(unsigned long timeoutMs);
  void startPowerLatch();
  void stopPowerLatch();
  bool powerLatchTriggered();
  void returnToUser();
  void goToSleepAndPoll();
  void scheduleNextInteractiveRefresh();
  void exitDashboardMode();

  std::string dashboardUrl() const;
  const char* configuredUrl() const;
  char* configuredUrlBuffer() const;
  size_t configuredUrlCapacity() const;
  const char* imagePath() const;
  const char* tempPath() const;
  const char* backupPath() const;
  const char* title() const;
  const char* urlLabel() const;
  uint8_t refreshMinutes() const;
  uint8_t activeDashboardMode() const;

  void recoverInterruptedSwap();
  bool validateImageFile(const char* path);
  bool promoteDownloadedImage();
  bool renderCachedImage() const;
  void drawStatusFooter(int pageWidth, int pageHeight) const;
  void renderDefaultSleepScreen() const;
  void renderMessage(const char* message) const;
};
