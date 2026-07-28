"""Fetch the libraries the native editor builds against.

They live in vendor/, which is not committed: raylib is a few megabytes of
prebuilt binaries and the json header is a single very large file. Neither
belongs in a repository that is mostly artwork.

    python tools/get_libraries.py
"""

import io
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENDOR = ROOT / "vendor"

RAYLIB_URL = ("https://github.com/raysan5/raylib/releases/download/5.5/"
              "raylib-5.5_win64_mingw-w64.zip")
JSON_URL = ("https://raw.githubusercontent.com/nlohmann/json/v3.11.3/"
            "single_include/nlohmann/json.hpp")


def fetch_raylib() -> bool:
    """Unpack raylib's headers and static library into vendor/raylib."""
    target = VENDOR / "raylib"
    if (target / "libraylib.a").exists():
        print("raylib is already here")
        return True

    print("Downloading raylib 5.5...")
    with urllib.request.urlopen(RAYLIB_URL) as response:  # noqa: S310
        payload = response.read()

    target.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        for member in archive.namelist():
            name = Path(member).name
            if not name:
                continue
            if "/include/" in member or "/lib/" in member:
                with archive.open(member) as source, (target / name).open("wb") as out:
                    shutil.copyfileobj(source, out)

    print(f"  raylib -> {target.relative_to(ROOT)}")
    return True


def fetch_json() -> bool:
    """Grab the single-header json parser."""
    target = VENDOR / "json" / "json.hpp"
    if target.exists():
        print("json.hpp is already here")
        return True

    print("Downloading json.hpp...")
    target.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(JSON_URL) as response:  # noqa: S310
        target.write_bytes(response.read())

    print(f"  json.hpp -> {target.relative_to(ROOT)}")
    return True


def main() -> int:
    """Fetch everything the build needs."""
    fetch_raylib()
    fetch_json()

    print("\nNow build it:")
    print("  cd native && mingw32-make")
    print("\nYou will need a C++ compiler. w64devkit is a single portable")
    print("download that needs no installer:")
    print("  https://github.com/skeeto/w64devkit/releases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
