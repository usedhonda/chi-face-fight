#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_common.h"
#include "driver/i2s_pdm.h"
#include "driver/spi_master.h"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "dl_image_jpeg.hpp"
#include "human_face_detect.hpp"

extern const unsigned char chi_walk_rgba[];
extern const unsigned char chi_actions_rgba[];
extern const unsigned char chi_kick_rgba[];
extern const unsigned char chi_hit_rgba[];
extern const unsigned char chi_victory_rgba[];
extern const unsigned char chi_punch_combo_rgba[];
extern const unsigned char chi_block_rgba[];
extern const unsigned char chi_dodge_rgba[];
extern const unsigned char chi_jump_rgba[];
extern const unsigned char chi_roundhouse_rgba[];
extern const unsigned char chi_knockdown_rgba[];
extern const unsigned char chi_get_up_rgba[];
extern const unsigned char chi_defeat_rgba[];
extern const uint16_t arena_office_rgb565[240 * 280];
extern const uint16_t arena_rooftop_rgb565[240 * 280];
extern const int ui_fight_width, ui_fight_height;
extern const int ui_hit_width, ui_hit_height;
extern const int ui_block_width, ui_block_height;
extern const int ui_combo_width, ui_combo_height;
extern const int ui_ko_width, ui_ko_height;
extern const int ui_chi_wins_width, ui_chi_wins_height;
extern const int ui_face_wins_width, ui_face_wins_height;
extern const int ui_title_width, ui_title_height;
extern const int ui_next_level_width, ui_next_level_height;
extern const int ui_exit_width, ui_exit_height;
extern const int ui_retry_width, ui_retry_height;
extern const int ui_level_up_width, ui_level_up_height;
extern const uint8_t ui_fight_rgba[];
extern const uint8_t ui_hit_rgba[];
extern const uint8_t ui_block_rgba[];
extern const uint8_t ui_combo_rgba[];
extern const uint8_t ui_ko_rgba[];
extern const uint8_t ui_chi_wins_rgba[];
extern const uint8_t ui_face_wins_rgba[];
extern const uint8_t ui_title_rgba[];
extern const uint8_t ui_next_level_rgba[];
extern const uint8_t ui_exit_rgba[];
extern const uint8_t ui_retry_rgba[];
extern const uint8_t ui_level_up_rgba[];

namespace {
constexpr int kCameraWidth = 320;
constexpr int kCameraHeight = 240;
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 280;
constexpr int kCameraY = 20;
constexpr int kCropX = 40;
constexpr int kSpriteSourceWidth = 80;
constexpr int kSpriteSourceHeight = 96;
constexpr int kSpriteWidth = 100;
constexpr int kSpriteHeight = 120;
constexpr int kSpriteFrames = 16;
constexpr int kKickFrames = 6;
constexpr int kHitFrames = 4;
constexpr int kVictoryFrames = 4;
constexpr int kPunchComboFrames = 6;
constexpr int kBlockFrames = 4;
constexpr int kDodgeFrames = 4;
constexpr int kJumpFrames = 5;
constexpr int kRoundhouseFrames = 6;
constexpr int kKnockdownFrames = 6;
constexpr int kGetUpFrames = 5;
constexpr int kDefeatFrames = 4;
constexpr float kRoamSpeed = 48.0f;
constexpr float kTouchSpeed = 96.0f;
constexpr int kInferenceEvery = 3;
constexpr int64_t kPresenceHoldUs = 600000;
constexpr int64_t kActionDurationUs = 2000000;
constexpr int64_t kTouchOverrideUs = 3500000;
constexpr int64_t kKickFrameUs = 100000;
constexpr int64_t kHitFrameUs = 125000;
constexpr int64_t kVictoryFrameUs = 160000;
constexpr int64_t kDefeatHoldUs = 1000000;
constexpr uint8_t kTouchAddress = 0x15;
constexpr uint8_t kTouchDataRegister = 0x01;
constexpr uint8_t kTouchDisableSleepRegister = 0xfe;
constexpr int kFaceSize = 88;
constexpr int kFaceX = 142;
constexpr int kFaceY = 54;
constexpr int64_t kComboTimeoutUs = 1200000;
constexpr int64_t kBossWarningUs = 750000;
constexpr int kChoiceY = 218;

static const char *TAG = "chi_creature";

enum class Action : uint8_t { Idle = 0, Wave = 1, Happy = 2, Listen = 3 };
enum class GameAction : uint8_t {
  None = 0,
  Kick,
  Hit,
  Victory,
  PunchCombo,
  Block,
  Dodge,
  Jump,
  Roundhouse,
  Knockdown,
  GetUp,
  Defeat,
};
enum class Direction : uint8_t { Right = 0, Left = 1, Up = 2, Down = 3 };
enum class FightState : uint8_t {
  FindFace = 0,
  Intro,
  Ready,
  Fighting,
  ChiWins,
  FaceWins,
};
enum class BossState : uint8_t { Idle = 0, Warning, Strike };
enum class CombatMotion : uint8_t { Home = 0, Approach, Attack, Retreat };

esp_lcd_panel_handle_t panel = nullptr;
i2c_master_bus_handle_t touch_bus = nullptr;
i2c_master_dev_handle_t touch_device = nullptr;
uint16_t *screen = nullptr;
uint16_t *face_portrait = nullptr;
uint8_t *face_alpha = nullptr;
HumanFaceDetect *detector = nullptr;

portMUX_TYPE input_mux = portMUX_INITIALIZER_UNLOCKED;
bool touch_pending = false;
bool touch_pressed = false;
int touch_x = kDisplayWidth / 2;
int touch_y = kDisplayHeight / 2;
uint32_t touch_event_count = 0;
uint32_t touch_release_count = 0;
uint32_t touch_read_errors = 0;
int64_t last_touch_poll_us = 0;
volatile float mic_level = 0.0f;
volatile bool sound_pending = false;

float chi_x = 78.0f;
float chi_y = 146.0f;
float target_x = 78.0f;
float target_y = 146.0f;
Direction direction = Direction::Down;
Action action = Action::Idle;
GameAction game_action = GameAction::None;
bool moving = false;
bool touch_directed_move = false;
bool play_kick_on_arrival = false;
bool touch_effect_active = false;
bool person_present = false;
bool raw_person = false;
bool person_enter_pending = false;
float person_score = 0.0f;
int person_count = 0;
int person_box[4] = {0, 0, 0, 0};
int stable_face_frames = 0;
int64_t last_detection_us = 0;
int64_t last_touch_us = 0;
int touch_marker_x = kDisplayWidth / 2;
int touch_marker_y = kDisplayHeight / 2;
int64_t action_until_us = 0;
int64_t game_action_started_us = 0;
int64_t next_roam_us = 0;
int64_t last_behavior_us = 0;
uint32_t frame_count = 0;
uint32_t inference_ms = 0;
uint8_t showcase_action_index = 0;
FightState fight_state = FightState::FindFace;
BossState boss_state = BossState::Idle;
int chi_hp = 100;
int boss_hp = 100;
int combo = 0;
uint32_t chi_hit_count = 0;
int64_t last_attack_us = 0;
int64_t last_boss_attack_us = 0;
int64_t boss_state_since_us = 0;
int64_t next_boss_attack_us = 0;
bool face_ready = false;
bool guard_active = false;
bool camera_active = false;
bool camera_stop_pending = false;
bool camera_start_pending = false;
int arena_index = 0;
int fight_level = 1;
bool boss_warning_feint = false;
bool boss_body_attack = false;
CombatMotion combat_motion = CombatMotion::Home;
int queued_attacks = 0;
int64_t last_combat_motion_us = 0;
int boss_draw_x = kFaceX;
int boss_draw_y = kFaceY;
int64_t round_state_since_us = 0;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xf8) << 8) | ((g & 0xfc) << 3) |
                               (b >> 3));
}

void set_pixel(int x, int y, uint16_t color) {
  if (x >= 0 && x < kDisplayWidth && y >= 0 && y < kDisplayHeight) {
    screen[y * kDisplayWidth + x] = color;
  }
}

void fill_rect(int x, int y, int width, int height, uint16_t color) {
  if (width <= 0 || height <= 0) return;
  const int x0 = std::clamp(x, 0, kDisplayWidth);
  const int y0 = std::clamp(y, 0, kDisplayHeight);
  const int x1 = std::clamp(x + width, 0, kDisplayWidth);
  const int y1 = std::clamp(y + height, 0, kDisplayHeight);
  if (x0 >= x1 || y0 >= y1) return;
  for (int py = y0; py < y1; ++py) {
    std::fill(screen + py * kDisplayWidth + x0,
              screen + py * kDisplayWidth + x1, color);
  }
}

