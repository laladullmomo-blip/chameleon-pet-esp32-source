#!/usr/bin/env python3
"""Package explicitly selected firmware files and committed source; no tools or secrets."""
import hashlib
import json
import re
import subprocess
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
FILES = ["bootloader/bootloader.bin", "partition_table/partition-table.bin",
         "round_pet.bin", "merged.bin", "flash_args", "flasher_args.json",
         "build-info.json", "SHA256SUMS"]


def main():
    metadata = json.loads((DIST / "build-info.json").read_text())
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if metadata["source_commit"] != revision or metadata["source_dirty"]:
        raise SystemExit("请提交源码并重新执行 scripts/build.sh，再生成交付包。")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip():
        raise SystemExit("工作区有未提交改动，请先保存相应 Git 版本。")
    for row in (DIST / "SHA256SUMS").read_text().splitlines():
        expected, name = row.split("  ", 1)
        if hashlib.sha256((DIST / name).read_bytes()).hexdigest() != expected:
            raise SystemExit(f"固件校验失败：{name}")
    version = metadata["app_version"]
    if not isinstance(version, str) or not re.fullmatch(r"[0-9]+(?:\.[0-9]+){1,3}", version):
        raise SystemExit("固件版本格式无效，无法命名交付包。")
    firmware = DIST / f"卷卷变色龙-v{version}-1.75C-烧录包.zip"
    with ZipFile(firmware, "w", ZIP_DEFLATED) as archive:
        for name in FILES:
            archive.write(DIST / name, name)
        archive.write(ROOT / "docs/烧录与真机验收.md", "烧录说明.md")
        archive.write(ROOT / "docs/验证记录.md", "验证记录.md")
        archive.write(ROOT / "NOTICE.md", "开源来源.md")
        for path in sorted((ROOT / "THIRD_PARTY_LICENSES").glob("*.txt")):
            archive.write(path, f"THIRD_PARTY_LICENSES/{path.name}")
    source = DIST / f"卷卷变色龙-v{version}-源码.zip"
    subprocess.run(["git", "archive", "--format=zip", f"--output={source}", "HEAD"], cwd=ROOT, check=True)
    with ZipFile(firmware) as archive:
        if archive.testzip():
            raise SystemExit("烧录包 ZIP 校验失败")
    with ZipFile(source) as archive:
        if archive.testzip():
            raise SystemExit("源码 ZIP 校验失败")
    rows = [f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in (firmware, source)]
    (DIST / "交付包-SHA256SUMS.txt").write_text("".join(rows))
    print(f"烧录包：{firmware}\n源码包：{source}")


if __name__ == "__main__":
    main()
