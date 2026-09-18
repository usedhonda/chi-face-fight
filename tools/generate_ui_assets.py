#!/usr/bin/env python3
"""Crop the approved UI atlas into small, pre-scaled RGBA firmware assets."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ATLAS = ROOT / "assets/face-fight-ui/face-fight-ui-atlas-v1.png"
BUTTON_ATLAS = ROOT / "assets/face-fight-ui/level-buttons-v1.png"
GENERATED = ROOT / "assets/face-fight-ui/generated"
OUTPUT = ROOT / "main/ui_assets.cpp"

# name: crop geometry, maximum output geometry
ASSETS = {
    "title": ("1100x315+50+0", "220x72>"),
    "fight": ("360x145+600+555", "190x90>"),
    "hit": ("250x150+430+700", "117x70>"),
    "block": ("260x145+190+700", "130x70>"),
    "combo": ("270x145+0+835", "125x70>"),
    "ko": ("250x165+235+825", "121x80>"),
    "chi_wins": ("250x145+455+835", "140x75>"),
    "face_wins": ("260x145+680+835", "140x75>"),
}

BUTTON_ASSETS = {
    "next_level": ("720x480+0+80", "108x56>"),
    "exit": ("728x480+720+80", "108x56>"),
    "retry": ("720x520+0+520", "108x56>"),
    "level_up": ("728x520+720+520", "108x56>"),
}


def run(*args: str, capture: bool = False) -> bytes:
    return subprocess.run(args, check=True, capture_output=capture).stdout


def main() -> None:
    GENERATED.mkdir(parents=True, exist_ok=True)
    declarations = ["#include <cstdint>\n"]
    for name, (crop, resize) in {**ASSETS, **BUTTON_ASSETS}.items():
        source = BUTTON_ATLAS if name in BUTTON_ASSETS else ATLAS
        png = GENERATED / f"{name}.png"
        raw = GENERATED / f"{name}.rgba"
        run("magick", str(source), "-crop", crop, "-trim", "+repage",
            "-resize", resize, str(png))
        size = run("magick", "identify", "-format", "%w %h", str(png),
                   capture=True).decode().split()
        width, height = map(int, size)
        run("magick", str(png), "rgba:" + str(raw))
        data = raw.read_bytes()
        if len(data) != width * height * 4:
            raise RuntimeError(f"unexpected RGBA size for {name}")
        declarations.append(
            f"extern const int ui_{name}_width = {width};\n"
            f"extern const int ui_{name}_height = {height};\n"
            f"extern const uint8_t ui_{name}_rgba[] = {{\n")
        for offset in range(0, len(data), 16):
            chunk = data[offset:offset + 16]
            declarations.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        declarations.append("};\n\n")
        raw.unlink()
    OUTPUT.write_text("".join(declarations))


if __name__ == "__main__":
    main()