void draw_rect(int x, int y, int width, int height, uint16_t color) {
  fill_rect(x, y, width, 2, color);
  fill_rect(x, y + height - 2, width, 2, color);
  fill_rect(x, y, 2, height, color);
  fill_rect(x + width - 2, y, 2, height, color);
}

bool point_in_rect(int px, int py, int x, int y, int width, int height) {
  return px >= x && px < x + width && py >= y && py < y + height;
}

const uint8_t *glyph(char c) {
  static const uint8_t kFont[][5] = {
      {0x00,0x00,0x00,0x00,0x00}, {0x7e,0x11,0x11,0x11,0x7e},
      {0x7f,0x49,0x49,0x49,0x36}, {0x3e,0x41,0x41,0x41,0x22},
      {0x7f,0x41,0x41,0x22,0x1c}, {0x7f,0x49,0x49,0x49,0x41},
      {0x7f,0x09,0x09,0x09,0x01}, {0x3e,0x41,0x49,0x49,0x7a},
      {0x7f,0x08,0x08,0x08,0x7f}, {0x00,0x41,0x7f,0x41,0x00},
      {0x20,0x40,0x41,0x3f,0x01}, {0x7f,0x08,0x14,0x22,0x41},
      {0x7f,0x40,0x40,0x40,0x40}, {0x7f,0x02,0x0c,0x02,0x7f},
      {0x7f,0x04,0x08,0x10,0x7f}, {0x3e,0x41,0x41,0x41,0x3e},
      {0x7f,0x09,0x09,0x09,0x06}, {0x3e,0x41,0x51,0x21,0x5e},
      {0x7f,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
      {0x01,0x01,0x7f,0x01,0x01}, {0x3f,0x40,0x40,0x40,0x3f},
      {0x1f,0x20,0x40,0x20,0x1f}, {0x3f,0x40,0x38,0x40,0x3f},
      {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07},
      {0x61,0x51,0x49,0x45,0x43},
      {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
      {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
      {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
      {0x3c,0x4a,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
      {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1e},
      {0x00,0x00,0x5f,0x00,0x00}, {0x00,0x60,0x60,0x00,0x00},
      {0x00,0x40,0x30,0x00,0x00},
  };
  if (c >= 'A' && c <= 'Z') return kFont[1 + c - 'A'];
  if (c >= '0' && c <= '9') return kFont[27 + c - '0'];
  if (c == '!') return kFont[37];
  if (c == '.') return kFont[38];
  if (c == ',') return kFont[39];
  return kFont[0];
}

void draw_text(const char *text, int x, int y, int scale, uint16_t color,
               uint16_t shadow = 0) {
  const int start_x = x;
  for (const char *p = text; *p; ++p) {
    if (*p == '\n') { x = start_x; y += 9 * scale; continue; }
    const uint8_t *g = glyph(*p);
    for (int col = 0; col < 5; ++col) {
      for (int row = 0; row < 7; ++row) {
        if ((g[col] >> row) & 1) {
          if (shadow) fill_rect(x + col * scale + scale, y + row * scale + scale,
                                scale, scale, shadow);
          fill_rect(x + col * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    x += 6 * scale;
  }
}

void draw_centered_text(const char *text, int y, int scale, uint16_t color) {
  int count = 0;
  for (const char *p = text; *p && *p != '\n'; ++p) ++count;
  draw_text(text, std::max(2, (kDisplayWidth - count * 6 * scale) / 2), y,
            scale, color, rgb565(10, 8, 20));
}

void draw_arena() {
  const uint16_t *background = arena_index == 0 ? arena_office_rgb565
                                                : arena_rooftop_rgb565;
  std::copy(background, background + kDisplayWidth * kDisplayHeight, screen);
  fill_rect(0, 0, kDisplayWidth, 25, rgb565(12, 12, 25));
}

uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t alpha) {
  const int fr = ((fg >> 11) & 31) * 255 / 31;
  const int fg_g = ((fg >> 5) & 63) * 255 / 63;
  const int fb = (fg & 31) * 255 / 31;
  const int br = ((bg >> 11) & 31) * 255 / 31;
  const int bg_g = ((bg >> 5) & 63) * 255 / 63;
  const int bb = (bg & 31) * 255 / 31;
  return rgb565((fr * alpha + br * (255 - alpha)) / 255,
                (fg_g * alpha + bg_g * (255 - alpha)) / 255,
                (fb * alpha + bb * (255 - alpha)) / 255);
}

void draw_rgba_image(const uint8_t *pixels, int width, int height, int x, int y) {
  for (int sy = 0; sy < height; ++sy) {
    const int dy = y + sy;
    if (dy < 0 || dy >= kDisplayHeight) continue;
    for (int sx = 0; sx < width; ++sx) {
      const int dx = x + sx;
      if (dx < 0 || dx >= kDisplayWidth) continue;
      const size_t offset = (static_cast<size_t>(sy) * width + sx) * 4;
      const uint8_t alpha = pixels[offset + 3];
      if (alpha < 16) continue;
      const uint16_t color = rgb565(pixels[offset], pixels[offset + 1],
                                    pixels[offset + 2]);
      const size_t destination = static_cast<size_t>(dy) * kDisplayWidth + dx;
      screen[destination] = alpha > 245
                                ? color
                                : blend565(color, screen[destination], alpha);
    }
  }
}

void draw_centered_rgba(const uint8_t *pixels, int width, int height, int y) {
  draw_rgba_image(pixels, width, height, (kDisplayWidth - width) / 2, y);
}

void draw_disc(int cx, int cy, int radius, uint16_t color) {
  for (int y = -radius; y <= radius; ++y) {
    const int half = static_cast<int>(std::sqrt(radius * radius - y * y));
    fill_rect(cx - half, cy + y, half * 2 + 1, 1, color);
  }
}

void draw_ring(int cx, int cy, int radius, uint16_t color) {
  for (int i = 0; i < 32; ++i) {
    const float angle = i * 6.2831853f / 32.0f;
    set_pixel(cx + static_cast<int>(std::cos(angle) * radius),
              cy + static_cast<int>(std::sin(angle) * radius), color);
  }
}

int64_t boss_warning_duration_us() {
  return std::max<int64_t>(220000, kBossWarningUs - (fight_level - 1) * 85000);
}

int64_t boss_attack_delay_us() {
  return std::max<int64_t>(750000, 2300000 - (fight_level - 1) * 180000);
}


void capture_face_candidate(const dl::image::img_t &image) {
  if (face_ready || !person_present || stable_face_frames < 1 ||
      face_portrait == nullptr || face_alpha == nullptr) return;
  const int person_w = person_box[2] - person_box[0];
  const int person_h = person_box[3] - person_box[1];
  if (person_w < 36 || person_h < 36) return;

  // The dedicated face model supplies the actual face box. Add modest room for
  // forehead, hair and chin, then freeze the portrait for the whole round.
  const int crop = std::clamp(std::max(person_w, person_h) * 6 / 5, 44, 160);
  const int cx = (person_box[0] + person_box[2]) / 2;
  const int x0 = std::clamp(cx - crop / 2, 0, kCameraWidth - crop);
  const int cy = (person_box[1] + person_box[3]) / 2;
  const int y0 = std::clamp(cy - crop / 2, 0, kCameraHeight - crop);
  const auto *rgb = static_cast<const uint8_t *>(image.data);
  for (int y = 0; y < kFaceSize; ++y) {
    const int sy = y0 + y * crop / kFaceSize;
    for (int x = 0; x < kFaceSize; ++x) {
      const int sx = x0 + x * crop / kFaceSize;
      const size_t source = (static_cast<size_t>(sy) * kCameraWidth + sx) * 3;
      face_portrait[y * kFaceSize + x] =
          rgb565(rgb[source], rgb[source + 1], rgb[source + 2]);
      // A feathered head silhouette removes the square camera background.
      // It deliberately leaves extra width above the eyes for hair.
      const float ny = (y - kFaceSize * 0.49f) / (kFaceSize * 0.51f);
      const float width = ny > 0.30f ? 0.86f - (ny - 0.30f) * 0.28f : 0.92f;
      const float nx = (x - kFaceSize * 0.50f) / (kFaceSize * 0.50f * width);
      const float d = nx * nx + ny * ny;
      face_alpha[y * kFaceSize + x] = d <= 0.78f ? 255
          : d >= 1.0f ? 0 : static_cast<uint8_t>((1.0f - d) / 0.22f * 255.0f);
    }
  }
  face_ready = true;
  fight_state = FightState::Intro;
  boss_state = BossState::Idle;
  chi_hp = 100;
  boss_hp = 100;
  combo = 0;
  chi_x = 12;
  chi_y = 136;
  moving = false;
  game_action = GameAction::None;
  combat_motion = CombatMotion::Home;
  queued_attacks = 0;
  last_combat_motion_us = esp_timer_get_time();
  round_state_since_us = esp_timer_get_time();
  camera_stop_pending = true;
  ESP_LOGI(TAG,
           "FACE_EVENT type=CAPTURED score=%.3f box=(%d,%d,%d,%d) crop=(%d,%d,%d)",
           person_score, person_box[0], person_box[1], person_box[2],
           person_box[3], x0, y0, crop);
}

void draw_face_portrait(int64_t now) {
  if (!face_ready) return;
  int shake_x = 0;
  if (now - last_attack_us < 180000) {
    shake_x = static_cast<int>((now / 30000) % 3) * 3 - 3;
  }
  // The boss visibly patrols instead of looking like a pinned portrait.
  const float speed = 1.0f + std::min(fight_level - 1, 8) * 0.18f;
  const int travel_x = 16 + std::min(fight_level - 1, 8) * 5;
  const int travel_y = 10 + std::min(fight_level - 1, 8) * 3;
  boss_draw_x = kFaceX + static_cast<int>(std::sin(now / (680000.0 / speed)) * travel_x);
  boss_draw_y = kFaceY + static_cast<int>(std::sin(now / (410000.0 / speed)) * travel_y);
  if (fight_state == FightState::ChiWins) {
    const int64_t defeated_age = now - round_state_since_us;
    boss_draw_x = kFaceX + static_cast<int>(std::sin(now / 65000.0) * 3.0f);
    boss_draw_y = kFaceY + std::min<int64_t>(48, defeated_age / 26000);
  }
  if (fight_level >= 4) {
    boss_draw_x += static_cast<int>(std::sin(now / 73000.0) * (fight_level - 3));
  }
  if (boss_state == BossState::Warning) {
    boss_draw_x += ((now / 45000) & 1) ? -4 : 2;
  } else if (boss_state == BossState::Strike && boss_body_attack) {
    const float phase = std::clamp((now - boss_state_since_us) / 420000.0, 0.0, 1.0);
    const float lunge = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
    boss_draw_x -= static_cast<int>(lunge * 105.0f);
  }
  if (now - last_attack_us < 220000) boss_draw_x += 18;
  boss_draw_x += shake_x;
  for (int y = 0; y < kFaceSize; ++y) {
    for (int x = 0; x < kFaceSize; ++x) {
      const uint8_t alpha = face_alpha[y * kFaceSize + x];
      if (alpha == 0) continue;
      uint16_t color = face_portrait[y * kFaceSize + x];
      if (now - last_attack_us < 120000 && ((x + y) & 1) == 0) {
        color = rgb565(255, 80, 70);
      }
      const int dx = boss_draw_x + x;
      const int dy = boss_draw_y + y;
      if (dx < 0 || dx >= kDisplayWidth || dy < 0 || dy >= kDisplayHeight) continue;
      set_pixel(dx, dy, alpha > 245 ? color
                                   : blend565(color, screen[dy * kDisplayWidth + dx], alpha));
    }
  }
  if (fight_state == FightState::ChiWins) {
    const uint16_t tear = rgb565(75, 205, 255);
    const uint16_t shine = rgb565(210, 250, 255);
    const int fall = static_cast<int>(((now - round_state_since_us) / 35000) % 34);
    for (int eye_offset : {28, 59}) {
      const int tear_x = boss_draw_x + eye_offset;
      const int tear_y = boss_draw_y + 38 + fall;
      draw_disc(tear_x, tear_y, 4, tear);
      draw_disc(tear_x - 1, tear_y - 2, 1, shine);
      draw_disc(tear_x, tear_y - 10, 2, tear);
    }
  }
}

void draw_boss_attack(int64_t now) {
  const int face_cx = boss_draw_x + kFaceSize / 2;
  const int face_cy = boss_draw_y + kFaceSize / 2;
  const uint16_t red = rgb565(255, 45, 35);
  const uint16_t orange = rgb565(255, 155, 25);
  const uint16_t yellow = rgb565(255, 245, 120);
  if (boss_state == BossState::Warning) {
    const int phase = static_cast<int>((now / 45000) % 6);
    const int pulse = 64 - phase * 3;
    draw_ring(face_cx, face_cy, pulse, red);
    draw_ring(face_cx, face_cy, pulse - 5, orange);
    for (int i = 0; i < 10; ++i) {
      const float angle = i * 0.6283185f + phase * 0.08f;
      const int spark_radius = pulse + 7 + (i & 1) * 5;
      draw_disc(face_cx + static_cast<int>(std::cos(angle) * spark_radius),
                face_cy + static_cast<int>(std::sin(angle) * spark_radius),
                boss_body_attack ? 3 : 2, i & 1 ? yellow : red);
    }
    if (boss_body_attack) {
      draw_disc(face_cx - pulse + 4, face_cy, 5 + (phase & 1), yellow);
    } else {
      draw_disc(boss_draw_x + 5, face_cy, 5 + (phase & 1), yellow);
    }
  } else if (boss_state == BossState::Strike) {
    if (boss_body_attack) return;
    const float t = std::clamp((now - boss_state_since_us) / 420000.0, 0.0, 1.0);
    const int target_x = static_cast<int>(chi_x) + kSpriteWidth * 3 / 4;
    const int target_y = static_cast<int>(chi_y) + kSpriteHeight / 2;
    const int start_x = face_cx - kFaceSize / 2;
    const int start_y = face_cy;
    const int orb_x = start_x + static_cast<int>((target_x - start_x) * t);
    const int orb_y = start_y + static_cast<int>((target_y - start_y) * t);
    for (int trail = 3; trail >= 1; --trail) {
      const float tt = std::max(0.0f, t - trail * 0.08f);
      draw_disc(start_x + static_cast<int>((target_x - start_x) * tt),
                start_y + static_cast<int>((target_y - start_y) * tt),
                2 + trail, orange);
    }
    draw_disc(orb_x, orb_y, 9, red);
    draw_disc(orb_x, orb_y, 5, yellow);
  }
  if (now - last_boss_attack_us < 180000) {
    const int cx = static_cast<int>(chi_x) + kSpriteWidth / 2;
    const int cy = static_cast<int>(chi_y) + kSpriteHeight / 2;
    for (int r = 8; r <= 28; r += 7) draw_ring(cx, cy, r, guard_active ? rgb565(80,220,255) : yellow);
  }
}

void draw_fight_hud(int64_t now) {
  if (fight_state == FightState::FindFace) {
    const uint16_t guide = ((now / 300000) % 2 == 0)
                               ? rgb565(255, 225, 105)
                               : rgb565(125, 170, 185);
    const char *status = "FIND YOUR FACE";
    if (person_present) {
      const int w = person_box[2] - person_box[0];
      const int cx = (person_box[0] + person_box[2]) / 2;
      if (w < 54) status = "MOVE CLOSER";
      else if (std::abs(cx - kCameraWidth / 2) > 55) status = "CENTER YOUR FACE";
      else status = "FACE FOUND!";
    }
    fill_rect(0, 0, kDisplayWidth, 24, rgb565(12, 18, 30));
    draw_centered_text(status, 7, 1, guide);
    return;
  }
  fill_rect(6, 5, 104, 12, rgb565(38, 30, 42));
  fill_rect(8, 7, chi_hp, 8, rgb565(70, 220, 130));
  fill_rect(130, 5, 104, 12, rgb565(38, 30, 42));
  fill_rect(232 - boss_hp, 7, boss_hp, 8, rgb565(245, 75, 70));
  if (combo > 1) {
    fill_rect(106, 22, std::min(combo, 10) * 3, 5, rgb565(255, 210, 55));
  }
  draw_text("CHI", 7, 7, 1, rgb565(255,255,255));
  draw_text("BOSS", 202, 7, 1, rgb565(255,255,255));
  char level_text[] = {'L', 'V', 'L', ' ', static_cast<char>('0' + std::min(fight_level, 9)), '\0'};
  draw_centered_text(level_text, 20, 1, rgb565(255, 225, 80));
  if (fight_state == FightState::Intro) {
    const int64_t age = now - round_state_since_us;
    const int slide_y = age < 500000
        ? -ui_title_height + static_cast<int>(age * (68 + ui_title_height) / 500000)
        : 68;
    fill_rect(0, 42, kDisplayWidth, 104, rgb565(7, 8, 20));
    const int pulse = 104 - static_cast<int>((age / 50000) % 7) * 8;
    draw_ring(kDisplayWidth / 2, 118, pulse, rgb565(255, 45, 45));
    draw_ring(kDisplayWidth / 2, 118, std::max(8, pulse - 7),
              rgb565(255, 210, 55));
    draw_centered_rgba(ui_title_rgba, ui_title_width, ui_title_height, slide_y);
    if (age >= 650000 && age < 1450000) {
      draw_centered_text("CHALLENGER LOCKED", 190, 1, rgb565(255, 225, 80));
    } else if (age >= 1450000) {
      draw_centered_rgba(ui_fight_rgba, ui_fight_width, ui_fight_height, 155);
    }
  } else if (fight_state == FightState::Ready) {
    const int64_t age = now - round_state_since_us;
    if (fight_level > 1 && age < 700000) {
      draw_centered_rgba(ui_level_up_rgba, ui_level_up_width,
                         ui_level_up_height, 88);
    } else if (fight_level > 1) {
      draw_centered_text("TAP FACE TO START", 244, 1, rgb565(255,255,255));
    } else if (age < 350000) {
      draw_centered_text("CAPTURED!", 34, 2, rgb565(255, 225, 70));
    } else if (age < 700000) {
      draw_centered_text("CUTTING OUT...", 34, 1, rgb565(255, 225, 70));
    } else {
      draw_centered_text("FACE READY!", 34, 2, rgb565(255, 225, 70));
      draw_centered_text("TAP FACE TO START", 244, 1, rgb565(255,255,255));
    }
  } else if (fight_state == FightState::ChiWins ||
             fight_state == FightState::FaceWins) {
    fill_rect(45, 43, 150, 66, rgb565(7, 8, 20));
    draw_rect(45, 43, 150, 66, rgb565(255, 210, 55));
    draw_centered_text("K.O.", 57, 4, rgb565(255, 245, 225));
    if (fight_state == FightState::ChiWins) {
      draw_centered_rgba(ui_chi_wins_rgba, ui_chi_wins_width,
                         ui_chi_wins_height, 132);
    } else {
      draw_centered_rgba(ui_face_wins_rgba, ui_face_wins_width,
                         ui_face_wins_height, 132);
    }
    const bool won = fight_state == FightState::ChiWins;
    const uint8_t *left = won ? ui_next_level_rgba : ui_retry_rgba;
    const int left_width = won ? ui_next_level_width : ui_retry_width;
    const int left_height = won ? ui_next_level_height : ui_retry_height;
    draw_rgba_image(left, left_width, left_height,
                    (120 - left_width) / 2, kChoiceY);
    draw_rgba_image(ui_exit_rgba, ui_exit_width, ui_exit_height,
                    120 + (120 - ui_exit_width) / 2, kChoiceY);
  } else if (boss_state == BossState::Warning) {
    if (fight_level < 4 || ((now / 90000) & 1) == 0) {
      draw_centered_text("HOLD!", 34, 2, rgb565(255, 65, 55));
    }
  } else if (guard_active) {
    draw_centered_rgba(ui_block_rgba, ui_block_width, ui_block_height, 28);
  } else if (combo > 1 && now - last_attack_us < 500000) {
    draw_centered_rgba(ui_combo_rgba, ui_combo_width, ui_combo_height, 28);
  } else if (fight_state == FightState::Fighting &&
             now - round_state_since_us < 650000) {
    draw_centered_rgba(ui_fight_rgba, ui_fight_width, ui_fight_height, 88);
  } else if (fight_state == FightState::Fighting &&
             now - last_attack_us < 180000) {
    draw_centered_rgba(ui_hit_rgba, ui_hit_width, ui_hit_height, 34);
  }
}

camera_config_t camera_config() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = board_pins::camera_y2;
  c.pin_d1 = board_pins::camera_y3;
  c.pin_d2 = board_pins::camera_y4;
  c.pin_d3 = board_pins::camera_y5;
  c.pin_d4 = board_pins::camera_y6;
  c.pin_d5 = board_pins::camera_y7;
  c.pin_d6 = board_pins::camera_y8;
  c.pin_d7 = board_pins::camera_y9;
  c.pin_xclk = board_pins::camera_xclk;
  c.pin_pclk = board_pins::camera_pclk;
  c.pin_vsync = board_pins::camera_vsync;
  c.pin_href = board_pins::camera_href;
  c.pin_sccb_sda = board_pins::camera_siod;
  c.pin_sccb_scl = board_pins::camera_sioc;
  c.pin_pwdn = board_pins::camera_pwdn;
  c.pin_reset = board_pins::camera_reset;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_QVGA;
  c.jpeg_quality = 12;
  c.fb_count = 2;
  c.grab_mode = CAMERA_GRAB_LATEST;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  return c;
}

bool start_camera() {
  if (camera_active) return true;
  camera_config_t config = camera_config();
  const esp_err_t error = esp_camera_init(&config);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "CAMERA_START_FAILED error=%s", esp_err_to_name(error));
    return false;
  }
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor != nullptr && sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
  }
  camera_active = true;
  camera_start_pending = false;
  ESP_LOGI(TAG, "CAMERA_READY pid=0x%04x format=JPEG size=320x240",
           sensor == nullptr ? 0 : sensor->id.PID);
  return true;
}

void stop_camera(const char *reason) {
  if (!camera_active) {
    camera_stop_pending = false;
    return;
  }
  const esp_err_t error = esp_camera_deinit();
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "CAMERA_STOP_FAILED error=%s", esp_err_to_name(error));
    return;
  }
  camera_active = false;
  camera_stop_pending = false;
  ESP_LOGI(TAG, "CAMERA_STOPPED reason=%s psram_free=%u", reason,
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void init_lcd() {
  gpio_config_t output = {};
  output.mode = GPIO_MODE_OUTPUT;
  output.pin_bit_mask = (1ULL << board_pins::lcd_backlight) |
                        (1ULL << board_pins::sd_cs);
  ESP_ERROR_CHECK(gpio_config(&output));
  gpio_set_level(static_cast<gpio_num_t>(board_pins::sd_cs), 1);
  gpio_set_level(static_cast<gpio_num_t>(board_pins::lcd_backlight), 0);

  spi_bus_config_t bus = {};
  bus.sclk_io_num = board_pins::lcd_sck;
  bus.mosi_io_num = board_pins::lcd_mosi;
  bus.miso_io_num = -1;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = kDisplayWidth * kDisplayHeight * sizeof(uint16_t) + 16;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.dc_gpio_num = board_pins::lcd_dc;
  io_config.cs_gpio_num = board_pins::lcd_cs;
  io_config.pclk_hz = 40000000;
  io_config.spi_mode = 0;
  io_config.trans_queue_depth = 1;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  esp_lcd_panel_io_handle_t io = nullptr;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
      static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config, &io));

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = board_pins::lcd_reset;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
  panel_config.bits_per_pixel = 16;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, true));
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, 20));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
  gpio_set_level(static_cast<gpio_num_t>(board_pins::lcd_backlight), 1);
}

