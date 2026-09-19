// Minimal ESP-IDF stand-ins so the firmware's drawing and game logic can be
// compiled on the host for README screenshots. Hardware code is stripped by
// render_screens.py before this is used.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

typedef void *esp_lcd_panel_handle_t;
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
class HumanFaceDetect;

struct portMUX_TYPE { int unused; };
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))

#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))

extern int64_t host_now_us;
inline int64_t esp_timer_get_time() { return host_now_us; }

// Deterministic so every render produces identical images.
inline uint32_t esp_random() {
  static uint32_t state = 12345u;
  state = state * 1664525u + 1013904223u;
  return state;
}

namespace dl::image {
struct img_t {
  void *data;
  uint16_t width;
  uint16_t height;
};
}  // namespace dl::image
