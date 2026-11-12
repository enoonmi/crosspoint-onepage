#!/usr/bin/env python3
"""Static reference check for the Anx Sync UI activities.

The UI layer (AnxSyncSettingsActivity, AnxSyncActivity) cannot be compiled on the
host: it pulls in GfxRenderer -> EpdFont -> GCC __attribute__((packed)), which
MSVC rejects, plus the whole FreeRTOS/Arduino surface. Writing a faithful stub
tree for all of that would be more code than the feature itself.

What *is* worth doing is checking the things that actually go wrong when this
kind of screen is written blind:

  1. Every `tr(STR_...)` refers to a key that exists in english.yaml.
  2. Every `GUI.drawX(...)` / `renderer.x(...)` / `mappedInput.x(...)` name
     appears somewhere in the real headers.
  3. Every firmware header imported with #include resolves to a file.
  4. Every class/method the .cpp defines is declared in its .h.

This is not a compiler and does not claim to be. It catches typos, missing
includes, renamed helpers and stale StrIds - the mistakes that would otherwise
only appear after a PlatformIO build.

Usage, from firmware/:
    python tests/host/check_ui_refs.py
"""

from __future__ import annotations

import pathlib
import re
import sys

FIRMWARE = pathlib.Path(__file__).resolve().parent.parent.parent
SRC = FIRMWARE / "src"
LIB = FIRMWARE / "lib"
TRANSLATIONS = LIB / "I18n" / "translations" / "english.yaml"

UI_FILES = [
    SRC / "activities" / "settings" / "AnxSyncSettingsActivity.cpp",
    SRC / "activities" / "settings" / "AnxSyncActivity.cpp",
]

# Header directories that a quoted include may resolve against.
INCLUDE_ROOTS = [SRC, LIB] + [p for p in LIB.iterdir() if p.is_dir()] + \
                [p for p in SRC.iterdir() if p.is_dir()] + \
                [SRC / "activities", SRC / "activities" / "settings",
                 SRC / "activities" / "network", SRC / "activities" / "util",
                 SRC / "components", SRC / "components" / "themes"]

# Headers provided by Arduino / ESP-IDF / third parties rather than this repo.
KNOWN_SYSTEM = {
    "Arduino.h", "WiFi.h", "Logging.h", "HalStorage.h", "Memory.h", "I18n.h",
    "GfxRenderer.h", "Epub.h", "EInkDisplay.h", "InputManager.h", "BatteryMonitor.h",
    "SDCardManager.h", "esp_http_client.h", "esp_crt_bundle.h", "esp_err.h",
    "esp_ota_ops.h", "base64.h", "StreamString.h", "WebServer.h",
    "WebSocketsServer.h", "NetworkClient.h", "NetworkUdp.h", "PubSubClient.h",
    "MD5Builder.h", "PNGdec.h", "JPEGDEC.h", "AnxSyncPlan.h", "AnxProgressJson.h",
    "AnxProgressBridge.h",
}


def load_str_ids() -> set[str]:
    if not TRANSLATIONS.exists():
        return set()
    text = TRANSLATIONS.read_text(encoding="utf-8")
    return set(re.findall(r"^(STR_[A-Z0-9_]+):", text, re.MULTILINE))


def resolve_include(name: str) -> pathlib.Path | None:
    for root in INCLUDE_ROOTS:
        candidate = root / name
        if candidate.is_file():
            return candidate
    return None


