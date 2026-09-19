# Chi Face Fight

A self-contained face-fighting game for the Seeed Studio XIAO ESP32S3 Sense
and a Waveshare 1.69-inch ST7789V2 touch LCD.

The OV3660 camera detects and captures one face locally. Camera acquisition and
face inference then stop for the entire fight. The captured face becomes a
transparent, moving opponent while Secretary Chi walks, punches, kicks, dodges,
blocks, gets hit, and celebrates using pixel-art sprite animation.

<p align="center">
  <img src="docs/images/lock-on.gif" width="240" alt="Sniper lock-on sequence after the face is captured">
</p>

## Screenshots

| Find face | Lock on | Intro | Attack | Warning |
|:-:|:-:|:-:|:-:|:-:|
| <img src="docs/images/01-find-face.png" width="150"> | <img src="docs/images/02-lock-on.png" width="150"> | <img src="docs/images/03-intro.png" width="150"> | <img src="docs/images/04-attack.png" width="150"> | <img src="docs/images/05-warning.png" width="150"> |

| Flick dodge | Weak point | Critical | Chi wins | Face wins |
|:-:|:-:|:-:|:-:|:-:|
| <img src="docs/images/06-flick-jump.png" width="150"> | <img src="docs/images/07-weak-point.png" width="150"> | <img src="docs/images/08-critical.png" width="150"> | <img src="docs/images/09-chi-wins.png" width="150"> | <img src="docs/images/10-face-wins.png" width="150"> |

These screenshots are rendered by the firmware's own drawing code compiled on
the host (see [Regenerating screenshots](#regenerating-screenshots)); the
opponent is a synthetic cartoon face, not a real person.

## Game flow

1. Find a face with the on-device ESP-DL detector. The preview is mirrored like
   a selfie camera.
2. A sniper scope locks onto the captured face and fires; the portrait is cut
   out with a feathered head silhouette.
3. Play a title sequence and enter the office or rooftop arena.
4. Fight until one side reaches 0 HP.
5. Win to continue to a harder level, or exit to restart the camera and find a
   new face.

The firmware uses no Wi-Fi, cloud service, or microSD storage.

## Controls

| Input | Effect |
|---|---|
| Tap the face | Queue an attack. Chi walks into range; every third landed hit is a kick. |
| Tap the glowing weak point | The next hit is a critical (damage x2 + 2). |
| Hold Chi | Guard. Blocked hits deal only chip damage. |
| Flick from Chi | Dodge: flick up to jump, any other direction to side-step. |
| Flick just before impact | Just dodge: Chi counter-attacks with a critical. |

## Combat

| | Damage |
|---|---|
| Punch / kick | 7 / 12 (critical 16 / 26) |
| Boss orb, unblocked / blocked | 14 / 2 |
| Boss body charge (level 2+), unblocked / blocked | 18 / 4 |

- Boss HP grows by 12 per level (max 180). Warnings get shorter, attacks come
  faster, feints appear from level 3, and the face dodges around from level 4.
- The opponent's face shows a bruise at 70% HP, cracks at 45%, and a bandage at
  20%.
- Full rules and the state machine live in
  [docs/FACE_FIGHT_GAME.md](docs/FACE_FIGHT_GAME.md).

## Hardware and wiring

- Seeed Studio XIAO ESP32S3 Sense with OV3660 camera and PSRAM
- Waveshare 1.69-inch Touch LCD Module, 240 x 280, ST7789V2 + CST816T
- LCD SPI: MOSI GPIO9, SCK GPIO7, CS GPIO2, DC GPIO4, RST GPIO1, BL GPIO43
- Touch I2C: SDA GPIO5, SCL GPIO6, RST GPIO44, IRQ GPIO3

## Build

Install PlatformIO, connect the XIAO by USB, then run:

```sh
pio run
pio run --target upload --upload-port /dev/cu.usbmodem101
pio device monitor --port /dev/cu.usbmodem101 --baud 115200
```

Without a PlatformIO install, prefix the commands with
`uvx --from platformio`, for example `uvx --from platformio pio run`.
Replace the serial device path when macOS assigns a different port.

The serial log reports gameplay as `FIGHT_EVENT`, `FACE_EVENT`, and
`TOUCH_TRACE` lines, which is the quickest way to check input and balance on
the device.

## Regenerating screenshots

```sh
uv run --with pillow python tools/screenshots/render_screens.py
```

The script compiles `main/main.cpp` with its hardware functions stripped out
(`tools/screenshots/host_stubs.h` stands in for ESP-IDF), plays scripted scenes
through `tools/screenshots/host_main.cpp`, and writes `docs/images/`. It needs
`clang++` and `uv`.

## Project structure

- `main/`: firmware and generated sprite/UI/background arrays
- `assets/`: source sprite sheets, previews, and animation preparation tools
- `tools/`: reproducible UI asset generation and host screenshot rendering
- `components/`: local ESP-DL model component
- `docs/`: game design notes and README images
