#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import os
import sys
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
BOOT_REL = Path("assets/dualeye_sdcard_v0_1/sdcard/boot/xiaoba_x1")
BRAIN_BOOT = ROOT / BOOT_REL
FIRMWARE_ROOT = Path(os.environ.get("ATLAS_FIRMWARE_ROOT", ROOT.parent / "Atlas-One-Firmware"))
FIRMWARE_BOOT = FIRMWARE_ROOT / BOOT_REL
OUTPUT_DIR = ROOT / "output" / "verification"
SOURCE_BOOT = ROOT / "assets" / "dualeye_sdcard_v0_1" / "source" / "boot" / "xiaoba_x1"
BURN_PREVIEW = SOURCE_BOOT / "xiaoba_x1_boot_intro_burn_preview_v0_1.png"
SIZE = 240


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def load_manifest(root: Path) -> dict[str, Any]:
    return json.loads((root / "manifest.json").read_text(encoding="utf-8"))


def load_rgba(path: Path) -> Image.Image:
    return Image.open(path).convert("RGBA")


def connected_components(mask: set[tuple[int, int]]) -> list[dict[str, Any]]:
    components: list[dict[str, Any]] = []
    while mask:
        start = mask.pop()
        stack = [start]
        xs = [start[0]]
        ys = [start[1]]
        for x, y in stack:
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if (nx, ny) in mask:
                    mask.remove((nx, ny))
                    stack.append((nx, ny))
                    xs.append(nx)
                    ys.append(ny)
        area = len(stack)
        components.append({
            "area": area,
            "bbox": [min(xs), min(ys), max(xs), max(ys)],
        })
    return components


def upper_eye_artifacts(image: Image.Image) -> list[dict[str, Any]]:
    bg = Image.new("RGBA", image.size, (0, 0, 0, 255))
    bg.alpha_composite(image)
    rgb = bg.convert("RGB")
    pixels = rgb.load()
    mask: set[tuple[int, int]] = set()
    for y in range(24, 72):
        for x in range(15, 225):
            r, g, b = pixels[x, y]
            # Eye-white-ish pixels. This deliberately ignores skin highlights
            # unless they are large separated blobs in the upper forehead band.
            if r > 185 and g > 165 and b > 135 and abs(r - g) < 65 and abs(g - b) < 85 and r - b < 115:
                mask.add((x, y))
    return [
        comp for comp in connected_components(mask)
        if comp["area"] >= 120 and comp["bbox"][3] <= 70
    ]


def circular_lcd(image: Image.Image, scale: float = 0.58) -> Image.Image:
    out_size = int(SIZE * scale)
    base = Image.new("RGBA", (out_size, out_size), (0, 0, 0, 0))
    shadow = Image.new("RGBA", (out_size, out_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(shadow)
    draw.ellipse((3, 5, out_size - 3, out_size - 1), fill=(0, 0, 0, 58))
    base.alpha_composite(shadow)
    panel = Image.new("RGBA", (SIZE, SIZE), (8, 13, 24, 255))
    panel.alpha_composite(image)
    panel = panel.resize((out_size - 10, out_size - 10), Image.Resampling.LANCZOS)
    mask = Image.new("L", panel.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, panel.size[0] - 1, panel.size[1] - 1), fill=255)
    base.alpha_composite(Image.composite(panel, Image.new("RGBA", panel.size, (0, 0, 0, 0)), mask), (5, 3))
    ring = ImageDraw.Draw(base)
    ring.ellipse((5, 3, out_size - 6, out_size - 8), outline=(225, 230, 218, 210), width=2)
    return base


def make_burn_preview(root: Path, frame_count: int) -> None:
    SOURCE_BOOT.mkdir(parents=True, exist_ok=True)
    cell_w, cell_h = 320, 190
    sheet = Image.new("RGB", (cell_w * 3, cell_h * 2), (237, 236, 228))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for i in range(frame_count):
        x = (i % 3) * cell_w
        y = (i // 3) * cell_h
        left = circular_lcd(load_rgba(root / "left" / f"frame_{i:02d}.png"))
        right = circular_lcd(load_rgba(root / "right" / f"frame_{i:02d}.png"))
        sheet.paste(left.convert("RGB"), (x + 16, y + 28), left)
        sheet.paste(right.convert("RGB"), (x + 166, y + 28), right)
        draw.text((x + 10, y + 8), f"firmware frame {i:02d}", fill=(35, 38, 35), font=font)
    sheet.save(BURN_PREVIEW, optimize=True)


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []
    if not BRAIN_BOOT.exists():
        errors.append(f"missing brain boot assets: {BRAIN_BOOT}")
    if not FIRMWARE_BOOT.exists():
        errors.append(f"missing firmware boot assets: {FIRMWARE_BOOT}")
    if errors:
        print(json.dumps({"ok": False, "errors": errors}, ensure_ascii=False, indent=2))
        return 1

    brain_manifest = load_manifest(BRAIN_BOOT)
    firmware_manifest = load_manifest(FIRMWARE_BOOT)
    frame_count = int(firmware_manifest.get("frame_count", 0))
    if brain_manifest != firmware_manifest:
        errors.append("brain and firmware boot manifests differ")
    if frame_count != 6:
        errors.append(f"unexpected firmware frame_count={frame_count}; expected 6")
    if int(firmware_manifest.get("fps", 0)) != 6:
        errors.append(f"unexpected firmware fps={firmware_manifest.get('fps')}; expected 6")

    checked_files = ["manifest.json", "fallback_left.png", "fallback_right.png"]
    checked_files += [f"left/frame_{i:02d}.png" for i in range(frame_count)]
    checked_files += [f"right/frame_{i:02d}.png" for i in range(frame_count)]
    for rel in checked_files:
        brain_path = BRAIN_BOOT / rel
        firmware_path = FIRMWARE_BOOT / rel
        if not brain_path.exists() or not firmware_path.exists():
            errors.append(f"missing asset pair: {rel}")
            continue
        if sha256(brain_path) != sha256(firmware_path):
            errors.append(f"brain/firmware asset mismatch: {rel}")
        if rel.endswith(".png"):
            image = load_rgba(firmware_path)
            if image.size != (SIZE, SIZE):
                errors.append(f"{rel} size={image.size}; expected {(SIZE, SIZE)}")

    artifact_report = []
    for i in range(frame_count):
        rel = f"left/frame_{i:02d}.png"
        artifacts = upper_eye_artifacts(load_rgba(FIRMWARE_BOOT / rel))
        artifact_report.append({"frame": i, "upper_eye_like_components": len(artifacts), "components": artifacts})
        if len(artifacts) > 1:
            errors.append(f"{rel} has {len(artifacts)} upper eye-like components; likely four-eye artifact")

    make_burn_preview(FIRMWARE_BOOT, frame_count)
    payload = {
        "ok": not errors,
        "brain_boot": str(BRAIN_BOOT),
        "firmware_boot": str(FIRMWARE_BOOT),
        "manifest": firmware_manifest,
        "artifact_report": artifact_report,
        "burn_preview": str(BURN_PREVIEW),
        "warnings": warnings,
        "errors": errors,
    }
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
