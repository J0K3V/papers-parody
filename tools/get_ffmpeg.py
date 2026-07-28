"""Download ffmpeg into tools/.

The binary is too big to keep in the repository, so it is fetched on demand.
Only the desktop app needs it; the web editor records straight from the
browser and needs nothing installed.

    python tools/get_ffmpeg.py
"""

import io
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGET = ROOT / "tools" / "ffmpeg.exe"

RELEASE_URL = (
    "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/"
    "ffmpeg-master-latest-win64-gpl.zip"
)


def main() -> int:
    """Fetch ffmpeg.exe unless it is already there."""
    if TARGET.exists():
        size = TARGET.stat().st_size / (1024 * 1024)
        print(f"Already present: {TARGET} ({size:.0f} MB)")
        return 0

    if sys.platform != "win32":
        print("This helper only covers Windows. Install ffmpeg with your package manager,")
        print("or drop the binary at tools/ffmpeg.exe yourself.")
        return 1

    print("Downloading ffmpeg (about 40 MB compressed)...")
    with urllib.request.urlopen(RELEASE_URL) as response:  # noqa: S310
        payload = response.read()

    print("Extracting...")
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        member = next((n for n in archive.namelist() if n.endswith("bin/ffmpeg.exe")), None)
        if member is None:
            print("Could not find ffmpeg.exe inside the archive.")
            return 1

        TARGET.parent.mkdir(parents=True, exist_ok=True)
        TARGET.write_bytes(archive.read(member))

    print(f"Saved to {TARGET} ({TARGET.stat().st_size / (1024 * 1024):.0f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
