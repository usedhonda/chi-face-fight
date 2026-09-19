// Host driver: renders README screenshots with the firmware's own drawing code.
// Built by render_screens.py; not part of the firmware.
#include "host_stubs.h"

#include <cstdlib>
#include <string>

int64_t host_now_us = 0;

#include "game.inc"

namespace {

dl::image::img_t camera_image = {};
std::string out_dir;

// Mirrors the render block of app_main() in main/main.cpp.
void render_frame(int64_t now) {
  host_now_us = now;
  if (fight_state == FightState::LockOn) {
    draw_lock_on(now);
    return;
  }
  const bool camera_frame = fight_state == FightState::FindFace;
  if (camera_frame) camera_to_screen(camera_image);
  else draw_arena();
  if (camera_frame) draw_detection();
  draw_face_portrait(now);
  draw_boss_attack(now);
  const bool result_overlay = fight_state == FightState::ChiWins ||
                              fight_state == FightState::FaceWins;
  if (!result_overlay) draw_fight_hud(now);

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
    sprite_frame = static_cast<int>((now - game_action_started_us) /
                                    game_action_frame_us(game_action));
    if (fight_state == FightState::ChiWins && game_action == GameAction::Victory) {
      sprite_frame %= atlas_frames;
    } else {
      sprite_frame = std::clamp(sprite_frame, 0, atlas_frames - 1);
    }
    atlas = game_action_atlas(game_action);
    game_action_draw_offset(game_action, sprite_frame, &sprite_offset_x,
                            &sprite_offset_y);
  } else if (fight_state != FightState::FindFace) {
    atlas = chi_walk_rgba;
    sprite_frame = ((now / 450000) % 2 == 0) ? 0 : 2;
  }
  if (fight_state != FightState::FindFace) {
    int sprite_x = static_cast<int>(chi_x) + sprite_offset_x;
    int sprite_y = static_cast<int>(chi_y) + sprite_offset_y;
    if (fight_state == FightState::ChiWins) {
      const float celebration = (now - round_state_since_us) / 1000000.0f;
      sprite_x = 70 + static_cast<int>(std::sin(celebration * 4.1f) * 62.0f);
      sprite_y = 78 + static_cast<int>(std::sin(celebration * 6.7f) * 48.0f);
    }
    draw_sprite(atlas, sprite_frame, atlas_frames, sprite_x, sprite_y);
  }
  if (result_overlay) draw_fight_hud(now);
}

void save(const char *name) {
  const std::string path = out_dir + "/" + name + ".ppm";
  FILE *file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) std::exit(1);
  std::fprintf(file, "P6 %d %d 255\n", kDisplayWidth, kDisplayHeight);
  for (int i = 0; i < kDisplayWidth * kDisplayHeight; ++i) {
    const uint16_t p = screen[i];
    const unsigned char rgb[3] = {
        static_cast<unsigned char>(((p >> 11) & 0x1f) * 255 / 31),
        static_cast<unsigned char>(((p >> 5) & 0x3f) * 255 / 63),
        static_cast<unsigned char>((p & 0x1f) * 255 / 31)};
    std::fwrite(rgb, 1, 3, file);
  }
  std::fclose(file);
}

void start_fight(int64_t now, int hp_percent) {
  fight_state = FightState::Fighting;
  round_state_since_us = now - 5000000;
  boss_state = BossState::Idle;
  boss_hp = boss_max_hp() * hp_percent / 100;
  chi_hp = 100;
  chi_x = 12;
  chi_y = 136;
  moving = false;
  game_action = GameAction::None;
  guard_active = false;
  weak_until_us = 0;
  last_attack_us = 0;
  last_crit_us = 0;
  last_counter_us = 0;
  last_dodge_us = 0;
  combo = 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) return 2;
  const std::string camera_path = argv[1];
  out_dir = argv[2];

  static uint16_t screen_buffer[kDisplayWidth * kDisplayHeight];
  static uint16_t portrait[kFaceSize * kFaceSize];
  static uint8_t alpha[kFaceSize * kFaceSize];
  static uint16_t backdrop[kDisplayWidth * kCameraHeight];
  static uint8_t camera_rgb[kCameraWidth * kCameraHeight * 3];
  screen = screen_buffer;
  face_portrait = portrait;
  face_alpha = alpha;
  lock_backdrop = backdrop;

  FILE *file = std::fopen(camera_path.c_str(), "rb");
  if (file == nullptr ||
      std::fread(camera_rgb, 1, sizeof(camera_rgb), file) != sizeof(camera_rgb)) {
    return 3;
  }
  std::fclose(file);
  camera_image = {camera_rgb, kCameraWidth, kCameraHeight};

  // Face box of the synthetic player in camera coordinates (see render_screens.py).
  person_present = true;
  person_box[0] = 150;
  person_box[1] = 62;
  person_box[2] = 230;
  person_box[3] = 150;
  stable_face_frames = 3;

  int64_t now = 10000000;
  render_frame(now);
  save("01-find-face");

  host_now_us = now;
  capture_face_candidate(camera_image);
  const int64_t lock_start = now;
  for (int ms = 0; ms <= 2000; ms += 50) {
    render_frame(lock_start + ms * 1000);
    char name[32];
    std::snprintf(name, sizeof(name), "lock-%04d", ms);
    save(name);
  }

  now += 3000000;
  fight_state = FightState::Intro;
  round_state_since_us = now - 1600000;
  render_frame(now);
  save("03-intro");

  now += 3000000;
  start_fight(now, 82);
  chi_x = 70;
  chi_y = 86;
  game_action = GameAction::PunchCombo;
  game_action_started_us = now - 280000;
  last_attack_us = now - 60000;
  combo = 3;
  render_frame(now);
  save("04-attack");

  now += 3000000;
  start_fight(now, 64);
  boss_state = BossState::Warning;
  boss_state_since_us = now - 300000;
  render_frame(now);
  save("05-warning");

  now += 3000000;
  start_fight(now, 64);
  boss_state = BossState::Strike;
  boss_state_since_us = now - 230000;
  game_action = GameAction::Jump;
  game_action_started_us = now - 270000;
  render_frame(now);
  save("06-flick-jump");

  now += 3000000;
  start_fight(now, 40);
  weak_ox = 34;
  weak_oy = 52;
  weak_until_us = now + 400000;
  render_frame(now);
  save("07-weak-point");

  now += 3000000;
  start_fight(now, 16);
  chi_x = 70;
  chi_y = 86;
  game_action = GameAction::Kick;
  game_action_started_us = now - 300000;
  last_attack_us = now - 60000;
  last_crit_us = now - 100000;
  render_frame(now);
  save("08-critical");

  now += 3000000;
  start_fight(now, 0);
  fight_state = FightState::ChiWins;
  round_state_since_us = now - 900000;
  game_action = GameAction::Victory;
  game_action_started_us = now - 900000;
  render_frame(now);
  save("09-chi-wins");

  now += 3000000;
  start_fight(now, 58);
  fight_state = FightState::FaceWins;
  chi_hp = 0;
  round_state_since_us = now - 900000;
  game_action = GameAction::Defeat;
  game_action_started_us = now - 900000;
  render_frame(now);
  save("10-face-wins");
  return 0;
}
