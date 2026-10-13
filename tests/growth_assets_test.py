#!/usr/bin/env python3
"""Audit approved identities and actual converted growth-stage frames."""

import hashlib
import json
from pathlib import Path
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "preview/assets"
EXPECTED = {
    "idle": 6, "happy": 6, "eat": 8, "sleep": 4, "walk": 8,
    "blink": 4, "stretch": 6, "wave": 6, "dizzy": 6, "angry": 6,
    "sick": 4, "bath": 6, "pet": 6, "shelter": 6,
}
UPSTREAM = "e6fac947bfdebec3b5e12a7856f95997a934ac07"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class GrowthAssetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = json.loads((ASSETS / "growth/stages.json").read_text())
        cls.stages = cls.catalog["stages"]
        cls.manifests = {
            stage["id"]: json.loads((ASSETS / stage["manifest"]).read_text())
            for stage in cls.stages
        }

    def test_three_approved_identities_and_original_teen_are_preserved(self):
        self.assertEqual(self.catalog["version"], 1)
        self.assertEqual(self.catalog["default_stage"], "teen")
        self.assertEqual([s["id"] for s in self.stages], ["baby", "teen", "adult"])
        self.assertEqual([s["label"] for s in self.stages], ["幼年", "少年", "成年"])
        self.assertEqual([s["reference"] for s in self.stages],
                         ["growth-concepts/baby-v2.png", "chameleon.png",
                          "growth-concepts/adult-v1.png"])
        self.assertEqual(self.stages[1]["manifest"], "frames/manifest.json")
        self.assertEqual(len({sha(ASSETS / s["reference"]) for s in self.stages}), 3)
        for stage in self.stages:
            manifest = self.manifests[stage["id"]]
            self.assertEqual(manifest["reference"]["sha256"], sha(ASSETS / stage["reference"]))
        self.assertLess(self.stages[0]["render_scale"], self.stages[1]["render_scale"])
        self.assertGreater(self.stages[2]["render_scale"], self.stages[1]["render_scale"])

    def test_every_stage_has_14_enabled_actions_and_82_real_frames(self):
        for stage in self.stages:
            manifest = self.manifests[stage["id"]]
            with self.subTest(stage=stage["id"]):
                self.assertEqual(set(manifest["actions"]) - {"grip"}, set(EXPECTED))
                self.assertEqual(sum(manifest["actions"][key]["count"] for key in EXPECTED), 82)
                if stage["id"] != "teen":
                    self.assertNotIn("grip", manifest["actions"])
                    self.assertEqual(manifest["total_frames"], 82)
                for key, count in EXPECTED.items():
                    action = manifest["actions"][key]
                    self.assertEqual(action["count"], count)
                    self.assertEqual(len(action["frames"]), count)
                    self.assertEqual(len(set(action["hashes"])), count,
                                     f"{stage['id']}/{key} contains duplicate poses")
        self.assertEqual(sum(sum(m["actions"][key]["count"] for key in EXPECTED)
                             for m in self.manifests.values()), 246)
        self.assertNotEqual(self.manifests["baby"]["actions"]["idle"]["hashes"],
                            self.manifests["adult"]["actions"]["idle"]["hashes"])
        self.assertNotEqual(self.manifests["teen"]["actions"]["idle"]["hashes"],
                            self.manifests["adult"]["actions"]["idle"]["hashes"])

    def test_real_converter_provenance_and_continuous_shared_scale(self):
        core_sha = sha(ROOT / "third_party/pixel_art_converter/core.py")
        license_sha = sha(ROOT / "third_party/pixel_art_converter/LICENSE")
        for stage in self.stages:
            manifest = self.manifests[stage["id"]]
            with self.subTest(stage=stage["id"]):
                self.assertEqual(manifest["converter"]["commit"], UPSTREAM)
                self.assertEqual(manifest["converter"]["core_sha256"], core_sha)
                self.assertEqual(manifest["converter"]["license_sha256"], license_sha)
                self.assertEqual(manifest["converter"]["api"], "make_pixel_art_animation")
                self.assertFalse(manifest["processing"]["pose_generation"])
                self.assertFalse(manifest["processing"]["frame_duplication"])
                for key in EXPECTED:
                    action = manifest["actions"][key]
                    self.assertEqual(sha(ROOT / action["source"]), action["source_sha256"])
                    self.assertFalse(action["alignment"]["stretched"])
                    self.assertFalse(action["alignment"]["per_frame_scaling"])

    def test_png_pixels_transparency_palette_hashes_and_sheet_order(self):
        for stage in self.stages:
            manifest = self.manifests[stage["id"]]
            palette = {tuple(color) for color in manifest["palette"]}
            self.assertEqual(len(palette), 32)
            for key in EXPECTED:
                action = manifest["actions"][key]
                with self.subTest(stage=stage["id"], action=key):
                    sheet_path = ASSETS / action["sheet"]
                    self.assertEqual(sha(sheet_path), action["sheet_sha256"])
                    with Image.open(sheet_path) as source_sheet:
                        sheet = source_sheet.convert("RGBA")
                    self.assertEqual(sheet.size, (128 * action["count"], 128))
                    for i, name in enumerate(action["frames"]):
                        path = ASSETS / name
                        self.assertEqual(sha(path), action["png_sha256"][i])
                        with Image.open(path) as source_frame:
                            frame = source_frame.convert("RGBA")
                        self.assertEqual(frame.size, (128, 128))
                        self.assertEqual(hashlib.sha256(frame.tobytes()).hexdigest(),
                                         action["hashes"][i])
                        raw = frame.tobytes()
                        pixels = list(zip(raw[::4], raw[1::4], raw[2::4], raw[3::4]))
                        self.assertEqual({p[3] for p in pixels}, {0, 255})
                        self.assertTrue({p[:3] for p in pixels if p[3]} <= palette)
                        self.assertTrue(all(p[:3] == (0, 0, 0) for p in pixels if not p[3]))
                        self.assertEqual(sheet.crop((i * 128, 0, (i + 1) * 128, 128)).tobytes(),
                                         frame.tobytes())

    def test_sleep_is_four_second_breath_with_fixed_nose_and_support(self):
        for stage in self.stages:
            sleep = self.manifests[stage["id"]]["actions"]["sleep"]
            with self.subTest(stage=stage["id"]):
                self.assertEqual((sleep["count"], sleep["fps"], sleep["loop"]), (4, 1, True))
                anchors = sleep["foot_anchors"]
                self.assertEqual(len({a["pixel_y"] for a in anchors}), 1)
                self.assertEqual(len({a["body_bounds"][2] for a in anchors}), 1)
                bounds = [a["body_bounds"] for a in anchors]
                for coordinate in (0, 1, 3):
                    self.assertLessEqual(max(b[coordinate] for b in bounds)
                                         - min(b[coordinate] for b in bounds), 3,
                                         "Sleeping silhouette must only move slightly")
                registration = sleep["alignment"]["output_registration"]
                self.assertEqual(registration["method"], "integer_translation_nose_and_support")
                self.assertFalse(registration["scaling"])
                self.assertFalse(registration["rotation"])
                self.assertFalse(registration["pose_pixels_modified"])

    def test_feeding_anchors_are_only_in_open_mouth_frames(self):
        for stage in self.stages:
            eat = self.manifests[stage["id"]]["actions"]["eat"]
            with self.subTest(stage=stage["id"]):
                self.assertEqual((eat["count"], eat["fps"], eat["loop"]), (8, 6, False))
                self.assertEqual([i for i, mouth in enumerate(eat["mouths"]) if mouth], [2, 3, 4])
                for i in (2, 3, 4):
                    mouth = eat["mouths"][i]
                    self.assertTrue(0 <= mouth["x"] <= 1 and 0 <= mouth["y"] <= 1)
                    x, y = round(mouth["x"] * 128), round(mouth["y"] * 128)
                    with Image.open(ASSETS / eat["frames"][i]) as source:
                        frame = source.convert("RGBA")
                    nearby = frame.crop((max(0, x - 2), max(0, y - 2),
                                         min(128, x + 3), min(128, y + 3)))
                    self.assertTrue(any(nearby.getchannel("A").tobytes()),
                                    "Mouth anchor must touch the character, not empty canvas")


if __name__ == "__main__":
    unittest.main()
