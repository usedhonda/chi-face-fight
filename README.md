# Chi Face Fight

A self-contained face-fighting game for the Seeed Studio XIAO ESP32S3 Sense
and a Waveshare 1.69-inch ST7789V2 touch LCD.

The OV3660 camera detects and captures one face locally. Camera acquisition and
face inference then stop for the entire fight. The captured face becomes a
transparent, moving opponent while Secretary Chi walks, punches, kicks, blocks,
gets hit, and celebrates using pixel-art sprite animation.

## Game flow

1. Find and capture a face with the on-device ESP-DL detector.
2. Play a title sequence and enter the office or rooftop arena.
3. Tap the face to queue Chi's attacks. Chi walks into range before damage is
   applied; every third landed attack is a stronger kick.
4. Hold Chi to block enemy projectiles or body charges.
5. Win to continue to a harder level, or exit to restart the camera and find a
   new face.

The firmware uses no Wi-Fi, cloud service, or microSD storage.

## Hardware and wiring

- Seeed Studio XIAO ESP32S3 Sense with OV3660 camera and PSRAM
- Waveshare 1.69-inch Touch LCD Module, 240 x 280, ST7789V2 + CST816
- LCD SPI: MOSI GPIO9, SCK GPIO7, CS GPIO2, DC GPIO4, RST GPIO1, BL GPIO43
- Touch I2C: SDA GPIO5, SCL GPIO6, RST GPIO44, IRQ GPIO3

## Build

Install PlatformIO, connect the XIAO by USB, then run:

```sh
pio run
pio run --target upload --upload-port /dev/cu.usbmodem101
pio device monitor --port /dev/cu.usbmodem101 --baud 115200
```

Replace the serial device path when macOS assigns a different port.

## Project structure

- `main/`: firmware and generated sprite/UI/background arrays
- `assets/`: source sprite sheets, previews, and animation preparation tools
- `tools/`: reproducible UI asset generation
- `components/`: local ESP-DL model component
- `docs/`: game design and implementation notes