bool touch_write(uint8_t reg, uint8_t value) {
  const uint8_t bytes[2] = {reg, value};
  return i2c_master_transmit(touch_device, bytes, sizeof(bytes), 50) == ESP_OK;
}

bool touch_read(uint8_t reg, uint8_t *data, size_t length) {
  return i2c_master_transmit_receive(touch_device, &reg, 1, data, length, 20) ==
         ESP_OK;
}

void poll_touch(int64_t now) {
  if (now - last_touch_poll_us < 10000) return;
  last_touch_poll_us = now;
  static bool was_pressed = false;
  uint8_t data[6] = {};
  if (!touch_read(kTouchDataRegister, data, sizeof(data))) {
    ++touch_read_errors;
    touch_pressed = false;
    was_pressed = false;
    return;
  }
  const bool pressed = (data[1] & 0x0f) > 0;
  touch_pressed = pressed;
  if (pressed) {
    const int raw_x = ((data[2] & 0x0f) << 8) | data[3];
    const int raw_y = ((data[4] & 0x0f) << 8) | data[5];
    touch_x = std::clamp(kDisplayWidth - 1 - raw_x, 0, kDisplayWidth - 1);
    touch_y = std::clamp(kDisplayHeight - 1 - raw_y, 0, kDisplayHeight - 1);
    if (!was_pressed) {
      touch_pending = true;
      ++touch_event_count;
    }
  } else if (was_pressed) {
    ++touch_release_count;
  }
  was_pressed = pressed;
}

