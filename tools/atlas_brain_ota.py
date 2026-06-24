"""OTA package manifest and file routes for Atlas Brain."""

from __future__ import annotations

import hashlib
import os
from http import HTTPStatus
from typing import Any


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))


def _resolve_dualeye_build_dir() -> tuple[str, str]:
    env_path = os.getenv("ATLAS_FIRMWARE_BUILD_DIR", "").strip()
    if env_path:
        return os.path.abspath(os.path.expanduser(env_path)), "ATLAS_FIRMWARE_BUILD_DIR"

    repo_local = os.path.join(REPO_ROOT, "firmware", "dualeye", "build")
    if os.path.exists(repo_local):
        return repo_local, "repo_local"

    sibling = os.path.join(os.path.dirname(REPO_ROOT), "Atlas-One-Firmware", "firmware", "dualeye", "build")
    if os.path.exists(sibling):
        return os.path.abspath(sibling), "sibling_worktree"

    return repo_local, "repo_local_missing"


DUALEYE_BUILD_DIR, DUALEYE_BUILD_DIR_SOURCE = _resolve_dualeye_build_dir()
OTA_PACKAGE_FILES = [
    ("bootloader", "bootloader/bootloader.bin", "0x0"),
    ("partition_table", "partition_table/partition-table.bin", "0x8000"),
    ("ota_data_initial", "ota_data_initial.bin", "0xd000"),
    ("sr_model", "srmodels/srmodels.bin", "0x10000"),
    ("app_ota", "atlas_rover_dualeye.bin", "0x100000"),
    ("spiffs_storage", "storage.bin", "0xB00000"),
]


def _file_sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as fp:
        for chunk in iter(lambda: fp.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_ota_manifest() -> dict[str, Any]:
    packages: list[dict[str, Any]] = []
    missing: list[str] = []
    for name, rel_path, offset in OTA_PACKAGE_FILES:
        abs_path = os.path.join(DUALEYE_BUILD_DIR, rel_path)
        rel_repo = os.path.relpath(abs_path, REPO_ROOT)
        if os.path.exists(abs_path):
            stat = os.stat(abs_path)
            packages.append({
                "name": name,
                "path": rel_repo,
                "flash_offset": offset,
                "size": stat.st_size,
                "sha256": _file_sha256(abs_path),
                "mtime": int(stat.st_mtime),
            })
        else:
            missing.append(rel_repo)
    ready = len(missing) == 0
    flash_args = [
        "python3", "-m", "esptool",
        "--chip", "esp32s3",
        "-b", "460800",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_size", "16MB",
        "--flash_freq", "80m",
    ]
    for package in packages:
        flash_args.extend([str(package["flash_offset"]), str(package["path"])])
    return {
        "ok": True,
        "protocol": "atlas.ota.manifest.v0",
        "device_model": "waveshare-dualeye-s3-1.28",
        "project": "atlas-rover-mk1",
        "channel": "dev",
        "version": "0.14.7-acceptance",
        "status": "package_ready" if ready else "missing_build_artifacts",
        "ota_supported": True,
        "app_ota_supported": True,
        "full_image_ota_supported": False,
        "apply_endpoint": "/api/ota/apply",
        "package_management": True,
        "transport": "http_app_ota_plus_usb_full_flash",
        "partition_layout": "dual_ota_app_plus_model_plus_storage",
        "build_dir": os.path.relpath(DUALEYE_BUILD_DIR, REPO_ROOT),
        "build_dir_source": DUALEYE_BUILD_DIR_SOURCE,
        "packages": packages,
        "missing": missing,
        "flash_args": flash_args if ready else [],
        "notes": "P5 supports app OTA via DualEye /api/ota/apply. Bootloader, partition table, model and SPIFFS storage still require USB full flash.",
    }


def _with_package_urls(manifest: dict[str, Any], package_base_url: str) -> dict[str, Any]:
    manifest = dict(manifest)
    manifest["packages"] = [dict(package) for package in manifest.get("packages", []) if isinstance(package, dict)]
    manifest["package_base_url"] = package_base_url
    for package in manifest["packages"]:
        package["url"] = f"{package_base_url}/{package.get('name', '')}"
    return manifest


def build_ota_manifest_response(package_base_url: str) -> dict[str, Any]:
    return _with_package_urls(build_ota_manifest(), package_base_url)


def build_ota_packages_response(package_base_url: str) -> dict[str, Any]:
    manifest = _with_package_urls(build_ota_manifest(), package_base_url)
    return {
        "ok": True,
        "protocol": manifest["protocol"],
        "status": manifest["status"],
        "packages": manifest["packages"],
        "missing": manifest["missing"],
        "flash_args": manifest["flash_args"],
        "package_base_url": package_base_url,
        "build_dir": manifest["build_dir"],
        "build_dir_source": manifest["build_dir_source"],
    }


def send_ota_package(handler: Any, package_name: str) -> None:
    package_map = {pkg_name: rel_path for pkg_name, rel_path, _offset in OTA_PACKAGE_FILES}
    rel_path = package_map.get(package_name)
    if rel_path is None:
        handler.send_json({"ok": False, "error": "unknown package"}, HTTPStatus.NOT_FOUND)
        return

    build_dir = os.path.abspath(DUALEYE_BUILD_DIR)
    abs_path = os.path.abspath(os.path.join(build_dir, rel_path))
    if not abs_path.startswith(build_dir + os.sep) or not os.path.exists(abs_path):
        handler.send_json({"ok": False, "error": "package not built"}, HTTPStatus.NOT_FOUND)
        return

    try:
        stat = os.stat(abs_path)
        handler.send_response(HTTPStatus.OK)
        handler.send_header("Content-Type", "application/octet-stream")
        handler.send_header("Cache-Control", "no-store")
        handler.send_header("Content-Disposition", f'attachment; filename="{os.path.basename(abs_path)}"')
        handler.send_header("Content-Length", str(stat.st_size))
        handler.end_headers()
        with open(abs_path, "rb") as fp:
            for chunk in iter(lambda: fp.read(1024 * 256), b""):
                handler.wfile.write(chunk)
    except (BrokenPipeError, ConnectionResetError):
        return
    except Exception as exc:
        handler.send_json({"ok": False, "error": str(exc)}, HTTPStatus.INTERNAL_SERVER_ERROR)
