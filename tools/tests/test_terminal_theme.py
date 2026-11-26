import re
import struct
import unittest
import xml.etree.ElementTree as ET
import zipfile
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "Telegram/Resources"
PALETTE = RESOURCES / "terminal.tdesktop-palette"
THEME = RESOURCES / "terminal.tdesktop-theme"
COLOR = re.compile(r"^\s*(\w+)\s*:\s*(#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?|\w+)\s*(?:\|\s*\w+\s*)?;", re.M)


def colors(path):
    return dict(COLOR.findall(path.read_text(encoding="utf-8-sig")))


def luminance(color):
    values = [int(color[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    values = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in values]
    return sum(v * w for v, w in zip(values, (0.2126, 0.7152, 0.0722)))


class TerminalThemeTests(unittest.TestCase):
    def test_palette_covers_current_application_colors(self):
        schema = colors(ROOT / "Telegram/lib_ui/ui/colors.palette")
        actual = colors(PALETTE)
        self.assertEqual(set(actual), set(schema))
        self.assertTrue(all(re.fullmatch(r"#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?", v) for v in actual.values()))

    def test_primary_text_and_interactive_states_have_readable_contrast(self):
        palette = colors(PALETTE)
        pairs = [
            ("windowFg", "windowBg"), ("windowSubTextFg", "windowBg"),
            ("boxTextFgError", "windowBg"),
            ("dialogsNameFg", "dialogsBg"), ("dialogsTextFg", "dialogsBg"),
            ("dialogsNameFgActive", "dialogsBgActive"),
            ("dialogsTextFgActive", "dialogsBgActive"),
            ("historyTextInFg", "msgInBg"), ("historyTextOutFg", "msgOutBg"),
            ("msgInDateFg", "msgInBg"), ("msgOutDateFg", "msgOutBg"),
            ("historyComposeAreaFg", "historyComposeAreaBg"),
            ("placeholderFg", "historyComposeAreaBg"),
            ("activeButtonFg", "activeButtonBg"),
        ]
        for foreground, background in pairs:
            with self.subTest(foreground=foreground, background=background):
                a, b = sorted((luminance(palette[foreground]), luminance(palette[background])))
                self.assertGreaterEqual((b + 0.05) / (a + 0.05), 4.5)

    def test_secondary_screen_contrast(self):
        palette = colors(PALETTE)
        for foreground, background in (
            ("mediaviewMenuFg", "mediaviewMenuBg"),
            ("mediaviewControlFg", "mediaviewVideoBg"),
            ("introDescriptionFg", "introBg"),
            ("callNameFg", "callBgOpaque"),
        ):
            with self.subTest(foreground=foreground):
                self.assertGreaterEqual(
                    (luminance(palette[foreground]) + 0.05)
                    / (luminance(palette[background]) + 0.05), 4.5)

    def test_terminal_surfaces_are_dark_and_accent_is_warm(self):
        palette = colors(PALETTE)
        for key in ("windowBg", "dialogsBg", "msgInBg", "msgOutBg", "historyComposeAreaBg"):
            self.assertLess(luminance(palette[key]), 0.04, key)
        self.assertEqual(palette["windowBgActive"], "#e4b16e")
        self.assertEqual(palette["msgInShadow"][-2:], "00")
        self.assertEqual(palette["msgOutShadow"][-2:], "00")

    def test_large_panels_are_neutral_and_message_colors_match_design(self):
        palette = colors(PALETTE)
        for key in ("windowBg", "dialogsBg", "topBarBg", "sideBarBg", "msgInBg", "msgOutBg"):
            with self.subTest(key=key):
                rgb = bytes.fromhex(palette[key][1:7])
                self.assertLessEqual(max(rgb) - min(rgb), 8)
        self.assertEqual(palette["windowFg"], "#e6e3de")
        self.assertEqual(palette["msgInBg"], "#201f1d")
        self.assertEqual(palette["msgOutBg"], "#292724")

    def test_dates_and_service_text_are_muted_grey(self):
        palette = colors(PALETTE)
        for key in ("dialogsDateFg", "dialogsTextFgService", "msgInDateFg", "msgOutDateFg",
                    "msgInServiceFg", "msgOutServiceFg", "msgServiceFg", "msgDateImgFg"):
            with self.subTest(key=key):
                rgb = bytes.fromhex(palette[key][1:7])
                self.assertLessEqual(max(rgb) - min(rgb), 24)
                self.assertLess(luminance(palette[key]), luminance(palette["windowFg"]))

    def test_distributed_theme_contains_current_palette_and_grain_background(self):
        with zipfile.ZipFile(THEME) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(set(archive.namelist()), {"colors.tdesktop-palette", "tiled.png"})
            self.assertEqual(archive.read("colors.tdesktop-palette"), PALETTE.read_bytes())
            png = archive.read("tiled.png")
            self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(struct.unpack(">II", png[16:24]), (256, 256))
            self.assertEqual(png, (RESOURCES / "terminal-grain.png").read_bytes())

    def test_native_resources_resolve_to_existing_theme_and_font(self):
        qrc = RESOURCES / "qrc/telegram/telegram.qrc"
        files = {entry.attrib.get("alias"): (qrc.parent / entry.text).resolve()
                 for entry in ET.parse(qrc).iter("file")}
        for alias in ("terminal.tdesktop-theme", "fonts/JetBrainsMono-Regular.ttf"):
            self.assertIn(alias, files)
            self.assertTrue(files[alias].is_file(), alias)
        self.assertEqual(files["terminal.tdesktop-theme"], THEME.resolve())
        self.assertEqual(files["fonts/JetBrainsMono-Regular.ttf"].read_bytes()[:4], b"\x00\x01\x00\x00")


GLASS = RESOURCES / "glass.tdesktop-theme"


def glass_colors():
    with zipfile.ZipFile(GLASS) as archive:
        text = archive.read("colors.tdesktop-palette").decode("utf-8-sig")
    return dict(COLOR.findall(text))


class GlassThemeTests(unittest.TestCase):
    def test_palette_is_complete_and_keeps_terminal_roles(self):
        self.assertEqual(set(glass_colors()), set(colors(PALETTE)))

    def test_text_stays_readable_on_glass_surfaces(self):
        palette = glass_colors()
        for foreground, background in (
            ("windowFg", "windowBg"), ("windowSubTextFg", "windowBg"),
            ("dialogsNameFg", "dialogsBg"), ("dialogsNameFgActive", "dialogsBgActive"),
            ("historyTextInFg", "msgInBg"), ("historyTextOutFg", "msgOutBg"),
            ("msgInDateFg", "msgInBg"), ("activeButtonFg", "activeButtonBg"),
        ):
            with self.subTest(foreground=foreground, background=background):
                a, b = sorted((luminance(palette[foreground]), luminance(palette[background])))
                self.assertGreaterEqual((b + 0.05) / (a + 0.05), 4.5)

    def test_bubbles_are_translucent_and_accent_is_gold(self):
        palette = glass_colors()
        for key in ("msgInBg", "msgOutBg", "msgServiceBg"):
            with self.subTest(key=key):
                self.assertEqual(len(palette[key]), 9)
                self.assertLess(int(palette[key][7:], 16), 0xe0)
        self.assertEqual(palette["windowBgActive"], "#e6b53d")
        self.assertEqual(palette["dialogsOnlineBadgeFg"], "#3fae86")

    def test_glass_theme_is_bundled(self):
        qrc = RESOURCES / "qrc/telegram/telegram.qrc"
        files = {entry.attrib.get("alias"): (qrc.parent / entry.text).resolve()
                 for entry in ET.parse(qrc).iter("file")}
        self.assertEqual(files["glass.tdesktop-theme"], GLASS.resolve())


if __name__ == "__main__":
    unittest.main()
