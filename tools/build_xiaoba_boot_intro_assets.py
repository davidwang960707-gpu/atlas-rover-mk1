from __future__ import annotations

import json
import math
import shutil
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
PACK_ROOT = ROOT / "assets" / "dualeye_sdcard_v0_1"
BOOT_ROOT = PACK_ROOT / "sdcard" / "boot" / "xiaoba_x1"
SOURCE_ROOT = PACK_ROOT / "source" / "boot" / "xiaoba_x1"
PET_ROOT = PACK_ROOT / "sdcard" / "atlas_pet_head"
ZIP_PATH = PACK_ROOT / "xiaoba_x1_boot_intro_sdcard_v0_1.zip"
PREVIEW_PATH = SOURCE_ROOT / "xiaoba_x1_boot_intro_contact_sheet_v0_1.png"
GIF_PATH = SOURCE_ROOT / "xiaoba_x1_boot_intro_preview_v0_1.gif"

SIZE = 240
FRAME_COUNT = 6
FPS = 6

FONT_CANDIDATES = [
    Path("/System/Library/Fonts/STHeiti Medium.ttc"),
    Path("/System/Library/Fonts/Supplemental/Arial Unicode.ttf"),
    Path("/System/Library/Fonts/Supplemental/Songti.ttc"),
]

PALETTE = {
    "bg": (8, 13, 24),
    "panel": (14, 29, 44),
    "panel_2": (19, 43, 58),
    "line": (102, 231, 213),
    "line_dim": (49, 133, 140),
    "cream": (244, 238, 210),
    "orange": (241, 151, 71),
    "green": (95, 234, 149),
    "red": (239, 92, 92),
    "blue": (87, 177, 255),
}


def load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    for candidate in FONT_CANDIDATES:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size=size)
    return ImageFont.load_default()


FONT_TITLE = load_font(22)
FONT_SMALL = load_font(11)
FONT_TINY = load_font(8)


def ensure_dirs() -> None:
    for path in (BOOT_ROOT / "left", BOOT_ROOT / "right", SOURCE_ROOT):
        path.mkdir(parents=True, exist_ok=True)


def clean_output() -> None:
    if BOOT_ROOT.exists():
        shutil.rmtree(BOOT_ROOT)
    ensure_dirs()


def quantize_png(image: Image.Image, colors: int = 128) -> Image.Image:
    return image.convert("RGBA").quantize(colors=colors, method=Image.Quantize.FASTOCTREE)


