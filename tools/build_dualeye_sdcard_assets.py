from __future__ import annotations

import json
import math
import shutil
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parents[1]
PACK_ROOT = ROOT / "assets" / "dualeye_sdcard_v0_1"
OUT_ROOTS = [
    PACK_ROOT / "sdcard" / "atlas_eyes",
    PACK_ROOT / "atlas_eyes",
]
CONTACT_V2 = PACK_ROOT / "atlas_eyes_contact_sheet_v0_2.png"
CONTACT_V3 = PACK_ROOT / "atlas_eyes_contact_sheet_v0_3.png"
CONTACT_V4 = PACK_ROOT / "atlas_eyes_contact_sheet_v0_4.png"
CONTACT_LEGACY = PACK_ROOT / "atlas_eyes_contact_sheet_v0_1.png"
ZIP_V2 = PACK_ROOT / "atlas_eyes_sdcard_v0_2.zip"
ZIP_V3 = PACK_ROOT / "atlas_eyes_sdcard_v0_3.zip"
ZIP_V4 = PACK_ROOT / "atlas_eyes_sdcard_v0_4.zip"
ZIP_LEGACY = PACK_ROOT / "atlas_eyes_sdcard_v0_1.zip"
MASTER_SOURCE = PACK_ROOT / "source" / "atlas_dualeye_theme_master_v0_1.png"
PET_PREVIEW_ASSETS = {
    "idle": ROOT / "tools" / "preview_assets" / "epet_awake.png",
    "blink": ROOT / "tools" / "preview_assets" / "epet_sleepy.png",
    "listen": ROOT / "tools" / "preview_assets" / "epet_talk.png",
}
UPLOADED_THEME_SOURCES = {
    "blue_pupil": PACK_ROOT / "source" / "uploads" / "blue_pupil_source.png",
    "no_smoking": PACK_ROOT / "source" / "uploads" / "no_smoking_source.png",
    "tomoe_spin": PACK_ROOT / "source" / "uploads" / "tomoe_spin_source.png",
}
ROTATING_THEMES = {"tomoe_spin"}

SCALE = 3
SIZE = 240
CANVAS = SIZE * SCALE

THEMES = [
    ("raptor", "猛禽眼"),
    ("mecha", "机械电子眼"),
    ("goggle", "黄色护目镜眼"),
    ("pet", "电子宠物巡游"),
    ("blue_pupil", "蓝色瞳孔"),
    ("no_smoking", "禁烟禁电子烟"),
    ("tomoe_spin", "红色旋纹"),
]

STATES = [
    ("idle", "待机"),
    ("blink", "眨眼"),
    ("listen", "聆听"),
]

EYES = [("L", "left"), ("R", "right")]

MASTER_THEME_INDEX = {
    "raptor": 0,
    "mecha": 1,
    "goggle": 2,
}
MASTER_STATE_INDEX = {
    "idle": 0,
    "blink": 1,
    "listen": 2,
}
MASTER_COLUMNS = [
    (10, 359),
    (372, 720),
    (732, 1077),
    (1090, 1437),
]
MASTER_ROWS = [
    (122, 360),
    (416, 656),
    (715, 954),
]
MASTER_SINGLE_EYE_CROP = 176
GOGGLE_VERTICAL_CIRCLE_SCALE = 0.86


def s(value: float) -> int:
    return int(round(value * SCALE))


def box(values: tuple[float, float, float, float]) -> tuple[int, int, int, int]:
    return tuple(s(v) for v in values)


def points(values: list[tuple[float, float]]) -> list[tuple[int, int]]:
    return [(s(x), s(y)) for x, y in values]


