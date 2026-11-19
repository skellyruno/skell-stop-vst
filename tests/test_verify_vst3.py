from __future__ import annotations

import json
import struct
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.verify_vst3 import VerificationError, verify_bundle

BUNDLE = "StopDial.vst3"
MODULEINFO = "Contents/Resources/moduleinfo.json"
BINARY = "Contents/x86_64-win/StopDial.vst3"


def fake_pe(machine: int = 0x8664, optional_magic: int = 0x20B) -> bytes:
    data = bytearray(512)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<H", data, 0x84, machine)
    struct.pack_into("<H", data, 0x98, optional_magic)
    return bytes(data)


def moduleinfo(name: str = "StopDial") -> bytes:
    return json.dumps({
        "Name": name,
        "Version": "2.3.1",
        "Classes": [{
            "CID": "0123456789abcdef0123456789abcdef",
            "Category": "Audio Module Class",
            "Name": "StopDial",
        }],
    }).encode("utf-8")


class VerifyVst3Tests(unittest.TestCase):
    def make_bundle(self, root: Path, binary: bytes | None = None) -> Path:
        bundle = root / BUNDLE
        (bundle / "Contents/Resources").mkdir(parents=True)
        (bundle / "Contents/x86_64-win").mkdir(parents=True)
        (bundle / MODULEINFO).write_bytes(moduleinfo())
        (bundle / BINARY).write_bytes(binary if binary is not None else fake_pe())
        return bundle

    def test_valid_directory_and_manifest_fields(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            result = verify_bundle(bundle)
            self.assertTrue(result["verified"])
            self.assertEqual(result["product"], "StopDial")
            self.assertEqual(result["version"], "2.3.1")
            self.assertEqual(result["platform"], "windows-x64")
            self.assertEqual(len(result["binary_sha256"]), 64)

    def test_valid_zip_with_bundle_at_archive_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = self.make_bundle(root)
            archive_path = root / "StopDial.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                for item in bundle.rglob("*"):
                    if item.is_file():
                        archive.write(item, f"{BUNDLE}/{item.relative_to(bundle).as_posix()}")
            self.assertTrue(verify_bundle(archive_path)["verified"])

    def test_rejects_wrong_machine(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp), fake_pe(machine=0x14C))
            with self.assertRaisesRegex(VerificationError, "AMD64"):
                verify_bundle(bundle)

    def test_rejects_pe32_not_pe32_plus(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp), fake_pe(optional_magic=0x10B))
            with self.assertRaisesRegex(VerificationError, r"PE32\+"):
                verify_bundle(bundle)

    def test_rejects_missing_moduleinfo(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            (bundle / MODULEINFO).unlink()
            with self.assertRaisesRegex(VerificationError, "metadata"):
                verify_bundle(bundle)

    def test_accepts_steinberg_json5_trailing_commas_and_normalizes_staged_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            metadata = json.loads(moduleinfo())
            metadata["StringProbe"] = 'literal ,} and "quoted" and \\path'
            text = json.dumps(metadata, indent=2)
            text = text.replace("\n    }", ",\n    }").replace("\n  ]", ",\n  ]")
            text = text[:-1] + ",\n}"
            moduleinfo_path = bundle / MODULEINFO
            moduleinfo_path.write_text(text, encoding="utf-8")

            self.assertTrue(verify_bundle(bundle)["verified"])

            normalized_text = moduleinfo_path.read_text(encoding="utf-8")
            normalized = json.loads(normalized_text)
            self.assertEqual(
                normalized["StringProbe"],
                'literal ,} and "quoted" and \\path',
            )
            self.assertEqual(
                normalized_text,
                json.dumps(normalized, indent=2, ensure_ascii=False) + "\n",
            )

    def test_zip_rejects_un_normalized_json5_trailing_commas(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = self.make_bundle(root)
            moduleinfo_path = bundle / MODULEINFO
            text = json.dumps(json.loads(moduleinfo()), indent=2)
            text = text[:-1] + ",\n}"
            moduleinfo_path.write_text(text, encoding="utf-8")

            archive_path = root / "unnormalized-json5.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                for item in bundle.rglob("*"):
                    if item.is_file():
                        archive.write(item, f"{BUNDLE}/{item.relative_to(bundle).as_posix()}")

            with self.assertRaisesRegex(VerificationError, "strict JSON"):
                verify_bundle(archive_path)

    def test_rejects_duplicate_top_level_metadata_keys(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            raw = moduleinfo().decode("utf-8")
            raw = raw.replace('"Name": "StopDial",', '"Name": "StopDial", "Name": "StopDial",', 1)
            (bundle / MODULEINFO).write_text(raw, encoding="utf-8")
            with self.assertRaisesRegex(VerificationError, "Duplicate"):
                verify_bundle(bundle)

    def test_rejects_duplicate_nested_metadata_keys(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            raw = moduleinfo().decode("utf-8")
            raw = raw.replace('"Category": "Audio Module Class",', '"Category": "Audio Module Class", "Category": "Audio Module Class",', 1)
            (bundle / MODULEINFO).write_text(raw, encoding="utf-8")
            with self.assertRaisesRegex(VerificationError, "Duplicate"):
                verify_bundle(bundle)

    def test_rejects_case_colliding_metadata_keys(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            raw = moduleinfo().decode("utf-8")
            raw = raw.replace('"Name": "StopDial",', '"Name": "StopDial", "name": "StopDial",', 1)
            (bundle / MODULEINFO).write_text(raw, encoding="utf-8")
            with self.assertRaisesRegex(VerificationError, "case-colliding"):
                verify_bundle(bundle)

    def test_rejects_nonfinite_json_constants(self):
        for token in ("NaN", "Infinity", "-Infinity"):
            with self.subTest(token=token), tempfile.TemporaryDirectory() as temp:
                bundle = self.make_bundle(Path(temp))
                raw = moduleinfo().decode("utf-8")
                raw = raw[:-1] + f', "NumericProbe": {token}}}'
                (bundle / MODULEINFO).write_text(raw, encoding="utf-8")
                with self.assertRaisesRegex(VerificationError, "non-finite"):
                    verify_bundle(bundle)

    def test_rejects_exponent_overflow(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            raw = moduleinfo().decode("utf-8")
            raw = raw[:-1] + ', "NumericProbe": 1e999}'
            (bundle / MODULEINFO).write_text(raw, encoding="utf-8")
            with self.assertRaisesRegex(VerificationError, "overflows"):
                verify_bundle(bundle)

    def test_rejects_malformed_steinberg_json5_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            (bundle / MODULEINFO).write_text('{"Name" "StopDial"}', encoding="utf-8")
            with self.assertRaisesRegex(VerificationError, "moduleinfo.json"):
                verify_bundle(bundle)

    def test_rejects_wrong_product_name(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            (bundle / MODULEINFO).write_bytes(moduleinfo("OtherPlugin"))
            with self.assertRaisesRegex(VerificationError, "product name"):
                verify_bundle(bundle)

    def test_rejects_standalone_executable(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            (bundle / "Contents/Resources/StopDial.exe").write_bytes(b"MZ")
            with self.assertRaisesRegex(VerificationError, "Standalone"):
                verify_bundle(bundle)

    def test_rejects_nested_zip(self):
        with tempfile.TemporaryDirectory() as temp:
            bundle = self.make_bundle(Path(temp))
            (bundle / "Contents/Resources/inner.zip").write_bytes(b"PK")
            with self.assertRaisesRegex(VerificationError, "Nested ZIP"):
                verify_bundle(bundle)

    def test_rejects_case_colliding_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = self.make_bundle(root)
            archive_path = root / "case-collision.zip"
            # Windows paths are case-insensitive, so preserve both spellings
            # as ZIP entries rather than trying to create colliding files on disk.
            with zipfile.ZipFile(archive_path, "w") as archive:
                for item in bundle.rglob("*"):
                    if item.is_file():
                        archive.write(item, f"{BUNDLE}/{item.relative_to(bundle).as_posix()}")
                archive.writestr(
                    f"{BUNDLE}/Contents/Resources/MODULEINFO.JSON",
                    moduleinfo(),
                )
            with self.assertRaisesRegex(VerificationError, "case-colliding"):
                verify_bundle(archive_path)

    def test_rejects_archive_path_traversal(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = self.make_bundle(root)
            archive_path = root / "unsafe.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                for item in bundle.rglob("*"):
                    if item.is_file():
                        archive.write(item, f"{BUNDLE}/{item.relative_to(bundle).as_posix()}")
                archive.writestr("../outside.txt", b"no")
            with self.assertRaisesRegex(VerificationError, "Unsafe archive path"):
                verify_bundle(archive_path)


if __name__ == "__main__":
    unittest.main()
