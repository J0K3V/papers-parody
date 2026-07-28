"""Bring in the game's own files, under their own names.

The frames that shipped with the original generator had been renamed to 0.png,
1.png and so on, and upscaled to 480x320. These are the untouched originals at
their native 240x160, which the renderer now enlarges with nearest neighbour so
the pixels stay sharp.

Any project that still points at the old numbered files is rewritten to the new
names, so nothing you saved breaks.

    python tools/import_game_assets.py "C:/path/to/Assets"
    python tools/import_game_assets.py "C:/path/to/maybe" --into sprites
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

DESTINATIONS = {
    "default": ROOT / "assets" / "images" / "default",
    "sprites": ROOT / "assets" / "images" / "sprites",
    "custom": ROOT / "assets" / "images" / "custom",
    "sounds": ROOT / "assets" / "sounds" / "default",
}

AUDIO_SUFFIXES = {".mp3", ".wav", ".ogg", ".flac"}
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp"}

#: What the old numbered frames were really called.
RENAMES = {
    "assets/images/default/0.png": "assets/images/default/Arrested.png",
    "assets/images/default/1.png": "assets/images/default/JailDoor.png",
    "assets/images/default/2.png": "assets/images/default/JailNear.png",
    "assets/images/default/3.png": "assets/images/default/JailFar.png",
    "assets/images/default/4.png": "assets/images/default/FamilyDead.png",
    "assets/images/default/5.png": "assets/images/default/EzicCelebrate.png",
    "assets/images/default/6.png": "assets/images/default/NightTrain.png",
    "assets/images/default/7.png": "assets/images/default/EndNews.png",
    "assets/images/default/arstotzka.png": "assets/images/default/Arstotzka.png",
    "assets/images/default/inspector.png": "assets/images/default/Obrinspector.png",
}

#: Long soundtrack files get tidier names.
AUDIO_RENAMES = {
    "02. Good Ending": "GoodEnding",
    "03. Bad Ending": "BadEnding",
    "01. Glory to Arstotzka (Main Theme)": "MainTheme",
}


def tidy_audio_name(stem: str) -> str:
    """Drop the track numbers the download site adds."""
    if stem in AUDIO_RENAMES:
        return AUDIO_RENAMES[stem]

    cleaned = stem
    if "." in cleaned[:4]:
        cleaned = cleaned.split(".", 1)[1]
    return cleaned.strip().replace(" ", "")


def copy_in(source: Path, into: str, replace: bool) -> tuple[int, int]:
    """Copy every asset from a folder into one of ours."""
    images = DESTINATIONS[into]
    sounds = DESTINATIONS["sounds"]
    images.mkdir(parents=True, exist_ok=True)
    sounds.mkdir(parents=True, exist_ok=True)

    if replace and into == "default":
        for stale in images.glob("*.png"):
            stale.unlink()
        print(f"  cleared the previous contents of {images.relative_to(ROOT)}")

    picture_count = sound_count = 0
    for item in sorted(source.iterdir()):
        if not item.is_file():
            continue

        suffix = item.suffix.lower()
        if suffix in IMAGE_SUFFIXES:
            shutil.copy2(item, images / item.name)
            picture_count += 1
        elif suffix in AUDIO_SUFFIXES:
            target = sounds / f"{tidy_audio_name(item.stem)}{suffix}"
            shutil.copy2(item, target)
            print(f"  {item.name}  ->  {target.name}")
            sound_count += 1

    return picture_count, sound_count


def rewrite_projects() -> int:
    """Point saved projects at the new file names."""
    folder = ROOT / "projects"
    if not folder.exists():
        return 0

    touched = 0
    for path in sorted(folder.glob("*.json")):
        payload = json.loads(path.read_text(encoding="utf-8"))
        changed = False

        for scene in payload.get("scenes", []):
            image = scene.get("image", "")
            if image in RENAMES:
                scene["image"] = RENAMES[image]
                changed = True

        if changed:
            path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            print(f"  updated {path.name}")
            touched += 1

    return touched


def main() -> int:
    """Copy a folder of game assets into the project."""
    parser = argparse.ArgumentParser(description="Import the game's own assets.")
    parser.add_argument("source", help="folder holding the files to import")
    parser.add_argument("--into", choices=list(DESTINATIONS), default="default",
                        help="where the pictures land (default: default)")
    parser.add_argument("--keep-old", action="store_true",
                        help="leave the previous frames in place instead of clearing them")
    args = parser.parse_args()

    source = Path(args.source)
    if not source.is_dir():
        print(f"Not a folder: {source}")
        return 1

    print(f"Importing from {source}")
    pictures, sounds = copy_in(source, args.into, replace=not args.keep_old)
    print(f"  {pictures} picture(s) -> {DESTINATIONS[args.into].relative_to(ROOT)}")
    if sounds:
        print(f"  {sounds} sound(s) -> {DESTINATIONS['sounds'].relative_to(ROOT)}")

    if args.into == "default":
        print("Rewriting saved projects...")
        print(f"  {rewrite_projects()} project(s) updated")

    print("\nNow refresh the manifest the web editor reads:")
    print("  python tools/build_manifest.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
