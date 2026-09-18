#pragma once

namespace board_pins {
constexpr int camera_pwdn = -1;
constexpr int camera_reset = -1;
constexpr int camera_xclk = 10;
constexpr int camera_siod = 40;
constexpr int camera_sioc = 39;
constexpr int camera_y9 = 48;
constexpr int camera_y8 = 11;
constexpr int camera_y7 = 12;
constexpr int camera_y6 = 14;
constexpr int camera_y5 = 16;
constexpr int camera_y4 = 18;
constexpr int camera_y3 = 17;
constexpr int camera_y2 = 15;
constexpr int camera_vsync = 38;
constexpr int camera_href = 47;
constexpr int camera_pclk = 13;

constexpr int lcd_mosi = 9;
constexpr int lcd_sck = 7;
constexpr int lcd_cs = 2;
constexpr int lcd_dc = 4;
constexpr int lcd_reset = 1;
constexpr int lcd_backlight = 43;

constexpr int touch_sda = 5;
constexpr int touch_scl = 6;
constexpr int touch_reset = 44;
constexpr int touch_irq = 3;

constexpr int microphone_data = 41;
constexpr int microphone_clock = 42;
constexpr int sd_cs = 21;
}  // namespace board_pins
