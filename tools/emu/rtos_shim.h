// The slice of FreeRTOS the firmware uses -- audio.cpp's queue and task -- on
// std::thread. One tick is one millisecond here.
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

struct EmuQueue {
  std::mutex m;
  std::condition_variable cv;
  std::deque<std::vector<uint8_t>> items;
  size_t cap = 0, size = 0;
};
typedef EmuQueue *QueueHandle_t;

// Leaked, like the firmware's: a detached task may still wait on it at exit.
inline QueueHandle_t xQueueCreate(size_t len, size_t size) {
  QueueHandle_t q = new EmuQueue;
  q->cap = len; q->size = size;
  return q;
}
// A full queue drops the item whatever the timeout; the firmware only sends with 0.
inline BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t) {
  std::lock_guard<std::mutex> l(q->m);
  if (q->items.size() >= q->cap) return pdFALSE;
  const uint8_t *p = (const uint8_t *)item;
  q->items.emplace_back(p, p + q->size);
  q->cv.notify_one();
  return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t q, void *out, TickType_t ticks) {
  std::unique_lock<std::mutex> l(q->m);
  auto ready = [q] { return !q->items.empty(); };
  if (ticks == portMAX_DELAY) q->cv.wait(l, ready);
  else if (!q->cv.wait_for(l, std::chrono::milliseconds(ticks), ready)) return pdFALSE;
  memcpy(out, q->items.front().data(), q->size);
  q->items.pop_front();
  return pdTRUE;
}
inline BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *, uint32_t, void *arg,
                                          int, TaskHandle_t *h, int) {
  std::thread(fn, arg).detach();
  if (h) *h = nullptr;
  return pdPASS;
}
