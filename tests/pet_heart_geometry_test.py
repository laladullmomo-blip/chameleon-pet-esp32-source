#!/usr/bin/env python3
"""Inspect approved art and the board-only pixel-heart geometry without a UI emulator.

The native LVGL tests cover actual object visibility and redraws. These checks
cover the source decorations, every per-frame alpha contour and round display
clearances used by the independent overlay.
"""
import importlib.util
import json
import math
from pathlib import Path
import re
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "preview/assets"
SPEC = importlib.util.spec_from_file_location("board_pet_effects", ROOT / "scripts/board_pet_effects.py")
effects = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(effects)
UI = (ROOT / "main/chameleon_ui.cpp").read_text(encoding="utf-8")
WIDTH = int(re.search(r"kPetHeartWidth=(\d+)", UI)[1])
HEIGHT = int(re.search(r"kPetHeartHeight=(\d+)", UI)[1])
GAP = int(re.search(r"kPetHeartGap=(\d+)", UI)[1])
SAFE_TOP = int(re.search(r"kPetHeartSafeTop=(\d+)", UI)[1])
PATTERN = re.findall(r'"([0-3]+)"', re.search(r"pet_heart_pattern\[\]=\{([^}]+)\}", UI)[1])


def lround(value):
    return math.floor(value + .5) if value >= 0 else math.ceil(value - .5)


