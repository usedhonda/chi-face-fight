#!/usr/bin/env python3
"""Render README screenshots from the firmware's own drawing code.

The game logic and renderer in main/main.cpp are compiled on the host with
hardware functions stripped out. An AI-generated pixel-art adult stands in for
the camera input so no real person appears in the images.

Usage: uv run --with pillow python tools/screenshots/render_screens.py
Output: docs/images/*.png and docs/images/lock-on-v3.gif
"""
import pathlib
import re
import subprocess
import tempfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
HERE = pathlib.Path(__file__).resolve().parent
OUT = ROOT / "docs" / "images"
SCALE = 2

# Functions that touch hardware; everything else is compiled as-is.
HARDWARE_FUNCTIONS = [
    "camera_config", "start_camera", "stop_camera", "camera_frame_signature",
    "mark_camera_progress", "camera_watchdog_task", "init_lcd", "touch_write",
    "touch_read", "poll_touch", "touch_task", "init_touch", "microphone_task",
    "run_detection",
]

ASSET_SOURCES = [
    "chi_walk.cpp", "chi_actions.cpp", "chi_game_actions.cpp",
    "chi_fighting_actions.cpp", "ui_assets.cpp", "arena_backgrounds.cpp",
]


def extract_game_source() -> str:
    lines = (ROOT / "main" / "main.cpp").read_text().splitlines()
    first = next(i for i, line in enumerate(lines) if line.startswith("extern const"))
    last = next(i for i, line in enumerate(lines) if line.startswith('extern "C" void app_main'))
    body = lines[first:last]
    pattern = re.compile(r"^[A-Za-z_][\w:<>\s\*&]*\b(%s)\(" % "|".join(HARDWARE_FUNCTIONS))
    kept, skipping = [], False
    for line in body:
        if not skipping and pattern.match(line):
            skipping = True
        if skipping:
            if line == "}":
                skipping = False
            continue
        kept.append(line)
    return "\n".join(kept) + "\n"


def synthetic_camera_frame(path: pathlib.Path) -> None:
    """Resize the fictional README player to the camera's native frame."""
    source = HERE / "synthetic-player.png"
    image = Image.open(source).convert("RGB").resize((320, 240), Image.Resampling.LANCZOS)
    path.write_bytes(image.tobytes())


def load_ppm(path: pathlib.Path) -> Image.Image:
    image = Image.open(path).convert("RGB")
    return image.resize((image.width * SCALE, image.height * SCALE), Image.NEAREST)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temp:
        temp = pathlib.Path(temp)
        (temp / "game.inc").write_text(extract_game_source())
        synthetic_camera_frame(temp / "camera.rgb")
        binary = temp / "render"
        subprocess.run(
            ["clang++", "-std=c++17", "-O2", "-w", f"-I{HERE}", f"-I{temp}",
             "-o", str(binary), str(HERE / "host_main.cpp")]
            + [str(ROOT / "main" / name) for name in ASSET_SOURCES],
            check=True)
        frames = temp / "frames"
        frames.mkdir()
        subprocess.run([str(binary), str(temp / "camera.rgb"), str(frames)], check=True)

        for ppm in sorted(frames.glob("[0-9]*.ppm")):
            load_ppm(ppm).save(OUT / f"{ppm.stem}.png")
        lock = [load_ppm(p) for p in sorted(frames.glob("lock-*.ppm"))]
        lock[0].save(OUT / "lock-on-v3.gif", save_all=True, append_images=lock[1:],
                     duration=50, loop=0)
        lock[len(lock) // 2].save(OUT / "02-lock-on.png")
    print("wrote", ", ".join(sorted(p.name for p in OUT.iterdir())))


if __name__ == "__main__":
    main()
