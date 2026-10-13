#!/usr/bin/env python3
"""Convert real, independently drawn sprite poses with PixelArtConverter.

Example:
  python3 scripts/build_pet_frames.py --spec preview/assets/animation-source/spec.json

The spec uses reference/file paths relative to its own directory. Each action is
{id,file,cols,rows,count,fps,loop}. No pose generation or duplicated fill frames
is performed here. Source cells are aspect-fitted without stretching. Optional
mouth / mouths anchors use normalized output-frame coordinates; a mouths array
has exactly count entries and may use null for frames with no custom anchor.
Only shelter may opt into native_grid=true, native_grid_size=128 and
canvas_padding=1.0: six fixed 512px cells are reduced to the original 128px
grid with the pinned core's palette/majority APIs, without crop, fit or filters.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "third_party/pixel_art_converter"
LOCAL_DEPS = ROOT / ".tools/pixel-art-deps"
if LOCAL_DEPS.is_dir():
    sys.path.insert(0, str(LOCAL_DEPS))

try:
    import numpy as np
    from PIL import Image, ImageDraw
    import skimage
    import scipy
except ImportError as error:
    raise SystemExit(
        f"Missing conversion dependency: {error}. Install requirements from "
        f"{VENDOR / 'requirements.txt'} into .tools/pixel-art-deps."
    ) from error

UPSTREAM_COMMIT = "e6fac947bfdebec3b5e12a7856f95997a934ac07"
ACCENTS = [
    (248, 176, 190), (227, 118, 154), (187, 70, 109), (110, 41, 77),
    (211, 243, 255), (131, 209, 237), (65, 151, 183), (34, 84, 111),
]


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_core():
    module_spec = importlib.util.spec_from_file_location("pixel_art_converter_core", VENDOR / "core.py")
    if module_spec is None or module_spec.loader is None:
        raise ValueError("Cannot load vendored PixelArtConverter core")
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    provenance = json.loads((VENDOR / "provenance.json").read_text(encoding="utf-8"))
    if provenance["commit"] != UPSTREAM_COMMIT or module.__version__ != provenance["upstream_version"]:
        raise ValueError("Vendored converter provenance does not match the fixed source")
    if file_hash(VENDOR / "core.py") != provenance["core_sha256"] or file_hash(VENDOR / "LICENSE") != provenance["license_sha256"]:
        raise ValueError("Vendored upstream files were modified; fixed-SHA hashes do not match")
    return module, provenance


def opaque_hash(frame: Image.Image) -> str:
    array = np.array(frame.convert("RGBA"), dtype=np.uint8)
    array[array[..., 3] <= 127] = 0
    array[..., 3] = np.where(array[..., 3] > 127, 255, 0)
    return hashlib.sha256(array.tobytes()).hexdigest()


def require_unique(frames: list[Image.Image], action_id: str, phase: str):
    seen = {}
    for index, frame in enumerate(frames):
        digest = opaque_hash(frame)
        if digest in seen:
            raise ValueError(f"{action_id}: {phase} frames {seen[digest]} and {index} are identical; redraw the pose, do not duplicate frames")
        seen[digest] = index


def remove_background(frame: Image.Image, core, policy: str) -> Image.Image:
    frame = frame.convert("RGBA")
    array = np.array(frame)
    # Pure-color generated backgrounds can be keyed with the actual upstream
    # implementation. Already-transparent artwork is retained without rembg.
    opaque = bool(np.all(array[..., 3] > 127))
    if policy == "edge_color" or (policy == "auto" and opaque):
        frame = core.remove_bg_by_color(frame)
        array = np.array(frame)
    if not np.any(array[..., 3] <= 127):
        raise ValueError("Source has no transparent pixels after background preparation; supply a transparent PNG or an edge-keyable plain background")
    if np.count_nonzero(array[..., 3] > 127) < max(20, array.shape[0] * array.shape[1] * 0.005):
        raise ValueError("Source pose is empty or almost empty after background removal")
    return frame


def align_frames(frames: list[Image.Image], extraction: dict, padding: float) -> tuple[list[Image.Image], dict]:
    """Keep original grid-relative pose motion at one common normalized scale.

    Alpha-island crops discard transparent margins only. Paste their unchanged
    pixels at the original nominal cell coordinates on a common square canvas.
    All actions use the same padding factor; no crop is individually enlarged.
    The source's 0.85 baseline maps to the output's 0.85 baseline, while any
    genuine pose / crouching displacement inside the original grid remains.
    """
    cell_width, cell_height = extraction["nominal_cell_span"]
    side = math.ceil(max(cell_width, cell_height) * padding)
    offset_x = side * 0.5 - cell_width * 0.5
    offset_y = side * 0.85 - cell_height * 0.85
    aligned, placements = [], []
    for index, (frame, bbox) in enumerate(zip(frames, extraction["frame_bounds"])):
        column, row = index % extraction["cols"], index // extraction["cols"]
        x = round(bbox[0] - column * cell_width + offset_x)
        y = round(bbox[1] - row * cell_height + offset_y)
        if x < 0 or y < 0 or x + frame.width > side or y + frame.height > side:
            raise ValueError(f"Pose {index} exceeds the shared padded canvas; increase canvas_padding for all actions")
        square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        square.paste(frame, (x, y))
        aligned.append(square)
        placements.append([x, y])
    return aligned, {"method": "original_grid_coordinates_shared_scale", "source_canvas": [side, side],
                     "canvas_padding": padding, "baseline": side * 0.85,
                     "source_bounds": extraction["frame_bounds"], "placements": placements,
                     "preserves_vertical_motion": True, "stretched": False, "per_frame_scaling": False}


def split_source(image: Image.Image, cols: int, rows: int, count: int, core, policy: str) -> tuple[list[Image.Image], dict]:
    """Preserve alpha islands that slightly overlap an AI sheet's nominal grid.

    The upstream grid splitter provides the nominal cells. Independent source
    poses may extend a few pixels over those cell borders; in that case label
    their transparent-separated islands and assign each island by its center.
    This crops full original pixels; it does not synthesize or deform a pose.
    """
    from scipy.ndimage import label, find_objects

    image = remove_background(image, core, policy)
    nominal_cells = core.split_sprite_sheet(image, cols, rows)[:count]
    array = np.array(image.convert("RGBA"))
    labels, total = label(array[..., 3] > 127)
    regions = find_objects(labels)
    assigned = [[] for _ in range(cols * rows)]
    assigned_bounds = [[] for _ in range(cols * rows)]
    cell_width, cell_height = image.width / cols, image.height / rows
    for component, region in enumerate(regions, start=1):
        if region is None:
            continue
        sy, sx = region
        center_x, center_y = (sx.start + sx.stop) / 2, (sy.start + sy.stop) / 2
        column = min(cols - 1, int(center_x / cell_width))
        row = min(rows - 1, int(center_y / cell_height))
        if sx.stop - sx.start > cell_width * 1.8 or sy.stop - sy.start > cell_height * 1.8:
            raise ValueError("Source poses appear connected across grid cells; redraw with transparent separation")
        index = row * cols + column
        assigned[index].append(component)
        assigned_bounds[index].append((sx.start, sy.start, sx.stop, sy.stop))
    frames, frame_bounds = [], []
    for index in range(count):
        bounds = assigned_bounds[index]
        if not bounds:
            raise ValueError(f"Grid cell {index} contains no visible pose")
        left, top = min(b[0] for b in bounds), min(b[1] for b in bounds)
        right, bottom = max(b[2] for b in bounds), max(b[3] for b in bounds)
        crop = array[top:bottom, left:right].copy()
        own_pixels = np.isin(labels[top:bottom, left:right], assigned[index])
        crop[~own_pixels] = 0
        frames.append(Image.fromarray(crop, "RGBA"))
        frame_bounds.append([left, top, right, bottom])
    return frames, {"method": "transparent_component_centers_to_nominal_grid", "components": total,
                    "nominal_cell_dimensions": list(nominal_cells[0].size),
                    "nominal_cell_span": [cell_width, cell_height], "cols": cols, "rows": rows,
                    "frame_bounds": frame_bounds,
                    "source_edge_contacts": [index for index, (left, top, right, bottom) in enumerate(frame_bounds)
                        if left == 0 or top == 0 or right == image.width or bottom == image.height]}


def _require_native_grid_action(action: dict):
    if (not isinstance(action, dict)
            or any(type(action.get(field)) is not int for field in ("cols", "rows", "count"))
            or action.get("loop") is not True
            or (action.get("id"), action.get("cols"), action.get("rows"), action.get("count"),
         action.get("fps"), action.get("loop"), action.get("background"))
            != ("shelter", 3, 2, 6, 4, True, "transparent")
            or "registration" in action):
        raise ValueError("Native grid is only available for six transparent 3x2 shelter poses at 4 FPS without registration")


def validate_native_grid_spec(spec: dict) -> bool:
    """Leave every legacy action unchanged; strictly bound the shelter opt-in."""
    enabled = spec.get("native_grid", False)
    if not isinstance(enabled, bool):
        raise ValueError("native_grid must be a boolean")
    if not enabled:
        return False
    size, native_size = spec.get("size", 128), spec.get("native_grid_size")
    padding = spec.get("canvas_padding", 1.2)
    if (type(size) is not int or size != 128 or type(native_size) is not int or native_size != 128
            or isinstance(padding, bool) or padding != 1.0
            or spec.get("palette_colors", 32) != 32):
        raise ValueError("Native shelter grid requires size=128, native_grid_size=128, canvas_padding=1.0 and 32 colors")
    actions = spec.get("actions")
    if not isinstance(actions, list) or len(actions) != 1:
        raise ValueError("A native-grid spec must contain only the shelter action")
    _require_native_grid_action(actions[0])
    return True


def native_grid_processing() -> dict:
    """Document the actual fixed-grid path, including the second core pass."""
    return {"enabled": True, "native_grid_size": 128, "source_cell_size": 512,
            "source_to_native_factor": 4, "fixed_source_coordinates": True,
            "color_apis": ["_quantize_to_palette_lab", "_majority_downscale"],
            "alpha_threshold": 127, "alpha_resampling": "NEAREST",
            "converter_input": [128, 128], "converter_output": [128, 128],
            "converter_ratio": 1.0, "lanczos": False, "bilateral": False,
            "dither": False, "outline": False, "fixed_palette": True}


def prepare_native_grid(image: Image.Image, action: dict, core, palette: list[tuple[int, int, int]]):
    """Reduce exact original cell coordinates, never individually crop or fit.

    Quantization followed by foreground majority voting is the pinned core's
    real finalization order. Reducing binary alpha with its NEAREST rule keeps
    the same sample location in every source 4x4 block. The second real core
    pass receives native 128px frames, so neither LANCZOS nor bilateral runs.
    """
    _require_native_grid_action(action)
    if image.size != (1536, 1024):
        raise ValueError("Native shelter source must be exactly 1536x1024: six 512px cells (128px grid x4)")
    raw = core.split_sprite_sheet(image, 3, 2)
    require_unique(raw, "shelter", "raw native-grid source")
    native = []
    fixed_palette = np.asarray(palette, dtype=np.uint8)
    for index, frame in enumerate(raw):
        alpha = np.array(frame.convert("RGBA").getchannel("A"), dtype=np.uint8)
        binary = np.where(alpha > 127, 255, 0).astype(np.uint8)
        if not np.any(binary) or np.all(binary):
            raise ValueError(f"Native shelter cell {index} must contain a visible pose and transparent background")
        quantized = core._quantize_to_palette_lab(frame, fixed_palette)
        rgb = core._majority_downscale(quantized, 128, 128, binary)
        reduced = rgb.convert("RGBA")
        reduced.putalpha(Image.fromarray(binary).resize((128, 128), Image.Resampling.NEAREST))
        native.append(normalize_output(reduced, palette, "shelter", index))
    require_unique(native, "shelter", "native 128px source")
    bounds = [[column * 512, row * 512, (column + 1) * 512, (row + 1) * 512]
              for row in range(2) for column in range(3)]
    extraction = {"method": "fixed_native_grid_cells", "source_dimensions": [1536, 1024],
                  "nominal_cell_dimensions": [512, 512], "nominal_cell_span": [512, 512],
                  "cols": 3, "rows": 2, "frame_bounds": bounds,
                  "native_grid_size": 128, "source_to_native_factor": 4,
                  "cropped": False}
    alignment = {"method": "fixed_native_grid_coordinates_no_fit", "source_canvas": [512, 512],
                 "native_canvas": [128, 128], "canvas_padding": 1.0, "baseline": 128 * 0.85,
                 "source_bounds": bounds, "placements": [[0, 0] for _ in raw],
                 "preserves_vertical_motion": True, "source_grid_preserved": True,
                 "stretched": False, "per_frame_scaling": False, "cropped": False}
    return raw, native, extraction, alignment


def contact_sheet(action_frames: dict[str, list[Image.Image]], path: Path):
    columns = max(len(frames) for frames in action_frames.values())
    width, row_height = columns * 136 + 112, 152
    sheet = Image.new("RGB", (width, len(action_frames) * row_height), (241, 244, 226))
    draw = ImageDraw.Draw(sheet)
    for row, (action_id, frames) in enumerate(action_frames.items()):
        y = row * row_height
        draw.text((10, y + 16), action_id, fill=(38, 63, 39))
        for index, frame in enumerate(frames):
            x = 112 + index * 136
            for by in range(0, 128, 8):
                for bx in range(0, 128, 8):
                    color = (218, 228, 202) if (bx // 8 + by // 8) % 2 else (244, 247, 234)
                    draw.rectangle((x + bx, y + by, x + bx + 7, y + by + 7), fill=color)
            preview = frame.resize((128, 128), Image.Resampling.NEAREST)
            sheet.paste(preview, (x, y), preview)
            draw.text((x + 3, y + 132), f"{index:02d}", fill=(38, 63, 39))
    sheet.save(path)


def build_palette(reference: Image.Image, core) -> list[tuple[int, int, int]]:
    prepared = remove_background(reference, core, "auto")
    array = np.array(prepared.convert("RGBA"))
    pixels = array[array[..., 3] > 127, :3]
    # Deterministic sampling caps memory, without adding background pixels or
    # requiring the optional upstream K-means / scikit-learn branch.
    if len(pixels) > 240_000:
        pixels = pixels[::math.ceil(len(pixels) / 240_000)]
    strip = Image.fromarray(pixels.reshape(1, -1, 3), "RGB")
    quantized = strip.quantize(colors=24, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    flat_palette = quantized.getpalette()
    counts = sorted(quantized.getcolors() or [], reverse=True)
    role_colors = [tuple(flat_palette[index * 3:index * 3 + 3]) for _count, index in counts]
    if len(role_colors) != 24 or len(set(role_colors + ACCENTS)) != 32:
        raise ValueError("Reference cannot form a 24-color character palette plus 8 unique effect colors")
    return role_colors + ACCENTS


def normalize_output(frame: Image.Image, palette: list[tuple[int, int, int]], action_id: str, index: int) -> Image.Image:
    array = np.array(frame.convert("RGBA"), dtype=np.uint8)
    array[..., 3] = np.where(array[..., 3] > 127, 255, 0).astype(np.uint8)
    array[array[..., 3] == 0, :3] = 0
    visible = array[array[..., 3] > 127, :3]
    if not len(visible):
        raise ValueError(f"{action_id} frame {index}: converter returned an empty pose")
    actual_colors = {tuple(int(value) for value in color) for color in visible}
    if not actual_colors.issubset(set(palette)):
        raise ValueError(f"{action_id} frame {index}: output includes a color outside the shared palette")
    if set(np.unique(array[..., 3])) - {0, 255}:
        raise ValueError(f"{action_id} frame {index}: output alpha is not binary")
    return Image.fromarray(array, "RGBA")


def foot_anchor(frame: Image.Image) -> dict:
    """Measure the feet without resizing the pose or including detached effects.

    The largest alpha island contains the character body. Its right half omits
    the coiled tail on the left; the bottom opaque row in that half marks the
    foot support line. Renderers may translate to this line while retaining the
    common pixel scale and the full, unmodified converted frame.
    """
    from scipy.ndimage import label

    mask = np.array(frame.convert("RGBA"))[..., 3] > 127
    labels, count = label(mask, structure=np.ones((3, 3), dtype=np.uint8))
    areas = np.bincount(labels.ravel())
    areas[0] = 0
    body = labels == int(np.argmax(areas))
    body_y, body_x = np.nonzero(body)
    if not count or not len(body_x):
        raise ValueError("Cannot measure a foot anchor for an empty frame")
    left, right = int(body_x.min()), int(body_x.max()) + 1
    split_x = (left + right) / 2
    foot_mask = body & (np.arange(frame.width)[None, :] >= split_x)
    foot_y, foot_x = np.nonzero(foot_mask)
    bottom_row = int(foot_y.max())
    support_x = foot_x[foot_y == bottom_row]
    center_x = float(np.median(support_x)) + 0.5
    line_y = bottom_row + 1
    return {"x": round(center_x / frame.width, 6), "y": round(line_y / frame.height, 6),
            "pixel_x": round(center_x, 3), "pixel_y": line_y,
            "body_bounds": [left, int(body_y.min()), right, int(body_y.max()) + 1]}


def checked_int(value, label: str, low: int, high: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ValueError(f"{label} must be an integer from {low} to {high}")
    return value


def register_sleep_frames(frames: list[Image.Image]) -> tuple[list[Image.Image], dict]:
    """Remove AI grid-placement drift using integer translation only.

    The body silhouette and every original pixel remain intact. A sleeping
    cycle uses the nose's rightmost column and the right-half support row as
    fixed anchors; the redrawn back contour is free to breathe between them.
    """
    anchors = [foot_anchor(frame) for frame in frames]
    target_right = anchors[0]["body_bounds"][2]
    target_support = anchors[0]["pixel_y"]
    registered, shifts = [], []
    for frame, anchor in zip(frames, anchors):
        dx = target_right - anchor["body_bounds"][2]
        dy = target_support - anchor["pixel_y"]
        left, top, right, bottom = frame.getbbox()
        if min(left + dx, top + dy) < 0 or right + dx > frame.width or bottom + dy > frame.height:
            raise ValueError("Registered sleeping frame would crop visible pixels; redraw with more padding")
        aligned = Image.new("RGBA", frame.size, (0, 0, 0, 0))
        aligned.paste(frame, (dx, dy))
        registered.append(aligned)
        shifts.append([dx, dy])
    return registered, {"method": "integer_translation_nose_and_support", "translations": shifts,
                        "scaling": False, "rotation": False, "pose_pixels_modified": False}


def normalized_point(value, label: str, allow_none: bool = False) -> dict | None:
    if value is None and allow_none:
        return None
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object with normalized x and y coordinates")
    point = {}
    for coordinate in ("x", "y"):
        number = value.get(coordinate)
        if (not isinstance(number, (int, float)) or isinstance(number, bool)
                or not 0 <= number <= 1 or not math.isfinite(number)):
            raise ValueError(f"{label}.{coordinate} must be a finite number from 0 to 1")
        point[coordinate] = number
    return point


def mouth_metadata(action: dict, count: int, action_id: str) -> dict:
    """Copy only validated, manually calibrated mouth markers into output."""
    metadata = {}
    if "mouth" in action:
        metadata["mouth"] = normalized_point(action["mouth"], f"{action_id}.mouth", allow_none=True)
    if "mouths" in action:
        mouths = action["mouths"]
        if not isinstance(mouths, list) or len(mouths) != count:
            raise ValueError(f"{action_id}.mouths must contain exactly {count} entries")
        metadata["mouths"] = [normalized_point(point, f"{action_id}.mouths[{index}]", allow_none=True)
                              for index, point in enumerate(mouths)]
    return metadata


def convert(spec_path: Path, output: Path, selected: set[str] | None, dry_inspect: bool = False, allow_missing: bool = False):
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    core, provenance = load_core()
    size = checked_int(spec.get("size", 128), "size", 128, 128)
    native_grid = validate_native_grid_spec(spec)
    padding = spec.get("canvas_padding", 1.2)
    if not native_grid and (not isinstance(padding, (int, float)) or isinstance(padding, bool) or not math.isfinite(padding) or not 1.1 <= padding <= 2):
        raise ValueError("canvas_padding must be from 1.1 to 2.0 and is shared across all actions")
    if spec.get("palette_colors", 32) != 32:
        raise ValueError("This project uses one shared 32-color palette")
    reference_path = (spec_path.parent / spec["reference"]).resolve()
    with Image.open(reference_path) as reference:
        palette = build_palette(reference, core)
    actions = spec.get("actions")
    if not isinstance(actions, list) or not actions:
        raise ValueError("spec.actions must be a non-empty array")
    action_ids = [action.get("id") for action in actions]
    if len(set(action_ids)) != len(action_ids):
        raise ValueError("Action IDs must be unique")
    if selected and not selected.issubset(set(action_ids)):
        raise ValueError(f"Unknown selected actions: {selected - set(action_ids)}")
    preview_assets = ROOT / "preview/assets"
    try:
        output_relative = output.resolve().relative_to(preview_assets).as_posix()
    except ValueError:
        output_relative = output.name
    manifest = {
        "version": 1,
        "converter": {**provenance, "core_sha256": file_hash(VENDOR / "core.py"), "api": "make_pixel_art_animation"},
        "processing": {"width": size, "height": size, "palette_colors": 32, "reference_colors": 24,
                       "effect_colors": 8, "dither": False, "outline": False, "remove_bg_ai": False,
                       "alpha": [0, 255], "pose_generation": False, "frame_duplication": False,
                       "foot_anchor_method": "largest_8_connected_alpha_island_right_half_bottom_row"},
        "runtime": {"python": sys.version.split()[0], "Pillow": Image.__version__, "numpy": np.__version__,
                    "scikit_image": skimage.__version__, "scipy": scipy.__version__},
        "reference": {"file": reference_path.relative_to(ROOT).as_posix() if reference_path.is_relative_to(ROOT) else str(reference_path),
                      "sha256": file_hash(reference_path)},
        "palette": [list(color) for color in palette],
        "actions": {},
    }
    if native_grid:
        manifest["processing"]["native_grid"] = native_grid_processing()
    # Stage a complete valid conversion before replacing any output files.
    output.mkdir(parents=True, exist_ok=True)
    if allow_missing and not dry_inspect:
        raise ValueError("--allow-missing is only available with --dry-inspect")
    prepared_actions, converted_actions, inspection = {}, {}, {}
    prior = None
    if selected and not dry_inspect and (output / "manifest.json").exists():
        prior = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
        if prior.get("palette") != manifest["palette"]:
            raise ValueError("Shared palette changed; rebuild all actions instead of --only")
    with tempfile.TemporaryDirectory(prefix="pet-frames-", dir=output.parent) as temporary:
        stage = Path(temporary)
        for action in actions:
            action_id = action["id"]
            if not isinstance(action_id, str) or not re.fullmatch(r"[a-z][a-z0-9_-]{0,31}", action_id):
                raise ValueError(f"Invalid action ID: {action_id!r}")
            if selected and action_id not in selected:
                continue
            cols = checked_int(action["cols"], f"{action_id}.cols", 1, 16)
            rows = checked_int(action["rows"], f"{action_id}.rows", 1, 16)
            count = checked_int(action["count"], f"{action_id}.count", 2, cols * rows)
            calibrated_mouth = mouth_metadata(action, count, action_id)
            fps = action["fps"]
            if not isinstance(fps, (int, float)) or isinstance(fps, bool) or not math.isfinite(fps) or not 1 <= fps <= 30:
                raise ValueError(f"{action_id}.fps must be from 1 to 30")
            source_path = (spec_path.parent / action["file"]).resolve()
            if not source_path.exists() and allow_missing:
                inspection[action_id] = {"status": "source_missing", "file": str(source_path)}
                continue
            policy = action.get("background", "auto")
            if policy not in {"auto", "transparent", "edge_color"}:
                raise ValueError(f"{action_id}: unsupported background policy")
            with Image.open(source_path) as image:
                source_dimensions = list(image.size)
                if native_grid:
                    raw_cells, cells, extraction, alignment = prepare_native_grid(image, action, core, palette)
                else:
                    cells, extraction = split_source(image, cols, rows, count, core, policy)
            if not native_grid:
                require_unique(cells, action_id, "source")
                cells, alignment = align_frames(cells, extraction, padding)
            require_unique(cells, action_id, "aligned source")
            prepared_actions[action_id] = cells
            inspection[action_id] = {"status": "inspected", "source_dimensions": source_dimensions,
                "cols": cols, "rows": rows, "count": count, "source_alpha_present": True,
                **calibrated_mouth,
                "alignment": alignment, "extraction": extraction,
                "distinct_source_frames": len({opaque_hash(frame) for frame in cells})}
            if native_grid:
                inspection[action_id]["native_grid"] = manifest["processing"]["native_grid"]
                inspection[action_id]["raw_rgba_sha256"] = [opaque_hash(frame) for frame in raw_cells]
                inspection[action_id]["aligned_rgba_sha256"] = [opaque_hash(frame) for frame in cells]
            if dry_inspect:
                print(f"Inspected {action_id}: {count} distinct poses; shared source canvas {alignment['source_canvas']}", flush=True)
                continue
            print(f"Converting {action_id}: {count} independent poses, {fps} FPS", flush=True)
            converted = core.make_pixel_art_animation(cells, size, size, 32, False, False, False, fixed_palette=palette)
            converted = [normalize_output(frame, palette, action_id, index) for index, frame in enumerate(converted)]
            if native_grid and [opaque_hash(frame) for frame in converted] != [opaque_hash(frame) for frame in cells]:
                raise ValueError("Native-grid converter changed original 128px coordinates, colors or alpha")
            registration = action.get("registration")
            if registration:
                if action_id != "sleep" or registration != "nose_and_support":
                    raise ValueError("Only sleep supports the nose_and_support registration policy")
                converted, registration_metadata = register_sleep_frames(converted)
                alignment["output_registration"] = registration_metadata
            require_unique(converted, action_id, "converted")
            converted_actions[action_id] = converted
            action_dir = stage / action_id
            action_dir.mkdir()
            files, hashes, png_hashes = [], [], []
            for index, frame in enumerate(converted):
                filename = f"{action_id}_{index:02d}.png"
                frame_path = action_dir / filename
                frame.save(frame_path)
                files.append(f"{output_relative}/{action_id}/{filename}")
                hashes.append(opaque_hash(frame))
                png_hashes.append(file_hash(frame_path))
            sheet_path = stage / f"{action_id}.png"
            core.compose_sprite_sheet(converted).save(sheet_path)
            manifest["actions"][action_id] = {
                "count": count, "fps": fps, "loop": bool(action.get("loop", True)),
                "width": size, "height": size, "pivot": {"x": 0.5, "y": 0.85},
                "foot_anchors": [foot_anchor(frame) for frame in converted],
                **calibrated_mouth,
                "sheet": f"{output_relative}/{action_id}.png", "frames": files,
                "hashes": hashes, "png_sha256": png_hashes, "sheet_sha256": file_hash(sheet_path),
                "source": source_path.relative_to(ROOT).as_posix() if source_path.is_relative_to(ROOT) else str(source_path),
                "source_sha256": file_hash(source_path), "source_grid": {"cols": cols, "rows": rows},
                "distinct_frames": len(set(hashes)), "background": policy,
                "alignment": alignment,
                "extraction": extraction,
            }
            if native_grid:
                manifest["actions"][action_id]["native_grid"] = {
                    **native_grid_processing(),
                    "raw_rgba_sha256": [opaque_hash(frame) for frame in raw_cells],
                    "aligned_rgba_sha256": [opaque_hash(frame) for frame in cells],
                    "output_rgba_sha256": hashes,
                }
        if not prepared_actions:
            raise ValueError("No source action images are available to inspect or convert")
        if dry_inspect:
            contact_sheet(prepared_actions, output / "contact-source.png")
            (output / "inspection.json").write_text(json.dumps(inspection, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            print(f"Inspection only; no converted frames written. Contact sheet: {output / 'contact-source.png'}", flush=True)
            return inspection
        generated_ids = list(manifest["actions"])
        if prior:
            manifest["actions"] = {**prior.get("actions", {}), **manifest["actions"]}
            # A selective rebuild still publishes a contact sheet for the full
            # manifest, so unchanged actions remain inspectable together.
            for action_id, action in manifest["actions"].items():
                if action_id in converted_actions:
                    continue
                retained = []
                for frame_file in action["frames"]:
                    with Image.open(output / action_id / Path(frame_file).name) as frame:
                        retained.append(frame.convert("RGBA"))
                converted_actions[action_id] = retained
            converted_actions = {action_id: converted_actions[action_id] for action_id in manifest["actions"]}
        # Validate and read all retained frames before replacing staged files.
        for action_id in generated_ids:
            final_dir = output / action_id
            final_dir.mkdir(exist_ok=True)
            for frame_path in (stage / action_id).iterdir():
                frame_path.replace(final_dir / frame_path.name)
            (stage / f"{action_id}.png").replace(output / f"{action_id}.png")
        manifest["total_frames"] = sum(action["count"] for action in manifest["actions"].values())
        staged_manifest = stage / "manifest.json"
        staged_manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        staged_manifest.replace(output / "manifest.json")
        contact_sheet(converted_actions, output / "contact-sheet.png")
    print(f"Saved {len(manifest['actions'])} actions / {manifest['total_frames']} frames to {output}", flush=True)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", type=Path, default=ROOT / "preview/assets/animation-source/spec.json")
    parser.add_argument("--output", type=Path, default=ROOT / "preview/assets/frames")
    parser.add_argument("--only", help="Comma-separated action IDs (keeps other existing outputs)")
    parser.add_argument("--dry-inspect", action="store_true", help="Validate source cells and alignment, then write a source contact sheet without conversion")
    parser.add_argument("--allow-missing", action="store_true", help="With --dry-inspect, report missing source actions instead of failing")
    args = parser.parse_args()
    selected = set(args.only.split(",")) if args.only else None
    try:
        convert(args.spec.resolve(), args.output.resolve(), selected, args.dry_inspect, args.allow_missing)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"Frame conversion failed: {error}\n")


if __name__ == "__main__":
    main()
