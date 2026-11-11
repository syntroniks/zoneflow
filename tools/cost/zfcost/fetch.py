"""Refresh the JLCPCB parts snapshot from the JLCPCB parts search.

The endpoint is the one the JLCPCB parts search page calls. It is not a
documented API: it may change or stop answering. One request per second.

    uv run zfcost fetch --out data/jlcpcb-parts.csv

Writes one row per LCSC number in hardware/bom-preliminary.csv, plus the
extra numbers in EXTRA_CODES, with the date of the fetch in every row.
"""

from __future__ import annotations

import csv
import datetime as dt
import json
import time
import urllib.request
from pathlib import Path

URL = "https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList"
USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130 Safari/537.36"

# Parts that are not BOM lines but are priced: the replacement 3 A fuse named
# in the note of the F1/F4 line.
EXTRA_CODES = ["C99550"]

FIELDS = [
    "lcsc",
    "manufacturer",
    "mpn",
    "package",
    "library",
    "preferred",
    "stock",
    "least_patch_number",
    "loss_number",
    "min_purchase",
    "price_breaks",
    "date_checked",
    "source",
]


def _query(code: str) -> dict | None:
    body = json.dumps({"keyword": code, "currentPage": 1, "pageSize": 10}).encode()
    req = urllib.request.Request(URL, data=body, headers={"Content-Type": "application/json", "User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.load(resp)
    for item in data["data"]["componentPageInfo"]["list"]:
        if item["componentCode"] == code:
            return item
    return None


def row_from_item(item: dict, date: str) -> dict:
    breaks = ";".join(f"{p['startNumber']}:{p['productPrice']}" for p in item["componentPrices"])
    return {
        "lcsc": item["componentCode"],
        "manufacturer": item["componentBrandEn"],
        "mpn": item["componentModelEn"],
        "package": item["componentSpecificationEn"],
        "library": {"base": "basic", "expand": "extended"}.get(
            item["componentLibraryType"], item["componentLibraryType"]
        ),
        "preferred": "yes" if item["preferredComponentFlag"] else "no",
        "stock": item["stockCount"],
        "least_patch_number": item["leastPatchNumber"],
        "loss_number": item["lossNumber"],
        "min_purchase": item["minPurchaseNum"],
        "price_breaks": breaks,
        "date_checked": date,
        "source": "JLCPCB parts search, selectSmtComponentList",
    }


def fetch(bom_csv: Path, out_csv: Path) -> int:
    with bom_csv.open(newline="") as f:
        codes = [r["lcsc"] for r in csv.DictReader(f) if r["lcsc"]]
    codes += [c for c in EXTRA_CODES if c not in codes]
    date = dt.date.today().isoformat()
    rows = []
    for code in codes:
        item = _query(code)
        if item is None:
            print(f"not found: {code}")
        else:
            rows.append(row_from_item(item, date))
        time.sleep(1.0)
    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    return len(rows)
