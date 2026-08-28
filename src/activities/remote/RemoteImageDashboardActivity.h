#pragma once

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
  bool skipLoopDelay() override { return state == State::Connecting || state == State::Fetching; }
  bool preventAutoSleep() override { return autoRefresh || state != State::Failed; }

 private:
  enum class State { Connecting, Fetching, Showing, Failed };

  static constexpr unsigned long WIFI_TIMEOUT_MS = 45000;
  static constexpr unsigned long DISPLAY_GRACE_INTERACTIVE_MS = 20000;
  static constexpr uint32_t POWER_LATCH_TASK_STACK_SIZE = 2048;
  static constexpr const char* IMAGE_PATH = "/.crosspoint/remote-image.bmp";
  static constexpr const char* TEMP_PATH = "/.crosspoint/remote-image.tmp";
  static constexpr const char* BACKUP_PATH = "/.crosspoint/remote-image.bak";

  const bool autoRefresh;
  State state = State::Connecting;
  bool wifiUsed = false;
  bool cachedImageAvailable = false;
  bool powerInputArmed = false;
  std::atomic<bool> powerExitRequested{false};
  TaskHandle_t powerLatchTask = nullptr;
  unsigned long wifiConnectStart = 0;
  unsigned long sleepAt = 0;
  const char* errorMessage = nullptr;

  void promptUrl();
  void beginUpdate();
  void startDirectWifiConnect();
  void runFetch();
  void startPowerLatch();
  void stopPowerLatch();
  static void powerLatchTaskTrampoline(void* param);
  void returnToUser();
  void goToSleepAndPoll();
  void exitDashboardMode();

  void recoverInterruptedSwap();
  bool validateImageFile(const char* path) const;
  bool promoteDownloadedImage();
  bool renderCachedImage() const;
  void renderMessage(const char* message) const;
};
