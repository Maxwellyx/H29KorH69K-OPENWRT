#!/usr/bin/env python3
"""Validate a built OpenWrt image without confusing fwtool trailers with gzip errors."""
import json
from pathlib import Path
import sys
import zlib


def verify(image_path: Path, metadata_path: Path) -> dict:
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    supported = metadata.get("supported_devices", [])
    if "hinlink,opc-h29k" not in supported:
        raise ValueError(f"Image does not identify H29K: {supported!r}")
    decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
    first_sector = bytearray()
    uncompressed_bytes = 0
    with image_path.open("rb") as source:
        while chunk := source.read(256 * 1024):
            data = decoder.decompress(chunk)
            uncompressed_bytes += len(data)
            if len(first_sector) < 512:
                first_sector.extend(data[: 512 - len(first_sector)])
            if decoder.eof:
                break
    if not decoder.eof:
        raise ValueError("Truncated gzip image")
    if len(first_sector) != 512 or first_sector[510:512] != b"\x55\xaa":
        raise ValueError("Missing disk partition-table signature")
    if uncompressed_bytes < 16 * 1024 * 1024:
        raise ValueError("Disk image is unexpectedly small")
    return {
        "supported_devices": supported,
        "compressed_bytes": image_path.stat().st_size,
        "uncompressed_bytes": uncompressed_bytes,
        "gzip_crc": "passed",
        "disk_signature": "passed",
        "hardware_tested": False,
    }


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: verify-image.py IMAGE.img.gz METADATA.json")
    print(json.dumps(verify(Path(sys.argv[1]), Path(sys.argv[2])), indent=2))