void init_touch() {
  gpio_set_direction(static_cast<gpio_num_t>(board_pins::touch_reset),
                     GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(board_pins::touch_irq),
                     GPIO_MODE_INPUT);
  gpio_set_level(static_cast<gpio_num_t>(board_pins::touch_reset), 1);
  vTaskDelay(pdMS_TO_TICKS(5));
  gpio_set_level(static_cast<gpio_num_t>(board_pins::touch_reset), 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(static_cast<gpio_num_t>(board_pins::touch_reset), 1);
  vTaskDelay(pdMS_TO_TICKS(50));

  i2c_master_bus_config_t bus_config = {};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = static_cast<gpio_num_t>(board_pins::touch_sda);
  bus_config.scl_io_num = static_cast<gpio_num_t>(board_pins::touch_scl);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &touch_bus));

  i2c_device_config_t device_config = {};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = kTouchAddress;
  device_config.scl_speed_hz = 400000;
  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(touch_bus, &device_config, &touch_device));

  uint8_t chip_id = 0;
  const bool present = touch_read(0xa7, &chip_id, 1);
  const bool awake = touch_write(kTouchDisableSleepRegister, 0x07);
  ESP_LOGI(TAG, "TOUCH_READY present=%s chip=0x%02x auto_sleep_off=%s",
           present ? "true" : "false", chip_id, awake ? "true" : "false");
}

