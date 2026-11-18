"""Package the native Terminal palette without building the application."""

import struct
import zipfile
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "Telegram/Resources"


def solid_background():
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    header = struct.pack(">IIBBBBB", 32, 32, 8, 2, 0, 0, 0)
    pixels = (b"\x00" + bytes.fromhex("111316") * 32) * 32
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(pixels, 9)) + chunk(b"IEND", b"")


def package():
    path = RESOURCES / "terminal.tdesktop-theme"
    entries = {
        "colors.tdesktop-palette": (RESOURCES / "terminal.tdesktop-palette").read_bytes(),
        "tiled.png": solid_background(),
    }
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in entries.items():
            info = zipfile.ZipInfo(name, date_time=(2026, 11, 16, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
    print(path)


if __name__ == "__main__":
    package()