def heart_geometry(image, size, sprite_x, sprite_y, drift):
    alpha = image.convert("RGBA").getchannel("A")
    tops = [-1] * 128
    right = 64
    for column in range(64, 128):
        for row in range(128):
            if alpha.getpixel((column, row)):
                tops[column] = row
                right = column
                break
    half_source = (WIDTH * 128 + size * 2 - 1) // (size * 2)
    for center in range((128 * 85 + 50)//100, min(127, right+half_source)+1):
        eligible = [tops[column] for column in range(max(64, center-half_source),
                                                     min(127, center+half_source)+1)
                    if tops[column] >= 0]
        if not eligible:
            continue
        forehead_y = sprite_y + lround(size * min(eligible)/128)
        if forehead_y-HEIGHT-GAP-2 < SAFE_TOP:
            continue
        return (sprite_x+lround(size*center/128)-WIDTH//2,
                forehead_y-HEIGHT-GAP-drift, forehead_y)
    raise AssertionError("No safe forehead anchor for the actual frame")


class PetHeartTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.frames = []
        for stage in json.loads((ASSETS / "growth/stages.json").read_text())["stages"]:
            action = json.loads((ASSETS / stage["manifest"]).read_text())["actions"]["pet"]
            for index, path in enumerate(action["frames"]):
                source_path = ASSETS / path
                with Image.open(source_path) as image:
                    source = image.convert("RGBA")
                board, audit = effects.strip_teen_pet_hearts(source) if stage["id"] == "teen" else (source, None)
                cls.frames.append((stage, index, source_path, source, board, audit,
                                   action["foot_anchors"][index]["y"]))

    def test_teen_decoration_extraction_preserves_the_complete_body_and_sources(self):
        counts = []
        for stage, index, path, source, board, audit, _ in self.frames:
            if stage["id"] != "teen":
                continue
            original_bytes = path.read_bytes()
            self.assertEqual(effects.rgba_hash(source), audit["source_rgba_sha256"])
            self.assertEqual(effects.rgba_hash(board), audit["board_rgba_sha256"])
            body = effects.alpha_components(source)[0]
            for point in body:
                self.assertEqual(source.getpixel(point), board.getpixel(point))
            differences = [(x, y) for y in range(128) for x in range(128)
                           if source.getpixel((x, y)) != board.getpixel((x, y))]
            self.assertEqual(len(differences), audit["removed_pixels"])
            self.assertTrue(all(point not in body and effects.is_pink(source.getpixel(point))
                                and board.getpixel(point) == (0, 0, 0, 0) for point in differences))
            self.assertFalse(any(effects.is_pink(board.getpixel((x, y)))
                                 for y in range(128) for x in range(128)))
            self.assertEqual(path.read_bytes(), original_bytes)
            counts.append(audit["removed_pixels"])
        self.assertEqual(counts, [61, 67, 86, 99, 84, 86])

    def test_filter_rejects_missing_or_connected_decorations(self):
        with self.assertRaises(ValueError):
            effects.strip_teen_pet_hearts(Image.new("RGBA", (64, 64)))
        with self.assertRaises(ValueError):
            effects.strip_teen_pet_hearts(Image.new("RGBA", (128, 128)))
        with self.assertRaises(ValueError):
            effects.strip_teen_pet_hearts(Image.new("RGBA", (128, 128), (60, 140, 70, 255)))
        connected = Image.new("RGBA", (128, 128), (60, 140, 70, 255))
        connected.putpixel((110, 40), (240, 100, 170, 255))
        with self.assertRaisesRegex(ValueError, "connected to the body"):
            effects.strip_teen_pet_hearts(connected)

    def test_single_small_pixel_heart_has_a_symmetric_silhouette(self):
        self.assertEqual((WIDTH, HEIGHT), (18, 16))
        self.assertEqual(len(PATTERN), 8)
        self.assertTrue(all(len(row) == 9 for row in PATTERN))
        shape = ["".join("1" if value != "0" else "0" for value in row) for row in PATTERN]
        self.assertTrue(all(row == row[::-1] for row in shape))
        self.assertEqual(shape[0], "011000110")
        self.assertEqual(shape[-1], "000010000")
        image = Image.new("RGBA", (9, 8))
        for y, row in enumerate(PATTERN):
            for x, value in enumerate(row):
                if value != "0":
                    image.putpixel((x, y), (244, 141, 178, 255))
        self.assertEqual(len(effects.alpha_components(image)), 1)
        self.assertIn("0xc95683,0xf48db2,0xffcadb", UI)

    def test_all_three_stages_all_six_frames_clear_the_forehead_clock_controls_and_circle(self):
        controls = [(18, 177, 78, 221), (20, 278, 80, 322),
                    (388, 177, 448, 221), (386, 278, 446, 322)]
        checked = 0
        for stage, index, _, _, board, _, foot_y in self.frames:
            size = lround(250*stage["render_scale"])
            alpha = board.getchannel("A")
            for base in (280, 296, 315):
                sprite_y = lround(base-size*foot_y)
                for movement in (-24, 0, 24):
                    sprite_x = lround((233-size*.5+movement)/2)*2
                    for drift in (0, 1, 2):
                        with self.subTest(stage=stage["id"], frame=index, base=base, movement=movement, drift=drift):
                            left, top, forehead_y = heart_geometry(board, size, sprite_x, sprite_y, drift)
                            right, bottom = left+WIDTH-1, top+HEIGHT-1
                            self.assertGreaterEqual(top, SAFE_TOP)
                            self.assertGreaterEqual(forehead_y-bottom, GAP+1)
                            for x, y in ((left, top), (right, top), (left, bottom), (right, bottom)):
                                self.assertLess((x-233)**2+(y-233)**2, 233**2)
                            for x1, y1, x2, y2 in controls:
                                self.assertTrue(right < x1 or left > x2 or bottom < y1 or top > y2)
                            for source_y in range(128):
                                py = sprite_y+(source_y+.5)*size/128
                                if not top-1 <= py <= bottom+1:
                                    continue
                                for source_x in range(128):
                                    px = sprite_x+(source_x+.5)*size/128
                                    if left-1 <= px <= right+1:
                                        self.assertFalse(alpha.getpixel((source_x, source_y)),
                                                         "The heart must never cover any actual body pixel")
                            checked += 1
        self.assertEqual(checked, 486)

    def test_each_stage_follows_the_distinct_real_frame_anchors(self):
        positions = {stage: set() for stage in ("baby", "teen", "adult")}
        for stage, _, _, _, board, _, foot_y in self.frames:
            size = lround(250*stage["render_scale"])
            left = lround((233-size*.5)/2)*2
            positions[stage["id"]].add(heart_geometry(board, size, left, lround(280-size*foot_y), 0)[:2])
        self.assertTrue(all(len(points) >= 3 for points in positions.values()))


if __name__ == "__main__":
    unittest.main()
