"""Synthetic fixtures only: no Sony firmware is needed for these tests."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from analyze_pops import Image, parse_exports, parse_imports, parse_module_info


def fixture(words=5, functions=1, variables=0):
    blob = bytearray(256)
    struct.pack_into("<5I", blob, 0, 0x80, 0x40010011,
                     words | (variables << 8) | (functions << 16), 0xA0, 0xC0)
    if words >= 6:
        struct.pack_into("<I", blob, 20, 0xE0)
    blob[0x80:0x84] = b"lib\0"
    struct.pack_into("<I", blob, 0xA0, 0x12345678)
    img = Image.__new__(Image)
    img.data = bytes(blob)
    img.loads = [{"vaddr": 0, "offset": 0, "filesz": len(blob)}]
    return img, {"stub_top": 0, "stub_end": words * 4}


class InventoryTests(unittest.TestCase):
    def test_kernel_module_info_flag_is_not_an_offset(self):
        img, _ = fixture()
        blob = bytearray(img.data)
        struct.pack_into("<HBB28s5I", blob, 128, 0x1000, 0, 1,
                         b"test", 0, 16, 32, 32, 52)
        img.data = bytes(blob)
        img.loads[0]["paddr"] = 0x80000080
        result = parse_module_info(img)
        self.assertEqual(result["file_offset"], 128)
        self.assertEqual(result["name"], "test")

    def test_exports_separate_functions_and_variables(self):
        img, _ = fixture()
        blob = bytearray(img.data)
        struct.pack_into("<4I", blob, 0, 0, 0x80000000, 4 | (1 << 8) | (1 << 16), 64)
        struct.pack_into("<4I", blob, 64, 0xD632ACDB, 0xF01D73A7, 100, 128)
        img.data = bytes(blob)
        result = parse_exports(img, {"ent_top": 0, "ent_end": 16})
        self.assertEqual(result[0]["name"], "syslib")
        self.assertEqual(result[0]["entries"][0],
                         {"nid": 0xD632ACDB, "address": 100, "kind": "function"})
        self.assertEqual(result[0]["entries"][1]["kind"], "variable")

    def test_export_descriptor_overrun_rejected(self):
        img, _ = fixture()
        with self.assertRaises(ValueError):
            parse_exports(img, {"ent_top": 0, "ent_end": 16})

    def test_export_truncation_rejected(self):
        img, _ = fixture()
        with self.assertRaises(ValueError):
            parse_exports(img, {"ent_top": 0, "ent_end": 15})

    def test_reversed_export_range_rejected(self):
        img, _ = fixture()
        with self.assertRaises(ValueError):
            parse_exports(img, {"ent_top": 20, "ent_end": 16})

    def test_final_five_word_descriptor_is_not_dropped(self):
        img, mod = fixture()
        imports = parse_imports(img, mod)
        self.assertEqual(len(imports), 1)
        self.assertEqual(imports[0]["name"], "lib")
        self.assertEqual(imports[0]["functions"][0]["nid"], 0x12345678)
        self.assertIsNone(imports[0]["var_table"])

    def test_optional_sixth_word(self):
        img, mod = fixture(words=6)
        self.assertEqual(parse_imports(img, mod)[0]["var_table"], 0xE0)

    def test_zero_length_descriptor_rejected(self):
        img, mod = fixture(words=0)
        mod["stub_end"] = 20
        with self.assertRaises(ValueError):
            parse_imports(img, mod)

    def test_truncated_descriptor_rejected(self):
        img, mod = fixture()
        mod["stub_end"] = 19
        with self.assertRaises(ValueError):
            parse_imports(img, mod)

    def test_descriptor_overruns_table(self):
        img, mod = fixture(words=6)
        mod["stub_end"] = 20
        with self.assertRaises(ValueError):
            parse_imports(img, mod)

    def test_variables_need_sixth_word(self):
        img, mod = fixture(variables=1)
        with self.assertRaises(ValueError):
            parse_imports(img, mod)

    def test_cross_segment_read_rejected(self):
        img, _ = fixture()
        with self.assertRaises(ValueError):
            img.bytes_at(254, 4)

    def test_negative_read_rejected(self):
        img, _ = fixture()
        with self.assertRaises(ValueError):
            img.bytes_at(0, -1)

    def test_short_file_rejected(self):
        img, _ = fixture()
        img.data = img.data[:10]
        with self.assertRaises(ValueError):
            img.bytes_at(8, 4)


if __name__ == "__main__":
    unittest.main()
