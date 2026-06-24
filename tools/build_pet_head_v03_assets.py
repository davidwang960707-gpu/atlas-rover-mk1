from __future__ import annotations

import json
import math
import shutil
import zipfile
from collections import deque
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter, ImageOps


ROOT = Path(__file__).resolve().parents[1]
PACK_ROOT = ROOT / "assets" / "dualeye_sdcard_v0_1"
PET_ROOT = PACK_ROOT / "sdcard" / "atlas_pet_head"
SOURCE_ROOT = PACK_ROOT / "source" / "pet_head"
SIZE = 240

KEYFRAME_STATES = [
    "idle",
    "listen",
    "speak",
    "sing",
    "happy",
    "laugh",
    "cry",
    "sleepy",
    "think",
    "surprised",
]

VIEW_STATES = ["idle", "listen", "think", "speak"]
VIEWS = ["yaw_l30", "yaw_l15", "yaw_c", "yaw_r15", "yaw_r30"]

ANIMATIONS = {
    "blink": {"frames": 6, "fps": 12, "loop": False, "fallback": "idle"},
    "speak": {"frames": 8, "fps": 10, "loop": True, "fallback": "speak"},
    "sing": {"frames": 10, "fps": 10, "loop": True, "fallback": "sing"},
    "laugh": {"frames": 8, "fps": 12, "loop": True, "fallback": "laugh"},
}

TURN_TRANSITIONS = [
    ("yaw_c", "yaw_l30"),
    ("yaw_l30", "yaw_c"),
    ("yaw_c", "yaw_r30"),
    ("yaw_r30", "yaw_c"),
]


def ensure_dirs() -> None:
    (SOURCE_ROOT / "generated").mkdir(parents=True, exist_ok=True)
    PET_ROOT.mkdir(parents=True, exist_ok=True)


def is_background_pixel(pixel: tuple[int, int, int, int], threshold: int = 58) -> bool:
    r, g, b, a = pixel
    if a < 8:
        return True
    # Remove the existing near-black LCD backdrop only when flood-filled from edges.
    return max(r, g, b) <= threshold and (max(r, g, b) - min(r, g, b)) <= 34


def remove_edge_background(image: Image.Image) -> Image.Image:
    """Remove only dark background connected to the image edge; keep dark facial details."""
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    w, h = rgba.size
    visited = bytearray(w * h)
    q: deque[tuple[int, int]] = deque()

    def push(x: int, y: int) -> None:
        if x < 0 or y < 0 or x >= w or y >= h:
            return
        idx = y * w + x
        if visited[idx]:
            return
        if not is_background_pixel(pixels[x, y]):
            return
        visited[idx] = 1
        q.append((x, y))

    for x in range(w):
        push(x, 0)
        push(x, h - 1)
    for y in range(h):
        push(0, y)
        push(w - 1, y)

    while q:
        x, y = q.popleft()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            push(nx, ny)

    alpha = Image.new("L", (w, h), 255)
    alpha_px = alpha.load()
    for y in range(h):
        for x in range(w):
            if visited[y * w + x]:
                alpha_px[x, y] = 0

    # Feather only the removed edge so the round LCD does not show a hard halo.
    soft = alpha.filter(ImageFilter.GaussianBlur(0.55))
    original_alpha = rgba.getchannel("A")
    rgba.putalpha(ImageChops.multiply(soft, original_alpha))
    return crop_to_canvas(rgba)


