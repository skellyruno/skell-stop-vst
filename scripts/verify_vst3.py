#!/usr/bin/env python3
"""Fail-closed verifier for the Windows x64 StopDial VST3 bundle."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any

PRODUCT_NAME = "StopDial"
BUNDLE_NAME = "StopDial.vst3"
MODULEINFO_PATH = "Contents/Resources/moduleinfo.json"
BINARY_PATH = "Contents/x86_64-win/StopDial.vst3"
PE_MACHINE_AMD64 = 0x8664
PE_MAGIC_PE32_PLUS = 0x20B
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")


class VerificationError(ValueError):
    """Raised when a package does not satisfy the release contract."""


def _field(mapping: dict[str, Any], *names: str) -> Any:
    for name in names:
        if name in mapping:
            return mapping[name]
    lowered = {str(key).casefold(): value for key, value in mapping.items()}
    for name in names:
        if name.casefold() in lowered:
            return lowered[name.casefold()]
    return None


def _validate_pe32_plus_amd64(data: bytes) -> None:
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise VerificationError("VST3 module is not a valid PE file (missing MZ header)")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset < 0x40 or pe_offset + 26 > len(data):
        raise VerificationError("VST3 module has an invalid PE header offset")
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise VerificationError("VST3 module is not a valid PE file (missing PE signature)")
    machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
    optional_magic = struct.unpack_from("<H", data, pe_offset + 24)[0]
    if machine != PE_MACHINE_AMD64:
        raise VerificationError(f"Expected AMD64 PE machine 0x8664, found 0x{machine:04x}")
    if optional_magic != PE_MAGIC_PE32_PLUS:
        raise VerificationError(
            f"Expected PE32+ optional header 0x20b, found 0x{optional_magic:04x}"
        )


def _strip_steinberg_json5_trailing_commas(source: str) -> str:
    """Remove only trailing commas emitted by Steinberg's official JSON5Writer."""
    output: list[str] = []
    in_string = False
    escaped = False
    index = 0
    while index < len(source):
        char = source[index]
        if in_string:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue
        if char == '"':
            in_string = True
            output.append(char)
            index += 1
            continue
        if char == ",":
            lookahead = index + 1
            while lookahead < len(source) and source[lookahead].isspace():
                lookahead += 1
            if lookahead < len(source) and source[lookahead] in "}]":
                index += 1
                continue
        output.append(char)
        index += 1
    return "".join(output)


def _validate_moduleinfo(raw: bytes) -> tuple[str, str]:
    try:
        source = raw.decode("utf-8-sig")
        info = json.loads(_strip_steinberg_json5_trailing_commas(source))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise VerificationError(f"moduleinfo.json is not valid UTF-8/Steinberg JSON5 metadata: {exc}") from exc
    if not isinstance(info, dict):
        raise VerificationError("moduleinfo.json root must be a JSON object")

    name = _field(info, "Name", "name")
    version = _field(info, "Version", "version")
    if name != PRODUCT_NAME:
        raise VerificationError(f"Expected VST3 product name {PRODUCT_NAME!r}, found {name!r}")
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        raise VerificationError(f"VST3 version is missing or invalid: {version!r}")

    classes = _field(info, "Classes", "classes")
    if not isinstance(classes, list):
        raise VerificationError("moduleinfo.json must contain a Classes array")
    audio_classes = [
        item for item in classes
        if isinstance(item, dict)
        and _field(item, "Category", "category") == "Audio Module Class"
        and _field(item, "Name", "name") == PRODUCT_NAME
    ]
    if len(audio_classes) != 1:
        raise VerificationError(
            "Expected exactly one StopDial Audio Module Class in moduleinfo.json"
        )
    class_id = _field(audio_classes[0], "CID", "cid")
    if not isinstance(class_id, str) or not class_id.strip():
        raise VerificationError("StopDial Audio Module Class is missing its CID")
    return version, class_id


def _check_relative_name(name: str) -> str:
    normalized = name.replace("\\", "/")
    path = PurePosixPath(normalized)
    if normalized.startswith("/") or re.match(r"^[A-Za-z]:", normalized):
        raise VerificationError(f"Absolute archive path is not allowed: {name!r}")
    if any(part in ("..", "") for part in path.parts):
        raise VerificationError(f"Unsafe archive path: {name!r}")
    return normalized


