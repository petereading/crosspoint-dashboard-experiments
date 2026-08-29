#include "ActivityWorker.h"

#include <Logging.h>

#include <cassert>

ActivityWorker::~ActivityWorker() {
  // Deleting a task externally would skip C++ destructors for its active stack
  // frames. Activities must therefore cancel cooperatively and either observe
  // completion or commit to a chip restart before destruction.
  assert(!isRunning() && "ActivityWorker destroyed while its task is running");
}

bool ActivityWorker::start(const char* taskName, uint32_t stackSize, RunCallback runCallback, void* runContext,
                           UBaseType_t priority) {
  if (!runCallback || !reset()) return false;

  callback = runCallback;
  context = runContext;
  running.store(true, std::memory_order_release);

  // FreeRTOS owns the task control block and stack until the trampoline
  // self-deletes. This is the only allocation performed by ActivityWorker.
  const BaseType_t result = xTaskCreate(taskTrampoline, taskName, stackSize, this, priority, nullptr);
  if (result != pdPASS) {
    context = nullptr;
    callback = nullptr;
    running.store(false, std::memory_order_release);
    LOG_ERR("WORKER", "Could not create task %s (%lu-byte stack)", taskName, static_cast<unsigned long>(stackSize));
    return false;
  }
  return true;
}

void ActivityWorker::requestCancel() { cancelRequestedFlag.store(true, std::memory_order_release); }

bool ActivityWorker::cancelRequested() const { return cancelRequestedFlag.load(std::memory_order_acquire); }

bool ActivityWorker::isRunning() const { return running.load(std::memory_order_acquire); }

bool ActivityWorker::isComplete() const { return complete.load(std::memory_order_acquire); }

bool ActivityWorker::reset() {
  if (isRunning()) return false;
  cancelRequestedFlag.store(false, std::memory_order_release);
  complete.store(false, std::memory_order_release);
  context = nullptr;
  callback = nullptr;
  return true;
}

void ActivityWorker::taskTrampoline(void* parameter) {
  auto* worker = static_cast<ActivityWorker*>(parameter);
  worker->callback(*worker, worker->context);

  // Clear the callback state, publish the result as complete, then publish
  // that the worker is no longer running. The owner only consumes completion
  // after isRunning() becomes false, so it cannot reset this object between
  // those two stores.
  worker->callback = nullptr;
  worker->context = nullptr;
  worker->complete.store(true, std::memory_order_release);
  worker->running.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}