def crop_to_canvas(image: Image.Image) -> Image.Image:
    rgba = image.convert("RGBA")
    bbox = rgba.getbbox()
    if not bbox:
        return Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    crop = rgba.crop(bbox)
    # Keep the head large on the round screen but leave small breathing room.
    scale = min(224 / crop.width, 224 / crop.height, 1.35)
    new_size = (max(1, int(round(crop.width * scale))), max(1, int(round(crop.height * scale))))
    crop = crop.resize(new_size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    canvas.alpha_composite(crop, ((SIZE - crop.width) // 2, (SIZE - crop.height) // 2 + 2))
    return canvas


def add_soft_shadow(image: Image.Image, opacity: int = 30) -> Image.Image:
    rgba = image.convert("RGBA")
    alpha = rgba.getchannel("A")
    shadow = Image.new("RGBA", rgba.size, (0, 0, 0, 0))
    shadow_alpha = alpha.filter(ImageFilter.GaussianBlur(5))
    shadow_alpha = ImageEnhance.Brightness(shadow_alpha).enhance(opacity / 255)
    shadow.putalpha(shadow_alpha)
    out = Image.new("RGBA", rgba.size, (0, 0, 0, 0))
    out.alpha_composite(shadow, (0, 4))
    out.alpha_composite(rgba)
    return out


def quantize_png(image: Image.Image) -> Image.Image:
    # Palette+alpha keeps SPIFFS small; LVGL can still read PNG with alpha.
    return image.convert("RGBA").quantize(colors=128, method=Image.Quantize.FASTOCTREE)


def save_png(image: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    quantize_png(image).save(path, optimize=True, compress_level=9)


def clean_runtime_output() -> None:
    for name in ("keyframes", "views", "animations", "transitions"):
        path = PET_ROOT / name
        if path.exists():
            shutil.rmtree(path)


def load_existing_keyframe(state: str) -> Image.Image:
    src = PET_ROOT / "keyframes" / f"{state}.png"
    if not src.exists():
        raise FileNotFoundError(src)
    return remove_edge_background(Image.open(src))


def make_view_from_center(center: Image.Image, view: str, state: str) -> Image.Image:
    if view == "yaw_c":
        return center.copy()

    yaw = {
        "yaw_l30": -30,
        "yaw_l15": -15,
        "yaw_r15": 15,
        "yaw_r30": 30,
    }[view]
    amount = abs(yaw) / 30.0
    direction = -1 if yaw < 0 else 1
    rgba = center.convert("RGBA")
    bbox = rgba.getbbox()
    if not bbox:
        return rgba
    crop = rgba.crop(bbox)
    # Simulate yaw by narrowing, horizontal offset and a slight vertical bob.
    new_w = max(1, int(round(crop.width * (1.0 - 0.095 * amount))))
    new_h = max(1, int(round(crop.height * (1.0 + 0.012 * amount))))
    warped = crop.resize((new_w, new_h), Image.Resampling.BICUBIC)

    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    x = (SIZE - new_w) // 2 + int(round(direction * 8 * amount))
    y = (SIZE - new_h) // 2 + 2 + int(round(1 * amount))
    canvas.alpha_composite(warped, (x, y))

    # Perspective cue: one side warmer/brighter, the far side slightly darker.
    shade = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(shade)
    if direction < 0:
        draw.rectangle((0, 0, 86, SIZE), fill=(0, 0, 0, int(20 * amount)))
        draw.rectangle((SIZE - 68, 0, SIZE, SIZE), fill=(255, 220, 170, int(18 * amount)))
    else:
        draw.rectangle((SIZE - 86, 0, SIZE, SIZE), fill=(0, 0, 0, int(20 * amount)))
        draw.rectangle((0, 0, 68, SIZE), fill=(255, 220, 170, int(18 * amount)))
    shade.putalpha(ImageChops.multiply(shade.getchannel("A"), canvas.getchannel("A")))
    canvas = Image.alpha_composite(canvas, shade)

    # Expression-specific nudge: listen/think gets a slightly sillier tilt.
    tilt = 0
    if state == "listen":
        tilt = -2 if yaw < 0 else 2
    elif state == "think":
        tilt = 2 if yaw < 0 else -2
    if tilt:
        canvas = canvas.rotate(tilt, resample=Image.Resampling.BICUBIC, center=(SIZE // 2, SIZE // 2))
    return canvas


def interpolate_images(a: Image.Image, b: Image.Image, t: float) -> Image.Image:
    return Image.blend(a.convert("RGBA"), b.convert("RGBA"), t)


def crop_generated_angle_sheet(generated_path: Path) -> list[Image.Image]:
    """Optional source/reference crops for review; not the primary runtime style."""
    image = Image.open(generated_path).convert("RGBA")
    w, h = image.size
    cell_w = w / 5
    out: list[Image.Image] = []
    for i in range(5):
        crop = image.crop((int(round(i * cell_w)), 0, int(round((i + 1) * cell_w)), h))
        crop = remove_green_background(crop)
        out.append(crop_to_canvas(crop))
    return out


def remove_green_background(image: Image.Image) -> Image.Image:
    rgba = image.convert("RGBA")
    px = rgba.load()
    w, h = rgba.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if g > 150 and r < 80 and b < 90:
                px[x, y] = (r, g, b, 0)
    rgba.putalpha(rgba.getchannel("A").filter(ImageFilter.GaussianBlur(0.25)))
    return rgba


def write_manifest() -> None:
    manifest = {
        "schema": "atlas.pet_head.v0.3",
        "version": "0.3.0",
        "canvas": [SIZE, SIZE],
        "background": "transparent",
        "style": "smooth_2_5d_soft_vinyl_marmot_head",
        "asset_policy": "embedded_spiffs_no_sdcard",
        "states": KEYFRAME_STATES + ["offline"],
        "views": VIEWS,
        "view_states": VIEW_STATES,
        "animations": ANIMATIONS,
        "transitions": [
            {"name": f"turn_{a}_to_{b}", "from": a, "to": b, "frames": 6, "fps": 12}
            for a, b in TURN_TRANSITIONS
        ],
        "runtime": {
            "idle_micro_motion_fps": 8,
            "speak_fps": 10,
            "sing_fps": 10,
            "laugh_fps": 12,
            "max_ui_fps_budget": 12,
            "audio_priority": True,
        },
        "fallback_order": [
            "transitions/{transition}/frame_%02u.png",
            "animations/{animation}/{view}/frame_%02u.png",
            "animations/{animation}/frame_%02u.png",
            "views/{state}/{view}.png",
            "keyframes/{state}.png",
            "lvgl_builtin_pet_head",
        ],
    }
    (PET_ROOT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def create_contact_sheet(images: list[tuple[str, Image.Image]], path: Path, cols: int = 5) -> None:
    rows = math.ceil(len(images) / cols)
    cell_w, cell_h = 260, 286
    sheet = Image.new("RGB", (cols * cell_w, rows * cell_h), (242, 240, 234))
    draw = ImageDraw.Draw(sheet)
    for idx, (label, image) in enumerate(images):
        x = (idx % cols) * cell_w
        y = (idx // cols) * cell_h
        bg = Image.new("RGBA", (SIZE, SIZE), (18, 24, 22, 255))
        bg.alpha_composite(image.convert("RGBA"))
        sheet.paste(bg.convert("RGB"), (x + 10, y + 8))
        draw.text((x + 12, y + 252), label, fill=(45, 45, 40))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path, optimize=True)


def zip_pet_head() -> None:
    zip_path = PACK_ROOT / "atlas_pet_head_sdcard_v0_3.zip"
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for file in sorted(PET_ROOT.rglob("*")):
            if file.is_file():
                zf.write(file, file.relative_to(PET_ROOT.parent))


def main() -> None:
    ensure_dirs()
    generated_source = Path(
        "/Users/macbook/.codex/generated_images/019eca07-b3ba-7e02-9007-08380a1769d3/"
        "ig_034a1a0a3502d5ad016a3b2900585481918ec4739ffd30f0c4.png"
    )
    if generated_source.exists():
        shutil.copy2(generated_source, SOURCE_ROOT / "atlas_marmot_pet_head_2_5d_yaw_sheet_v0_3_chromakey.png")
        generated_views = crop_generated_angle_sheet(generated_source)
        create_contact_sheet(
            list(zip([f"ai_ref/{v}" for v in VIEWS], generated_views)),
            SOURCE_ROOT / "atlas_marmot_pet_head_2_5d_yaw_sheet_v0_3_preview.png",
            cols=5,
        )

    # Load current frames before cleaning; the current repository already carries
    # V0.2 runtime assets, and this builder transforms them into the V0.3 pack.
    keyframes: dict[str, Image.Image] = {state: load_existing_keyframe(state) for state in KEYFRAME_STATES}
    animation_frames: dict[str, list[Image.Image]] = {}
    for animation, meta in ANIMATIONS.items():
        frames: list[Image.Image] = []
        old_dir = PET_ROOT / "animations" / animation
        for i in range(int(meta["frames"])):
            old_src = old_dir / f"frame_{i:02d}.png"
            if old_src.exists():
                frames.append(remove_edge_background(Image.open(old_src)))
            else:
                frames.append(keyframes[str(meta["fallback"])])
        animation_frames[animation] = frames

    clean_runtime_output()

    for state in KEYFRAME_STATES:
        frame = keyframes[state]
        if state == "surprised":
            # Keep surprised a touch brighter for visibility on the physical LCD.
            rgb = ImageEnhance.Brightness(frame.convert("RGBA")).enhance(1.04)
            frame = rgb
        keyframes[state] = frame
        save_png(add_soft_shadow(frame, 18), PET_ROOT / "keyframes" / f"{state}.png")

    # Offline is a calm visual fallback, not an alarming error page.
    offline = ImageEnhance.Color(keyframes["think"]).enhance(0.72)
    offline = ImageEnhance.Brightness(offline).enhance(0.86)
    save_png(add_soft_shadow(offline, 18), PET_ROOT / "keyframes" / "offline.png")

    view_frames: dict[tuple[str, str], Image.Image] = {}
    for state in VIEW_STATES:
        for view in VIEWS:
            frame = make_view_from_center(keyframes[state], view, state)
            view_frames[(state, view)] = frame
            save_png(add_soft_shadow(frame, 18), PET_ROOT / "views" / state / f"{view}.png")

    # Backward-compatible animation frames stay at the old path.
    for animation, meta in ANIMATIONS.items():
        old_dir = PET_ROOT / "animations" / animation
        for i, frame in enumerate(animation_frames[animation]):
            save_png(add_soft_shadow(frame, 18), old_dir / f"frame_{i:02d}.png")

    for start, end in TURN_TRANSITIONS:
        name = f"turn_{start}_to_{end}"
        start_frame = view_frames[("idle", start)]
        end_frame = view_frames[("idle", end)]
        for i in range(6):
            t = i / 5
            frame = interpolate_images(start_frame, end_frame, t)
            save_png(add_soft_shadow(frame, 18), PET_ROOT / "transitions" / name / f"frame_{i:02d}.png")

    write_manifest()

    contact_images: list[tuple[str, Image.Image]] = []
    for state in KEYFRAME_STATES + ["offline"]:
        contact_images.append((f"key/{state}", Image.open(PET_ROOT / "keyframes" / f"{state}.png").convert("RGBA")))
    for state in VIEW_STATES:
        for view in VIEWS:
            contact_images.append((f"{state}/{view}", Image.open(PET_ROOT / "views" / state / f"{view}.png").convert("RGBA")))
    create_contact_sheet(contact_images, SOURCE_ROOT / "atlas_marmot_pet_head_2_5d_contact_sheet_v0_3.png", cols=5)
    zip_pet_head()

    total = sum(p.stat().st_size for p in PET_ROOT.rglob("*") if p.is_file())
    print(f"pet_head v0.3 generated: {PET_ROOT}")
    print(f"files={sum(1 for p in PET_ROOT.rglob('*') if p.is_file())} bytes={total}")
    print(f"zip={PACK_ROOT / 'atlas_pet_head_sdcard_v0_3.zip'}")


if __name__ == "__main__":
    main()
