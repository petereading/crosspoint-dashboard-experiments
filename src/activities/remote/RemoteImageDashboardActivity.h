#pragma once

#include <atomic>

#include "activities/Activity.h"

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
  bool skipLoopDelay() override { return state == State::Connecting || state == State::Fetching || renderPending; }
  bool preventAutoSleep() override { return autoRefresh || state != State::Failed; }

 private:
  enum class State { Connecting, Fetching, Showing, Failed };

  static constexpr unsigned long WIFI_TIMEOUT_MS = 45000;
  static constexpr unsigned long DISPLAY_GRACE_INTERACTIVE_MS = 20000;
  static constexpr uint32_t DOWNLOAD_TASK_STACK_SIZE = 24576;
  static constexpr const char* IMAGE_PATH = "/.crosspoint/remote-image.bmp";
  static constexpr const char* TEMP_PATH = "/.crosspoint/remote-image.tmp";
  static constexpr const char* BACKUP_PATH = "/.crosspoint/remote-image.bak";

  const bool autoRefresh;
  State state = State::Connecting;
  bool wifiUsed = false;
  bool cachedImageAvailable = false;
  bool exitRequested = false;
  bool downloadStarted = false;
  bool renderPending = false;
  bool renderSeenBusy = false;
  bool sleepAfterRender = false;
  bool finalRenderNeeded = false;
  std::atomic<bool> downloadDone{false};
  std::atomic<int> downloadResult{0};
  unsigned long wifiConnectStart = 0;
  unsigned long sleepAt = 0;
  const char* errorMessage = nullptr;

  void promptUrl();
  void beginUpdate();
  void startDirectWifiConnect();
  void startFetchTask();
  static void fetchTaskTrampoline(void* param);
  void processFetchResult();
  void queueRender(bool sleepAfter);
  void serviceRenderState();
  void returnToUser();
  void goToSleepAndPoll();
  void exitDashboardMode();

  void recoverInterruptedSwap();
  bool validateImageFile(const char* path) const;
  bool promoteDownloadedImage();
  bool renderCachedImage() const;
  void renderMessage(const char* message) const;
};
