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
  // Why the last refresh failed, in technical terms ("HTTP 404", "bad image:
  // NotBmp"). A failed refresh keeps the last good card on screen, which is
  // right for an unattended lock screen but leaves an open card looking merely
  // stale, so an interactive card shows this in a footer. Fixed buffer: no
  // allocation on an error path. Empty once a refresh succeeds.
  char failureDetail[48] = {};
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
  void drawFailureFooter(int pageWidth, int pageHeight) const;
  void renderDefaultSleepScreen() const;
  void renderMessage(const char* message) const;
};
