#!/usr/bin/env python3
"""Read a 32 MiB board backup in resumable chunks; never writes device flash."""
import argparse
import hashlib
import subprocess
import sys
from pathlib import Path


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    if args.output.exists():
        raise SystemExit("备份文件已存在，请保留它并指定新的文件名。")
    chunks = args.output.parent / (args.output.stem + "-chunks")
    chunks.mkdir(parents=True, exist_ok=True)
    size = 4 * 1024 * 1024
    paths = []
    for i in range(8):
        dest = chunks / f"{i:02d}.bin"
        if not dest.exists() or dest.stat().st_size != size:
            for attempt, baud in enumerate((460800, 230400, 115200), 1):
                with (chunks / f"{i:02d}-{attempt}.log").open("w") as log:
                    result = subprocess.run([
                        sys.executable, "-m", "esptool", "--chip", "esp32s3",
                        "--port", args.port, "--baud", str(baud), "read-flash",
                        "--no-progress", hex(i * size), hex(size), str(dest)
                    ], stdout=log, stderr=subprocess.STDOUT)
                if result.returncode == 0 and dest.exists() and dest.stat().st_size == size:
                    break
                print(f"分段 {i + 1} 第 {attempt} 次中断，降低速度重试。", flush=True)
            else:
                raise SystemExit(f"分段 {i + 1} 未完成；保留已完成分段，请检查连接。")
        paths.append(dest)
        print(f"备份完成 {i + 1}/8", flush=True)
    with args.output.open("xb") as out:
        for path in paths:
            out.write(path.read_bytes())
    assert args.output.stat().st_size == 32 * 1024 * 1024
    digest = hashlib.sha256(args.output.read_bytes()).hexdigest()
    args.output.with_suffix(".sha256").write_text(f"{digest}  {args.output.name}\n")
    print(f"32 MiB 原始备份已保存：{args.output}", flush=True)


if __name__ == "__main__":
    main()
