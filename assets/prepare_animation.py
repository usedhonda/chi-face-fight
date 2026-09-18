#!/usr/bin/env python3
"""Normalize ordered transparent PNG animation frames for the Chi display."""

from __future__ import annotations

import argparse
import json
import sys
from array import array
from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw


CANVAS = (80, 96)
PRESET_BASELINES = {"kick": 95, "hit": 95, "victory": 94}
PROMINENT_COMPONENT_RATIO = 0.05


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Normalize ordered transparent PNGs to common-scale 80x96 frames, "
            "then emit raw RGBA, a PNG atlas, a contact sheet, and JSON metrics."
        )
    )
    parser.add_argument("frames", nargs="*", type=Path, help="ordered input PNG files")
    parser.add_argument(
        "--sheet-input",
        type=Path,
        help="transparent sprite sheet; extracts prominent connected characters left-to-right",
    )
    parser.add_argument(
        "--expected-frames",
        type=int,
        help="required prominent-character count for --sheet-input (ambiguous counts fail)",
    )
    parser.add_argument(
        "--sheet-crop-padding",
        type=int,
        default=2,
        help="transparent pixels retained around each extracted sheet character (default: 2)",
    )
    parser.add_argument("--output-dir", required=True, type=Path, help="new or empty output directory")
    parser.add_argument("--name", required=True, help="output stem, for example chi_kick_v1")
    parser.add_argument("--action", choices=sorted(PRESET_BASELINES), default="kick")
    parser.add_argument(
        "--baseline",
        type=int,
        help="inclusive ground row (default: action preset; kick/hit=95, victory=94)",
    )
    parser.add_argument("--padding", type=int, default=2, help="minimum horizontal/top canvas padding")
    parser.add_argument("--alpha-threshold", type=int, default=1, help="alpha >= value counts as content")
    parser.add_argument("--preview-scale", type=int, default=3, help="contact-sheet integer zoom")
    return parser.parse_args()


def fail(message: str) -> None:
    raise ValueError(message)


def alpha_bbox(image: Image.Image, threshold: int) -> tuple[int, int, int, int] | None:
    alpha = image.getchannel("A").point(lambda value: 255 if value >= threshold else 0)
    return alpha.getbbox()


def load_frames(paths: list[Path], threshold: int) -> list[dict]:
    frames: list[dict] = []
    for path in paths:
        if path.suffix.lower() != ".png" or not path.is_file():
            fail(f"not a readable PNG: {path}")
        with Image.open(path) as source:
            has_alpha = "A" in source.getbands() or "transparency" in source.info
            if not has_alpha:
                fail(f"PNG has no alpha channel/transparency: {path}")
            rgba = source.convert("RGBA")
        bbox = alpha_bbox(rgba, threshold)
        if bbox is None:
            fail(f"PNG has no pixels at alpha threshold {threshold}: {path}")
        frames.append({"path": path, "image": rgba, "bbox": bbox})
    return frames