def save_png(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    quantize_png(image).save(path, optimize=True, compress_level=9)


def load_pet_asset(rel: str) -> Image.Image:
    path = PET_ROOT / rel
    if not path.exists():
        raise FileNotFoundError(path)
    return Image.open(path).convert("RGBA")


def alpha_blur(image: Image.Image, radius: float = 7.0, opacity: int = 74) -> Image.Image:
    alpha = image.getchannel("A").filter(ImageFilter.GaussianBlur(radius))
    shadow = Image.new("RGBA", image.size, (0, 0, 0, 0))
    shadow.putalpha(ImageEnhance.Brightness(alpha).enhance(opacity / 255))
    return shadow


def fit_pet(image: Image.Image, zoom: float, offset_y: int, alpha: int = 255, rotate: float = 0.0) -> Image.Image:
    rgba = image.convert("RGBA")
    if rotate:
        rgba = rgba.rotate(rotate, resample=Image.Resampling.BICUBIC, center=(SIZE // 2, SIZE // 2))
    if zoom != 1.0:
        new_size = max(1, int(round(SIZE * zoom)))
        resized = rgba.resize((new_size, new_size), Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        canvas.alpha_composite(resized, ((SIZE - new_size) // 2, (SIZE - new_size) // 2 + offset_y))
        rgba = canvas
    elif offset_y:
        canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        canvas.alpha_composite(rgba, (0, offset_y))
        rgba = canvas
    if alpha < 255:
        a = rgba.getchannel("A")
        a = ImageEnhance.Brightness(a).enhance(alpha / 255)
        rgba.putalpha(a)
    return rgba


def pet_frame(index: int) -> Image.Image:
    # A deliberately sleepy, slightly delayed wake-up. It keeps the "only head"
    # constraint and reuses the current no-SD pet_head assets.
    if index == 0:
        base = load_pet_asset("keyframes/sleepy.png")
        alpha = 150
        zoom = 0.94
        y = 8
        frame = fit_pet(base, zoom, y, alpha=alpha)
    elif index == 1:
        base = load_pet_asset("keyframes/idle.png")
        frame = fit_pet(base, 0.98, 2)
    elif index == 2:
        blink_index = 4
        base = load_pet_asset(f"animations/blink/frame_{blink_index:02d}.png")
        frame = fit_pet(base, 0.98, 2, rotate=-1.0)
    elif index == 3:
        view = "yaw_l30"
        base = load_pet_asset(f"views/listen/{view}.png")
        frame = fit_pet(base, 1.0, 0)
    elif index == 4:
        base = load_pet_asset("animations/speak/frame_06.png")
        frame = fit_pet(base, 1.01, -1, rotate=0.8 * math.sin(index))
    else:
        base = load_pet_asset("views/idle/yaw_c.png")
        frame = fit_pet(base, 1.0, 0)

    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    shadow = alpha_blur(frame)
    out.alpha_composite(shadow, (0, 7))
    out.alpha_composite(frame)
    return out


def pixel_canvas(scale: int = 2) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    image = Image.new("RGB", (SIZE // scale, SIZE // scale), PALETTE["bg"])
    return image, ImageDraw.Draw(image)


def px_rect(draw: ImageDraw.ImageDraw,
            xy: tuple[int, int, int, int],
            fill: tuple[int, int, int],
            outline: tuple[int, int, int] | None = None,
            width: int = 1) -> None:
    draw.rectangle(xy, fill=fill, outline=outline, width=width)


def draw_pixel_icon(draw: ImageDraw.ImageDraw, x: int, y: int, kind: str, active: bool) -> None:
    color = PALETTE["green"] if active else PALETTE["line_dim"]
    off = PALETTE["line_dim"]
    if kind == "LCD":
        px_rect(draw, (x, y + 2, x + 12, y + 10), None, color, 1)
        px_rect(draw, (x + 4, y + 12, x + 8, y + 13), color)
    elif kind == "Wi-Fi":
        draw.arc((x, y, x + 14, y + 14), 210, 330, fill=color, width=1)
        draw.arc((x + 3, y + 4, x + 11, y + 12), 210, 330, fill=color, width=1)
        px_rect(draw, (x + 6, y + 12, x + 8, y + 14), color)
    elif kind == "Brain":
        px_rect(draw, (x + 1, y + 3, x + 12, y + 12), color)
        for dx in (0, 13):
            px_rect(draw, (x + dx, y + 5, x + dx, y + 6), off)
            px_rect(draw, (x + dx, y + 9, x + dx, y + 10), off)
    elif kind == "Audio":
        px_rect(draw, (x, y + 6, x + 4, y + 10), color)
        draw.line((x + 5, y + 5, x + 8, y + 3, x + 8, y + 13, x + 5, y + 11), fill=color, width=1)
        if active:
            draw.arc((x + 8, y + 3, x + 16, y + 13), 300, 60, fill=color, width=1)


def draw_progress(draw: ImageDraw.ImageDraw, progress: float) -> None:
    x0, y0, blocks, gap = 18, 86, 17, 2
    px_rect(draw, (x0 - 3, y0 - 4, x0 + blocks * 4 + (blocks - 1) * gap + 2, y0 + 9), PALETTE["panel"], PALETTE["line_dim"])
    filled = int(round(blocks * progress))
    for i in range(blocks):
        color = PALETTE["green"] if i < filled else (24, 47, 54)
        if i == filled and filled < blocks:
            color = PALETTE["orange"]
        x = x0 + i * (4 + gap)
        px_rect(draw, (x, y0, x + 3, y0 + 5), color)


def center_text(draw: ImageDraw.ImageDraw,
                y: int,
                text: str,
                font: ImageFont.ImageFont,
                fill: tuple[int, int, int]) -> None:
    bbox = draw.textbbox((0, 0), text, font=font)
    x = (SIZE // 2 // 2) - (bbox[2] - bbox[0]) // 2
    draw.text((x, y), text, font=font, fill=fill)


def right_frame(index: int) -> Image.Image:
    low, draw = pixel_canvas(scale=2)
    frame = index / max(1, FRAME_COUNT - 1)

    # Pixel border and subtle scan blocks.
    px_rect(draw, (8, 8, 111, 111), PALETTE["panel"], PALETTE["line"])
    px_rect(draw, (12, 12, 107, 107), PALETTE["panel_2"], PALETTE["line_dim"])
    for y in range(18, 105, 12):
        draw.line((14, y, 106, y), fill=(13, 34, 45), width=1)
    for x in (18, 102):
        px_rect(draw, (x, 18, x + 3, 21), PALETTE["orange"])
        px_rect(draw, (x, 99, x + 3, 102), PALETTE["blue"])

    # Chinese product name is pre-rendered into PNG, not delegated to firmware fonts.
    center_text(draw, 28, "小鲅 X1", FONT_TITLE, PALETTE["cream"])
    draw.text((26, 53), "BOOT", font=FONT_SMALL, fill=PALETTE["orange"])
    draw.text((61, 53), "READY" if index >= 21 else "START", font=FONT_SMALL, fill=PALETTE["line"])

    draw_progress(draw, frame)

    icon_states = [
        ("LCD", index >= 3),
        ("Wi-Fi", index >= 8),
        ("Brain", index >= 14),
        ("Audio", index >= 18),
    ]
    for i, (label, active) in enumerate(icon_states):
        y = 100 + i * 0  # keep icons on one row for the round safe area.
        x = 23 + i * 22
        draw_pixel_icon(draw, x, 70, label, active or (index + i) % 6 < 2)
        draw.text((x - 1, 57), label[:3].upper(), font=FONT_TINY, fill=PALETTE["cream"] if active else PALETTE["line_dim"])

    if index >= 5:
        px_rect(draw, (38, 100, 82, 110), PALETTE["green"], PALETTE["cream"])
        center_text(draw, 99, "READY", FONT_SMALL, PALETTE["bg"])
    elif index >= 4:
        draw.text((39, 99), "Brain...", font=FONT_SMALL, fill=PALETTE["line"])
    elif index >= 3:
        draw.text((40, 99), "Wi-Fi...", font=FONT_SMALL, fill=PALETTE["line"])
    else:
        draw.text((44, 99), "LCD OK", font=FONT_SMALL, fill=PALETTE["line"])

    # Upscale with nearest-neighbor for real 8-bit blockiness.
    image = low.resize((SIZE, SIZE), Image.Resampling.NEAREST).convert("RGBA")
    vignette = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    vp = vignette.load()
    cx = cy = (SIZE - 1) / 2
    for y in range(SIZE):
        for x in range(SIZE):
            d = math.hypot(x - cx, y - cy)
            if d > 110:
                vp[x, y] = (0, 0, 0, int(min(190, (d - 110) * 20)))
    return Image.alpha_composite(image, vignette)


def write_manifest() -> None:
    manifest = {
        "protocol": "atlas.boot_intro.v0",
        "id": "xiaoba_x1_boot_intro",
        "product_name": "小鲅 X1",
        "internal_codename": "dualeye_pet_device",
        "version": "0.1.0",
        "canvas": {"width": SIZE, "height": SIZE},
        "fps": FPS,
        "duration_ms": int(FRAME_COUNT / FPS * 1000),
        "frame_count": FRAME_COUNT,
        "asset_policy": "embedded_spiffs_no_sdcard",
        "left": {
            "style": "pet_head_2_5d",
            "frames": "left/frame_%02d.png",
            "fallback": "fallback_left.png",
        },
        "right": {
            "style": "8bit_pixel_status",
            "frames": "right/frame_%02d.png",
            "fallback": "fallback_right.png",
        },
        "outcomes": {
            "ready": {"right_label": "READY", "next_page": "eyes"},
            "local_mode": {"right_label": "LOCAL", "next_page": "clock"},
            "pairing": {"right_label": "PAIR", "next_page": "provisioning"},
        },
    }
    (BOOT_ROOT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def make_contact_sheet(left_frames: list[Image.Image], right_frames: list[Image.Image]) -> None:
    cols = 6
    cell_w, cell_h = 176, 388
    rows = math.ceil(FRAME_COUNT / cols)
    sheet = Image.new("RGB", (cols * cell_w, rows * cell_h), (236, 235, 227))
    draw = ImageDraw.Draw(sheet)
    font = load_font(12)
    for i in range(FRAME_COUNT):
        x = (i % cols) * cell_w
        y = (i // cols) * cell_h
        bg_l = Image.new("RGBA", (SIZE, SIZE), (10, 14, 20, 255))
        bg_l.alpha_composite(left_frames[i])
        l = bg_l.resize((152, 152), Image.Resampling.LANCZOS)
        r = right_frames[i].resize((152, 152), Image.Resampling.NEAREST)
        sheet.paste(l.convert("RGB"), (x + 12, y + 22))
        sheet.paste(r.convert("RGB"), (x + 12, y + 198))
        draw.text((x + 12, y + 4), f"frame {i:02d}", fill=(40, 42, 38), font=font)
    PREVIEW_PATH.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(PREVIEW_PATH, optimize=True)


def make_gif(left_frames: list[Image.Image], right_frames: list[Image.Image]) -> None:
    frames: list[Image.Image] = []
    for left, right in zip(left_frames, right_frames):
        canvas = Image.new("RGBA", (SIZE * 2 + 24, SIZE), (12, 15, 20, 255))
        bg_l = Image.new("RGBA", (SIZE, SIZE), (10, 14, 20, 255))
        bg_l.alpha_composite(left)
        canvas.alpha_composite(bg_l, (0, 0))
        canvas.alpha_composite(right, (SIZE + 24, 0))
        frames.append(canvas.convert("P", palette=Image.Palette.ADAPTIVE, colors=128))
    frames[0].save(
        GIF_PATH,
        save_all=True,
        append_images=frames[1:],
        duration=int(1000 / FPS),
        loop=0,
        optimize=True,
    )


def zip_boot_intro() -> None:
    if ZIP_PATH.exists():
        ZIP_PATH.unlink()
    with zipfile.ZipFile(ZIP_PATH, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for file in sorted(BOOT_ROOT.rglob("*")):
            if file.is_file():
                zf.write(file, file.relative_to(BOOT_ROOT.parent.parent))


def main() -> None:
    clean_output()
    left_frames: list[Image.Image] = []
    right_frames: list[Image.Image] = []
    for i in range(FRAME_COUNT):
        left = pet_frame(i)
        right = right_frame(i)
        left_frames.append(left)
        right_frames.append(right)
        save_png(left, BOOT_ROOT / "left" / f"frame_{i:02d}.png")
        save_png(right, BOOT_ROOT / "right" / f"frame_{i:02d}.png")

    save_png(left_frames[-1], BOOT_ROOT / "fallback_left.png")
    save_png(right_frames[-1], BOOT_ROOT / "fallback_right.png")
    write_manifest()
    make_contact_sheet(left_frames, right_frames)
    make_gif(left_frames, right_frames)
    zip_boot_intro()

    total = sum(p.stat().st_size for p in BOOT_ROOT.rglob("*") if p.is_file())
    print(f"xiaoba boot intro generated: {BOOT_ROOT}")
    print(f"frames={FRAME_COUNT} fps={FPS} bytes={total}")
    print(f"preview={PREVIEW_PATH}")
    print(f"gif={GIF_PATH}")
    print(f"zip={ZIP_PATH}")


if __name__ == "__main__":
    main()