void microphone_task(void *) {
  i2s_chan_handle_t rx = nullptr;
  i2s_chan_config_t channel =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  if (i2s_new_channel(&channel, nullptr, &rx) != ESP_OK) {
    ESP_LOGE(TAG, "MIC_INIT channel_failed");
    vTaskDelete(nullptr);
  }
  i2s_pdm_rx_config_t config = {
      .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_PDM_RX_SLOT_PCM_FMT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
      .gpio_cfg = {.clk = static_cast<gpio_num_t>(board_pins::microphone_clock),
                   .din = static_cast<gpio_num_t>(board_pins::microphone_data),
                   .invert_flags = {.clk_inv = false}},
  };
  if (i2s_channel_init_pdm_rx_mode(rx, &config) != ESP_OK ||
      i2s_channel_enable(rx) != ESP_OK) {
    ESP_LOGE(TAG, "MIC_INIT pdm_failed");
    vTaskDelete(nullptr);
  }
  ESP_LOGI(TAG, "MIC_READY rate=16000 data=%d clk=%d", board_pins::microphone_data,
           board_pins::microphone_clock);

  int16_t samples[256];
  float baseline = 400.0f;
  int64_t last_trigger = 0;
  while (true) {
    size_t bytes = 0;
    if (i2s_channel_read(rx, samples, sizeof(samples), &bytes, 150) == ESP_OK &&
        bytes >= sizeof(int16_t)) {
      const size_t count = bytes / sizeof(int16_t);
      uint64_t sum = 0;
      for (size_t i = 0; i < count; ++i) {
        sum += std::abs(static_cast<int>(samples[i]));
      }
      const float level = static_cast<float>(sum) / count;
      baseline = baseline * 0.98f + level * 0.02f;
      const int64_t now = esp_timer_get_time();
      portENTER_CRITICAL(&input_mux);
      mic_level = level;
      if (level > std::max(1800.0f, baseline * 3.2f) &&
          now - last_trigger > 1500000) {
        sound_pending = true;
        last_trigger = now;
      }
      portEXIT_CRITICAL(&input_mux);
    }
  }
}

void camera_to_screen(const dl::image::img_t &image) {
  const auto *rgb = static_cast<const uint8_t *>(image.data);
  fill_rect(0, 0, kDisplayWidth, kDisplayHeight, rgb565(0, 0, 0));
  for (int y = 0; y < kCameraHeight; ++y) {
    for (int x = 0; x < kDisplayWidth; ++x) {
      const size_t source = (static_cast<size_t>(y) * kCameraWidth + x + kCropX) * 3;
      screen[(y + kCameraY) * kDisplayWidth + x] =
          rgb565(rgb[source], rgb[source + 1], rgb[source + 2]);
    }
  }
}

void draw_detection() {
  if (!person_present) return;
  const int left = std::clamp(person_box[0] - kCropX, 0, kDisplayWidth - 1);
  const int right = std::clamp(person_box[2] - kCropX, 0, kDisplayWidth - 1);
  const int top = std::clamp(person_box[1] + kCameraY, kCameraY,
                             kCameraY + kCameraHeight - 1);
  const int bottom = std::clamp(person_box[3] + kCameraY, kCameraY,
                                kCameraY + kCameraHeight - 1);
  if (right - left > 4 && bottom - top > 4) {
    draw_rect(left, top, right - left, bottom - top, rgb565(80, 255, 150));
  }
}

void draw_status() {
  fill_rect(0, 0, kDisplayWidth, kCameraY, rgb565(4, 10, 18));
  fill_rect(0, kCameraY + kCameraHeight, kDisplayWidth, 20, rgb565(4, 10, 18));
  fill_rect(5, 6, person_present ? 38 : 12, 8,
            person_present ? rgb565(70, 255, 130) : rgb565(80, 90, 100));
  const int score_width = static_cast<int>(std::clamp(person_score, 0.0f, 1.0f) * 55.0f);
  fill_rect(50, 6, score_width, 8, rgb565(255, 190, 70));
  float level = 0.0f;
  portENTER_CRITICAL(&input_mux);
  level = mic_level;
  portEXIT_CRITICAL(&input_mux);
  const int audio_width = static_cast<int>(std::min(100.0f, level / 40.0f));
  fill_rect(132, 6, audio_width, 8, rgb565(80, 180, 255));
  set_pixel(static_cast<int>(target_x + kSpriteWidth / 2), 269, rgb565(255, 255, 255));
}

void draw_touch_effect(int64_t now) {
  const int64_t age = now - last_touch_us;
  if (!touch_effect_active || last_touch_us == 0 || age < 0 ||
      age >= kTouchOverrideUs) return;

  const int start_x = static_cast<int>(chi_x) + kSpriteWidth / 2;
  const int start_y = static_cast<int>(chi_y) + kSpriteHeight / 2;
  for (int step = 2; step < 10; step += 2) {
    const int x = start_x + (touch_marker_x - start_x) * step / 10;
    const int y = start_y + (touch_marker_y - start_y) * step / 10;
    fill_rect(x - 1, y - 1, 3, 3, rgb565(255, 220, 90));
  }

  const float phase = static_cast<float>(age % 500000) / 500000.0f;
  const int radius = 5 + static_cast<int>(phase * 9.0f);
  const uint16_t gold = rgb565(255, 225, 90);
  const uint16_t white = rgb565(255, 255, 235);
  for (int i = 0; i < 16; ++i) {
    const float angle = static_cast<float>(i) * 6.2831853f / 16.0f;
    const int x = touch_marker_x + static_cast<int>(std::cos(angle) * radius);
    const int y = touch_marker_y + static_cast<int>(std::sin(angle) * radius);
    set_pixel(x, y, gold);
  }
  fill_rect(touch_marker_x - 1, touch_marker_y - radius - 4, 3, 7, white);
  fill_rect(touch_marker_x - 3, touch_marker_y - radius - 2, 7, 3, white);
  fill_rect(touch_marker_x + radius + 2, touch_marker_y - 1, 3, 3, white);
}

void draw_sprite(const uint8_t *atlas, int frame, int frame_count, int x, int y) {
  const uint8_t *pixels = atlas +
      static_cast<size_t>(std::clamp(frame, 0, frame_count - 1)) *
          kSpriteSourceWidth * kSpriteSourceHeight * 4;
  for (int sy = 0; sy < kSpriteHeight; ++sy) {
    const int dy = y + sy;
    if (dy < kCameraY || dy >= kCameraY + kCameraHeight) continue;
    const int source_y = sy * kSpriteSourceHeight / kSpriteHeight;
    for (int sx = 0; sx < kSpriteWidth; ++sx) {
      const int dx = x + sx;
      if (dx < 0 || dx >= kDisplayWidth) continue;
      const int source_x = sx * kSpriteSourceWidth / kSpriteWidth;
      const size_t offset =
          (static_cast<size_t>(source_y) * kSpriteSourceWidth + source_x) * 4;
      const uint8_t alpha = pixels[offset + 3];
      if (alpha < 24) continue;
      const uint8_t r = pixels[offset];
      const uint8_t g = pixels[offset + 1];
      const uint8_t b = pixels[offset + 2];
      if (alpha > 245) {
        set_pixel(dx, dy, rgb565(r, g, b));
      } else {
        const uint16_t old = screen[dy * kDisplayWidth + dx];
        const uint8_t old_r = ((old >> 11) & 0x1f) * 255 / 31;
        const uint8_t old_g = ((old >> 5) & 0x3f) * 255 / 63;
        const uint8_t old_b = (old & 0x1f) * 255 / 31;
        set_pixel(dx, dy,
                  rgb565((r * alpha + old_r * (255 - alpha)) / 255,
                         (g * alpha + old_g * (255 - alpha)) / 255,
                         (b * alpha + old_b * (255 - alpha)) / 255));
      }
    }
  }
}