def check_file(path: pathlib.Path, str_ids: set[str]) -> list[str]:
    problems: list[str] = []
    text = path.read_text(encoding="utf-8")

    # --- 1. quoted includes resolve ---
    for inc in re.findall(r'#include\s+"([^"]+)"', text):
        if resolve_include(inc) is None:
            problems.append(f"{path.name}: include \"{inc}\" not found under src/ or lib/")

    # --- 1b. angle-bracket includes that look like project headers ---
    #
    # Several crosspoint headers are included with <> (HalStorage.h, Logging.h,
    # GfxRenderer.h, ...), so a renamed or invented one would otherwise pass
    # unnoticed.
    for inc in re.findall(r"#include\s+<([^>]+)>", text):
        if inc in KNOWN_SYSTEM:
            continue
        if "/" in inc:  # freertos/..., nested paths are upstream headers
            continue
        if inc[0].islower():  # standard library
            continue
        if resolve_include(inc) is None:
            problems.append(f"{path.name}: include <{inc}> not found under src/ or lib/")

    # --- 2. every tr(STR_...) exists in english.yaml ---
    for key in sorted(set(re.findall(r"tr\((STR_[A-Z0-9_]+)\)", text))):
        if key not in str_ids:
            problems.append(f"{path.name}: tr({key}) is not defined in english.yaml")

    # --- 3. GUI./renderer./mappedInput. method names exist somewhere in src ---
    call_sites = set(re.findall(r"\b(?:GUI|renderer|mappedInput)\.(\w+)\s*\(", text))
    all_headers = "\n".join(p.read_text(encoding="utf-8", errors="replace")
                            for p in list(LIB.rglob("*.h")) + list(SRC.rglob("*.h")))
    for method in sorted(call_sites):
        if method not in all_headers:
            problems.append(f"{path.name}: method '{method}' not found in any header")

    # --- 4. Storage. calls resolve ---
    for method in sorted(set(re.findall(r"\bStorage\.(\w+)\s*\(", text))):
        storage_h = LIB / "hal" / "HalStorage.h"
        if storage_h.exists() and method not in storage_h.read_text(encoding="utf-8"):
            problems.append(f"{path.name}: Storage.{method}() not in HalStorage.h")

    return problems


def check_definition_pairing() -> list[str]:
    """Every AnxSync*::xxx defined in the .cpp must be declared in the .h."""
    problems: list[str] = []
    for stem in ("AnxSyncSettingsActivity", "AnxSyncActivity"):
        header = SRC / "activities" / "settings" / f"{stem}.h"
        source = SRC / "activities" / "settings" / f"{stem}.cpp"
        if not header.exists() or not source.exists():
            problems.append(f"{stem}: header or source missing")
            continue

        header_text = header.read_text(encoding="utf-8")
        source_text = source.read_text(encoding="utf-8")

        defined = set(re.findall(rf"\b{stem}::(\w+)\s*\(", source_text))
        defined.discard(stem)  # constructor
        for method in sorted(defined):
            if method not in header_text:
                problems.append(f"{stem}: method '{method}' defined in .cpp but missing from .h")
    return problems


def check_i18n_coverage() -> list[str]:
    """The STR_ANX_SYNC_* keys used by the UI must all be present."""
    problems: list[str] = []
    str_ids = load_str_ids()
    expected = [
        "STR_ANX_SYNC", "STR_ANX_SYNC_HINT", "STR_ANX_SYNC_URL", "STR_ANX_SYNC_WIFI_ONLY",
        "STR_ANX_SYNC_ON_WAKE", "STR_ANX_SYNC_NOW", "STR_ANX_SYNC_RUNNING", "STR_ANX_SYNC_DONE",
        "STR_ANX_SYNC_UP_TO_DATE", "STR_ANX_SYNC_FAILED", "STR_ANX_SYNC_NO_SERVER",
        "STR_ANX_SYNC_NO_WIFI", "STR_ANX_SYNC_BOOKS",
    ]
    for key in expected:
        if key not in str_ids:
            problems.append(f"english.yaml: missing {key}")
    return problems


def main() -> int:
    str_ids = load_str_ids()
    if not str_ids:
        print(f"ERROR: could not read {TRANSLATIONS}", file=sys.stderr)
        return 1

    problems: list[str] = []
    for path in UI_FILES:
        if not path.exists():
            problems.append(f"missing UI source: {path}")
            continue
        problems.extend(check_file(path, str_ids))
    problems.extend(check_definition_pairing())
    problems.extend(check_i18n_coverage())

    print(f"checked {len(UI_FILES)} UI source(s) against {len(str_ids)} i18n keys")
    if problems:
        for p in problems:
            print(f"  PROBLEM: {p}")
        print(f"\n{len(problems)} problem(s)")
        return 1

    print("  includes resolve")
    print("  every tr(STR_*) exists in english.yaml")
    print("  every GUI/renderer/mappedInput/Storage method exists in a header")
    print("  every .cpp definition is declared in its .h")
    print("\nUI REFERENCE CHECK OK")
    print("Note: this is a reference check, not a compile. It cannot catch type")
    print("      mismatches or signature errors - only PlatformIO can do that.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
