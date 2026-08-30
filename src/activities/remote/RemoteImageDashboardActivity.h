#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
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
  bool skipLoopDelay() override { return state == State::Connecting || (autoRefresh && state == State::Fetching); }
  bool preventAutoSleep() override { return autoRefresh || state != State::Failed; }

 private:
  enum class State { Connecting, Fetching, Showing, Failed };

  static constexpr unsigned long WIFI_TIMEOUT_MS = 45000;
  static constexpr unsigned long FETCH_TOTAL_TIMEOUT_MS = 60000;
  static constexpr unsigned long FETCH_FIRST_ATTEMPT_MS = 30000;
  static constexpr unsigned long FETCH_OPERATION_TIMEOUT_MS = 20000;
  static constexpr unsigned long WIFI_RETRY_TIMEOUT_MS = 8000;

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
  TaskHandle_t interactiveFetchTaskHandle = nullptr;
  std::atomic<bool> interactiveFetchFinished{false};
  std::atomic<bool> interactiveFetchCancelRequested{false};
  HttpDownloader::DownloadError interactiveFetchResult = HttpDownloader::HTTP_ERROR;

  void promptUrl();
  void beginUpdate();
  void startDirectWifiConnect();
  void runFetch();
  void startInteractiveFetch();
  static void interactiveFetchTask(void* context);
  void completeFetch(HttpDownloader::DownloadError downloadResult);
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
  bool validateImageFile(const char* path) const;
  bool promoteDownloadedImage();
  bool renderCachedImage() const;
  void renderDefaultSleepScreen() const;
  void renderMessage(const char* message) const;
};
