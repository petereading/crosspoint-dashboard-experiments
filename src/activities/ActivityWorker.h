#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// Lifecycle-aware, single-shot worker for activity-owned blocking operations.
// The owning activity must remain alive until isComplete() is true, or commit
// to a chip restart after requestCancel() so the task cannot outlive its owner.
class ActivityWorker {
 public:
  using RunCallback = void (*)(ActivityWorker& worker, void* context);

  ActivityWorker() = default;
  ~ActivityWorker();

  ActivityWorker(const ActivityWorker&) = delete;
  ActivityWorker& operator=(const ActivityWorker&) = delete;

  bool start(const char* taskName, uint32_t stackSize, RunCallback callback, void* context, UBaseType_t priority = 1);
  void requestCancel();
  bool cancelRequested() const;
  bool isRunning() const;
  bool isComplete() const;
  bool reset();

 private:
  static void taskTrampoline(void* parameter);

  std::atomic<bool> cancelRequestedFlag{false};
  std::atomic<bool> running{false};
  std::atomic<bool> complete{false};
  RunCallback callback = nullptr;
  void* context = nullptr;
};
