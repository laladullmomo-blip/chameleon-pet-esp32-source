#!/usr/bin/env python3
"""Convert the board-only tangible item sprite with the fixed upstream core."""
from __future__ import annotations

import argparse
import io
import json
import math
from pathlib import Path

from build_pet_frames import load_core, file_hash, normalize_output
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPEC = ROOT / "main/assets/source/poop.json"


def convert() -> tuple[bytes, dict]:
    spec = json.loads(SPEC.read_text(encoding="utf-8"))
    source_path = SPEC.parent / spec["source"]
    if file_hash(source_path) != spec["source_sha256"]:
        raise ValueError("Board item source changed without provenance update")
    core, provenance = load_core()
    config = spec["processing"]
    with Image.open(source_path) as image:
        image = image.convert("RGBA")
        mask = image.getchannel("A").point(lambda a: 255 if a > config["alpha_threshold"] else 0)
        bounds = mask.getbbox()
        if not bounds:
            raise ValueError("Generated item source has no opaque silhouette")
        # Crop empty source margins, then place unmodified source pixels on a
        # square canvas. The converter samples this canvas; no asymmetric
        # scaling or hand-painted replacement of the generated item occurs.
        crop = image.crop(bounds)
        side = math.ceil(crop.width * config["width"] / config["silhouette_width"])
        baseline = round(side * config["baseline"] / config["height"])
        if crop.height > baseline:
            side = math.ceil(crop.height * config["height"] / config["baseline"])
            baseline = round(side * config["baseline"] / config["height"])
        square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        placement = [round((side - crop.width) / 2), baseline - crop.height]
        square.paste(crop, tuple(placement))
        frame = core.make_pixel_art(square, config["width"], config["height"], config["colors"],
                                    config["dither"], config["remove_bg"], config["outline"],
                                    fixed_palette=config["palette"], brightness=config["brightness"],
                                    contrast=config["contrast"], saturation=config["saturation"])
        frame = normalize_output(frame, [tuple(c) for c in config["palette"]], "poop", 0)
        output = io.BytesIO()
        frame.save(output, format="PNG")
    audit = {"version": 1, "id": "poop", "source_spec": str(SPEC.relative_to(ROOT)),
             "source_spec_sha256": file_hash(SPEC),
             "source": str(source_path.relative_to(ROOT)), "source_sha256": file_hash(source_path),
             "converter": {**provenance, "api": "make_pixel_art"}, "processing": config,
             "registration": {"source_bounds": list(bounds), "square_canvas": side,
                              "placement": placement, "stretched": False},
             "output": spec["output"]}
    return output.getvalue(), audit


def main() -> int:
    import hashlib
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    png, audit = convert()
    audit["png_sha256"] = hashlib.sha256(png).hexdigest()
    outputs = {ROOT / audit["output"]: png,
               ROOT / "main/assets/items_manifest.json": (json.dumps(audit, ensure_ascii=False, indent=2) + "\n").encode()}
    for path, data in outputs.items():
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                raise ValueError(f"Board item conversion differs: {path.relative_to(ROOT)}")
        else:
            path.write_bytes(data)
    print(json.dumps({"ok": True, "check": args.check, "item": audit["id"],
                      "width": 32, "height": 32, "png_sha256": audit["png_sha256"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