def load_sheet(
    path: Path, threshold: int, expected_frames: int, crop_padding: int,
) -> list[dict]:
    if path.suffix.lower() != ".png" or not path.is_file():
        fail(f"not a readable PNG: {path}")
    with Image.open(path) as source:
        has_alpha = "A" in source.getbands() or "transparency" in source.info
        if not has_alpha:
            fail(f"PNG has no alpha channel/transparency: {path}")
        rgba = source.convert("RGBA")

    width, height = rgba.size
    alpha = rgba.getchannel("A").tobytes()
    labels = array("i", [-1]) * (width * height)
    components: list[dict] = []
    for start, value in enumerate(alpha):
        if value < threshold or labels[start] != -1:
            continue
        label = len(components)
        labels[start] = label
        queue = deque([start])
        area = 0
        min_x = max_x = start % width
        min_y = max_y = start // width
        while queue:
            pixel = queue.popleft()
            x, y = pixel % width, pixel // width
            area += 1
            min_x, max_x = min(min_x, x), max(max_x, x)
            min_y, max_y = min(min_y, y), max(max_y, y)
            for ny in range(max(0, y - 1), min(height, y + 2)):
                row = ny * width
                for nx in range(max(0, x - 1), min(width, x + 2)):
                    neighbor = row + nx
                    if alpha[neighbor] >= threshold and labels[neighbor] == -1:
                        labels[neighbor] = label
                        queue.append(neighbor)
        components.append({"label": label, "area": area, "bbox": (min_x, min_y, max_x + 1, max_y + 1)})

    if not components:
        fail(f"PNG has no pixels at alpha threshold {threshold}: {path}")
    largest_area = max(component["area"] for component in components)
    prominent = [
        component for component in components
        if component["area"] >= largest_area * PROMINENT_COMPONENT_RATIO
    ]
    if len(prominent) != expected_frames:
        areas = sorted((component["area"] for component in components), reverse=True)
        fail(
            f"expected {expected_frames} prominent characters but found {len(prominent)} "
            f"(component areas: {areas[:expected_frames + 2]})"
        )

    frames: list[dict] = []
    for component in sorted(prominent, key=lambda item: (item["bbox"][0], item["bbox"][1])):
        left, top, right, bottom = component["bbox"]
        crop_box = (
            max(0, left - crop_padding), max(0, top - crop_padding),
            min(width, right + crop_padding), min(height, bottom + crop_padding),
        )
        crop = rgba.crop(crop_box)
        pixels = list(crop.getdata())
        crop_width = crop.width
        for cy in range(crop.height):
            sheet_row = (crop_box[1] + cy) * width
            crop_row = cy * crop_width
            for cx in range(crop_width):
                if labels[sheet_row + crop_box[0] + cx] != component["label"]:
                    red, green, blue, _ = pixels[crop_row + cx]
                    pixels[crop_row + cx] = (red, green, blue, 0)
        crop.putdata(pixels)
        local_bbox = alpha_bbox(crop, threshold)
        if local_bbox is None:
            fail(f"component at {component['bbox']} became empty during extraction")
        frames.append(
            {
                "path": path,
                "image": crop,
                "bbox": local_bbox,
                "source_size": rgba.size,
                "source_bbox": component["bbox"],
                "component_area": component["area"],
            }
        )
    return frames


def render(frames: list[dict], baseline: int, padding: int) -> tuple[list[Image.Image], float, list[dict]]:
    max_width = max(frame["bbox"][2] - frame["bbox"][0] for frame in frames)
    max_height = max(frame["bbox"][3] - frame["bbox"][1] for frame in frames)
    available_width = CANVAS[0] - 2 * padding
    available_height = baseline + 1 - padding
    if available_width <= 0 or available_height <= 0:
        fail("padding/baseline leaves no drawable canvas")
    scale = min(1.0, available_width / max_width, available_height / max_height)
    if scale <= 0:
        fail("computed scale is not positive")

    output: list[Image.Image] = []
    metrics: list[dict] = []
    for index, frame in enumerate(frames):
        bbox = frame["bbox"]
        crop = frame["image"].crop(bbox)
        width = max(1, round(crop.width * scale))
        height = max(1, round(crop.height * scale))
        resized = crop.resize((width, height), Image.Resampling.LANCZOS)
        x = (CANVAS[0] - width) // 2
        y = baseline + 1 - height
        if x < 0 or y < 0 or x + width > CANVAS[0] or y + height > CANVAS[1]:
            fail(f"frame {index} would clip at {(x, y, x + width, y + height)}")
        canvas = Image.new("RGBA", CANVAS, (0, 0, 0, 0))
        canvas.alpha_composite(resized, (x, y))
        final_bbox = alpha_bbox(canvas, 1)
        if final_bbox is None:
            fail(f"frame {index} became empty after resize")
        metrics.append(
            {
                "index": index,
                "source": str(frame["path"]),
                "source_size": list(frame.get("source_size", frame["image"].size)),
                "source_bbox": list(frame.get("source_bbox", bbox)),
                "normalized_bbox": list(final_bbox),
                "opaque_or_partial_pixels": sum(
                    count for count, value in canvas.getchannel("A").getcolors(maxcolors=256) if value
                ),
                "contact_row": final_bbox[3] - 1,
                **({"source_component_pixels": frame["component_area"]} if "component_area" in frame else {}),
            }
        )
        output.append(canvas)
    return output, scale, metrics


