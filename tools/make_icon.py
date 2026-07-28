"""Build the application icon from the logo.

    python tools/make_icon.py

The logo is a tall poster, and an icon has to be square. Shrunk whole it turns
into a grey smudge, so two crops are used: the emblem on its own for the sizes
where nothing else would survive, and the emblem with the title below it for the
rest. Windows picks whichever size it needs.

Writes native/app.ico for the executable and assets/favicon.png for the web.
"""

import io
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "assets" / "images" / "sprites" / "icon_logo.png"
ICO = ROOT / "native" / "app.ico"
FAVICON = ROOT / "assets" / "favicon.png"

# Where each crop starts and how tall it is, in the source image.
EMBLEM_TOP, EMBLEM_HEIGHT = 230, 500      # the eagle and its stripes
FULL_TOP = 170                            # emblem and the title under it

# Below this, the title is unreadable and only gets in the way.
TITLE_NEEDS = 32


def as_bitmap(frame: Image.Image) -> bytes:
    """One icon entry in the old bitmap form: a header, the pixels, a mask.

    Rows run bottom to top and the header claims twice the real height, because
    the format expects a one-bit transparency mask stacked underneath. Ours is
    all zeroes - the artwork is opaque - but it has to be there.
    """
    image = frame.convert("RGBA")
    width, height = image.size

    pixels = b"".join(
        bytes(bytearray([b, g, r, a]))
        for row in range(height - 1, -1, -1)
        for r, g, b, a in [image.getpixel((column, row)) for column in range(width)]
    )

    mask_row = (width + 31) // 32 * 4      # each row padded to four bytes
    mask = b"\x00" * (mask_row * height)

    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0,
                         len(pixels) + len(mask), 0, 0, 0, 0)
    return header + pixels + mask


def write_ico(frames: list[Image.Image], target: Path) -> None:
    """Write an .ico holding each frame exactly as given.

    Written by hand rather than through Pillow's ICO writer, which takes one
    image and resizes it to the sizes asked for. That cannot hold a different
    crop at different sizes, and passing the frames as append_images - which is
    for GIF and TIFF - produced a file whose largest entry was random noise.

    Everything below 256 is stored as a bitmap and only the largest as a PNG,
    which is the convention. PNG entries at every size are legal on Vista and
    later, but plenty of things that read icons still expect bitmaps and show
    static instead.
    """
    payloads = []
    for frame in frames:
        if frame.width >= 256:
            buffer = io.BytesIO()
            frame.save(buffer, format="PNG")
            payloads.append(buffer.getvalue())
        else:
            payloads.append(as_bitmap(frame))

    # ICONDIR, then one ICONDIRENTRY each, then the images.
    header = struct.pack("<HHH", 0, 1, len(frames))
    offset = len(header) + 16 * len(frames)

    directory = b""
    for frame, payload in zip(frames, payloads):
        # 0 means 256 in a single byte.
        width = 0 if frame.width >= 256 else frame.width
        height = 0 if frame.height >= 256 else frame.height
        directory += struct.pack("<BBBBHHII", width, height, 0, 0, 1, 32,
                                 len(payload), offset)
        offset += len(payload)

    target.write_bytes(header + directory + b"".join(payloads))


def square(image: Image.Image, top: int, height: int) -> Image.Image:
    """Crop a band and centre it on a black square."""
    width = image.width
    band = image.crop((0, top, width, min(image.height, top + height)))

    canvas = Image.new("RGB", (width, width), (0, 0, 0))
    canvas.paste(band, (0, (width - band.height) // 2))
    return canvas


def main() -> int:
    """Write the icon and the favicon."""
    if not SOURCE.exists():
        print(f"No logo at {SOURCE.relative_to(ROOT)}")
        return 1

    logo = Image.open(SOURCE).convert("RGB")
    emblem = square(logo, EMBLEM_TOP, EMBLEM_HEIGHT)
    full = square(logo, FULL_TOP, logo.width)

    sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = []
    for size in sizes:
        source = full if size >= TITLE_NEEDS else emblem
        frames.append(source.resize((size, size), Image.LANCZOS))
        print(f"  {size:3}x{size:<3} {'emblem and title' if size >= TITLE_NEEDS else 'emblem alone'}")

    write_ico(frames, ICO)
    print(f"\n{ICO.relative_to(ROOT)}  {ICO.stat().st_size / 1024:.0f} KB")

    full.resize((256, 256), Image.LANCZOS).save(FAVICON)
    print(f"{FAVICON.relative_to(ROOT)}  {FAVICON.stat().st_size / 1024:.0f} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