void start_action(Action next, int64_t now) {
  moving = false;
  game_action = GameAction::None;
  action = next;
  action_until_us = now + kActionDurationUs;
}

int game_action_frame_count(GameAction current) {
  switch (current) {
    case GameAction::Kick: return kKickFrames;
    case GameAction::Hit: return kHitFrames;
    case GameAction::Victory: return kVictoryFrames;
    case GameAction::PunchCombo: return kPunchComboFrames;
    case GameAction::Block: return kBlockFrames;
    case GameAction::Dodge: return kDodgeFrames;
    case GameAction::Jump: return kJumpFrames;
    case GameAction::Roundhouse: return kRoundhouseFrames;
    case GameAction::Knockdown: return kKnockdownFrames;
    case GameAction::GetUp: return kGetUpFrames;
    case GameAction::Defeat: return kDefeatFrames;
    case GameAction::None: return 0;
  }
  return 0;
}

int64_t game_action_frame_us(GameAction current) {
  switch (current) {
    case GameAction::Kick: return kKickFrameUs;
    case GameAction::Hit: return kHitFrameUs;
    case GameAction::Victory: return kVictoryFrameUs;
    case GameAction::PunchCombo: return 100000;
    case GameAction::Block: return 150000;
    case GameAction::Dodge: return 120000;
    case GameAction::Jump: return 130000;
    case GameAction::Roundhouse: return 100000;
    case GameAction::Knockdown: return 140000;
    case GameAction::GetUp: return 130000;
    case GameAction::Defeat: return 180000;
    case GameAction::None: return 0;
  }
  return 0;
}

const uint8_t *game_action_atlas(GameAction current) {
  switch (current) {
    case GameAction::Kick: return chi_kick_rgba;
    case GameAction::Hit: return chi_hit_rgba;
    case GameAction::Victory: return chi_victory_rgba;
    case GameAction::PunchCombo: return chi_punch_combo_rgba;
    case GameAction::Block: return chi_block_rgba;
    case GameAction::Dodge: return chi_dodge_rgba;
    case GameAction::Jump: return chi_jump_rgba;
    case GameAction::Roundhouse: return chi_roundhouse_rgba;
    case GameAction::Knockdown: return chi_knockdown_rgba;
    case GameAction::GetUp: return chi_get_up_rgba;
    case GameAction::Defeat: return chi_defeat_rgba;
    case GameAction::None: return chi_actions_rgba;
  }
  return chi_actions_rgba;
}

GameAction next_showcase_action() {
  static constexpr GameAction kActions[] = {
      GameAction::PunchCombo, GameAction::Block,     GameAction::Dodge,
      GameAction::Jump,       GameAction::Roundhouse, GameAction::Hit,
      GameAction::Knockdown,  GameAction::Defeat,    GameAction::Victory,
  };
  const GameAction next = kActions[showcase_action_index];
  showcase_action_index = (showcase_action_index + 1) %
                          (sizeof(kActions) / sizeof(kActions[0]));
  return next;
}

void game_action_draw_offset(GameAction current, int frame, int *x, int *y) {
  if (current == GameAction::Dodge) {
    static constexpr int kDodgeX[kDodgeFrames] = {0, -10, -18, 0};
    *x = kDodgeX[std::clamp(frame, 0, kDodgeFrames - 1)];
  } else if (current == GameAction::Jump) {
    static constexpr int kJumpY[kJumpFrames] = {0, -12, -24, -10, 0};
    *y = kJumpY[std::clamp(frame, 0, kJumpFrames - 1)];
  }
}

void start_game_action(GameAction next, int64_t now) {
  moving = false;
  play_kick_on_arrival = false;
  game_action = next;
  game_action_started_us = now;
  action_until_us = 0;
}

bool touch_hits_character(int x, int y) {
  return x >= static_cast<int>(chi_x) &&
         x < static_cast<int>(chi_x) + kSpriteWidth &&
         y >= static_cast<int>(chi_y) &&
         y < static_cast<int>(chi_y) + kSpriteHeight;
}

bool touch_hits_face(int x, int y) {
  return point_in_rect(x, y, boss_draw_x - 5, boss_draw_y - 5,
                       kFaceSize + 10, kFaceSize + 10);
}

void reset_fight() {
  arena_index = (arena_index + 1) % 2;
  fight_state = FightState::FindFace;
  boss_state = BossState::Idle;
  face_ready = false;
  person_present = false;
  raw_person = false;
  person_count = 0;
  person_score = 0.0f;
  stable_face_frames = 0;
  last_detection_us = 0;
  fight_level = 1;
  guard_active = false;
  combo = 0;
  game_action = GameAction::None;
  combat_motion = CombatMotion::Home;
  queued_attacks = 0;
  action = Action::Idle;
  camera_start_pending = true;
}

void prepare_round(int64_t now, bool advance_level) {
  if (advance_level) ++fight_level;
  arena_index = (arena_index + 1) % 2;
  fight_state = FightState::Ready;
  boss_state = BossState::Idle;
  boss_warning_feint = false;
  boss_body_attack = false;
  chi_hp = 100;
  boss_hp = std::min(180, 100 + (fight_level - 1) * 12);
  combo = 0;
  guard_active = false;
  chi_x = 12;
  chi_y = 136;
  moving = false;
  game_action = GameAction::None;
  combat_motion = CombatMotion::Home;
  queued_attacks = 0;
  last_combat_motion_us = now;
  round_state_since_us = now;
  ESP_LOGI(TAG, "FIGHT_EVENT type=ROUND_PREP level=%d boss_hp=%d",
           fight_level, boss_hp);
}