def save_outputs(
    frames: list[Image.Image], output_dir: Path, name: str, scale: float, baseline: int,
    metrics: list[dict], preview_scale: int,
) -> None:
    if output_dir.exists() and any(output_dir.iterdir()):
        fail(f"output directory is not empty; refusing to overwrite: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)

    atlas = Image.new("RGBA", (CANVAS[0] * len(frames), CANVAS[1]), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        frame.save(output_dir / f"{name}-{index:02d}.png")
        atlas.alpha_composite(frame, (index * CANVAS[0], 0))
    atlas.save(output_dir / f"{name}-atlas.png")
    (output_dir / f"{name}.rgba").write_bytes(b"".join(frame.tobytes("raw", "RGBA") for frame in frames))

    zoom = max(1, preview_scale)
    tile_width, tile_height = CANVAS[0] * zoom, CANVAS[1] * zoom
    preview = Image.new("RGB", (tile_width * len(frames), tile_height + 20), "#d7d7d7")
    draw = ImageDraw.Draw(preview)
    for index, frame in enumerate(frames):
        checker = Image.new("RGB", CANVAS, "#eeeeee")
        checker_draw = ImageDraw.Draw(checker)
        for cy in range(0, CANVAS[1], 8):
            for cx in range(0, CANVAS[0], 8):
                if (cx // 8 + cy // 8) % 2:
                    checker_draw.rectangle((cx, cy, cx + 7, cy + 7), fill="#cfcfcf")
        checker.paste(frame, mask=frame.getchannel("A"))
        preview.paste(checker.resize((tile_width, tile_height), Image.Resampling.NEAREST), (index * tile_width, 0))
        draw.line((index * tile_width, baseline * zoom, (index + 1) * tile_width - 1, baseline * zoom), fill="#ff2d2d", width=1)
        draw.text((index * tile_width + 4, tile_height + 3), f"{index:02d}", fill="black")
    preview.save(output_dir / f"{name}-contact-sheet.png")

    report = {
        "format": "RGBA8888 frame-major raw bytes",
        "canvas": list(CANVAS),
        "frame_count": len(frames),
        "scale": scale,
        "baseline": baseline,
        "raw_bytes": len(frames) * CANVAS[0] * CANVAS[1] * 4,
        "frames": metrics,
    }
    (output_dir / f"{name}-metrics.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


def main() -> int:
    args = parse_args()
    try:
        if not 1 <= args.alpha_threshold <= 255:
            fail("--alpha-threshold must be 1..255")
        if args.padding < 0 or args.preview_scale < 1:
            fail("--padding must be >= 0 and --preview-scale must be >= 1")
        if args.sheet_crop_padding < 0:
            fail("--sheet-crop-padding must be >= 0")
        if args.sheet_input is not None:
            if args.frames:
                fail("use either positional frames or --sheet-input, not both")
            if args.expected_frames is None or args.expected_frames < 1:
                fail("--sheet-input requires --expected-frames >= 1")
        elif args.expected_frames is not None:
            fail("--expected-frames is only valid with --sheet-input")
        elif not args.frames:
            fail("provide positional frames or --sheet-input")
        baseline = args.baseline if args.baseline is not None else PRESET_BASELINES[args.action]
        if not 0 <= baseline < CANVAS[1]:
            fail(f"--baseline must be 0..{CANVAS[1] - 1}")
        frames = (
            load_sheet(args.sheet_input, args.alpha_threshold, args.expected_frames, args.sheet_crop_padding)
            if args.sheet_input is not None
            else load_frames(args.frames, args.alpha_threshold)
        )
        normalized, scale, metrics = render(frames, baseline, args.padding)
        save_outputs(normalized, args.output_dir, args.name, scale, baseline, metrics, args.preview_scale)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