def mix(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    t = max(0.0, min(1.0, t))
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def new_canvas(base: tuple[int, int, int], glow: tuple[int, int, int]) -> tuple[Image.Image, ImageDraw.ImageDraw]:
    image = Image.new("RGB", (CANVAS, CANVAS), base)
    draw = ImageDraw.Draw(image)
    cx = cy = SIZE / 2
    for r in range(132, 8, -4):
        color = mix(base, glow, (132 - r) / 132 * 0.36)
        draw.ellipse(box((cx - r, cy - r, cx + r, cy + r)), fill=color)
    for r, opacity in [(111, 0.22), (92, 0.18), (72, 0.14)]:
        color = mix(base, glow, opacity)
        draw.ellipse(box((cx - r, cy - r, cx + r, cy + r)), outline=color, width=s(2))
    return image, draw


def downsample(image: Image.Image) -> Image.Image:
    return image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def add_round_screen_vignette(image: Image.Image) -> Image.Image:
    """Darken only the last few edge pixels so assets can fill the round LCD."""
    rgba = image.convert("RGBA")
    overlay = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    pixels = overlay.load()
    cx = cy = (SIZE - 1) / 2
    inner = 113.0
    outer = 120.0
    for y in range(SIZE):
        for x in range(SIZE):
            d = math.hypot(x - cx, y - cy)
            if d <= inner:
                continue
            alpha = int(min(150, max(0, (d - inner) / (outer - inner) * 145)))
            pixels[x, y] = (0, 0, 0, alpha)
    return Image.alpha_composite(rgba, overlay).convert("RGB")


def scale_center_crop(image: Image.Image, factor: float) -> Image.Image:
    """Scale around the center and crop back to the LCD tile size."""
    if factor <= 1.0:
        return image
    new_size = int(round(SIZE * factor))
    resized = image.resize((new_size, new_size), Image.Resampling.LANCZOS)
    left = (new_size - SIZE) // 2
    top = (new_size - SIZE) // 2
    return resized.crop((left, top, left + SIZE, top + SIZE))


def circularize_goggle_eye(image: Image.Image) -> Image.Image:
    """Correct the master goggle crop from a tall oval into a round-screen badge."""
    target_h = int(round(SIZE * GOGGLE_VERTICAL_CIRCLE_SCALE))
    resized = image.resize((SIZE, target_h), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
    canvas.paste(resized, (0, (SIZE - target_h) // 2))
    return canvas


def crop_master_eye(theme: str, state: str, eye_name: str) -> Image.Image | None:
    """Use the approved high-detail design sheet, cropped to one eye per LCD."""
    if theme not in MASTER_THEME_INDEX or state not in MASTER_STATE_INDEX or not MASTER_SOURCE.exists():
        return None

    source = Image.open(MASTER_SOURCE).convert("RGB")
    x1, x2 = MASTER_COLUMNS[MASTER_THEME_INDEX[theme]]
    y1, y2 = MASTER_ROWS[MASTER_STATE_INDEX[state]]
    width = x2 - x1
    center_x = x1 + width * (0.255 if eye_name == "left" else 0.745)
    center_y = (y1 + y2) / 2
    half = MASTER_SINGLE_EYE_CROP / 2
    crop = source.crop((
        int(round(center_x - half)),
        int(round(center_y - half)),
        int(round(center_x + half)),
        int(round(center_y + half)),
    ))
    tile = crop.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    if theme == "goggle":
        tile = circularize_goggle_eye(tile)
        tile = scale_center_crop(tile, 1.14)
    return add_round_screen_vignette(tile)


def arc(draw: ImageDraw.ImageDraw,
        xy: tuple[float, float, float, float],
        start: int,
        end: int,
        fill: tuple[int, int, int],
        width: float = 2) -> None:
    draw.arc(box(xy), start=start, end=end, fill=fill, width=s(width))


def line(draw: ImageDraw.ImageDraw,
         xy: list[tuple[float, float]],
         fill: tuple[int, int, int],
         width: float = 2) -> None:
    draw.line(points(xy), fill=fill, width=s(width), joint="curve")


def rect(draw: ImageDraw.ImageDraw,
         xy: tuple[float, float, float, float],
         fill: tuple[int, int, int],
         outline: tuple[int, int, int] | None = None,
         width: float = 1,
         radius: float = 0) -> None:
    if radius:
        draw.rounded_rectangle(box(xy), radius=s(radius), fill=fill, outline=outline, width=s(width))
    else:
        draw.rectangle(box(xy), fill=fill, outline=outline, width=s(width))


def ellipse(draw: ImageDraw.ImageDraw,
            xy: tuple[float, float, float, float],
            fill: tuple[int, int, int] | None,
            outline: tuple[int, int, int] | None = None,
            width: float = 1) -> None:
    draw.ellipse(box(xy), fill=fill, outline=outline, width=s(width))


def polygon(draw: ImageDraw.ImageDraw,
            xy: list[tuple[float, float]],
            fill: tuple[int, int, int],
            outline: tuple[int, int, int] | None = None) -> None:
    draw.polygon(points(xy), fill=fill, outline=outline)


def draw_outer_status(draw: ImageDraw.ImageDraw, eye_name: str, active: bool) -> None:
    x = 21 if eye_name == "left" else 207
    colors = [(62, 235, 143), (255, 213, 76), (255, 94, 76)]
    for i, color in enumerate(colors):
        h = 14 + i * 8 if active else 11 + i * 4
        rect(draw,
             (x + i * 7, 117 - h / 2, x + 4 + i * 7, 117 + h / 2),
             color,
             radius=1.5)


def listen_waves(draw: ImageDraw.ImageDraw, eye_name: str, color: tuple[int, int, int]) -> None:
    if eye_name == "left":
        x1, x2, start, end = 18, 68, -45, 45
    else:
        x1, x2, start, end = 172, 222, 135, 225
    for offset in (0, 14, 28):
        arc(draw, (x1 - offset, 80 - offset, x2 + offset, 160 + offset), start, end, color, width=3)


def gaze_for(eye_name: str, state: str, amount: int = 8) -> int:
    if state == "blink":
        return 0
    inward = amount if eye_name == "left" else -amount
    return inward + (4 if state == "listen" and eye_name == "left" else 0) - (4 if state == "listen" and eye_name == "right" else 0)


def draw_raptor(state: str, eye_name: str) -> Image.Image:
    image, draw = new_canvas((6, 9, 11), (58, 78, 32))
    glow = (246, 179, 44)
    x = gaze_for(eye_name, state, 7)

    polygon(draw, [(34, 78), (119 + x, 45), (205, 80), (184, 114), (118 + x, 97), (56, 116)], (34, 42, 23))
    polygon(draw, [(45, 160), (118 + x, 190), (194, 161), (172, 137), (119 + x, 151), (66, 137)], (20, 31, 22))
    ellipse(draw, (38, 44, 202, 196), None, (92, 121, 50), width=3)

    if state == "blink":
        polygon(draw, [(42, 90), (119, 74), (198, 92), (182, 123), (119, 114), (56, 123)], (21, 29, 20))
        polygon(draw, [(48, 144), (119, 160), (191, 144), (175, 119), (119, 128), (63, 119)], (12, 19, 14))
        line(draw, [(61, 121), (92, 115), (119, 118), (148, 115), (181, 121)], glow, width=5)
        arc(draw, (67, 101, 173, 153), 196, 344, (255, 224, 118), width=2)
        draw_outer_status(draw, eye_name, False)
        return downsample(image)

    for r, color in [(70, (103, 76, 19)), (58, (186, 123, 26)), (46, (255, 194, 51)), (31, (104, 167, 67))]:
        ellipse(draw, (120 + x - r, 120 - r, 120 + x + r, 120 + r), color)
    for i in range(16):
        angle = i * math.tau / 16
        p1 = (120 + x + math.cos(angle) * 25, 120 + math.sin(angle) * 25)
        p2 = (120 + x + math.cos(angle) * 60, 120 + math.sin(angle) * 60)
        line(draw, [p1, p2], mix((50, 35, 14), glow, 0.45), width=1.2)
    ellipse(draw, (120 + x - 10, 64, 120 + x + 10, 176), (4, 5, 3))
    ellipse(draw, (120 + x - 5, 74, 120 + x + 5, 166), (0, 0, 0))
    ellipse(draw, (103 + x, 72, 119 + x, 88), (255, 236, 159))
    ellipse(draw, (132 + x, 144, 141 + x, 153), (255, 219, 117))

    if state == "listen":
        listen_waves(draw, eye_name, (81, 232, 225))
        arc(draw, (42, 42, 198, 198), 22, 142, (81, 232, 225), width=4)
    draw_outer_status(draw, eye_name, state == "listen")
    return downsample(image)


def draw_mecha(state: str, eye_name: str) -> Image.Image:
    image, draw = new_canvas((3, 6, 12), (12, 82, 105))
    x = gaze_for(eye_name, state, 9)
    cyan = (56, 226, 255)
    blue = (38, 111, 220)

    for g in range(24, 225, 24):
        line(draw, [(g, 18), (g + 16, 222)], (8, 23, 38), width=0.8)
        line(draw, [(18, g), (222, g - 12)], (8, 23, 38), width=0.8)
    for r, color, width in [(91, blue, 3), (72, (33, 184, 211), 2), (51, (115, 245, 255), 2)]:
        ellipse(draw, (120 - r, 120 - r, 120 + r, 120 + r), None, color, width=width)

    if state == "blink":
        for y, h in [(82, 17), (110, 15), (137, 17)]:
            rect(draw, (53, y, 187, y + h), (14, 20, 27), (64, 192, 222), width=2, radius=4)
        line(draw, [(64, 121), (176, 121)], cyan, width=5)
        arc(draw, (46, 46, 194, 194), 205, 335, cyan, width=3)
        draw_outer_status(draw, eye_name, False)
        return downsample(image)

    ellipse(draw, (68, 68, 172, 172), (7, 24, 34), (105, 247, 255), width=4)
    ellipse(draw, (86 + x, 86, 154 + x, 154), (41, 160, 206), (184, 255, 255), width=2)
    ellipse(draw, (102 + x, 102, 138 + x, 138), (1, 5, 9), (177, 255, 255), width=2)
    rect(draw, (115 + x, 63, 125 + x, 83), cyan, radius=2)
    rect(draw, (115 + x, 157, 125 + x, 177), cyan, radius=2)
    rect(draw, (63, 115, 83, 125), cyan, radius=2)
    rect(draw, (157, 115, 177, 125), cyan, radius=2)

    if state == "listen":
        arc(draw, (37, 37, 203, 203), 285, 55, (255, 207, 75), width=4)
        listen_waves(draw, eye_name, (255, 207, 75))
        for angle in (35, 145, 250):
            px = 120 + math.cos(math.radians(angle)) * 84
            py = 120 + math.sin(math.radians(angle)) * 84
            ellipse(draw, (px - 4, py - 4, px + 4, py + 4), (255, 207, 75))
    draw_outer_status(draw, eye_name, state == "listen")
    return downsample(image)


def draw_goggle(state: str, eye_name: str) -> Image.Image:
    image, draw = new_canvas((10, 10, 13), (84, 65, 24))
    x = gaze_for(eye_name, state, 10)
    yellow = (246, 191, 42)
    rim = (255, 218, 87)
    cyan = (132, 236, 255)

    ellipse(draw, (2, 2, 238, 238), (52, 43, 26), (255, 220, 73), width=9)
    ellipse(draw, (14, 14, 226, 226), yellow, (255, 240, 133), width=6)
    ellipse(draw, (31, 31, 209, 209), (35, 39, 45), (191, 209, 210), width=8)
    ellipse(draw, (45, 45, 195, 195), (12, 15, 19), (255, 236, 134), width=3)

    for px, py, active in (
        (34, 78, eye_name == "left"),
        (206, 78, eye_name == "right"),
        (34, 162, state == "listen"),
        (206, 162, state == "listen"),
    ):
        color = cyan if active else (101, 128, 122)
        ellipse(draw, (px - 4, py - 4, px + 4, py + 4), color)

    if state == "blink":
        ellipse(draw, (64, 64, 176, 176), (13, 15, 18), (113, 234, 255), width=4)
        rect(draw, (70, 108, 170, 132), (29, 35, 39), (125, 238, 255), width=3, radius=12)
        arc(draw, (72, 82, 168, 153), 185, 355, (245, 245, 229), width=3)
        return downsample(image)

    ellipse(draw, (62, 62, 178, 178), (226, 252, 244), (136, 234, 255), width=4)
    ellipse(draw, (81 + x, 81, 159 + x, 159), (108, 208, 231))
    ellipse(draw, (101 + x, 101, 139 + x, 139), (7, 9, 11))
    ellipse(draw, (93 + x, 90, 110 + x, 107), (255, 255, 247))
    ellipse(draw, (127 + x, 136, 136 + x, 145), (245, 255, 252))

    if state == "listen":
        arc(draw, (24, 24, 216, 216), 35, 125, cyan, width=4)
        arc(draw, (38, 38, 202, 202), 210, 315, (255, 224, 105), width=4)
    return downsample(image)


def pixel(draw: ImageDraw.ImageDraw,
          x: int,
          y: int,
          w: int,
          h: int,
          color: tuple[int, int, int]) -> None:
    rect(draw, (x, y, x + w, y + h), color)


def compose_pet_preview(state: str, eye_name: str) -> Image.Image | None:
    sprite_path = PET_PREVIEW_ASSETS.get(state)
    if sprite_path is None or not sprite_path.exists():
        return None

    image, draw = new_canvas((7, 10, 14), (46, 83, 73))
    sprite = Image.open(sprite_path).convert("RGBA")
    target = 188 if state != "blink" else 178

    plate = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    plate_draw = ImageDraw.Draw(plate)
    accent = (126, 224, 167) if state != "listen" else (137, 218, 255)
    warm = (255, 190, 116)
    for offset, alpha in [(0, 80), (10, 46), (20, 28)]:
        plate_draw.ellipse(
            box((24 + offset, 25 + offset, 216 - offset, 217 - offset)),
            outline=accent + (alpha,),
            width=s(2),
        )
    plate_draw.rounded_rectangle(
        box((47, 182, 193, 194)),
        radius=s(5),
        fill=(22, 38, 37, 210),
        outline=accent + (115,),
        width=s(2),
    )
    for i, color in enumerate([(111, 235, 180), warm, (255, 125, 111)]):
        x = 24 if eye_name == "left" else 205
        y = 99 + i * 17
        plate_draw.rounded_rectangle(
            box((x, y, x + 7, y + (18 if state == "listen" else 13))),
            radius=s(2),
            fill=color + (230,),
        )
    if state == "listen":
        listen_waves(ImageDraw.Draw(image), eye_name, (137, 218, 255))

    rgba = Image.alpha_composite(image.convert("RGBA"), plate)
    final = downsample(rgba.convert("RGB")).convert("RGBA")
    sprite = sprite.resize((target, target), Image.Resampling.NEAREST)
    x = (SIZE - target) // 2 + (-3 if eye_name == "left" else 3)
    y = 25 if state != "blink" else 34
    final.alpha_composite(sprite, (x, y))
    return add_round_screen_vignette(final).convert("RGB")


def draw_pet(state: str, eye_name: str) -> Image.Image:
    preview = compose_pet_preview(state, eye_name)
    if preview is not None:
        return preview

    image, draw = new_canvas((5, 7, 12), (28, 84, 94))
    x = gaze_for(eye_name, state, 7)
    gold = (239, 190, 70)
    gold_hi = (255, 224, 108)
    gold_dark = (112, 82, 31)
    cyan = (111, 236, 255)
    cyan_dark = (24, 88, 111)
    mint = (78, 235, 166)
    coral = (255, 94, 84)

    for y in range(42, 202, 18):
        shade = mix((8, 14, 22), (24, 60, 72), (y - 42) / 160)
        rect(draw, (34, y, 206, y + 7), shade, radius=2)
    for px, py, w, h, color in [
        (58, 54, 124, 18, gold_dark),
        (44, 72, 152, 20, gold),
        (36, 92, 168, 18, gold_hi),
        (48, 110, 144, 22, gold),
        (40, 132, 160, 20, gold),
        (58, 152, 124, 18, gold_dark),
        (72, 170, 96, 12, gold_dark),
        (62, 42, 26, 16, gold_hi),
        (152, 42, 26, 16, gold_hi),
    ]:
        pixel(draw, px, py, w, h, color)

    rect(draw, (66, 72, 174, 166), (12, 21, 28), gold_hi, width=4, radius=10)
    rect(draw, (76, 82, 164, 156), (8, 18, 24), (39, 105, 119), width=2, radius=8)
    for px, py, color in [(50, 105, mint), (184, 105, coral), (50, 126, (255, 205, 82)), (184, 126, mint)]:
        pixel(draw, px, py, 9, 9, color)

    if state == "blink":
        rect(draw, (84, 110, 156, 124), cyan, (229, 255, 255), width=2, radius=7)
        rect(draw, (96, 127, 144, 134), cyan_dark, radius=3)
        pixel(draw, 78, 90, 13, 13, mint)
        pixel(draw, 150, 90, 13, 13, mint)
        arc(draw, (69, 83, 171, 160), 202, 338, gold_hi, width=2)
        draw_outer_status(draw, eye_name, False)
        return add_round_screen_vignette(downsample(image))

    rect(draw, (82 + x, 84, 158 + x, 158), cyan, (232, 255, 255), width=3, radius=10)
    rect(draw, (97 + x, 99, 143 + x, 145), (14, 30, 38), radius=7)
    rect(draw, (105 + x, 106, 120 + x, 121), (242, 255, 255), radius=3)
    rect(draw, (126 + x, 132, 136 + x, 142), (181, 250, 255), radius=2)

    if state == "listen":
        pixel(draw, 108, 35, 24, 10, mint)
        line(draw, [(120, 45), (120, 62)], mint, width=3)
        listen_waves(draw, eye_name, (255, 214, 95))
        pixel(draw, 44 if eye_name == "right" else 187, 98, 10, 10, coral)
        pixel(draw, 56 if eye_name == "right" else 175, 111, 10, 10, coral)
        arc(draw, (70, 70, 170, 170), 28, 152, (255, 214, 95), width=3)
    draw_outer_status(draw, eye_name, state == "listen")
    return add_round_screen_vignette(downsample(image))


def uploaded_pixel_is_object(pixel: tuple[int, int, int]) -> bool:
    r, g, b = pixel
    avg = (r + g + b) / 3
    sat = max(r, g, b) - min(r, g, b)
    return avg < 205 or sat > 28


def crop_uploaded_source(theme: str) -> Image.Image:
    source_path = UPLOADED_THEME_SOURCES[theme]
    if not source_path.exists():
        raise FileNotFoundError(source_path)

    image = Image.open(source_path).convert("RGB")
    pixels = image.load()
    width, height = image.size
    min_x, min_y = width, height
    max_x = max_y = -1
    for y in range(height):
        for x in range(width):
            if uploaded_pixel_is_object(pixels[x, y]):
                min_x = min(min_x, x)
                min_y = min(min_y, y)
                max_x = max(max_x, x)
                max_y = max(max_y, y)
    if max_x < min_x or max_y < min_y:
        return image

    box_w = max_x - min_x + 1
    box_h = max_y - min_y + 1
    pad = int(round(max(box_w, box_h) * 0.04))
    min_x = max(0, min_x - pad)
    min_y = max(0, min_y - pad)
    max_x = min(width - 1, max_x + pad)
    max_y = min(height - 1, max_y + pad)

    center_x = (min_x + max_x) / 2
    center_y = (min_y + max_y) / 2
    side = int(round(max(max_x - min_x + 1, max_y - min_y + 1)))
    left = int(round(center_x - side / 2))
    top = int(round(center_y - side / 2))
    square = Image.new("RGBA", (side, side), (246, 246, 246, 255))
    crop = image.crop((max(0, left), max(0, top), min(width, left + side), min(height, top + side)))
    square.alpha_composite(crop.convert("RGBA"), (max(0, -left), max(0, -top)))
    return square.convert("RGB")


def clean_uploaded_source(theme: str) -> Image.Image:
    crop = crop_uploaded_source(theme)
    rgba = Image.new("RGBA", crop.size, (0, 0, 0, 0))
    rgba_pixels = rgba.load()
    pixels = crop.load()
    width, height = crop.size

    if theme == "blue_pupil":
        mask = Image.new("L", crop.size, 0)
        mask_pixels = mask.load()
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                if uploaded_pixel_is_object((r, g, b)):
                    rgba_pixels[x, y] = (r, g, b, 255)
                    mask_pixels[x, y] = 255
        mask = mask.filter(ImageFilter.GaussianBlur(1.1))
        rgba.putalpha(mask)
        return rgba

    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            avg = (r + g + b) / 3
            sat = max(r, g, b) - min(r, g, b)
            if theme == "tomoe_spin":
                if r > 120 and g < 125 and b < 125:
                    rgba_pixels[x, y] = (255, 8, 4, 255)
                elif avg < 105:
                    rgba_pixels[x, y] = (3, 3, 3, 255)
                else:
                    rgba_pixels[x, y] = (0, 0, 0, 0)
            else:
                if r > 135 and g < 130 and b < 130:
                    rgba_pixels[x, y] = (255, 15, 12, 255)
                elif avg < 110:
                    rgba_pixels[x, y] = (0, 0, 0, 255)
                elif avg > 170 and sat < 28:
                    rgba_pixels[x, y] = (255, 255, 255, 255)
                else:
                    rgba_pixels[x, y] = (r, g, b, 255)

    circle = Image.new("L", crop.size, 0)
    circle_draw = ImageDraw.Draw(circle)
    inset_ratio = 0.006
    if theme == "tomoe_spin":
        inset_ratio = 0.004
    inset = int(round(min(width, height) * inset_ratio))
    circle_draw.ellipse((inset, inset, width - inset - 1, height - inset - 1), fill=255)
    circle = circle.filter(ImageFilter.GaussianBlur(0.9))
    alpha = rgba.getchannel("A")
    rgba.putalpha(Image.composite(alpha, Image.new("L", crop.size, 0), circle))
    return rgba


def uploaded_background(theme: str) -> Image.Image:
    if theme == "blue_pupil":
        base, glow = (2, 5, 9), (26, 120, 150)
    elif theme == "no_smoking":
        base, glow = (12, 12, 12), (86, 0, 0)
    else:
        base, glow = (8, 5, 5), (112, 0, 0)
    image, _ = new_canvas(base, glow)
    return downsample(image).convert("RGBA")


def center_sprite_on_screen(theme: str, sprite: Image.Image) -> Image.Image:
    target = 232
    if theme == "blue_pupil":
        target = 238
    elif theme == "no_smoking":
        target = 240
    elif theme == "tomoe_spin":
        target = 252

    placed = sprite.resize((target, target), Image.Resampling.LANCZOS)
    final = uploaded_background(theme)
    if target > SIZE:
        left = (target - SIZE) // 2
        top = (target - SIZE) // 2
        final.alpha_composite(placed.crop((left, top, left + SIZE, top + SIZE)), (0, 0))
    else:
        final.alpha_composite(placed, ((SIZE - target) // 2, (SIZE - target) // 2))
    return add_round_screen_vignette(final).convert("RGB")


def add_pupil_blink(image: Image.Image) -> Image.Image:
    rgba = image.convert("RGBA")
    overlay = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    lid = (1, 5, 9, 248)
    draw.ellipse((-22, -82, 262, 128), fill=lid)
    draw.ellipse((-22, 112, 262, 322), fill=lid)
    draw.rounded_rectangle((48, 114, 192, 127), radius=7, fill=(37, 214, 244, 210))
    draw.line((62, 120, 178, 120), fill=(192, 250, 255, 235), width=2)
    return Image.alpha_composite(rgba, overlay).convert("RGB")


def add_pupil_listen(image: Image.Image, eye_name: str) -> Image.Image:
    rgba = image.convert("RGBA")
    overlay = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    color = (94, 236, 255, 190)
    if eye_name == "left":
        arcs = [(-18, 64, 72, 176), (-35, 46, 90, 194)]
        start, end = -45, 45
    else:
        arcs = [(168, 64, 258, 176), (150, 46, 275, 194)]
        start, end = 135, 225
    for arc_box in arcs:
        draw.arc(arc_box, start=start, end=end, fill=color, width=4)
    draw.ellipse((31, 31, 209, 209), outline=(94, 236, 255, 80), width=2)
    return Image.alpha_composite(rgba, overlay).convert("RGB")


def draw_uploaded_theme(theme: str, state: str, eye_name: str) -> Image.Image:
    sprite = clean_uploaded_source(theme)
    base = center_sprite_on_screen(theme, sprite)
    if theme == "blue_pupil":
        if state == "blink":
            return add_pupil_blink(base)
        if state == "listen":
            return add_pupil_listen(base, eye_name)
    return base


DRAWERS = {
    "raptor": draw_raptor,
    "mecha": draw_mecha,
    "goggle": draw_goggle,
    "pet": draw_pet,
    "blue_pupil": draw_uploaded_theme,
    "no_smoking": draw_uploaded_theme,
    "tomoe_spin": draw_uploaded_theme,
}


def draw_theme_eye(theme: str, state: str, eye_name: str) -> Image.Image:
    if theme in UPLOADED_THEME_SOURCES:
        return draw_uploaded_theme(theme, state, eye_name)
    crop = crop_master_eye(theme, state, eye_name)
    if crop is not None:
        return crop
    return DRAWERS[theme](state, eye_name)


def make_contact_sheet(entries: list[dict[str, object]]) -> None:
    tile_w, tile_h = 240, 296
    cols = len(THEMES) * 2
    rows = len(STATES)
    sheet = Image.new("RGB", (cols * tile_w, rows * tile_h + 72), (242, 241, 236))
    draw = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("/System/Library/Fonts/PingFang.ttc", 22)
        small = ImageFont.truetype("/System/Library/Fonts/PingFang.ttc", 16)
    except OSError:
        font = ImageFont.load_default()
        small = ImageFont.load_default()

    draw.text((24, 18), "Atlas Rover DualEye Asset Pack V0.4 - pet IP and Waveshare 90 degree mapping", fill=(26, 28, 31), font=font)
    primary_root = OUT_ROOTS[0].parent
    for entry in entries:
        theme_i = next(i for i, (key, _) in enumerate(THEMES) if key == entry["theme"])
        state_i = next(i for i, (key, _) in enumerate(STATES) if key == entry["state"])
        eye_i = 0 if entry["eye"] == "L" else 1
        col = theme_i * 2 + eye_i
        x = col * tile_w
        y = state_i * tile_h + 72
        img = Image.open(primary_root / str(entry["path"])).convert("RGB")
        sheet.paste(img, (x, y))
        label = f"{entry['theme']} {entry['state']} {entry['eye']}"
        draw.text((x + 16, y + 248), label, fill=(32, 32, 32), font=small)

    CONTACT_V4.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(CONTACT_V4)


def write_zip(zip_path: Path) -> None:
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for file in sorted(OUT_ROOTS[0].rglob("*")):
            if file.is_file():
                zf.write(file, file.relative_to(OUT_ROOTS[0].parent))


def main() -> None:
    entries: list[dict[str, object]] = []
    for root in OUT_ROOTS:
        if root.exists():
            shutil.rmtree(root)
        root.mkdir(parents=True, exist_ok=True)

    for state_key, state_zh in STATES:
        for theme_key, theme_zh in THEMES:
            for eye_short, eye_name in EYES:
                tile = draw_theme_eye(theme_key, state_key, eye_name)
                for root in OUT_ROOTS:
                    out_dir = root / theme_key / state_key
                    out_dir.mkdir(parents=True, exist_ok=True)
                    tile.save(out_dir / f"{eye_name}.png", optimize=True)
                entries.append({
                    "theme": theme_key,
                    "theme_zh": theme_zh,
                    "state": state_key,
                    "state_zh": state_zh,
                    "eye": eye_short,
                    "eye_name": eye_name,
                    "path": f"atlas_eyes/{theme_key}/{state_key}/{eye_name}.png",
                    "width": 240,
                    "height": 240,
                    "format": "PNG",
                    "notes": "single physical LCD eye; firmware rotates clockwise"
                    if theme_key in ROTATING_THEMES
                    else "single physical LCD eye; firmware adds blink and subtle breathing motion",
                })

    manifest = {
        "version": "0.4",
        "target": "ESP32-S3-DualEye-Touch-LCD-1.28",
        "storage": "SPIFFS bundled from sdcard directory; SD card compatible",
        "root": "/atlas_eyes",
        "size_px": 240,
        "orientation": "Waveshare ESP-IDF 90 degree panel mapping verified on Atlas DualEye",
        "themes": [{"id": key, "name_zh": zh} for key, zh in THEMES],
        "states": [{"id": key, "name_zh": zh} for key, zh in STATES],
        "assets": entries,
    }
    for root in OUT_ROOTS:
        (root / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    make_contact_sheet(entries)
    shutil.copyfile(CONTACT_V4, CONTACT_V3)
    shutil.copyfile(CONTACT_V3, CONTACT_V2)
    shutil.copyfile(CONTACT_V3, CONTACT_LEGACY)
    write_zip(ZIP_V4)
    write_zip(ZIP_V3)
    write_zip(ZIP_V2)
    write_zip(ZIP_LEGACY)
    print(f"wrote {len(entries)} single-eye PNG assets")
    print(f"primary SPIFFS source: {OUT_ROOTS[0]}")
    print(f"contact sheet: {CONTACT_V4}")


if __name__ == "__main__":
    main()