bool update_fight(int64_t now, bool got_touch, bool got_sound, int x, int y) {
  (void)got_sound;
  if (fight_state == FightState::FindFace) return false;

  if (fight_state == FightState::Intro) {
    guard_active = false;
    if (now - round_state_since_us >= 2200000) {
      fight_state = FightState::Ready;
      round_state_since_us = now;
      ESP_LOGI(TAG, "FIGHT_EVENT type=INTRO_COMPLETE level=%d", fight_level);
    }
    return true;
  }

  bool held = false;
  int held_x = 0;
  int held_y = 0;
  held = touch_pressed;
  held_x = touch_x;
  held_y = touch_y;

  if (fight_state == FightState::ChiWins || fight_state == FightState::FaceWins) {
    if (got_touch && now - round_state_since_us >= 600000 && y >= 190) {
      if (x < 120) {
        prepare_round(now, fight_state == FightState::ChiWins);
      } else {
        ESP_LOGI(TAG, "FIGHT_EVENT type=EXIT_TO_CAMERA level=%d", fight_level);
        reset_fight();
      }
    }
    return true;
  }

  if (fight_state == FightState::Ready) {
    guard_active = false;
    if (now - round_state_since_us >= 700000 && got_touch && touch_hits_face(x, y)) {
      fight_state = FightState::Fighting;
      round_state_since_us = now;
      next_boss_attack_us = now + boss_attack_delay_us();
      ESP_LOGI(TAG, "FIGHT_EVENT type=ROUND_START arena=%s level=%d",
               arena_index == 0 ? "office" : "rooftop", fight_level);
    }
    return true;
  }

  const float combat_delta = last_combat_motion_us == 0
      ? 0.0f
      : std::clamp((now - last_combat_motion_us) / 1000000.0f, 0.0f, 0.08f);
  last_combat_motion_us = now;

  if (game_action != GameAction::None && game_action != GameAction::Block) {
    const int64_t duration = game_action_frame_us(game_action) *
                             game_action_frame_count(game_action);
    if (now - game_action_started_us >= duration) {
      game_action = GameAction::None;
      action = Action::Idle;
      if (combat_motion == CombatMotion::Attack) {
        combat_motion = CombatMotion::Retreat;
        moving = true;
        direction = Direction::Left;
      }
    }
  }

  guard_active = combat_motion == CombatMotion::Home && held &&
                 touch_hits_character(held_x, held_y);
  if (guard_active) {
    if (game_action != GameAction::Block) start_game_action(GameAction::Block, now);
  } else if (game_action == GameAction::Block) {
    game_action = GameAction::None;
    action = Action::Idle;
  }

  if (combo > 0 && now - last_attack_us > kComboTimeoutUs) combo = 0;
  if (got_touch && touch_hits_face(x, y) && !guard_active) {
    queued_attacks = std::min(4, queued_attacks + 1);
  }

  auto move_toward = [&](float destination_x, float destination_y, float speed) {
    const float dx = destination_x - chi_x;
    const float dy = destination_y - chi_y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 4.0f) {
      chi_x = destination_x;
      chi_y = destination_y;
      return true;
    }
    const float step = std::min(distance, speed * combat_delta);
    chi_x += dx / distance * step;
    chi_y += dy / distance * step;
    return false;
  };

  if (combat_motion == CombatMotion::Home && queued_attacks > 0 && !guard_active) {
    combat_motion = CombatMotion::Approach;
    moving = true;
    direction = Direction::Right;
  }
  if (combat_motion == CombatMotion::Approach) {
    moving = true;
    direction = Direction::Right;
    const float attack_x = std::clamp(static_cast<float>(boss_draw_x - 68),
                                      12.0f, 128.0f);
    const float attack_y = std::clamp(static_cast<float>(boss_draw_y + 14),
                                      62.0f, 150.0f);
    if (move_toward(attack_x, attack_y, 175.0f + fight_level * 8.0f)) {
      moving = false;
      combat_motion = CombatMotion::Attack;
      --queued_attacks;
      ++combo;
      ++chi_hit_count;
      last_attack_us = now;
      const bool heavy = chi_hit_count % 3 == 0;
      boss_hp = std::max(0, boss_hp - (heavy ? 12 : 7));
      start_game_action(heavy ? GameAction::Kick : GameAction::PunchCombo, now);
      ESP_LOGI(TAG, "FIGHT_EVENT type=CHI_HIT level=%d damage=%d boss_hp=%d queued=%d",
               fight_level, heavy ? 12 : 7, boss_hp, queued_attacks);
      if (boss_hp == 0) {
        fight_state = FightState::ChiWins;
        round_state_since_us = now;
        combat_motion = CombatMotion::Home;
        start_game_action(GameAction::Victory, now);
      }
    }
  } else if (combat_motion == CombatMotion::Retreat) {
    moving = true;
    direction = Direction::Left;
    if (move_toward(12.0f, 136.0f, 190.0f)) {
      moving = false;
      combat_motion = CombatMotion::Home;
    }
  }

  if (fight_state != FightState::Fighting) return true;
  if (boss_state == BossState::Idle && now >= next_boss_attack_us) {
    boss_state = BossState::Warning;
    boss_state_since_us = now;
    const int feint_chance = std::min(35, std::max(0, fight_level - 2) * 10);
    boss_warning_feint = feint_chance > 0 &&
                         static_cast<int>(esp_random() % 100) < feint_chance;
    const int body_chance = std::min(65, 22 + fight_level * 7);
    boss_body_attack = fight_level >= 2 &&
                       static_cast<int>(esp_random() % 100) < body_chance;
    ESP_LOGI(TAG, "FIGHT_EVENT type=BOSS_WARNING level=%d move=%s feint=%s",
             fight_level, boss_body_attack ? "body" : "orb",
             boss_warning_feint ? "true" : "false");
  } else if (boss_state == BossState::Warning &&
             now - boss_state_since_us >= boss_warning_duration_us()) {
    if (boss_warning_feint) {
      boss_state = BossState::Idle;
      boss_warning_feint = false;
      boss_body_attack = false;
      next_boss_attack_us = now + std::max<int64_t>(450000, boss_attack_delay_us() / 2);
      ESP_LOGI(TAG, "FIGHT_EVENT type=BOSS_FEINT level=%d", fight_level);
    } else {
      boss_state = BossState::Strike;
      boss_state_since_us = now;
    }
  } else if (boss_state == BossState::Strike &&
             now - boss_state_since_us >= 420000) {
    const bool body_hit = boss_body_attack;
    const int damage = guard_active ? (body_hit ? 4 : 2)
                                    : (body_hit ? 18 : 14);
    chi_hp = std::max(0, chi_hp - damage);
    last_boss_attack_us = now;
    boss_state = BossState::Idle;
    boss_body_attack = false;
    next_boss_attack_us = now + boss_attack_delay_us() + esp_random() % 500000;
    if (!guard_active) {
      combat_motion = CombatMotion::Attack;
      start_game_action(GameAction::Hit, now);
    }
    ESP_LOGI(TAG,
             "FIGHT_EVENT type=BOSS_HIT move=%s blocked=%s damage=%d chi_hp=%d",
             body_hit ? "body" : "orb", guard_active ? "true" : "false",
             damage, chi_hp);
    if (chi_hp == 0) {
      fight_state = FightState::FaceWins;
      round_state_since_us = now;
      start_game_action(GameAction::Defeat, now);
    }
  }
  return true;
}

void select_target(float x, float y) {
  target_x = std::clamp(x - kSpriteWidth / 2.0f, 0.0f,
                        static_cast<float>(kDisplayWidth - kSpriteWidth));
  target_y = std::clamp(y - kSpriteHeight / 2.0f,
                        static_cast<float>(kCameraY),
                        static_cast<float>(kCameraY + kCameraHeight - kSpriteHeight));
  const float dx = target_x - chi_x;
  const float dy = target_y - chi_y;
  if (std::abs(dx) > std::abs(dy)) {
    direction = dx >= 0 ? Direction::Right : Direction::Left;
  } else {
    direction = dy >= 0 ? Direction::Down : Direction::Up;
  }
  moving = true;
}

void update_behavior(int64_t now) {
  const float delta_seconds = last_behavior_us == 0
                                  ? 0.0f
                                  : std::clamp((now - last_behavior_us) / 1000000.0f,
                                               0.0f, 0.25f);
  last_behavior_us = now;
  bool got_touch = false;
  bool got_sound = false;
  int x = 0;
  int y = 0;
  if (touch_pending) {
    got_touch = true;
    x = touch_x;
    y = touch_y;
    touch_pending = false;
  }
  if (sound_pending) {
    got_sound = true;
    sound_pending = false;
  }

  if (update_fight(now, got_touch, got_sound, x, y)) return;

  if (got_touch) {
    last_touch_us = now;
    touch_marker_x = std::clamp(x, 0, kDisplayWidth - 1);
    touch_marker_y = std::clamp(y, kCameraY, kCameraY + kCameraHeight - 1);
    if (touch_hits_character(x, y)) {
      touch_effect_active = false;
      touch_directed_move = false;
      start_game_action(next_showcase_action(), now);
    } else {
      game_action = GameAction::None;
      touch_effect_active = true;
      play_kick_on_arrival = true;
      touch_directed_move = true;
      action = Action::Idle;
      select_target(x, y);
    }
  } else if (got_sound && !moving && game_action == GameAction::None) {
    start_action(Action::Listen, now);
  }

  if (moving) {
    const float dx = target_x - chi_x;
    const float dy = target_y - chi_y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance <= 3.0f) {
      chi_x = target_x;
      chi_y = target_y;
      if (play_kick_on_arrival) {
        start_game_action(GameAction::Kick, now);
      } else {
        start_action(action, now);
      }
    } else {
      const float speed = touch_directed_move ? kTouchSpeed : kRoamSpeed;
      const float step = speed * delta_seconds;
      chi_x += dx / distance * std::min(step, distance);
      chi_y += dy / distance * std::min(step, distance);
    }
    return;
  }

  if (game_action != GameAction::None) {
    const int64_t duration = game_action_frame_us(game_action) *
                                 game_action_frame_count(game_action) +
                             (game_action == GameAction::Defeat
                                  ? kDefeatHoldUs
                                  : 0);
    if (now - game_action_started_us < duration) return;
    if (game_action == GameAction::Kick) {
      start_game_action(GameAction::Victory, now);
      return;
    }
    if (game_action == GameAction::Knockdown) {
      start_game_action(GameAction::GetUp, now);
      return;
    }
    game_action = GameAction::None;
    action = Action::Idle;
  }

  if (now < action_until_us) return;

  if (person_enter_pending && now - last_touch_us > kTouchOverrideUs) {
    person_enter_pending = false;
    const float px = (person_box[0] + person_box[2]) / 2.0f - kCropX;
    const float py = person_box[3] + kCameraY - kSpriteHeight * 0.35f;
    action = Action::Wave;
    touch_directed_move = false;
    select_target(px, py);
    return;
  }

  action = Action::Idle;
  if (now >= next_roam_us && now - last_touch_us > kTouchOverrideUs &&
      !person_present) {
    const float roam_x = static_cast<float>(
        esp_random() % (kDisplayWidth - kSpriteWidth)) + kSpriteWidth / 2.0f;
    const float roam_y = static_cast<float>(
        kCameraY + esp_random() % (kCameraHeight - kSpriteHeight)) +
        kSpriteHeight / 2.0f;
    touch_directed_move = false;
    select_target(roam_x, roam_y);
    next_roam_us = now + 3500000 + esp_random() % 3000000;
  }
}

