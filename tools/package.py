"""Package the native editor for release.

Gathers the executable, ffmpeg and the assets into dist/ParodyPlease so the
whole thing can be zipped and handed to someone who has nothing installed.

    python tools/package.py
"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NATIVE = ROOT / "native"
EXE = NATIVE / "build" / "ParodyPlease.exe"
FFMPEG = ROOT / "tools" / "ffmpeg.exe"
OUT = ROOT / "dist" / "ParodyPlease"


def build() -> bool:
    """Compile, if a compiler is on the path."""
    if shutil.which("mingw32-make") is None:
        print("mingw32-make is not on the path, using whatever is already built")
        return EXE.exists()

    print("Building...")
    result = subprocess.run(["mingw32-make"], cwd=str(NATIVE))  # noqa: S603, S607
    return result.returncode == 0


def main() -> int:
    """Assemble the release folder."""
    if not build():
        print("The build failed.")
        return 1

    if not EXE.exists():
        print(f"No executable at {EXE}")
        return 1

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    shutil.copy2(EXE, OUT / EXE.name)
    print(f"  {EXE.name}  {EXE.stat().st_size / (1024 * 1024):.1f} MB")

    if FFMPEG.exists():
        shutil.copy2(FFMPEG, OUT / FFMPEG.name)
        print(f"  {FFMPEG.name}  {FFMPEG.stat().st_size / (1024 * 1024):.0f} MB")
    else:
        print("  ffmpeg.exe is missing - run tools/get_ffmpeg.py first")

    # Hand-made artwork, imported sounds and the manifest listing them are the
    # author's own and are not for handing out. A release goes to strangers, so
    # this leaves them behind rather than trusting anyone to notice.
    def not_personal(folder: str, names: list[str]) -> set[str]:
        del folder
        return {name for name in names
                if name in {"custom", "manifest.local.json"}}

    shutil.copytree(ROOT / "assets", OUT / "assets", ignore=not_personal)

    # Only the sample report ships; personal ones stay on this machine.
    (OUT / "projects").mkdir()
    sample = ROOT / "projects" / "default.json"
    if sample.exists():
        shutil.copy2(sample, OUT / "projects" / sample.name)

    # Say out loud that nothing personal got in, rather than leaving it to be
    # noticed once the zip is already on the internet.
    strays = [item for item in OUT.rglob("*")
              if item.is_file() and ("custom" in item.parts
                                     or item.name == "manifest.local.json"
                                     or item.parent.name == "projects"
                                     and item.name != "default.json")]
    if strays:
        print("\nSomething personal ended up in the release folder:")
        for item in strays:
            print(f"  {item.relative_to(OUT)}")
        print("Not packaging that.")
        return 1

    total = sum(item.stat().st_size for item in OUT.rglob("*") if item.is_file())
    print(f"\n{OUT.relative_to(ROOT)}  {total / (1024 * 1024):.0f} MB")
    print("Checked: no personal artwork, sounds or reports.")
    print("Zip that folder for the release.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
