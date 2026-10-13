#!/usr/bin/env python3
"""Check packaged poses, audited board overrides and unchanged desktop sources."""
import importlib.util
import json
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("board_assets", ROOT / "scripts/build_board_assets.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class BoardAssetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.audit = json.loads((ROOT / "main/chameleon_assets_manifest.json").read_text(encoding="utf-8"))
        cls.desktops = {stage: json.loads((ROOT / board["source_manifest"]).read_text(encoding="utf-8"))
                        for stage, board in cls.audit["stages"].items()}
        source = (ROOT / "main/chameleon_assets.c").read_text(encoding="utf-8")
        cls.source = source
        descriptors = {name: int(size) for name, size in re.findall(
            r'static const lv_image_dsc_t (\w+) = \{.*?\.data_size = (\d+),', source, re.S)}
        cls.maps = {}
        for match in re.finditer(r'static const uint8_t (\w+)_data\[(\d+)\].*?=\n((?:    "[^"\n]*"\n)+);', source):
            values = bytes(int(b, 16) for b in re.findall(r"\\x([0-9a-f]{2})", match[3]))
            if len(values) + 1 != int(match[2]):
                raise AssertionError(f"C initializer terminator missing: {match[1]}")
            descriptor = descriptors.get(match[1])
            if descriptor != len(values):
                raise AssertionError(f"Descriptor must exclude C terminator: {match[1]}")
            cls.maps[match[1]] = (values + b"\0")[:descriptor]

    def all_actions(self):
        for stage in self.audit["stages"].values():
            yield from stage["actions"].values()

    def test_lvgl_color_plane_and_alpha_plane(self):
        # This small vector catches interleaved RGBA, RGB byte swap, channel order
        # and the accidental inclusion of the C string terminator.
        image = Image.new("RGBA", (2, 2))
        image.putdata([(255, 0, 0, 255), (0, 255, 0, 255),
                       (0, 0, 255, 255), (255, 255, 255, 0)])
        self.assertEqual(builder.rgb565(image, alpha=True),
                         bytes([0x00, 0xf8, 0xe0, 0x07, 0x1f, 0x00, 0x00, 0x00, 255, 255, 255, 0]))

    def test_transparency_is_not_silently_blurred(self):
        image = Image.new("RGBA", (1, 1), (10, 20, 30, 128))
        with self.assertRaises(ValueError):
            builder.rgb565(image, alpha=True)

    def test_override_profile_cannot_replace_other_actions_or_stage_sources(self):
        for overrides in ({"baby": {"pet": "main/assets/sick-redesign/baby/frames/manifest.json"}},
                          {"baby": {"sick": "main/assets/sick-redesign/adult/frames/manifest.json"}},
                          {"teen": {"shelter": "main/assets/shelter-redesign/teen/frames/manifest.json"}},
                          {"baby": {"shelter": "main/assets/shelter-redesign/adult/frames/manifest.json"}},
                          {"adult": {"shelter": "main/assets/sick-redesign/adult/frames/manifest.json"}},
                          {"unknown": {"sick": "main/assets/sick-redesign/baby/frames/manifest.json"}}):
            with self.subTest(overrides=overrides), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "profile.json"
                path.write_text(json.dumps({"version": 1, "default_stage": "baby", "action_overrides": overrides}), encoding="utf-8")
                with patch.object(builder, "PROFILE", path), self.assertRaises(ValueError):
                    builder.approved_stages()

    def test_all_246_stage_frames_present_and_distinct_with_stable_abi(self):
        self.assertEqual(builder.selected_stage()["id"], "baby", "Fresh pets start with the approved baby")
        self.assertEqual(self.audit["default_stage"], "baby")
        self.assertEqual(self.audit["stage_order"], ["baby", "teen", "adult"])
        self.assertEqual(set(self.audit["stages"]), set(builder.STAGES))
        self.assertEqual(self.audit["profile_sha256"], builder.sha256(builder.PROFILE.read_bytes()))
        self.assertEqual(self.audit["inactive_actions"], ["grip"])
        header = (ROOT / "main/chameleon_assets.h").read_text(encoding="utf-8")
        enum = re.search(r"typedef enum \{ (.*?) \} ch_anim_t;", header)[1]
        self.assertEqual(enum.split(", "), ["CH_" + a.upper() for a in builder.ACTIONS] + ["CH_ANIM_COUNT"])
        self.assertIn("CH_STAGE_BABY = 0, CH_STAGE_TEEN = 1, CH_STAGE_ADULT = 2, CH_STAGE_COUNT = 3", header)
        self.assertIn("CH_FRAME_COUNT = 246, CH_STAGE_FRAME_COUNT = 82, CH_ACTIVE_ANIM_COUNT = 14", header)
        self.assertIn("CH_PET_SIZE = 210, CH_WIDGET_PET_SIZE = 155", header)
        self.assertIn("CH_DEFAULT_STAGE = CH_STAGE_BABY", header)
        self.assertIn("extern const ch_stage_assets_t ch_stages[CH_STAGE_COUNT]", header)
        source = self.source
        self.assertIn('const char *ch_asset_stage(void) { return "baby"; }', source)
        self.assertEqual(source.count("[CH_GRIP] = { NULL, 0, 0, 0, NULL, NULL, NULL }"), 3)
        frames = [frame for action in self.all_actions() for frame in action["frames"]]
        self.assertEqual(len(frames), 246)
        self.assertEqual(self.audit["frame_count"], 246)
        self.assertEqual(len({f["rgba_sha256"] for f in frames}), 246)
        self.assertEqual(len(self.maps), 252)
        sizes = {"baby": (210, 155), "teen": (250, 185), "adult": (265, 196)}
        for stage in builder.approved_stages():
            board = self.audit["stages"][stage["id"]]
            desktop = self.desktops[stage["id"]]
            self.assertEqual(board["source_manifest"], "preview/assets/" + stage["manifest"])
            self.assertEqual(board["source_manifest_sha256"],
                             builder.sha256((ROOT / board["source_manifest"]).read_bytes()))
            self.assertEqual(board["reference"], desktop["reference"])
            self.assertEqual(board["reference"]["file"], "preview/assets/" + stage["reference"])
            self.assertEqual(board["reference"]["sha256"],
                             builder.sha256((ROOT / board["reference"]["file"]).read_bytes()))
            self.assertEqual(board["converter"], desktop["converter"])
            self.assertEqual(board["converter"]["commit"], "e6fac947bfdebec3b5e12a7856f95997a934ac07")
            self.assertEqual(set(board["actions"]), set(builder.ACTIVE_ACTIONS))
            self.assertEqual(board["render_scale"], stage["render_scale"])
            self.assertEqual((board["pet_size"], board["widget_size"]), sizes[stage["id"]])
            self.assertEqual(board["frame_count"], 82)
            array_name = "ch_animations" if stage["id"] == "baby" else f"ch_{stage['id']}_animations"
            self.assertIn(f"[CH_STAGE_{stage['id'].upper()}] = {{ {array_name}, "
                          f"{board['pet_size']}, {board['widget_size']}, "
                          + json.dumps(stage["id"], ensure_ascii=False) + ", "
                          + json.dumps(stage["label"], ensure_ascii=False) + " }", source)
            directory = (ROOT / board["source_manifest"]).parent.resolve()
            for action_id, action in board["actions"].items():
                action_directory = directory
                if "board_override" in action:
                    action_directory = (ROOT / action["board_override"]["source_manifest"]).parent.resolve()
                for frame in action["frames"]:
                    with self.subTest(stage=stage["id"], frame=frame["source"]):
                        path = ROOT / frame["source"]
                        self.assertTrue(path.resolve().is_relative_to(action_directory),
                                        "Stages cannot borrow poses from another identity")
                        self.assertEqual(builder.sha256(path.read_bytes()), frame["png_sha256"])
                        self.assertEqual(builder.sha256(self.maps[frame["symbol"]]), frame["rgb565a8_sha256"])
                        self.assertEqual(len(self.maps[frame["symbol"]]), 128 * 128 * 3)

    def test_every_opaque_pixel_keeps_pose_and_rgb565_quantization(self):
        for action in self.all_actions():
            for frame in action["frames"]:
                with self.subTest(frame=frame["source"]):
                    with Image.open(ROOT / frame["source"]) as image:
                        if "effect_extraction" in frame:
                            image, effect_audit = builder.strip_teen_pet_hearts(image)
                            self.assertEqual(effect_audit, frame["effect_extraction"])
                        pixels = image.convert("RGBA").tobytes()
                    data = self.maps[frame["symbol"]]
                    self.assertEqual(data[32768:], pixels[3::4], "alpha must stay pixel-for-pixel identical")
                    for offset, source_offset in enumerate(range(0, len(pixels), 4)):
                        r, g, b, a = pixels[source_offset:source_offset + 4]
                        value = data[offset * 2] | (data[offset * 2 + 1] << 8)
                        if a == 0:
                            self.assertEqual(value, 0)
                        else:
                            # Quantization may remove low bits; it must not move,
                            # interpolate, recolor or merge a pixel into its neighbor.
                            self.assertLessEqual(abs((value >> 11) * 8 - r), 7)
                            self.assertLessEqual(abs(((value >> 5) & 63) * 4 - g), 3)
                            self.assertLessEqual(abs((value & 31) * 8 - b), 7)

    def test_frame_anchors_and_playback_metadata_are_preserved(self):
        anchors = {"baby": ((98, 80), (100, 78), (102, 75)),
                   "teen": ((98, 84), (99, 84), (106, 74)),
                   "adult": ((102, 77), (101, 75), (105, 77))}
        for stage_id, stage in self.audit["stages"].items():
            for action, board in stage["actions"].items():
                desktop = self.desktops[stage_id]["actions"][action]
                if "board_override" in board:
                    replacement = json.loads((ROOT / board["board_override"]["source_manifest"]).read_text(encoding="utf-8"))
                    desktop = replacement["actions"][action]
                with self.subTest(stage=stage_id, action=action):
                    self.assertEqual((board["count"], board["fps"], board["loop"]),
                                     (desktop["count"], desktop["fps"], desktop["loop"]))
                    self.assertEqual(board["foot_y"], [anchor["y"] for anchor in desktop["foot_anchors"]])
                    self.assertEqual(board["mouths"], desktop.get("mouths", [desktop.get("mouth")] * desktop["count"]))
            eat = stage["actions"]["eat"]
            self.assertEqual([i for i, mouth in enumerate(eat["mouths"]) if mouth], [2, 3, 4])
            for index, pixel_anchor in zip((2, 3, 4), anchors[stage_id]):
                mouth = eat["mouths"][index]
                x, y = round(mouth["x"] * 128), round(mouth["y"] * 128)
                self.assertEqual((x, y), pixel_anchor, "Tongue must follow the current stage's own open mouth")
                frame = eat["frames"][index]
                with Image.open(ROOT / frame["source"]) as image:
                    nearby = image.convert("RGBA").crop((x - 2, y - 2, x + 3, y + 3))
                self.assertTrue(any(nearby.getchannel("A").tobytes()))

    def test_sick_redesign_is_board_only_and_replaces_exactly_twelve_frames(self):
        overrides = json.loads(builder.PROFILE.read_text(encoding="utf-8"))["action_overrides"]
        self.assertEqual(set(overrides), set(builder.STAGES))
        replacement_sources = []
        for stage_id, stage in self.audit["stages"].items():
            board = stage["actions"]["sick"]
            audit = board["board_override"]
            self.assertEqual(audit["source_manifest"], overrides[stage_id]["sick"])
            path = ROOT / audit["source_manifest"]
            manifest = json.loads(path.read_text(encoding="utf-8"))
            replacement = manifest["actions"]["sick"]
            desktop = self.desktops[stage_id]
            self.assertEqual(set(manifest["actions"]), {"sick"})
            self.assertEqual(manifest["palette"], desktop["palette"])
            self.assertEqual(manifest["reference"], desktop["reference"])
            self.assertEqual(manifest["converter"], desktop["converter"])
            self.assertEqual(audit["source_manifest_sha256"], builder.sha256(path.read_bytes()))
            self.assertEqual(audit["source_sheet_sha256"], builder.sha256((ROOT / audit["source_sheet"]).read_bytes()))
            provenance_path = ROOT / audit["generation_record"]
            self.assertEqual(audit["generation_record_sha256"], builder.sha256(provenance_path.read_bytes()))
            provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
            self.assertEqual(provenance["generator"], "built-in image_gen")
            self.assertEqual(provenance["reference"], desktop["reference"])
            self.assertEqual(provenance["independently_drawn_poses"], 4)
            self.assertFalse(provenance["pose_manufacture"])
            for field in ("prompt", "source", "spec"):
                self.assertEqual(provenance[field]["sha256"], builder.sha256((ROOT / provenance[field]["file"]).read_bytes()))
            self.assertEqual((board["count"], board["fps"], board["loop"]), (4, 2, True))
            self.assertEqual(replacement["distinct_frames"], 4)
            self.assertFalse(replacement["alignment"]["stretched"])
            self.assertFalse(replacement["alignment"]["per_frame_scaling"])
            self.assertEqual(audit["extra_frames_packed"], 0)
            self.assertEqual(board["mouths"], [None] * 4)
            self.assertEqual(board["foot_y"], [a["y"] for a in replacement["foot_anchors"]])
            self.assertFalse(set(replacement["hashes"]) & set(desktop["actions"]["sick"]["hashes"]))
            for index, frame in enumerate(board["frames"]):
                replacement_sources.append(frame["source"])
                self.assertEqual(frame["rgba_sha256"], replacement["hashes"][index])
                with Image.open(ROOT / frame["source"]) as image:
                    self.assertEqual(image.size, (128, 128))
                    bounds = image.getbbox()
                    self.assertTrue(0 < bounds[0] < bounds[2] < 128)
                    self.assertTrue(0 < bounds[1] < bounds[3] < 128)
            for action_id, action in stage["actions"].items():
                if action_id == "sick" or (stage_id in ("baby", "adult") and action_id == "shelter"):
                    continue
                self.assertNotIn("board_override", action)
                original = desktop["actions"][action_id]
                self.assertEqual([f["png_sha256"] for f in action["frames"]], original["png_sha256"])
                self.assertEqual([f["rgba_sha256"] for f in action["frames"]], original["hashes"])
        self.assertEqual(len(replacement_sources), 12)
        self.assertEqual(len(set(replacement_sources)), 12)
        self.assertEqual(self.audit["frame_count"], 246)
        self.assertEqual(self.audit["resource_bytes"], 14266024)

    def test_shelter_replaces_only_baby_and_adult_twelve_frames_and_preserves_teen(self):
        overrides = json.loads(builder.PROFILE.read_text(encoding="utf-8"))["action_overrides"]
        replacements = []
        for stage_id, stage in self.audit["stages"].items():
            board = stage["actions"]["shelter"]
            desktop = self.desktops[stage_id]
            original = desktop["actions"]["shelter"]
            if stage_id == "teen":
                self.assertNotIn("shelter", overrides[stage_id])
                self.assertNotIn("board_override", board)
                self.assertEqual([frame["png_sha256"] for frame in board["frames"]], original["png_sha256"])
                self.assertEqual([frame["rgba_sha256"] for frame in board["frames"]], original["hashes"])
                continue
            self.assertEqual(set(overrides[stage_id]), {"sick", "shelter"})
            audit = board["board_override"]
            self.assertEqual(audit["source_manifest"], overrides[stage_id]["shelter"])
            self.assertEqual(audit["replaces_action_only"], "shelter")
            self.assertEqual(audit["extra_frames_packed"], 0)
            path = ROOT / audit["source_manifest"]
            manifest = json.loads(path.read_text(encoding="utf-8"))
            replacement = manifest["actions"]["shelter"]
            self.assertEqual(set(manifest["actions"]), {"shelter"})
            self.assertEqual(manifest["total_frames"], 6)
            for field in ("palette", "reference", "converter"):
                self.assertEqual(manifest[field], desktop[field])
            self.assertEqual(audit["source_manifest_sha256"], builder.sha256(path.read_bytes()))
            self.assertEqual(audit["source_sheet_sha256"], builder.sha256((ROOT / audit["source_sheet"]).read_bytes()))
            provenance_path = ROOT / audit["generation_record"]
            self.assertEqual(audit["generation_record_sha256"], builder.sha256(provenance_path.read_bytes()))
            provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
            self.assertEqual(provenance["generator"], "built-in image_gen")
            self.assertEqual(provenance["stage"], stage_id)
            self.assertEqual(provenance["reference"], desktop["reference"])
            self.assertEqual(provenance["independently_drawn_poses"], 6)
            self.assertFalse(provenance["pose_manufacture"])
            for field in ("prompt", "source", "spec"):
                self.assertEqual(provenance[field]["sha256"], builder.sha256((ROOT / provenance[field]["file"]).read_bytes()))
            self.assertEqual((board["count"], board["fps"], board["loop"]), (6, 4, True))
            self.assertEqual(replacement["distinct_frames"], 6)
            self.assertEqual(board["mouths"], [None] * 6)
            self.assertEqual(board["foot_y"], [anchor["y"] for anchor in replacement["foot_anchors"]])
            self.assertFalse(set(replacement["hashes"]) & set(original["hashes"]))
            source_audit = audit["source_pose_audit"]
            self.assertEqual(source_audit["source_grid"], {"cols": 3, "rows": 2})
            self.assertEqual(source_audit["count"], 6)
            self.assertFalse(source_audit["stretched"])
            self.assertFalse(source_audit["per_frame_scaling"])
            for field in ("raw_rgba_sha256", "aligned_rgba_sha256", "output_rgba_sha256"):
                self.assertEqual(len(source_audit[field]), 6)
                self.assertEqual(len(set(source_audit[field])), 6)
            self.assertEqual(source_audit["output_rgba_sha256"], replacement["hashes"])
            source_spec = json.loads((ROOT / provenance["spec"]["file"]).read_text(encoding="utf-8"))
            if source_spec.get("native_grid"):
                import build_pet_frames as converter
                self.assertTrue(converter.validate_native_grid_spec(source_spec))
                self.assertEqual(manifest["processing"]["native_grid"], converter.native_grid_processing())
                self.assertEqual(source_audit["native_grid"], replacement["native_grid"])
                self.assertEqual(source_audit["aligned_rgba_sha256"], replacement["hashes"])
                self.assertEqual(replacement["alignment"]["canvas_padding"], 1.0)
                self.assertEqual(replacement["alignment"]["placements"], [[0, 0]] * 6)
                self.assertEqual(replacement["alignment"]["native_canvas"], [128, 128])
                self.assertFalse(replacement["extraction"]["cropped"])
            else:
                self.assertNotIn("native_grid", manifest["processing"])
                self.assertNotIn("native_grid", replacement)
                self.assertNotIn("native_grid", source_audit)
            for index, frame in enumerate(board["frames"]):
                replacements.append(frame["source"])
                self.assertEqual(frame["rgba_sha256"], replacement["hashes"][index])
                with Image.open(ROOT / frame["source"]) as image:
                    self.assertEqual(image.size, (128, 128))
                    rgba = image.convert("RGBA").tobytes()
                    self.assertEqual(set(rgba[3::4]), {0, 255})
                    colors = {tuple(rgba[pos:pos + 3]) for pos in range(0, len(rgba), 4) if rgba[pos + 3]}
                    self.assertTrue(colors.issubset({tuple(color) for color in manifest["palette"]}))
                    left, top, right, bottom = image.getbbox()
                    self.assertTrue(0 < left < right < 128 and 0 < top < bottom < 128)
                    self.assertEqual(builder.rgb565(image, alpha=True), self.maps[frame["symbol"]])
        self.assertEqual(len(replacements), 12)
        self.assertEqual(len(set(replacements)), 12)
        self.assertEqual(self.audit["frame_count"], 246)
        self.assertEqual(self.audit["sprite_bytes"], 246 * 128 * 128 * 3)
        self.assertEqual(self.audit["resource_bytes"], 14266024)

    def test_shelter_override_rejects_changed_playback_or_manufactured_poses(self):
        stage = next(stage for stage in builder.approved_stages() if stage["id"] == "baby")
        relative = "main/assets/shelter-redesign/baby/frames/manifest.json"
        path = ROOT / relative
        original = json.loads(path.read_text(encoding="utf-8"))
        read_text = Path.read_text
        for invalid in ("count", "fps", "loop", "duplicate", "grid", "manufactured"):
            manifest = json.loads(json.dumps(original))
            entry = manifest["actions"]["shelter"]
            if invalid == "count":
                entry["count"] = 5
            elif invalid == "fps":
                entry["fps"] = 8
            elif invalid == "loop":
                entry["loop"] = False
            elif invalid == "duplicate":
                entry["hashes"][1] = entry["hashes"][0]
            elif invalid == "grid":
                entry["source_grid"] = {"cols": 2, "rows": 3}
            else:
                manifest["processing"]["pose_generation"] = True
            def replacement_read(target, *args, **kwargs):
                return json.dumps(manifest) if target == path else read_text(target, *args, **kwargs)
            with self.subTest(invalid=invalid), patch.object(Path, "read_text", replacement_read), self.assertRaises(ValueError):
                builder.action_source(stage, self.desktops["baby"], "shelter")

    def test_native_shelter_audit_replays_grid_and_rejects_forged_filter_or_hashes(self):
        import build_pet_frames as converter
        original = json.loads((ROOT / "main/assets/shelter-redesign/baby/frames/manifest.json").read_text(encoding="utf-8"))
        palette = [tuple(color) for color in original["palette"]]
        core, _ = converter.load_core()
        action = {"id": "shelter", "file": "sheet.png", "cols": 3, "rows": 2,
                  "count": 6, "fps": 4, "loop": True, "background": "transparent"}
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            sheet = Image.new("RGBA", (1536, 1024), (0, 0, 0, 0))
            drawing = ImageDraw.Draw(sheet)
            for index in range(6):
                x = (index % 3) * 512 + 48 + index * 16
                y = (index // 3) * 512 + 256
                drawing.rectangle((x, y, x + 127, y + 127), fill=(*palette[0], 255))
            sheet_path = folder / "sheet.png"
            sheet.save(sheet_path)
            spec_path = folder / "spec.json"
            source_spec = {"version": 1, "size": 128, "palette_colors": 32,
                           "native_grid": True, "native_grid_size": 128, "canvas_padding": 1.0,
                           "reference": str(ROOT / original["reference"]["file"]), "actions": [action]}
            spec_path.write_text(json.dumps(source_spec), encoding="utf-8")
            raw, aligned, extraction, alignment = converter.prepare_native_grid(sheet, action, core, palette)
            raw_hashes = [converter.opaque_hash(frame) for frame in raw]
            hashes = [converter.opaque_hash(frame) for frame in aligned]
            manifest = json.loads(json.dumps(original))
            manifest["processing"]["native_grid"] = converter.native_grid_processing()
            entry = manifest["actions"]["shelter"]
            entry.update({"source": str(sheet_path), "extraction": extraction, "alignment": alignment,
                          "source_grid": {"cols": 3, "rows": 2}, "hashes": hashes,
                          "native_grid": {**converter.native_grid_processing(), "raw_rgba_sha256": raw_hashes,
                                          "aligned_rgba_sha256": hashes, "output_rgba_sha256": hashes}})
            provenance = {"independently_drawn_poses": 6, "spec": {"file": str(spec_path)},
                          "source": {"file": str(sheet_path)}}
            audit = builder.shelter_source_pose_audit(entry, manifest, provenance, original["reference"])
            self.assertEqual(audit["raw_rgba_sha256"], raw_hashes)
            self.assertEqual(audit["aligned_rgba_sha256"], hashes)
            self.assertEqual(audit["native_grid"], entry["native_grid"])
            entry["hashes"] = [hashes[1], *hashes[1:]]
            with self.assertRaisesRegex(ValueError, "provenance"):
                builder.shelter_source_pose_audit(entry, manifest, provenance, original["reference"])
            entry["hashes"] = hashes
            manifest["processing"]["native_grid"]["bilateral"] = True
            with self.assertRaisesRegex(ValueError, "unfiltered"):
                builder.shelter_source_pose_audit(entry, manifest, provenance, original["reference"])
            manifest["processing"]["native_grid"] = converter.native_grid_processing()
            source_spec["canvas_padding"] = 1.2
            spec_path.write_text(json.dumps(source_spec), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "canvas_padding"):
                builder.shelter_source_pose_audit(entry, manifest, provenance, original["reference"])

    def test_teen_pet_extracts_only_detached_pink_hearts_for_single_live_overlay(self):
        expected_removed = [61, 67, 86, 99, 84, 86]
        for stage_id, stage in self.audit["stages"].items():
            for action_id, action in stage["actions"].items():
                for index, frame in enumerate(action["frames"]):
                    eligible = stage_id == "teen" and action_id == "pet"
                    self.assertEqual("effect_extraction" in frame, eligible)
                    with Image.open(ROOT / frame["source"]) as image:
                        self.assertEqual(builder.sha256(builder.opaque_rgba(image)), frame["rgba_sha256"])
                        if not eligible:
                            continue
                        board_image, audit = builder.strip_teen_pet_hearts(image)
                        self.assertEqual(audit, frame["effect_extraction"])
                        self.assertEqual(audit["removed_pixels"], expected_removed[index])
                        self.assertTrue(audit["body_component_unchanged"])
                        self.assertTrue(audit["source_files_unchanged"])
                        self.assertEqual(builder.sha256(builder.opaque_rgba(board_image)), frame["board_rgba_sha256"])
                        original_pixels = image.convert("RGBA").tobytes()
                        board_pixels = board_image.tobytes()
                        changed = [(tuple(original_pixels[i:i+4]), tuple(board_pixels[i:i+4]))
                                   for i in range(0, len(original_pixels), 4)
                                   if original_pixels[i:i+4] != board_pixels[i:i+4]]
                        self.assertEqual(len(changed), expected_removed[index])
                        self.assertTrue(all(a[3] and a[0] * 2 > a[1] * 3 and a[2] * 5 > a[1] * 6
                                            and b == (0, 0, 0, 0) for a, b in changed))
                        self.assertEqual(builder.rgb565(board_image, alpha=True), self.maps[frame["symbol"]])

    def test_sleep_breathing_keeps_nose_and_support_still(self):
        for stage, desktop in self.desktops.items():
            with self.subTest(stage=stage):
                sleep = desktop["actions"]["sleep"]
                self.assertEqual(sleep["count"], 4)
                self.assertEqual(sleep["fps"], 1, "A complete breath takes four seconds")
                anchors = sleep["foot_anchors"]
                self.assertEqual(len({a["pixel_y"] for a in anchors}), 1, "Feet must not bob")
                self.assertEqual(len({a["body_bounds"][2] for a in anchors}), 1, "The nose must stay still")
                bounds = [a["body_bounds"] for a in anchors]
                for coordinate in (0, 1, 3):
                    self.assertLessEqual(max(b[coordinate] for b in bounds) - min(b[coordinate] for b in bounds), 3,
                                         "Sleep silhouette changes must stay within three source pixels")
                self.assertEqual(len(set(sleep["hashes"])), 4, "Breath phases must be actual distinct drawings")
                registration = sleep["alignment"]["output_registration"]
                self.assertEqual(registration["method"], "integer_translation_nose_and_support")
                self.assertFalse(registration["scaling"])
                self.assertFalse(registration["rotation"])
                self.assertFalse(registration["pose_pixels_modified"])

    def test_backgrounds_nearest_and_flash_budget(self):
        self.assertEqual([b["id"] for b in self.audit["backgrounds"]], list(builder.BACKGROUNDS))
        for background in self.audit["backgrounds"]:
            with Image.open(ROOT / background["source"]) as image:
                nearest = image.convert("RGB").resize((128, 128), Image.Resampling.NEAREST)
                data = builder.rgb565(nearest.resize((466, 466), Image.Resampling.NEAREST))
            self.assertEqual(data, self.maps[background["symbol"]])
            self.assertEqual(background["pixel_grid_size"], 128)
            self.assertEqual(background["render_dimensions"], [466, 466])
            self.assertEqual(len(data), 466 * 466 * 2)
        self.assertEqual(self.audit["sprite_bytes"], 246 * 128 * 128 * 3)
        self.assertTrue(all(stage["sprite_bytes"] == 82 * 128 * 128 * 3
                            for stage in self.audit["stages"].values()))
        self.assertEqual(self.audit["background_bytes"], 5 * 466 * 466 * 2)
        self.assertEqual(self.audit["item_bytes"], 32 * 32 * 3)
        self.assertEqual(self.audit["resource_bytes"], 14266024)
        self.assertEqual(self.audit["literal_terminator_bytes"], 252)
        self.assertEqual(self.audit["array_storage_bytes"], 14266276)
        self.assertLess(self.audit["array_storage_bytes"], 16 * 1024 * 1024,
                        "Resource floor must fit the existing 16 MiB app partition; link-time check covers code/font")

    def test_board_only_droppings_are_real_converted_transparent_pixels(self):
        self.assertEqual(len(self.audit["items"]), 1)
        item = self.audit["items"][0]
        self.assertEqual((item["id"], item["dimensions"], item["bytes"]), ("poop", [32, 32], 3072))
        self.assertEqual(item["source"], "main/assets/poop.png")
        self.assertEqual(item["converter"]["commit"], "e6fac947bfdebec3b5e12a7856f95997a934ac07")
        self.assertEqual(item["converter"]["api"], "make_pixel_art")
        manifest_path = ROOT / item["source_manifest"]
        self.assertEqual(builder.sha256(manifest_path.read_bytes()), item["source_manifest_sha256"])
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertFalse(manifest["registration"]["stretched"])
        self.assertFalse(manifest["processing"]["dither"])
        self.assertEqual(builder.sha256((ROOT / item["source"]).read_bytes()), item["png_sha256"])
        with Image.open(ROOT / item["source"]) as image:
            self.assertEqual(image.size, (32, 32))
            pixels = image.convert("RGBA").tobytes()
            alpha = pixels[3::4]
            self.assertEqual(set(alpha), {0, 255})
            self.assertGreater(sum(a == 255 for a in alpha), 200)
            colors = {tuple(pixels[i:i + 3]) for i in range(0, len(pixels), 4) if pixels[i + 3]}
            self.assertLessEqual(len(colors), 6)
            self.assertTrue(all(r > g > b for r, g, b in colors), "Droppings keep the earthy brown palette")
            self.assertEqual(builder.sha256(builder.opaque_rgba(image)), item["rgba_sha256"])
            self.assertEqual(builder.rgb565(image, alpha=True), self.maps[item["symbol"]])
        self.assertEqual(builder.sha256(self.maps[item["symbol"]]), item["rgb565a8_sha256"])
        header = (ROOT / "main/chameleon_assets.h").read_text(encoding="utf-8")
        self.assertIn("CH_POOP_SIZE = 32", header)
        self.assertIn("extern const lv_image_dsc_t * const ch_poop", header)
        self.assertIn("const lv_image_dsc_t * const ch_poop = &ch_item_poop", self.source)

    def test_font_covers_current_ui_chinese(self):
        self.assertTrue(set(builder.font_symbols()).issubset(set(self.audit["font"]["symbols"])))
        source = (ROOT / "main/chameleon_font.c").read_text(encoding="utf-8")
        self.assertIn('#include "lvgl.h"', source)
        self.assertNotIn('"lvgl/lvgl.h"', source)
        self.assertIn("const lv_font_t chameleon_font_18", source)
        for char in self.audit["font"]["symbols"]:
            self.assertIn(f'U+{ord(char):04X} "{char}"', source)

    def test_actual_board_ui_literals_include_symbols(self):
        font_chars = set(self.audit["font"]["symbols"])
        for path in (ROOT / "main").glob("chameleon_*.cpp"):
            ui_literals = re.findall(r'"(?:[^"\\]|\\.)*"', path.read_text(encoding="utf-8"))
            required = {char for literal in ui_literals for char in literal if ord(char) > 126}
            self.assertFalse(required - font_chars,
                             f"Missing actual UI glyphs in {path.name}: {sorted(required - font_chars)}")


if __name__ == "__main__":
    unittest.main()