def _validate_entries(entries: dict[str, bytes]) -> dict[str, Any]:
    if not entries:
        raise VerificationError("VST3 package is empty")

    bundle_roots = {name.split("/", 1)[0] for name in entries}
    if bundle_roots != {BUNDLE_NAME}:
        raise VerificationError(
            f"Expected one top-level {BUNDLE_NAME} bundle, found {sorted(bundle_roots)!r}"
        )

    for name in entries:
        suffix = PurePosixPath(name).suffix.casefold()
        if suffix == ".exe":
            raise VerificationError(f"Standalone executable is not allowed in VST3 package: {name}")
        if suffix == ".zip":
            raise VerificationError(f"Nested ZIP archive is not allowed in VST3 package: {name}")

    moduleinfo_name = f"{BUNDLE_NAME}/{MODULEINFO_PATH}"
    binary_name = f"{BUNDLE_NAME}/{BINARY_PATH}"
    if moduleinfo_name not in entries:
        raise VerificationError(f"Missing required VST3 metadata: {moduleinfo_name}")
    if binary_name not in entries:
        raise VerificationError(f"Missing Windows x64 VST3 module: {binary_name}")

    module_files = [
        name for name in entries
        if PurePosixPath(name).suffix.casefold() == ".vst3"
    ]
    if module_files != [binary_name]:
        raise VerificationError(
            f"Expected exactly one Windows VST3 module binary, found {sorted(module_files)!r}"
        )

    version, class_id = _validate_moduleinfo(entries[moduleinfo_name])
    binary = entries[binary_name]
    _validate_pe32_plus_amd64(binary)
    return {
        "product": PRODUCT_NAME,
        "format": "VST3",
        "platform": "windows-x64",
        "version": version,
        "bundle": BUNDLE_NAME,
        "binary": BINARY_PATH,
        "class_id": class_id,
        "binary_size": len(binary),
        "binary_sha256": hashlib.sha256(binary).hexdigest(),
        "verified": True,
    }


def verify_bundle(path: Path) -> dict[str, Any]:
    if path.is_file() and zipfile.is_zipfile(path):
        entries: dict[str, bytes] = {}
        seen: set[str] = set()
        try:
            with zipfile.ZipFile(path) as archive:
                for item in archive.infolist():
                    raw_name = item.filename.rstrip("/")
                    if not raw_name:
                        continue
                    name = _check_relative_name(raw_name)
                    key = name.casefold()
                    if key in seen:
                        raise VerificationError(f"Duplicate or case-colliding archive path: {name}")
                    seen.add(key)
                    if item.is_dir():
                        continue
                    entries[name] = archive.read(item)
        except zipfile.BadZipFile as exc:
            raise VerificationError(f"Invalid ZIP archive: {exc}") from exc
        return _validate_entries(entries)

    if not path.is_dir() or path.name != BUNDLE_NAME:
        raise VerificationError(f"Input must be a {BUNDLE_NAME} directory or a ZIP containing it")

    entries: dict[str, bytes] = {}
    seen: set[str] = set()
    for item in path.rglob("*"):
        if item.is_symlink():
            raise VerificationError(f"Symbolic links are not allowed in the VST3 bundle: {item}")
        if not item.is_file():
            continue
        relative = item.relative_to(path).as_posix()
        key = relative.casefold()
        if key in seen:
            raise VerificationError(f"Duplicate or case-colliding bundle path: {relative}")
        seen.add(key)
        entries[f"{BUNDLE_NAME}/{relative}"] = item.read_bytes()
    return _validate_entries(entries)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path, help="StopDial.vst3 directory or ZIP archive")
    parser.add_argument("--manifest-out", type=Path, help="Write a JSON provenance manifest")
    parser.add_argument("--source-commit", default="", help="Git commit SHA for this build")
    parser.add_argument("--pr-head-commit", default="",
                        help="PR branch head SHA when Actions builds a synthetic merge commit")
    args = parser.parse_args(argv)

    try:
        manifest = verify_bundle(args.artifact)
        manifest["source_commit"] = args.source_commit
        manifest["pr_head_commit"] = args.pr_head_commit
        if args.manifest_out:
            args.manifest_out.parent.mkdir(parents=True, exist_ok=True)
            args.manifest_out.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
        print(json.dumps(manifest, indent=2, sort_keys=True))
        return 0
    except (OSError, VerificationError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
