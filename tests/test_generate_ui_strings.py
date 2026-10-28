import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "tools" / "generate_ui_strings.py"
SPEC = importlib.util.spec_from_file_location("generate_ui_strings", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class GenerateUiStringsTests(unittest.TestCase):
    def test_utf8_chinese_becomes_armcc_ascii_escapes(self):
        entries = MODULE.parse_source('#define S_TEXT "已关" /* 状态 */')
        output = MODULE.generate_header(entries)
        self.assertIn(r'"\xE5\xB7\xB2\xE5\x85\xB3"', output)
        output.encode("ascii")

    def test_hex_escape_is_separated_from_following_hex_character(self):
        entries = MODULE.parse_source('#define S_TEXT "温A"')
        output = MODULE.generate_header(entries)
        self.assertIn(r'"\xE6\xB8\xA9A"', output)
        output.encode("ascii")

    def test_duplicate_macro_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Duplicate macro"):
            MODULE.parse_source('#define S_TEXT "甲"\n#define S_TEXT "乙"')

    def test_format_placeholders_are_preserved(self):
        entries = MODULE.parse_source('#define S_VALUE "目标 %ld.%ld℃"')
        output = MODULE.generate_header(entries)
        self.assertIn(r'%ld.%ld\xE2\x84\x83', output)


if __name__ == "__main__":
    unittest.main()
