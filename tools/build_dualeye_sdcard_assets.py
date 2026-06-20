from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
PACK_ROOT = ROOT / "assets" / "dualeye_sdcard_v0_1"
SOURCE = PACK_ROOT / "source" / "atlas_dualeye_theme_master_v0_1.png"
OUT_ROOT = PACK_ROOT / "sdcard" / "atlas_eyes"
CONTACT = PACK_ROOT / "atlas_eyes_contact_sheet_v0_1.png"

THEMES = [
    ("raptor", "猛禽眼"),
    ("mecha", "机械电子眼"),
    ("goggle", "黄色护目镜眼"),
    ("pet", "电子宠物巡游"),
]

STATES = [
    ("idle", "待机"),
    ("blink", "眨眼"),
    ("listen", "聆听"),
]

EYES = [("L", "left"), ("R", "right")]


def projection_segments(projection: np.ndarray, threshold: int, min_width: int) -> list[tuple[int, int]]:
    segments: list[tuple[int, int]] = []
    start: int | None = None
    for i, value in enumerate(projection):
        active = value > threshold
        if active and start is None:
            start = i
        if (not active or i == len(projection) - 1) and start is not None:
            end = i if not active else i + 1
            if end - start >= min_width:
                segments.append((start, end))
            start = None
    return segments


def significant_pair_boxes(image: Image.Image) -> list[tuple[int, int, int, int]]:
    arr = np.asarray(image.convert("RGB"))
    brightness = arr.max(axis=2)
    mask = brightness > 24
    x_segments = projection_segments(mask.sum(axis=0), threshold=60, min_width=200)
    y_segments = projection_segments(mask.sum(axis=1), threshold=80, min_width=160)
    if len(x_segments) != len(THEMES) or len(y_segments) != len(STATES):
        raise SystemExit(f"expected {len(THEMES)} columns and {len(STATES)} rows, found {len(x_segments)} columns and {len(y_segments)} rows")
    return [(x1, y1, x2, y2) for y1, y2 in y_segments for x1, x2 in x_segments]


def crop_eye(image: Image.Image, center_x: int, center_y: int, size: int) -> Image.Image:
    half = size // 2
    crop = image.crop((center_x - half, center_y - half, center_x + half, center_y + half))
    crop = crop.resize((240, 240), Image.Resampling.LANCZOS).convert("RGBA")
    return crop


def make_contact_sheet(entries: list[dict[str, str]]) -> None:
    tile_w, tile_h = 240, 296
    cols = len(THEMES) * 2
    rows = len(STATES)
    sheet = Image.new("RGB", (cols * tile_w, rows * tile_h + 64), (245, 244, 240))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/PingFang.ttc", 22)
        small = ImageFont.truetype("/System/Library/Fonts/PingFang.ttc", 16)
    except OSError:
        font = ImageFont.load_default()
        small = ImageFont.load_default()

    draw.text((24, 16), "Atlas Rover DualEye SD Asset Pack V0.1", fill=(28, 28, 28), font=font)
    for entry in entries:
        theme_i = next(i for i, (key, _) in enumerate(THEMES) if key == entry["theme"])
        state_i = next(i for i, (key, _) in enumerate(STATES) if key == entry["state"])
        eye_i = 0 if entry["eye"] == "L" else 1
        col = theme_i * 2 + eye_i
        x = col * tile_w
        y = state_i * tile_h + 64
        img = Image.open(PACK_ROOT / "sdcard" / entry["path"]).convert("RGBA")
        sheet.paste(img.convert("RGB"), (x, y))
        label = f"{entry['theme']} {entry['state']} {entry['eye']}"
        draw.text((x + 16, y + 248), label, fill=(32, 32, 32), font=small)

    CONTACT.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(CONTACT)


def main() -> None:
    image = Image.open(SOURCE).convert("RGB")
    boxes = significant_pair_boxes(image)
    expected = len(THEMES) * len(STATES)
    if len(boxes) != expected:
        raise SystemExit(f"expected {expected} eye pairs, found {len(boxes)}")

    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    entries: list[dict[str, str]] = []

    for row, state in enumerate(STATES):
        row_boxes = boxes[row * len(THEMES):(row + 1) * len(THEMES)]
        for col, theme in enumerate(THEMES):
            x1, y1, x2, y2 = row_boxes[col]
            pair_w = x2 - x1
            pair_h = y2 - y1
            size = int(pair_h * 0.76)
            size = max(176, min(size, 188))
            center_y = (y1 + y2) // 2
            center_x_left = int(x1 + pair_w * 0.25)
            center_x_right = int(x1 + pair_w * 0.75)

            for eye_short, eye_name in EYES:
                center_x = center_x_left if eye_short == "L" else center_x_right
                tile = crop_eye(image, center_x, center_y, size)
                theme_key, theme_zh = theme
                state_key, state_zh = state
                out_dir = OUT_ROOT / theme_key / state_key
                out_dir.mkdir(parents=True, exist_ok=True)
                filename = f"{eye_name}.png"
                tile.save(out_dir / filename)
                rel_path = f"atlas_eyes/{theme_key}/{state_key}/{filename}"
                entries.append({
                    "theme": theme_key,
                    "theme_zh": theme_zh,
                    "state": state_key,
                    "state_zh": state_zh,
                    "eye": eye_short,
                    "eye_name": eye_name,
                    "path": rel_path,
                    "width": "240",
                    "height": "240",
                    "format": "PNG",
                })

    manifest = {
        "version": "0.1",
        "target": "ESP32-S3-DualEye-Touch-LCD-1.28",
        "storage": "SD card",
        "root": "/atlas_eyes",
        "size_px": 240,
        "themes": [{"id": key, "name_zh": zh} for key, zh in THEMES],
        "states": [{"id": key, "name_zh": zh} for key, zh in STATES],
        "assets": entries,
    }
    (OUT_ROOT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    make_contact_sheet(entries)
    print(f"wrote {len(entries)} eye assets to {OUT_ROOT}")
    print(f"wrote contact sheet to {CONTACT}")


if __name__ == "__main__":
    main()
