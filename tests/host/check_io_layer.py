#!/usr/bin/env python3
"""Compile-check the AnxSyncIo layer against the host stubs.

The IO layer (AnxCredentialStore, AnxWebDavClient) depends on Arduino /
ESP-IDF / HalStorage, so it cannot be run on a development machine. It *can*
still be compiled, which catches the majority of real mistakes: typos, missing
declarations, wrong types, namespace errors, uninitialised members, switch gaps.

This script does exactly that, then links and runs a small smoke driver for the
configuration plumbing. It does not pretend the code has been tested on a
device: a successful run here means "it parses, links, and its URL handling
behaves", not "it syncs".

Usage, from firmware/:
    python tests/host/check_io_layer.py
"""

from __future__ import annotations

import os
import pathlib
import subprocess
import sys

VCVARS_CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat",
    r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
]


def main() -> int:
    here = pathlib.Path(__file__).resolve().parent
    firmware = here.parent.parent
    io_dir = firmware / "lib" / "AnxSyncIo"
    logic_dir = firmware / "lib" / "AnxSync"
    stub_dir = here / "stubs"
    build_dir = here / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    vcvars = next((c for c in VCVARS_CANDIDATES if os.path.exists(c)), None)
    if vcvars is None:
        print("ERROR: vcvars64.bat not found (install VS Build Tools with C++).", file=sys.stderr)
        return 1

    io_sources = sorted(io_dir.glob("*.cpp"))
    logic_sources = sorted(logic_dir.glob("*.cpp"))
    stub_sources = sorted(stub_dir.glob("*.cpp"))
    if not io_sources:
        print("ERROR: no sources in lib/AnxSyncIo", file=sys.stderr)
        return 1

    # Compile and link in two steps.
    #
    # A single `cl /Fo:"<dir>\" a.cpp b.cpp` does not work: the trailing
    # backslash before the closing quote escapes the quote, so the shell swallows
    # the rest of the command line as part of the /Fo value. MSVC then reports
    # "D8003: missing source filename". Compiling with /c and linking afterwards
    # sidesteps it entirely, and also works from a clean tree.
    quoted = [f'"{p}"' for p in (io_sources + logic_sources + stub_sources)]

    bat = build_dir / "check_io_layer.bat"
    lines = [
        "@echo off",
        f"set TMP={build_dir}",
        f"set TEMP={build_dir}",
        f'call "{vcvars}" >nul 2>&1',
        f'cd /d "{build_dir}"',
        # /W4 for real warnings; /WX off so a warning does not masquerade as failure.
        f'cl /nologo /utf-8 /std:c++20 /EHsc /W4 /WX- /c '
        f'/I"{stub_dir}" /I"{logic_dir}" /I"{io_dir}" ' + " ".join(quoted),
        "echo COMPILE_EXIT=%ERRORLEVEL%",
        "link /nologo /OUT:anx_io_check.exe *.obj",
        "echo LINK_EXIT=%ERRORLEVEL%",
        'if not exist "anx_io_check.exe" ( echo NO_EXE & exit /b 1 )',
        "anx_io_check.exe",
        "echo RUN_EXIT=%ERRORLEVEL%",
    ]
    bat.write_text("\r\n".join(lines) + "\r\n", encoding="ascii")

    print(f"compiling {len(io_sources)} IO source(s) + {len(logic_sources)} logic source(s) "
          f"+ {len(stub_sources)} stub source(s)")
    completed = subprocess.run(["cmd", "/c", str(bat)], text=True, capture_output=True)
    output = (completed.stdout or "") + (completed.stderr or "")
    print(output)

    ok = (
        "COMPILE_EXIT=0" in output
        and "LINK_EXIT=0" in output
        and "error C" not in output
        and "RUN_EXIT=0" in output
    )
    if ok:
        print("RESULT: AnxSyncIo compiles, links and passes the config smoke test.")
        print("        This proves it parses, type-checks and links, and that URL")
        print("        normalisation behaves. It does NOT prove it syncs on hardware:")
        print("        the esp_http_client / SD stubs are deliberately non-functional.")
        return 0

    print("RESULT: IO layer check FAILED - see the errors above.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
