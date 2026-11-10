"""The HTTP layer. One small interface so tests can replace it."""

from __future__ import annotations

import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Protocol

USER_AGENT = "zfflash"


class NetworkError(RuntimeError):
    pass


@dataclass
class Response:
    status: int
    body: bytes = b""
    headers: dict[str, str] = field(default_factory=dict)

    def json(self):
        import json

        return json.loads(self.body.decode("utf-8"))


class Http(Protocol):
    def request(
        self,
        method: str,
        url: str,
        *,
        headers: dict[str, str] | None = None,
        data: bytes | None = None,
        timeout: float = 30.0,
    ) -> Response: ...

    def download(self, url: str, dest: Path, *, timeout: float = 60.0) -> None: ...


class UrllibHttp:
    """Plain urllib. HTTPS is verified against the system certificate store. Non-2xx is a Response, not an exception."""

    def request(self, method, url, *, headers=None, data=None, timeout=30.0):
        req = urllib.request.Request(
            url, data=data, method=method, headers={"User-Agent": USER_AGENT, **(headers or {})}
        )
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return Response(r.status, r.read(), {k.lower(): v for k, v in r.headers.items()})
        except urllib.error.HTTPError as e:
            return Response(e.code, e.read(), {k.lower(): v for k, v in e.headers.items()})
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            raise NetworkError(f"{method} {url}: {getattr(e, 'reason', e)}") from e

    def download(self, url, dest, *, timeout=60.0):
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
        tmp = dest.with_name(dest.name + ".part")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r, tmp.open("wb") as f:
                if r.status != 200:
                    raise NetworkError(f"GET {url}: HTTP {r.status}")
                while chunk := r.read(1 << 20):
                    f.write(chunk)
        except urllib.error.HTTPError as e:
            tmp.unlink(missing_ok=True)
            raise NetworkError(f"GET {url}: HTTP {e.code}") from e
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            tmp.unlink(missing_ok=True)
            raise NetworkError(f"GET {url}: {getattr(e, 'reason', e)}") from e
        tmp.replace(dest)
