"""Prepare the asset manifest and web-friendly audio.

The browser cannot list a directory, so the web app reads assets/manifest.json
instead. Large wav files are also transcoded to mp3, because shipping a 22 MB
wav over GitHub Pages would make the page take forever to load.

Run it again whenever you add files to assets/.
"""

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
WEB_AUDIO = ASSETS / "sounds" / "web"

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp"}
SOUND_SUFFIXES = {".wav", ".mp3", ".ogg"}
TRANSCODE_OVER_BYTES = 2 * 1024 * 1024

def sound_packs() -> dict[str, dict[str, list[str]]]:
    """Which file plays in each role, per soundtrack.

    Kept in step with sound_packs() in native/src/mixer.cpp by hand. It is a
    dozen filenames that change about once a year, which is cheaper than having
    the build parse C++ to find them.
    """
    clicks = [f"TextReveal{n}.wav" for n in range(4)]
    page_turn = ["ButtonUp.wav"]

    return {
        "bad ending": {"music": ["BadEnding.mp3"], "next": page_turn, "letter": clicks},
        "good ending": {"music": ["GoodEnding.mp3"], "next": page_turn, "letter": clicks},
        "main theme": {"music": ["MainTheme.mp3"], "next": page_turn, "letter": clicks},
    }


def ffmpeg() -> str:
    """Locate ffmpeg, preferring the copy kept in tools/."""
    local = ROOT / "tools" / "ffmpeg.exe"
    return str(local) if local.exists() else "ffmpeg"


def web_audio_url(source: Path) -> str:
    """Return the URL the browser should load, transcoding big files to mp3."""
    # Already compressed, or small enough to serve as it is.
    if source.suffix.lower() != ".wav" or source.stat().st_size <= TRANSCODE_OVER_BYTES:
        return source.relative_to(ROOT).as_posix()

    WEB_AUDIO.mkdir(parents=True, exist_ok=True)
    target = WEB_AUDIO / f"{source.stem}.mp3"

    if not target.exists() or target.stat().st_mtime < source.stat().st_mtime:
        print(f"  transcoding {source.name} -> {target.name}")
        subprocess.run(  # noqa: S603
            [ffmpeg(), "-hide_banner", "-loglevel", "error", "-y",
             "-i", str(source), "-c:a", "libmp3lame", "-b:a", "128k", str(target)],
            check=True,
        )

    return target.relative_to(ROOT).as_posix()


def collect_images(folder: Path) -> list[dict[str, str]]:
    """List the pictures in a folder as manifest entries."""
    if not folder.exists():
        return []
    return [
        {"id": item.relative_to(ROOT).as_posix(), "name": item.stem, "url": item.relative_to(ROOT).as_posix()}
        for item in sorted(folder.iterdir())
        if item.is_file() and item.suffix.lower() in IMAGE_SUFFIXES
    ]


def collect_sounds(folder: Path) -> list[dict[str, str]]:
    """List the sounds in a folder, pointing at web friendly files."""
    if not folder.exists():
        return []
    entries = []
    for item in sorted(folder.iterdir()):
        if not item.is_file() or item.suffix.lower() not in SOUND_SUFFIXES:
            continue

        url = web_audio_url(item)
        entries.append({
            "id": item.relative_to(ROOT).as_posix(),
            "name": item.stem,
            "url": url,
            # The editor uses this to tell a two second stamp from a score.
            "bytes": (ROOT / url).stat().st_size,
        })
    return entries


def main() -> int:
    """Write assets/manifest.json."""
    print("Building the asset manifest...")

    manifest = {
        "images": {
            "default": collect_images(ASSETS / "images" / "default"),
            "custom": [],
            "sprites": collect_images(ASSETS / "images" / "sprites"),
        },
        "sounds": {
            "default": collect_sounds(ASSETS / "sounds" / "default"),
            "custom": [],
        },
        "packs": sound_packs(),
        "font": "assets/fonts/pixelplay.ttf",
    }

    target = ASSETS / "manifest.json"
    target.write_text(json.dumps(manifest, indent=2), encoding="utf-8")

    # Personal artwork is listed separately and never committed, so publishing
    # the site cannot leak it. The editor merges this file when it is there.
    local = {
        "images": {"custom": collect_images(ASSETS / "images" / "custom")},
        "sounds": {"custom": collect_sounds(ASSETS / "sounds" / "custom")},
    }
    local_target = ASSETS / "manifest.local.json"
    local_target.write_text(json.dumps(local, indent=2), encoding="utf-8")

    print(f"  {len(manifest['images']['default'])} game frames")
    print(f"  {len(manifest['images']['sprites'])} sprites")
    print(f"  {len(manifest['sounds']['default'])} sounds")
    print(f"Written to {target.relative_to(ROOT)}")
    print(f"  {len(local['images']['custom'])} personal picture(s) -> "
          f"{local_target.relative_to(ROOT)} (kept out of git)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
