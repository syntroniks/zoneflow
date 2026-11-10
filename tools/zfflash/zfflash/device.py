"""The controller's REST API: login, backup, restore and update. Routes: docs/firmware/openapi.json."""

from __future__ import annotations

import json
from dataclasses import dataclass

from .http import Http, Response


class DeviceError(RuntimeError):
    pass


def error_text(r: Response) -> str:
    try:
        e = r.json()["error"]
        where = f" ({e['path']})" if e.get("path") else ""
        return f"{e.get('code', 'error')}: {e.get('message', '')}{where}"
    except (ValueError, KeyError, TypeError):
        return r.body.decode("utf-8", "replace")[:200] or "no body"


@dataclass
class Device:
    base: str  # e.g. http://zoneflow-ab12.local/api/v1 (no trailing slash)
    http: Http
    token: str | None = None

    @classmethod
    def at(cls, host_or_url: str, http: Http) -> Device:
        url = host_or_url if "://" in host_or_url else f"http://{host_or_url}"
        url = url.rstrip("/")
        if not url.endswith("/api/v1"):
            url += "/api/v1"
        return cls(url, http)

    def _headers(self, extra: dict[str, str] | None = None) -> dict[str, str]:
        h = dict(extra or {})
        if self.token:
            h["Authorization"] = f"Bearer {self.token}"  # scripts use the bearer token; no CSRF header (API reference)
        return h

    def login(self, password: str) -> None:
        r = self.http.request(
            "POST",
            f"{self.base}/auth/login",
            headers={"Content-Type": "application/json"},
            data=json.dumps({"password": password}).encode(),
        )
        if r.status == 429:
            raise DeviceError(f"too many failed logins; wait {r.headers.get('retry-after', 'a while')} s and try again")
        if r.status != 200:
            raise DeviceError(f"login failed (HTTP {r.status}): {error_text(r)}")
        self.token = r.json()["token"]

    def info(self) -> dict:
        r = self.http.request("GET", f"{self.base}/info", headers=self._headers())
        if r.status != 200:
            raise DeviceError(f"GET /info: HTTP {r.status}: {error_text(r)}")
        return r.json()

    def backup(self) -> bytes:
        r = self.http.request("GET", f"{self.base}/config/backup", headers=self._headers())
        if r.status == 401:
            raise NeedsLogin("the controller requires a login to read its configuration")
        if r.status != 200:
            raise DeviceError(f"GET /config/backup: HTTP {r.status}: {error_text(r)}")
        doc = r.json()
        if doc.get("format") != "zoneflow-backup":
            raise DeviceError("the answer is not a ZoneFlow backup")
        return r.body

    def restore(self, body: bytes) -> None:
        r = self.http.request(
            "POST",
            f"{self.base}/config/restore",
            headers=self._headers({"Content-Type": "application/json"}),
            data=body,
        )
        if r.status == 401:
            raise NeedsLogin("restoring needs the admin password")
        if r.status != 200:
            raise DeviceError(f"POST /config/restore: HTTP {r.status}: {error_text(r)}")
        if not r.json().get("restored"):
            raise DeviceError("the controller did not report the configuration as restored")

    def upload(self, kind: str, data: bytes, force: bool = False) -> None:
        """kind is 'app' (POST /firmware/upload) or 'www' (POST /firmware/assets)."""
        path = "/firmware/upload" if kind == "app" else "/firmware/assets"
        if force and kind == "app":
            path += "?force=true"
        r = self.http.request(
            "POST",
            self.base + path,
            headers=self._headers({"Content-Type": "application/octet-stream"}),
            data=data,
            timeout=300.0,
        )
        if r.status == 401:
            raise NeedsLogin("updating needs the admin password")
        if r.status == 409:
            raise DeviceError(f"refused: {error_text(r)} (use --force to update while watering)")
        if r.status != 202:
            raise DeviceError(f"POST {path}: HTTP {r.status}: {error_text(r)}")


class NeedsLogin(DeviceError):
    pass
