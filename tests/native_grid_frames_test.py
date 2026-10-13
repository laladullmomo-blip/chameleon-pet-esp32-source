#!/usr/bin/env python3
"""Exercise shelter's opt-in native grid with synthetic, coordinate-known art.

No test reads a generated project sprite or replaces the fixed-SHA core. The
4x4 vectors distinguish color majority from nearest-color sampling, and alpha
nearest sampling from majority/averaged alpha. Temporary conversions also prove
that the real animation API receives 128px input without an intermediate filter.
"""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
MODULE_SPEC = importlib.util.spec_from_file_location("pet_frames_native_grid_test", ROOT / "scripts/build_pet_frames.py")
builder = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(builder)
np, Image = builder.np, builder.Image

RED, GREEN, BLUE = (240, 24, 48), (24, 216, 64), (32, 64, 224)
PALETTE = [RED, GREEN, BLUE] + [(value, value, value) for value in range(0, 232, 8)]
POSITIONS = [(0, 0), (108, 0), (0, 108), (108, 108), (4, 52), (104, 52)]


def native_spec() -> dict:
    return {"reference": "reference.png", "size": 128, "palette_colors": 32,
            "native_grid": True, "native_grid_size": 128, "canvas_padding": 1.0,
            "actions": [{"id": "shelter", "file": "source.png", "cols": 3, "rows": 2,
                         "count": 6, "fps": 4, "loop": True, "background": "transparent"}]}


