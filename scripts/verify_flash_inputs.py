#!/usr/bin/env python3
"""Fail closed when the files selected for flashing do not match the build manifest."""

from __future__ import annotations

import hashlib
import shlex
import sys
from pathlib import Path


def selected_files(dist: Path) -> list[Path]:
    tokens = shlex.split((dist / "flash_args").read_text())
    files: list[Path] = []
    for index, token in enumerate(tokens[:-1]):
        if token.startswith("0x") and token[2:] and all(c in "0123456789abcdefABCDEF" for c in token[2:]):
            candidate = dist / tokens[index + 1]
            if candidate.is_file():
                files.append(candidate)
    if not files:
        raise ValueError("flash_args does not select any existing firmware files")
    return files


def verify(dist: Path) -> None:
    manifest = dist / "SHA256SUMS"
    args = dist / "flash_args"
    if not args.is_file():
        raise FileNotFoundError("未找到 dist/flash_args，请先运行 scripts/build.sh。")
    if not manifest.is_file():
        raise FileNotFoundError("未找到 dist/SHA256SUMS，请重新运行 scripts/build.sh 生成校验清单。")
    expected: dict[Path, str] = {}
    for line in manifest.read_text().splitlines():
        fields = line.split(maxsplit=1)
        if len(fields) == 2:
            expected[dist / fields[1].lstrip(" *\\")] = fields[0].lower()
    for path in selected_files(dist):
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if expected.get(path) != actual:
            raise ValueError(f"固件校验失败：{path.relative_to(dist)}")


if __name__ == "__main__":
    try:
        verify(Path(sys.argv[1] if len(sys.argv) > 1 else "dist").resolve())
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
    print("固件校验通过：flash_args 中的文件与 SHA256SUMS 一致。")