void run_detection(dl::image::img_t &image, int64_t now) {
  const int64_t start = esp_timer_get_time();
  auto &results = detector->run(image);
  inference_ms = static_cast<uint32_t>((esp_timer_get_time() - start) / 1000);
  raw_person = !results.empty();
  person_count = static_cast<int>(results.size());
  person_score = 0.0f;
  for (const auto &result : results) {
    if (result.score > person_score && result.box.size() >= 4) {
      person_score = result.score;
      for (int i = 0; i < 4; ++i) person_box[i] = result.box[i];
    }
  }
  if (raw_person) {
    ++stable_face_frames;
    last_detection_us = now;
    if (!person_present) {
      person_present = true;
      person_enter_pending = true;
      ESP_LOGI(TAG, "FACE_EVENT type=FOUND score=%.3f count=%d", person_score,
               person_count);
    }
  } else if (person_present && now - last_detection_us >= kPresenceHoldUs) {
    person_present = false;
    person_enter_pending = false;
    stable_face_frames = 0;
    ESP_LOGI(TAG, "FACE_EVENT type=LOST");
  } else if (!raw_person) {
    stable_face_frames = 0;
  }
}
}  // namespace

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "BOOT_START chi_camera_creature reset_reason=%d",
           static_cast<int>(esp_reset_reason()));
  ESP_LOGI(TAG, "PSRAM bytes=%u", static_cast<unsigned>(
      heap_caps_get_total_size(MALLOC_CAP_SPIRAM)));

  screen = static_cast<uint16_t *>(heap_caps_malloc(
      kDisplayWidth * kDisplayHeight * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (screen == nullptr) {
    ESP_LOGE(TAG, "BOOT_FAIL stage=SCREEN_ALLOC");
    return;
  }
  face_portrait = static_cast<uint16_t *>(heap_caps_malloc(
      kFaceSize * kFaceSize * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  face_alpha = static_cast<uint8_t *>(heap_caps_malloc(
      kFaceSize * kFaceSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (face_portrait == nullptr || face_alpha == nullptr) {
    ESP_LOGE(TAG, "BOOT_FAIL stage=FACE_ALLOC");
    return;
  }
  init_lcd();
  fill_rect(0, 0, kDisplayWidth, kDisplayHeight, rgb565(8, 18, 28));
  draw_centered_text("STARTING CAMERA...", 132, 1, rgb565(255, 225, 90));
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, kDisplayWidth,
                                            kDisplayHeight, screen));
  ESP_LOGI(TAG, "LCD_READY width=240 height=280 spi_hz=40000000");

  init_touch();
  xTaskCreate(microphone_task, "chi_mic", 4096, nullptr, 2, nullptr);

  if (!start_camera()) return;

  detector = new HumanFaceDetect();
  ESP_LOGI(TAG, "AI_READY model=human_face_msr_mnp_s8_v1 every=%d",
           kInferenceEvery);
  next_roam_us = esp_timer_get_time() + 1500000;

  int64_t last_report = 0;
  int64_t last_render = 0;
  while (true) {
    dl::image::img_t image = {};
    if (camera_start_pending && !start_camera()) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    const bool camera_frame = fight_state == FightState::FindFace && camera_active;
    if (camera_frame) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb == nullptr) {
        ESP_LOGW(TAG, "FRAME_DROP reason=NULL");
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      dl::image::jpeg_img_t jpeg = {.data = fb->buf, .data_len = fb->len};
      image = dl::image::sw_decode_jpeg(jpeg,
                                        dl::image::DL_IMAGE_PIX_TYPE_RGB888);
      esp_camera_fb_return(fb);
      if (image.data == nullptr || image.width != kCameraWidth ||
          image.height != kCameraHeight) {
        ESP_LOGW(TAG, "FRAME_DROP reason=DECODE width=%u height=%u", image.width,
                 image.height);
        if (image.data != nullptr) heap_caps_free(image.data);
        continue;
      }
    }

    const int64_t now = esp_timer_get_time();
    poll_touch(now);
    if (camera_frame && frame_count % kInferenceEvery == 0) {
      run_detection(image, now);
    }
    if (camera_frame) capture_face_candidate(image);
    update_behavior(now);
    const bool render_due = camera_frame ||
                            now - last_render >= 33333;
    if (!render_due) {
      if (image.data != nullptr) heap_caps_free(image.data);
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    last_render = now;
    if (camera_frame) camera_to_screen(image);
    else draw_arena();
    if (camera_frame) draw_detection();
    draw_face_portrait(now);
    draw_boss_attack(now);
    const bool result_overlay = fight_state == FightState::ChiWins ||
                                fight_state == FightState::FaceWins;
    if (!result_overlay) draw_fight_hud(now);
    draw_touch_effect(now);

    int sprite_frame = 0;
    int sprite_offset_x = 0;
    int sprite_offset_y = 0;
    const uint8_t *atlas = chi_actions_rgba;
    int atlas_frames = kSpriteFrames;
    if (moving) {
      atlas = chi_walk_rgba;
      sprite_frame = static_cast<int>(direction) * 4 +
                     static_cast<int>((now / 166667) % 4);
    } else if (game_action != GameAction::None) {
      atlas_frames = game_action_frame_count(game_action);
      const int64_t frame_us = game_action_frame_us(game_action);
      sprite_frame = static_cast<int>((now - game_action_started_us) / frame_us);
      if (fight_state == FightState::ChiWins &&
          game_action == GameAction::Victory) {
        sprite_frame %= atlas_frames;
      } else {
        sprite_frame = std::clamp(sprite_frame, 0, atlas_frames - 1);
      }
      atlas = game_action_atlas(game_action);
      game_action_draw_offset(game_action, sprite_frame, &sprite_offset_x,
                              &sprite_offset_y);
    } else if (fight_state != FightState::FindFace) {
      // In combat Chi always faces the boss in a side-on fighting stance.
      // Front-facing idle is reserved for the camera/creature mode.
      atlas = chi_walk_rgba;
      atlas_frames = kSpriteFrames;
      sprite_frame = ((now / 450000) % 2 == 0) ? 0 : 2;
    } else {
      int local_frame = static_cast<int>((now / 500000) % 4);
      if (action == Action::Idle) {
        const int64_t idle_cycle = now % 5000000;
        local_frame = idle_cycle < 3000000
                          ? 0
                          : std::min(3, static_cast<int>((idle_cycle - 3000000) /
                                                        500000));
      }
      sprite_frame = static_cast<int>(action) * 4 + local_frame;
    }
    if (fight_state != FightState::FindFace) {
      int sprite_x = static_cast<int>(chi_x) + sprite_offset_x;
      int sprite_y = static_cast<int>(chi_y) + sprite_offset_y;
      if (fight_state == FightState::ChiWins) {
        const float celebration = (now - round_state_since_us) / 1000000.0f;
        sprite_x = 70 + static_cast<int>(std::sin(celebration * 4.1f) * 62.0f);
        sprite_y = 78 + static_cast<int>(std::sin(celebration * 6.7f) * 48.0f);
      }
      draw_sprite(atlas, sprite_frame, atlas_frames,
                  sprite_x, sprite_y);
    }
    if (result_overlay) draw_fight_hud(now);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, kDisplayWidth,
                                              kDisplayHeight, screen));
    if (image.data != nullptr) heap_caps_free(image.data);
    if (camera_stop_pending) stop_camera("FACE_CAPTURED");
    ++frame_count;
    vTaskDelay(pdMS_TO_TICKS(fight_state == FightState::FindFace ? 2 : 1));

    if (now - last_report >= 1000000) {
      float level = 0.0f;
      portENTER_CRITICAL(&input_mux);
      level = mic_level;
      portEXIT_CRITICAL(&input_mux);
      ESP_LOGI(TAG,
               "STATUS frames=%u person=%s count=%d score=%.3f infer_ms=%u "
               "mic=%.0f chi=(%.0f,%.0f) moving=%s action=%u game=%u "
               "fight=%u chi_hp=%d boss_hp=%d guard=%s combo=%d touches=%u "
               "releases=%u touch_errors=%u hits=%u psram_free=%u "
               "psram_largest=%u camera=%s",
               frame_count, person_present ? "yes" : "no", person_count,
               person_score, inference_ms, level, chi_x, chi_y,
               moving ? "yes" : "no", static_cast<unsigned>(action),
               static_cast<unsigned>(game_action),
               static_cast<unsigned>(fight_state), chi_hp, boss_hp,
               guard_active ? "yes" : "no", combo,
               static_cast<unsigned>(touch_event_count), touch_release_count,
               touch_read_errors, chi_hit_count,
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
               static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
               camera_active ? "on" : "off");
      last_report = now;
    }
  }
}
