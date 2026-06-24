#!/usr/bin/env python3
"""Small platform abstractions for Atlas Brain.

This file intentionally stays dependency-free so the Mac bridge can run on a
fresh macOS Python without installing a web framework.  It gives the bridge the
same kind of vocabulary that xiaozhi-style servers use: devices, providers,
protocols, apps, and platform snapshots.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass
class AtlasDevice:
    device_id: str
    name: str
    model: str
    base_url: str
    kind: str = "dualeye"
    online: bool = False
    capabilities: dict[str, Any] = field(default_factory=dict)
    status: dict[str, Any] = field(default_factory=dict)
    app_path: str = ""
    admin_path: str = "/admin"
    tags: list[str] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.device_id,
            "name": self.name,
            "kind": self.kind,
            "model": self.model,
            "base_url": self.base_url,
            "online": self.online,
            "capabilities": self.capabilities,
            "status": self.status,
            "app_path": self.app_path or f"/devices/{self.device_id}/app",
            "admin_path": self.admin_path,
            "tags": self.tags,
        }


@dataclass
class ProviderDescriptor:
    name: str
    kind: str
    configured: bool
    status: str = "unknown"
    model: str = ""
    endpoint: str = ""
    notes: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "kind": self.kind,
            "configured": self.configured,
            "status": self.status,
            "model": self.model,
            "endpoint": self.endpoint,
            "notes": self.notes,
        }


@dataclass
class BrainProtocolDescriptor:
    name: str
    transport: str
    endpoint: str
    direction: str
    stage: str
    enabled: bool
    audio_streaming: bool = False
    notes: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "transport": self.transport,
            "endpoint": self.endpoint,
            "direction": self.direction,
            "stage": self.stage,
            "enabled": self.enabled,
            "audio_streaming": self.audio_streaming,
            "notes": self.notes,
        }


@dataclass
class AppDescriptor:
    app_id: str
    device_id: str
    name: str
    route: str
    status: str = "enabled"
    features: list[str] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "id": self.app_id,
            "device_id": self.device_id,
            "name": self.name,
            "route": self.route,
            "status": self.status,
            "features": self.features,
        }


class DeviceRegistry:
    def __init__(self) -> None:
        self._devices: dict[str, AtlasDevice] = {}

    def upsert(self, device: AtlasDevice) -> None:
        self._devices[device.device_id] = device

    def get(self, device_id: str) -> AtlasDevice | None:
        return self._devices.get(device_id)

    def list(self) -> list[dict[str, Any]]:
        return [device.to_dict() for device in self._devices.values()]


class ProviderRegistry:
    def __init__(self) -> None:
        self._providers: dict[str, ProviderDescriptor] = {}

    def upsert(self, provider: ProviderDescriptor) -> None:
        self._providers[provider.name] = provider

    def list(self) -> list[dict[str, Any]]:
        return [provider.to_dict() for provider in self._providers.values()]


class ProtocolRegistry:
    def __init__(self) -> None:
        self._protocols: dict[str, BrainProtocolDescriptor] = {}

    def upsert(self, protocol: BrainProtocolDescriptor) -> None:
        self._protocols[protocol.name] = protocol

    def list(self) -> list[dict[str, Any]]:
        return [protocol.to_dict() for protocol in self._protocols.values()]


class AppRegistry:
    def __init__(self) -> None:
        self._apps: dict[str, AppDescriptor] = {}

    def upsert(self, app: AppDescriptor) -> None:
        self._apps[app.app_id] = app

    def list(self) -> list[dict[str, Any]]:
        return [app.to_dict() for app in self._apps.values()]


class PlatformBackend:
    def __init__(self) -> None:
        self.devices = DeviceRegistry()
        self.providers = ProviderRegistry()
        self.protocols = ProtocolRegistry()
        self.apps = AppRegistry()

    def snapshot(self) -> dict[str, Any]:
        devices = self.devices.list()
        providers = self.providers.list()
        protocols = self.protocols.list()
        apps = self.apps.list()
        return {
            "summary": {
                "device_count": len(devices),
                "online_devices": sum(1 for device in devices if device.get("online")),
                "provider_count": len(providers),
                "configured_providers": sum(1 for provider in providers if provider.get("configured")),
                "protocol_count": len(protocols),
                "enabled_protocols": sum(1 for protocol in protocols if protocol.get("enabled")),
                "app_count": len(apps),
            },
            "devices": devices,
            "providers": providers,
            "protocols": protocols,
            "apps": apps,
        }