def sheet_from_cells(cells: list[Image.Image]) -> Image.Image:
    sheet = Image.new("RGBA", (1536, 1024), (0, 0, 0, 0))
    for index, cell in enumerate(cells):
        sheet.paste(cell, (index % 3 * 512, index // 3 * 512))
    return sheet


def synthetic_source() -> tuple[Image.Image, list[Image.Image], list[Image.Image]]:
    """Six independent poses touch each cell edge, with decisive 4x4 vectors."""
    expected = []
    for index, (left, top) in enumerate(POSITIONS):
        rgba = np.zeros((128, 128, 4), dtype=np.uint8)
        # Distinct colors remain independent even on the legacy cropped route.
        rgba[top:top + 20, left:left + 20] = (*PALETTE[index], 255)
        expected.append(Image.fromarray(rgba, "RGBA"))
    raw = [frame.resize((512, 512), Image.Resampling.NEAREST) for frame in expected]
    source = np.array(raw[0]).copy()
    result = np.array(expected[0]).copy()

    # Quantize first: two near-red groups (4+4) plus red (1) beat blue (7).
    # Before quantization blue is the individual-color majority, and the
    # NEAREST sample at local (2,2) is blue, so both wrong orders fail.
    colors = [(241, 24, 48)] * 4 + [(240, 25, 48)] * 4 + [RED] + [BLUE] * 7
    block = np.array([(*color, 255) for color in colors], dtype=np.uint8).reshape(4, 4, 4)
    source[48:52, 40:44] = block
    result[12, 10] = (*RED, 255)

    # Hidden blue dominates 15:1 but only the sampled green is foreground.
    source[48:52, 44:48] = (*BLUE, 0)
    source[50, 46] = (*GREEN, 255)
    result[12, 11] = (*GREEN, 255)

    # Alpha 127 does not vote; the one blue pixel with alpha 128 does.
    source[48:52, 48:52] = (*RED, 127)
    source[50, 50] = (*BLUE, 128)
    result[12, 12] = (*BLUE, 255)

    # NEAREST is transparent even though 15/16 pixels are opaque.
    source[48:52, 52:56] = (*GREEN, 255)
    source[50, 54] = (*RED, 127)
    result[12, 13] = (0, 0, 0, 0)

    # NEAREST is opaque even though only 1/16 pixels exceed the threshold.
    source[48:52, 56:60] = (*BLUE, 0)
    source[50, 58] = (*GREEN, 128)
    result[12, 14] = (*GREEN, 255)

    raw[0], expected[0] = Image.fromarray(source, "RGBA"), Image.fromarray(result, "RGBA")
    return sheet_from_cells(raw), raw, expected


class NativeGridFramesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core, cls.provenance = builder.load_core()
        cls.source, cls.source_cells, cls.expected = synthetic_source()
        cls.prepared = None

    def prepared_source(self):
        if type(self).prepared is None:
            type(self).prepared = builder.prepare_native_grid(
                self.source, native_spec()["actions"][0], self.core, PALETTE)
        return type(self).prepared

    def assert_images_equal(self, actual: Image.Image, expected: Image.Image):
        self.assertEqual(actual.size, expected.size)
        np.testing.assert_array_equal(np.array(actual.convert("RGBA")), np.array(expected.convert("RGBA")))

    def test_opt_in_is_bounded_and_legacy_spec_is_not_revalidated(self):
        self.assertTrue(builder.validate_native_grid_spec(native_spec()))
        for enabled in (None, False):
            # Unknown native-only fields must not alter the established route.
            spec = {"actions": [{"id": "sleep", "registration": "nose_and_support"}],
                    "canvas_padding": 1.2, "native_grid_size": "unused"}
            if enabled is not None:
                spec["native_grid"] = enabled
            self.assertFalse(builder.validate_native_grid_spec(spec))

    def test_invalid_native_grid_specs_are_rejected(self):
        invalid = []
        for key, values in {
            "native_grid": (1, None, "true"),
            "size": (64, 128.0, True),
            "native_grid_size": (64, 128.0, True, None),
            "canvas_padding": (1.2, 0.9, True, "1.0", float("nan")),
            "palette_colors": (16, 64),
        }.items():
            for value in values:
                spec = native_spec()
                spec[key] = value
                invalid.append((key, value, spec))
        for key, values in {
            "id": ("sleep", "sick"), "cols": (2, 3.0, True),
            "rows": (3, 2.0, True), "count": (5, 6.0, True),
            "fps": (3, 5, True), "loop": (False, 1, None),
            "background": ("auto", "edge_color"),
            "registration": (None, "nose_and_support"),
        }.items():
            for value in values:
                spec = native_spec()
                spec["actions"][0][key] = value
                invalid.append((f"action.{key}", value, spec))
        for actions in ([], None, {}, [None], [native_spec()["actions"][0]] * 2):
            spec = native_spec()
            spec["actions"] = actions
            invalid.append(("actions", actions, spec))
        for label, value, spec in invalid:
            with self.subTest(field=label, value=value), self.assertRaises(ValueError):
                builder.validate_native_grid_spec(spec)

    def test_fixed_cells_preserve_source_coordinates_at_all_edges(self):
        raw, native, extraction, alignment = self.prepared_source()
        self.assertEqual(len(raw), 6)
        self.assertEqual(len(native), 6)
        for index in range(6):
            with self.subTest(frame=index):
                self.assert_images_equal(raw[index], self.source_cells[index])
                self.assert_images_equal(native[index], self.expected[index])
        expected_bounds = [[column * 512, row * 512, (column + 1) * 512, (row + 1) * 512]
                           for row in range(2) for column in range(3)]
        self.assertEqual(extraction["frame_bounds"], expected_bounds)
        self.assertEqual(extraction["nominal_cell_dimensions"], [512, 512])
        self.assertEqual(alignment["source_canvas"], [512, 512])
        self.assertEqual(alignment["native_canvas"], [128, 128])
        self.assertEqual(alignment["placements"], [[0, 0]] * 6)
        self.assertEqual(alignment["canvas_padding"], 1.0)
        self.assertTrue(alignment["source_grid_preserved"])
        for metadata in (extraction, alignment):
            self.assertFalse(metadata["cropped"])
        self.assertFalse(alignment["stretched"])
        self.assertFalse(alignment["per_frame_scaling"])

    def test_color_vote_is_quantized_first_and_excludes_transparent_pixels(self):
        native = np.array(self.prepared_source()[1][0])
        self.assertEqual(tuple(native[12, 10]), (*RED, 255))
        self.assertEqual(tuple(native[12, 11]), (*GREEN, 255))
        self.assertEqual(tuple(native[12, 12]), (*BLUE, 255))
        for frame in self.prepared_source()[1]:
            array = np.array(frame)
            visible = {tuple(color) for color in array[array[..., 3] > 127, :3]}
            self.assertTrue(visible.issubset(set(PALETTE)))

    def test_binary_alpha_uses_nearest_sample_in_every_4x4_block(self):
        raw, native, _extraction, _alignment = self.prepared_source()
        for index, (source, output) in enumerate(zip(raw, native)):
            with self.subTest(frame=index):
                expected_alpha = np.where(np.array(source)[2::4, 2::4, 3] > 127, 255, 0).astype(np.uint8)
                array = np.array(output)
                np.testing.assert_array_equal(array[..., 3], expected_alpha)
                self.assertLessEqual(set(np.unique(array[..., 3])), {0, 255})
                self.assertFalse(np.any(array[array[..., 3] == 0, :3]))
        first = np.array(native[0])
        self.assertEqual(int(first[12, 13, 3]), 0)
        self.assertEqual(int(first[12, 14, 3]), 255)

    def test_native_source_only_accepts_exact_six_512px_cells(self):
        for size in ((1535, 1024), (1536, 1023), (768, 512), (3072, 2048), (512, 512)):
            with self.subTest(size=size), self.assertRaises(ValueError):
                builder.prepare_native_grid(Image.new("RGBA", size), native_spec()["actions"][0], self.core, PALETTE)

    def test_raw_duplicate_poses_are_rejected(self):
        repeated = sheet_from_cells([self.source_cells[0]] * 6)
        with self.assertRaisesRegex(ValueError, "raw native-grid source.*identical"):
            builder.prepare_native_grid(repeated, native_spec()["actions"][0], self.core, PALETTE)

    def test_distinct_raw_poses_that_collapse_on_native_grid_are_rejected(self):
        base = np.zeros((512, 512, 4), dtype=np.uint8)
        base[0:80, 0:80] = (*RED, 255)
        cells = []
        for index in range(6):
            array = base.copy()
            array[10, 10 + index] = (*BLUE, 255)
            cells.append(Image.fromarray(array, "RGBA"))
        self.assertEqual(len({builder.opaque_hash(frame) for frame in cells}), 6)
        with self.assertRaisesRegex(ValueError, "native 128px source.*identical"):
            builder.prepare_native_grid(sheet_from_cells(cells), native_spec()["actions"][0], self.core, PALETTE)

    def test_each_source_cell_requires_both_foreground_and_transparency(self):
        for fill in ((0, 0, 0, 0), (*RED, 255)):
            cells = [frame.copy() for frame in self.source_cells]
            cells[2] = Image.new("RGBA", (512, 512), fill)
            with self.subTest(fill=fill), self.assertRaisesRegex(ValueError, "visible pose and transparent background"):
                builder.prepare_native_grid(sheet_from_cells(cells), native_spec()["actions"][0], self.core, PALETTE)

    def test_real_animation_pass_is_128_to_128_without_lanczos_or_bilateral(self):
        native = self.prepared_source()[1]
        resize_calls = []
        actual_resize = Image.Image.resize

        def checked_resize(image, size, resample=None, *args, **kwargs):
            self.assertNotEqual(resample, Image.Resampling.LANCZOS)
            resize_calls.append((image.size, size, resample))
            return actual_resize(image, size, resample, *args, **kwargs)

        with patch.object(Image.Image, "resize", new=checked_resize), \
                patch("skimage.restoration.denoise_bilateral", side_effect=AssertionError("native pass smoothed pixels")), \
                patch.object(self.core, "_split_and_smooth", wraps=self.core._split_and_smooth) as upstream_split:
            output = self.core.make_pixel_art_animation(native, 128, 128, 32, False, False, False, fixed_palette=PALETTE)
        self.assertEqual(upstream_split.call_count, 6)
        for call in upstream_split.call_args_list:
            self.assertEqual(call.args[0].size, (128, 128))
            self.assertEqual(call.args[1:], (128, 128))
        self.assertEqual(resize_calls, [((128, 128), (128, 128), Image.Resampling.NEAREST)] * 6)
        for actual, expected in zip(output, native):
            normalized = builder.normalize_output(actual, PALETTE, "shelter", 0)
            self.assert_images_equal(normalized, expected)

    def test_conversion_routes_native_cells_to_real_core_and_records_their_hashes(self):
        original_resize = Image.Image.resize
        resize_calls = []

        def checked_resize(image, size, resample=None, *args, **kwargs):
            self.assertNotEqual(resample, Image.Resampling.LANCZOS)
            resize_calls.append((image.size, size, resample))
            return original_resize(image, size, resample, *args, **kwargs)

        with tempfile.TemporaryDirectory(prefix="pet-native-grid-test-") as temporary:
            directory = Path(temporary)
            self.source.save(directory / "source.png")
            self.expected[0].save(directory / "reference.png")
            spec_path = directory / "spec.json"
            spec_path.write_text(json.dumps(native_spec()), encoding="utf-8")
            output = directory / "frames"
            with patch.object(builder, "load_core", return_value=(self.core, self.provenance)), \
                    patch.object(builder, "build_palette", return_value=PALETTE), \
                    patch.object(builder, "split_source", side_effect=AssertionError("native path cropped alpha islands")), \
                    patch.object(builder, "align_frames", side_effect=AssertionError("native path fit individual poses")), \
                    patch.object(self.core, "split_sprite_sheet", wraps=self.core.split_sprite_sheet) as fixed_split, \
                    patch.object(self.core, "_split_and_smooth", wraps=self.core._split_and_smooth) as upstream_split, \
                    patch.object(Image.Image, "resize", new=checked_resize), \
                    patch("skimage.restoration.denoise_bilateral", side_effect=AssertionError("native conversion smoothed pixels")):
                manifest = builder.convert(spec_path, output, None)
            self.assertEqual(fixed_split.call_count, 1)
            self.assertEqual(fixed_split.call_args.args[1:], (3, 2))
            self.assertEqual(upstream_split.call_count, 6)
            self.assertEqual([call.args[0].size for call in upstream_split.call_args_list], [(128, 128)] * 6)
            self.assertEqual(sum(source == (512, 512) and target == (128, 128)
                                 for source, target, _resample in resize_calls), 6)
            self.assertTrue(all(resample == Image.Resampling.NEAREST for _source, _target, resample in resize_calls))
            action = manifest["actions"]["shelter"]
            native_hashes = [builder.opaque_hash(frame) for frame in self.expected]
            self.assertEqual(action["hashes"], native_hashes)
            self.assertEqual(action["native_grid"]["aligned_rgba_sha256"], native_hashes)
            self.assertEqual(action["native_grid"]["output_rgba_sha256"], native_hashes)
            self.assertEqual(action["native_grid"]["raw_rgba_sha256"],
                             [builder.opaque_hash(frame) for frame in self.source_cells])
            processing = manifest["processing"]["native_grid"]
            self.assertEqual(processing["converter_input"], [128, 128])
            self.assertEqual(processing["converter_ratio"], 1.0)
            self.assertFalse(processing["lanczos"])
            self.assertFalse(processing["bilateral"])
            for index, expected in enumerate(self.expected):
                with Image.open(output / "shelter" / f"shelter_{index:02d}.png") as actual:
                    self.assert_images_equal(actual, expected)

    def test_legacy_alignment_keeps_shared_canvas_and_original_placements(self):
        first = Image.new("RGBA", (40, 40), (*RED, 255))
        second = Image.new("RGBA", (40, 40), (*BLUE, 255))
        extraction = {"nominal_cell_span": [512, 512], "cols": 3, "rows": 2,
                      "frame_bounds": [[4, 8, 44, 48], [600, 30, 640, 70]]}
        frames, metadata = builder.align_frames([first, second], extraction, 1.2)
        self.assertEqual(metadata["method"], "original_grid_coordinates_shared_scale")
        self.assertEqual(metadata["source_canvas"], [615, 615])
        self.assertEqual(metadata["placements"], [[56, 96], [140, 118]])
        self.assertAlmostEqual(metadata["baseline"], 522.75)
        for output, input_frame, (left, top) in zip(frames, (first, second), metadata["placements"]):
            self.assertEqual(output.size, (615, 615))
            self.assertEqual(output.getbbox(), (left, top, left + 40, top + 40))
            self.assert_images_equal(output.crop(output.getbbox()), input_frame)

    def test_missing_or_false_opt_in_still_uses_legacy_extraction_and_alignment(self):
        with tempfile.TemporaryDirectory(prefix="pet-legacy-grid-test-") as temporary:
            directory = Path(temporary)
            self.source.save(directory / "source.png")
            self.expected[0].save(directory / "reference.png")
            for enabled in (None, False):
                spec = native_spec()
                spec["canvas_padding"] = 1.2
                if enabled is None:
                    del spec["native_grid"]
                else:
                    spec["native_grid"] = enabled
                spec_path = directory / "spec.json"
                spec_path.write_text(json.dumps(spec), encoding="utf-8")
                with self.subTest(native_grid=enabled), \
                        patch.object(builder, "load_core", return_value=(self.core, self.provenance)), \
                        patch.object(builder, "build_palette", return_value=PALETTE), \
                        patch.object(builder, "prepare_native_grid", side_effect=AssertionError("legacy route invoked native grid")), \
                        patch.object(builder, "split_source", wraps=builder.split_source) as split, \
                        patch.object(builder, "align_frames", wraps=builder.align_frames) as align, \
                        patch.object(self.core, "make_pixel_art_animation", return_value=self.expected) as convert:
                    manifest = builder.convert(spec_path, directory / f"legacy-{enabled}", None)
                self.assertEqual(split.call_count, 1)
                self.assertEqual(align.call_count, 1)
                self.assertEqual(align.call_args.args[2], 1.2)
                self.assertEqual([frame.size for frame in convert.call_args.args[0]], [(615, 615)] * 6)
                action = manifest["actions"]["shelter"]
                self.assertEqual(action["alignment"]["source_canvas"], [615, 615])
                self.assertEqual(action["extraction"]["method"],
                                 "transparent_component_centers_to_nominal_grid")
                self.assertNotIn("native_grid", manifest["processing"])
                self.assertNotIn("native_grid", action)


if __name__ == "__main__":
    unittest.main()
