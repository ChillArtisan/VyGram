"""Package the native Terminal palette without building the application."""

import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "Telegram/Resources"


def package(name, palette):
    path = RESOURCES / name
    entries = {
        "colors.tdesktop-palette": palette,
        "tiled.png": (RESOURCES / "terminal-grain.png").read_bytes(),
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
    palette = (RESOURCES / "terminal.tdesktop-palette").read_bytes()
    package("terminal.tdesktop-theme", palette)
    # Both presets share semantic roles and geometry. Preserve media and avatar colours.
    steel = palette
    for graphite, replacement in {
        "#141311": "#12171d", "#191816": "#181e25", "#201f1d": "#212932",
        "#292724": "#28343e", "#302a22": "#273845", "#383530": "#38444e",
        "#e4b16e": "#85b8d5", "#a58a65": "#7695a7", "#e3c995": "#a7cfe5",
        "#bea36f": "#709bb5", "#e6e3de": "#e0e7eb", "#a39f98": "#a1abb4",
        "#85817b": "#87959e",
    }.items():
        steel = steel.replace(graphite.encode(), replacement.encode())
    package("cold-steel.tdesktop-theme", steel)
