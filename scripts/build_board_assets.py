#!/usr/bin/env python3
"""Package approved pixel frames and board-only item art as LVGL 9 Flash assets.

No poses are created here: sprites come from the exact 128 px PNG emitted by the
project's real PixelArtConverter workflow. The teen's detached pet hearts are
audited and extracted only for the board's single live heart overlay; the body
and original PNG remain unchanged. RGB is quantized once to RGB565;
binary transparency is stored in LVGL's separate A8 plane. Backgrounds retain
the 128 px pixel grid, expanded to the native 466 px screen with nearest-neighbor
sampling at build time so partial redraws never resample a clipped scaled image.

Shelter-source audits reuse the pinned PixelArtConverter inspection helpers.
Use the same Python 3.12 interpreter as build_pet_frames.py with the existing
.tools/pixel-art-deps; the system macOS Python 3.9 cannot load those extensions.
On this workspace the ready interpreter is:
python3
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unicodedata
import sys

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from board_pet_effects import strip_teen_pet_hearts

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "preview/assets"
ACTIONS = (
    "idle", "happy", "eat", "sleep", "walk", "blink", "stretch", "wave",
    "dizzy", "angry", "sick", "bath", "pet", "grip", "shelter",
)
ACTIVE_ACTIONS = tuple(action for action in ACTIONS if action != "grip")
STAGES = ("baby", "teen", "adult")
STAGE_FRAME_COUNT = 82
FRAME_COUNT = STAGE_FRAME_COUNT * len(STAGES)
PROFILE = ROOT / "main/chameleon_asset_profile.json"
OVERRIDE_RULES = {
    "sick": {"stages": STAGES, "folder": "sick-redesign", "count": 4, "fps": 2},
    "shelter": {"stages": ("baby", "adult"), "folder": "shelter-redesign", "count": 6, "fps": 4},
}
BACKGROUNDS = ("forest", "clearing", "night", "meadow", "meadow-night")
SIZE = 128
BACKGROUND_SIZE = 466
BOARD_ITEMS = ROOT / "main/assets/items_manifest.json"
ITEM_SIZE = 32
EXTRA_FONT_TEXT = (
    "变色龙森林蘑菇树枝抓紧松开左右倾斜放平举叶避雨自主生活昼夜天气"
    "清晨白天黄昏黑夜晴多云雾小大暴雨雷阵雪温度湿度城市联网离线未知"
    "蟋蟀蟑螂面包虫苍蝇捕食舌头张嘴观察慢走伸懒腰眨眼挥手洗澡抚摸"
    "状态健康饱食精力心情清洁背包相机照片图鉴设置声音亮度屏幕休眠"
    "充电电量时间日期自动校准同步网络连接断开配置重置继续取消保存"
    "暂无真实天气请连接无线网络获取天气数据设备忙碌本地缓存等待检测"
    "金币购买库存药水营养粉香皂治疗恢复玩耍追逐点击命中奖励结束"
    "返回主页菜单成功失败测量小时分钟秒今天昨天上午下午星期一二三四五六日"
    "选择场景枝头林间草地晴空日夜森林夜色名字作者关于帮助固件版本照片删除"
    "当前支持升级尚未开放模拟照片截图保留睡觉醒来早安晚安困了饿了累了不舒服"
    "·，。：！？℃°→←…（）％×‹›、"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def rgb565(image: Image.Image, alpha: bool = False) -> bytes:
    """Return little-endian RGB565 rows followed by a separate binary A8 plane."""
    rgba = image.convert("RGBA")
    output = bytearray()
    mask = bytearray()
    pixels = rgba.tobytes()
    for r, g, b, a in zip(pixels[0::4], pixels[1::4], pixels[2::4], pixels[3::4]):
        if alpha and a not in (0, 255):
            raise ValueError("Sprite transparency must be binary; do not blur poses")
        if alpha and a == 0:
            r = g = b = 0
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        output.extend((value & 255, value >> 8))
        if alpha:
            mask.append(a)
    output.extend(mask)
    return bytes(output)


def opaque_rgba(image: Image.Image) -> bytes:
    output = bytearray()
    pixels = image.convert("RGBA").tobytes()
    for r, g, b, a in zip(pixels[0::4], pixels[1::4], pixels[2::4], pixels[3::4]):
        output.extend((r, g, b, 255) if a > 127 else (0, 0, 0, 0))
    return bytes(output)


def float_array(name: str, values: list[float]) -> str:
    return f"static const float {name}[] = {{ " + ", ".join(f"{v:.6f}f" for v in values) + " };\n"


def emit_image(name: str, data: bytes, alpha: bool, size: int = SIZE) -> str:
    # Keep the literal's trailing C terminator for GCC 15's strict string
    # initializer check. It is not part of LVGL's exact image data_size.
    rows = [f'static const uint8_t {name}_data[{len(data) + 1}] __attribute__((aligned(4))) =\n']
    rows.extend('    "' + "".join(f"\\x{b:02x}" for b in data[i:i + 64]) + '"\n'
                for i in range(0, len(data), 64))
    rows.append(";\n")
    rows.append(f"static const lv_image_dsc_t {name} = {{\n"
                "    .header = { .magic = LV_IMAGE_HEADER_MAGIC, "
                f".cf = LV_COLOR_FORMAT_{'RGB565A8' if alpha else 'RGB565'}, "
                f".flags = 0, .w = {size}, .h = {size}, .stride = {size * 2}, .reserved_2 = 0 }},\n"
                f"    .data_size = {len(data)}, .data = {name}_data,\n"
                "    .reserved = NULL, .reserved_2 = NULL\n};\n\n")
    return "".join(rows)


def header_source() -> str:
    enums = ", ".join("CH_" + action.upper() for action in ACTIONS)
    stage = selected_stage()
    pet_size = round(250 * stage["render_scale"])
    widget_pet_size = round(185 * stage["render_scale"])
    return f'''// Generated by scripts/build_board_assets.py. Approved stage poses plus audited board overrides.
#pragma once
#include <stdint.h>
#include <lvgl.h>
#ifdef __cplusplus
extern "C" {{
#endif

typedef enum {{ {enums}, CH_ANIM_COUNT }} ch_anim_t;
typedef enum {{ CH_STAGE_BABY = 0, CH_STAGE_TEEN = 1, CH_STAGE_ADULT = 2, CH_STAGE_COUNT = 3 }} ch_stage_t;
enum {{ CH_FRAME_SIZE = 128, CH_FRAME_COUNT = {FRAME_COUNT}, CH_STAGE_FRAME_COUNT = {STAGE_FRAME_COUNT}, CH_ACTIVE_ANIM_COUNT = {len(ACTIVE_ACTIONS)}, CH_BACKGROUND_COUNT = 5, CH_BACKGROUND_SIZE = 466 }};
enum {{ CH_DEFAULT_STAGE = CH_STAGE_{stage['id'].upper()} }};
enum {{ CH_PET_SIZE = {pet_size}, CH_WIDGET_PET_SIZE = {widget_pet_size} }};
enum {{ CH_POOP_SIZE = {ITEM_SIZE} }};
typedef struct {{
    const lv_image_dsc_t * const *frames;
    uint8_t count;
    uint8_t fps;
    uint8_t loop;
    const float *foot_y;
    const float *mouth_x;
    const float *mouth_y;
}} ch_animation_t;
typedef struct {{
    const ch_animation_t *animations;
    uint16_t pet_size;
    uint16_t widget_size;
    const char *id;
    const char *label;
}} ch_stage_assets_t;

// Animations follow ch_anim_t; backgrounds: forest, clearing, night, meadow,
// meadow-night. Anchors are normalized in each unscaled 128 x 128 source frame.
// A mouth coordinate of -1 means the mouth is not visible in that frame.
// The legacy animation array is the baby stage; ch_stages references the same
// array rather than duplicating frame data. Each stage has its own 82 drawings.
extern const ch_animation_t ch_animations[CH_ANIM_COUNT];
extern const ch_stage_assets_t ch_stages[CH_STAGE_COUNT];
extern const lv_image_dsc_t * const ch_backgrounds[CH_BACKGROUND_COUNT];
// Board-only tangible droppings sprite; desktop assets remain unchanged.
extern const lv_image_dsc_t * const ch_poop;
// GRIP retains its ABI slot but has no frames; removed tilt actions stay disabled.
// This is the profile's default identity. Runtime growth selects ch_stages.
const char *ch_asset_stage(void);
LV_FONT_DECLARE(chameleon_font_18);

#ifdef __cplusplus
}}
#endif
'''


def approved_stages() -> tuple[dict, ...]:
    """Package every identity in the stable model-stage order without fallbacks."""
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))
    if (profile.get("version") != 1 or set(profile) - {"version", "default_stage", "action_overrides"}
            or profile["default_stage"] not in STAGES):
        raise ValueError("Board profile must select an approved default stage")
    overrides = profile.get("action_overrides", {})
    if not isinstance(overrides, dict) or set(overrides) - set(STAGES):
        raise ValueError("Board overrides must name approved stages")
    for stage_id, actions in overrides.items():
        if not isinstance(actions, dict) or not actions or set(actions) - set(OVERRIDE_RULES):
            raise ValueError("Only approved board sick and shelter drawings may override desktop poses")
        for action_id, relative in actions.items():
            rule = OVERRIDE_RULES[action_id]
            if stage_id not in rule["stages"]:
                raise ValueError("The approved teen shelter drawings must remain unchanged")
            expected = f"main/assets/{rule['folder']}/{stage_id}/frames/manifest.json"
            if relative != expected:
                raise ValueError("Board overrides must stay in their own stage and action folder")
    catalog = json.loads((ASSETS / "growth/stages.json").read_text(encoding="utf-8"))
    if (catalog.get("version") != 1 or len(catalog["stages"]) != len(STAGES)
            or {s["id"] for s in catalog["stages"]} != set(STAGES)):
        raise ValueError("All three board stages must exist exactly once in the approved catalog")
    stages = tuple(next(s for s in catalog["stages"] if s["id"] == stage_id) for stage_id in STAGES)
    for stage in stages:
        for field in ("manifest", "reference"):
            if not (ASSETS / stage[field]).resolve().is_relative_to(ASSETS.resolve()):
                raise ValueError(f"Stage {field} escapes the approved asset folder")
        if not 0 < stage["render_scale"] <= 2:
            raise ValueError("Stage render scale must fit the native board display")
    return stages


def selected_stage() -> dict:
    default = json.loads(PROFILE.read_text(encoding="utf-8"))["default_stage"]
    return next(stage for stage in approved_stages() if stage["id"] == default)


def action_source(stage: dict, desktop: dict, action: str) -> tuple[dict, Path, Path, dict | None]:
    """Resolve one board-only replacement without packing a second pose set."""
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))
    relative = profile.get("action_overrides", {}).get(stage["id"], {}).get(action)
    if relative is None:
        return desktop["actions"][action], ASSETS, (ASSETS / stage["manifest"]).parent.resolve(), None
    rule = OVERRIDE_RULES.get(action)
    if (rule is None or stage["id"] not in rule["stages"]
            or relative != f"main/assets/{rule['folder']}/{stage['id']}/frames/manifest.json"):
        raise ValueError("Unapproved board action override")
    count, fps = rule["count"], rule["fps"]
    path = ROOT / relative
    manifest = json.loads(path.read_text(encoding="utf-8"))
    reference = ASSETS / stage["reference"]
    expected_reference = {"file": str(reference.relative_to(ROOT)), "sha256": sha256(reference.read_bytes())}
    if (manifest.get("version") != 1 or set(manifest.get("actions", {})) != {action}
            or manifest.get("reference") != expected_reference
            or manifest.get("palette") != desktop.get("palette")
            or manifest.get("converter") != desktop.get("converter")
            or manifest.get("total_frames") != count):
        raise ValueError("Override must use its stage's unchanged reference and fixed converter palette")
    entry = manifest["actions"][action]
    if (entry["count"], entry["fps"], entry["loop"]) != (count, fps, True):
        raise ValueError(f"Board {action} drawings must remain {count} distinct looping {fps} FPS frames")
    if entry["distinct_frames"] != count or len(set(entry["hashes"])) != count:
        raise ValueError("Override poses must be independently drawn and distinct")
    provenance_path = path.parent.parent / "source/generation.json"
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    if (provenance.get("generator") != "built-in image_gen" or provenance.get("stage") != stage["id"]
            or provenance.get("reference") != expected_reference or provenance.get("pose_manufacture") is not False):
        raise ValueError("Override source identity and generation provenance must match")
    for field in ("prompt", "source", "spec"):
        record = provenance[field]
        checked = (ROOT / record["file"]).resolve()
        if (not checked.is_relative_to((path.parent.parent / "source").resolve())
                or sha256(checked.read_bytes()) != record["sha256"]):
            raise ValueError("Override source prompt, sheet or spec changed without provenance update")
    if (entry["source"] != provenance["source"]["file"]
            or entry["source_sha256"] != provenance["source"]["sha256"]):
        raise ValueError("Converted frames must originate from the recorded generated sheet")
    audit = {"source_manifest": relative, "source_manifest_sha256": sha256(path.read_bytes()),
             "source_sheet": entry["source"], "source_sheet_sha256": entry["source_sha256"],
             "generation_record": str(provenance_path.relative_to(ROOT)),
             "generation_record_sha256": sha256(provenance_path.read_bytes()),
             "reference": expected_reference, "converter": manifest["converter"],
             "replaces_action_only": action, "extra_frames_packed": 0}
    if action == "shelter":
        audit["source_pose_audit"] = shelter_source_pose_audit(entry, manifest, provenance, expected_reference)
    return entry, path.parent.parent, path.parent.resolve(), audit


def shelter_source_pose_audit(entry: dict, manifest: dict, provenance: dict, reference: dict) -> dict:
    """Recheck real source poses without converting or modifying any pixels."""
    import build_pet_frames as converter

    spec_path = ROOT / provenance["spec"]["file"]
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    native_grid = converter.validate_native_grid_spec(spec)
    actions = spec.get("actions", [])
    if (provenance.get("independently_drawn_poses") != 6 or len(actions) != 1
            or spec.get("size") != SIZE or spec.get("palette_colors") != 32
            or (spec_path.parent / spec["reference"]).resolve() != (ROOT / reference["file"]).resolve()):
        raise ValueError("Shelter must use six independent poses and its original 128px/32-color identity")
    action = actions[0]
    source_path = ROOT / provenance["source"]["file"]
    if ((action.get("id"), action.get("cols"), action.get("rows"), action.get("count"),
         action.get("fps"), action.get("loop"), action.get("background"))
            != ("shelter", 3, 2, 6, 4, True, "transparent")
            or action.get("registration")
            or (spec_path.parent / action["file"]).resolve() != source_path.resolve()
            or entry.get("source_grid") != {"cols": 3, "rows": 2}
            or entry.get("background") != "transparent"):
        raise ValueError("Shelter conversion must retain the six original transparent 3x2 source poses")
    processing = manifest.get("processing", {})
    if any(processing.get(key) != value for key, value in
           {"width": SIZE, "height": SIZE, "palette_colors": 32, "alpha": [0, 255],
            "pose_generation": False, "frame_duplication": False}.items()):
        raise ValueError("Shelter conversion must retain binary alpha and never manufacture poses")
    if native_grid:
        if (processing.get("native_grid") != converter.native_grid_processing()
                or any(processing.get(flag) is not False for flag in ("dither", "outline", "remove_bg_ai"))):
            raise ValueError("Native shelter must record its fixed original grid and unfiltered converter path")
    elif "native_grid" in processing or "native_grid" in entry:
        raise ValueError("Shelter native-grid metadata requires an explicit source-spec opt-in")
    core, _ = converter.load_core()
    with Image.open(source_path) as source:
        if native_grid:
            palette = [tuple(color) for color in manifest["palette"]]
            cells, aligned, extraction, alignment = converter.prepare_native_grid(source, action, core, palette)
        else:
            cells, extraction = converter.split_source(source, 3, 2, 6, core, "transparent")
    converter.require_unique(cells, "shelter", "raw source")
    raw_hashes = [converter.opaque_hash(cell) for cell in cells]
    if not native_grid:
        aligned, alignment = converter.align_frames(cells, extraction, spec.get("canvas_padding", 1.2))
    converter.require_unique(aligned, "shelter", "aligned source")
    aligned_hashes = [converter.opaque_hash(cell) for cell in aligned]
    if extraction != entry.get("extraction") or alignment != entry.get("alignment"):
        raise ValueError("Shelter alignment/extraction changed from the real converter's recorded source")
    audit = {"raw_rgba_sha256": raw_hashes,
            "aligned_rgba_sha256": aligned_hashes,
            "output_rgba_sha256": entry["hashes"], "source_grid": {"cols": 3, "rows": 2},
            "count": 6, "stretched": False, "per_frame_scaling": False}
    if native_grid:
        converted = core.make_pixel_art_animation(aligned, SIZE, SIZE, 32, False, False, False, fixed_palette=palette)
        output_hashes = [converter.opaque_hash(converter.normalize_output(frame, palette, "shelter", index))
                         for index, frame in enumerate(converted)]
        expected = {**converter.native_grid_processing(), "raw_rgba_sha256": raw_hashes,
                    "aligned_rgba_sha256": aligned_hashes, "output_rgba_sha256": output_hashes}
        if output_hashes != aligned_hashes or entry["hashes"] != output_hashes or entry.get("native_grid") != expected:
            raise ValueError("Native shelter source, original 128px grid or output provenance no longer matches")
        audit["native_grid"] = expected
    return audit


def build_resources() -> tuple[str, dict]:
    stages = approved_stages()
    default_stage = selected_stage()
    source = ["// Generated by scripts/build_board_assets.py; do not hand-edit.\n",
              "// Constant RGB565 rows + separate A8 plane; no pose deformation.\n",
              '#include "chameleon_assets.h"\n\n',
              f'const char *ch_asset_stage(void) {{ return "{default_stage["id"]}"; }}\n\n']
    audit = {"version": 3, "default_stage": default_stage["id"],
             "stage_order": list(STAGES), "stages": {},
             "profile": str(PROFILE.relative_to(ROOT)),
             "profile_sha256": sha256(PROFILE.read_bytes()),
             "inactive_actions": ["grip"],
             "format": "LVGL9 RGB565A8 little-endian color plane then binary alpha plane",
             "width": SIZE, "height": SIZE, "stride": SIZE * 2,
             "backgrounds": [], "items": [], "sprite_bytes": 0,
             "background_bytes": 0, "item_bytes": 0, "resource_bytes": 0}
    global_frame_count = 0
    stage_rows = []
    for stage in stages:
        source_path = ASSETS / stage["manifest"]
        manifest = json.loads(source_path.read_text(encoding="utf-8"))
        if manifest["version"] != 1 or set(manifest["actions"]) not in (set(ACTIVE_ACTIONS), set(ACTIONS)):
            raise ValueError("Stage manifest must contain every approved active action")
        reference = ASSETS / stage["reference"]
        approved_reference = "preview/assets/" + stage["reference"]
        reference_hash = sha256(reference.read_bytes())
        if manifest.get("reference") != {"file": approved_reference, "sha256": reference_hash}:
            raise ValueError("Stage manifest must use its own approved reference image")
        stage_audit = {"stage": stage["id"], "label": stage["label"],
                       "render_scale": stage["render_scale"],
                       "pet_size": round(250 * stage["render_scale"]),
                       "widget_size": round(185 * stage["render_scale"]),
                       "reference": {"file": approved_reference, "sha256": reference_hash},
                       "source_manifest": str(source_path.relative_to(ROOT)),
                       "source_manifest_sha256": sha256(source_path.read_bytes()),
                       "converter": manifest["converter"], "actions": {}, "sprite_bytes": 0}
        action_rows = []
        stage_frame_count = 0
        for action in ACTIONS:
            if action not in ACTIVE_ACTIONS:
                action_rows.append(f"    [CH_{action.upper()}] = {{ NULL, 0, 0, 0, NULL, NULL, NULL }},\n")
                continue
            entry, action_base, action_directory, override_audit = action_source(stage, manifest, action)
            count = entry["count"]
            if ((entry["width"], entry["height"]) != (SIZE, SIZE)
                    or count <= 0 or count != len(entry["frames"])
                    or len(entry["hashes"]) != count or len(entry["png_sha256"]) != count):
                raise ValueError(f"Invalid frame size, count or hashes: {stage['id']}/{action}")
            foot = entry["foot_anchors"]
            if len(foot) != count:
                raise ValueError(f"Missing per-frame foot anchors: {stage['id']}/{action}")
            mouths = entry.get("mouths", [entry.get("mouth")] * count)
            if len(mouths) != count:
                raise ValueError(f"Invalid per-frame mouth anchors: {stage['id']}/{action}")
            items = []
            frame_names = []
            prefix = f"ch_{stage['id']}_{action}"
            for index, relative_path in enumerate(entry["frames"]):
                path = (action_base / relative_path).resolve()
                if not path.is_relative_to(action_directory):
                    raise ValueError("A stage may only use its own approved frame directory")
                png = path.read_bytes()
                if sha256(png) != entry["png_sha256"][index]:
                    raise ValueError(f"Source PNG changed without manifest update: {relative_path}")
                with Image.open(path) as image:
                    if image.size != (SIZE, SIZE):
                        raise ValueError(f"Unexpected sprite size: {relative_path}")
                    rgba_digest = sha256(opaque_rgba(image))
                    if rgba_digest != entry["hashes"][index]:
                        raise ValueError(f"Source frame no longer matches converter output: {relative_path}")
                    effect_audit = None
                    board_image = image
                    if stage["id"] == "teen" and action == "pet":
                        board_image, effect_audit = strip_teen_pet_hearts(image)
                    board_rgba_digest = sha256(opaque_rgba(board_image))
                    data = rgb565(board_image, alpha=True)
                name = f"{prefix}_{index:02}"
                frame_names.append("&" + name)
                source.append(emit_image(name, data, alpha=True))
                item = {"source": str(path.relative_to(ROOT)), "png_sha256": sha256(png),
                              "rgba_sha256": rgba_digest, "rgb565a8_sha256": sha256(data),
                              "bytes": len(data), "symbol": name}
                if effect_audit:
                    item["board_rgba_sha256"] = board_rgba_digest
                    item["effect_extraction"] = effect_audit
                items.append(item)
                stage_audit["sprite_bytes"] += len(data)
                stage_frame_count += 1
            source.append(f"static const lv_image_dsc_t * const {prefix}_frames[] = {{ "
                          + ", ".join(frame_names) + " };\n")
            source.append(float_array(f"{prefix}_foot_y", [f["y"] for f in foot]))
            source.append(float_array(f"{prefix}_mouth_x", [m["x"] if m else -1 for m in mouths]))
            source.append(float_array(f"{prefix}_mouth_y", [m["y"] if m else -1 for m in mouths]))
            source.append("\n")
            action_rows.append(f"    [CH_{action.upper()}] = {{ {prefix}_frames, {count}, {entry['fps']}, "
                               f"{1 if entry['loop'] else 0}, {prefix}_foot_y, "
                               f"{prefix}_mouth_x, {prefix}_mouth_y }},\n")
            stage_audit["actions"][action] = {"count": count, "fps": entry["fps"], "loop": entry["loop"],
                                             "foot_y": [f["y"] for f in foot], "mouths": mouths, "frames": items}
            if override_audit:
                stage_audit["actions"][action]["board_override"] = override_audit
        if stage_frame_count != STAGE_FRAME_COUNT:
            raise ValueError(f"Expected all {STAGE_FRAME_COUNT} active {stage['id']} frames; found {stage_frame_count}")
        stage_audit["frame_count"] = stage_frame_count
        audit["stages"][stage["id"]] = stage_audit
        audit["sprite_bytes"] += stage_audit["sprite_bytes"]
        global_frame_count += stage_frame_count
        array_name = "ch_animations" if stage["id"] == "baby" else f"ch_{stage['id']}_animations"
        storage = "" if stage["id"] == "baby" else "static "
        source.append(f"{storage}const ch_animation_t {array_name}[CH_ANIM_COUNT] = {{\n")
        source.extend(action_rows)
        source.append("};\n\n")
        stage_rows.append(f"    [CH_STAGE_{stage['id'].upper()}] = {{ {array_name}, "
                          f"{stage_audit['pet_size']}, {stage_audit['widget_size']}, "
                          + json.dumps(stage["id"], ensure_ascii=False) + ", "
                          + json.dumps(stage["label"], ensure_ascii=False) + " },\n")
    if global_frame_count != FRAME_COUNT:
        raise ValueError(f"Expected all {FRAME_COUNT} active stage frames; found {global_frame_count}")
    source.append("const ch_stage_assets_t ch_stages[CH_STAGE_COUNT] = {\n")
    source.extend(stage_rows)
    source.append("};\n\n")
    names = []
    for index, background in enumerate(BACKGROUNDS):
        path = ASSETS / f"{background}.png"
        with Image.open(path) as image:
            # Approved backgrounds are square; refusing a changed aspect ratio
            # avoids accidentally stretching or silently cropping new artwork.
            if image.width != image.height:
                raise ValueError(f"Background must remain square: {background}")
            # Keep the existing low-resolution pixel grid while removing
            # LVGL's noninteger runtime transform from dirty-region rendering.
            grid = image.convert("RGB").resize((SIZE, SIZE), Image.Resampling.NEAREST)
            native = grid.resize((BACKGROUND_SIZE, BACKGROUND_SIZE), Image.Resampling.NEAREST)
            data = rgb565(native)
            source_size = list(image.size)
        name = f"ch_background_{index}"
        names.append("&" + name)
        source.append(emit_image(name, data, alpha=False, size=BACKGROUND_SIZE))
        audit["backgrounds"].append({"id": background, "source": f"preview/assets/{background}.png",
                                     "source_dimensions": source_size,
                                     "pixel_grid_size": SIZE,
                                     "render_dimensions": [BACKGROUND_SIZE, BACKGROUND_SIZE],
                                     "png_sha256": sha256(path.read_bytes()),
                                     "rgb565_sha256": sha256(data), "bytes": len(data), "symbol": name})
        audit["background_bytes"] += len(data)
    source.append("const lv_image_dsc_t * const ch_backgrounds[CH_BACKGROUND_COUNT] = { "
                  + ", ".join(names) + " };\n")
    item = json.loads(BOARD_ITEMS.read_text(encoding="utf-8"))
    if item.get("version") != 1 or item.get("id") != "poop":
        raise ValueError("Expected the approved board-only droppings item")
    item_path = ROOT / item["output"]
    if not item_path.resolve().is_relative_to((ROOT / "main/assets").resolve()):
        raise ValueError("Board-only item must stay in main/assets")
    spec_path = ROOT / item["source_spec"]
    source_path = ROOT / item["source"]
    if (sha256(item_path.read_bytes()) != item["png_sha256"]
            or sha256(spec_path.read_bytes()) != item["source_spec_sha256"]
            or sha256(source_path.read_bytes()) != item["source_sha256"]):
        raise ValueError("Board item provenance or converted PNG changed")
    if (item["converter"]["commit"] != "e6fac947bfdebec3b5e12a7856f95997a934ac07"
            or item["converter"]["api"] != "make_pixel_art"):
        raise ValueError("Board item must use the fixed PixelArtConverter core")
    with Image.open(item_path) as image:
        if image.size != (ITEM_SIZE, ITEM_SIZE):
            raise ValueError("Board droppings must remain an exact 32 px sprite")
        data = rgb565(image, alpha=True)
        rgba_digest = sha256(opaque_rgba(image))
    item_name = "ch_item_poop"
    source.append(emit_image(item_name, data, alpha=True, size=ITEM_SIZE))
    source.append(f"const lv_image_dsc_t * const ch_poop = &{item_name};\n")
    audit["items"].append({"id": "poop", "source": item["output"],
                          "source_manifest": str(BOARD_ITEMS.relative_to(ROOT)),
                          "source_manifest_sha256": sha256(BOARD_ITEMS.read_bytes()),
                          "converter": item["converter"], "dimensions": [ITEM_SIZE, ITEM_SIZE],
                          "png_sha256": item["png_sha256"], "rgba_sha256": rgba_digest,
                          "rgb565a8_sha256": sha256(data), "bytes": len(data), "symbol": item_name})
    audit["item_bytes"] += len(data)
    audit["resource_bytes"] = audit["sprite_bytes"] + audit["background_bytes"] + audit["item_bytes"]
    audit["literal_terminator_bytes"] = global_frame_count + len(BACKGROUNDS) + len(audit["items"])
    audit["array_storage_bytes"] = audit["resource_bytes"] + audit["literal_terminator_bytes"]
    audit["frame_count"] = global_frame_count
    return "".join(source), audit


def font_symbols() -> str:
    # Include every Chinese UI character in the actual desktop and board files,
    # so adding a button in either front end doesn't leave a blank glyph.
    text = EXTRA_FONT_TEXT + (ROOT / ".tools/fonts/ui_chars.txt").read_text(encoding="utf-8")
    for path in sorted((ROOT / "preview").glob("*")):
        if path.suffix in (".mjs", ".html"):
            text += path.read_text(encoding="utf-8")
    for path in sorted((ROOT / "main").glob("chameleon_*.cpp")):
        text += path.read_text(encoding="utf-8")
    return "".join(sorted({char for char in text if 0x4e00 <= ord(char) <= 0x9fff
                            or (ord(char) > 126 and unicodedata.category(char).startswith("P"))
                            or char in "℃°→←×"}))


def build_font(symbols: str) -> str:
    converter = ROOT / ".tools/font-converter/node_modules/.pnpm/lv_font_conv@1.5.3/node_modules/lv_font_conv/lv_font_conv.js"
    font = ROOT / ".tools/fonts/SourceHanSansCN-Normal.otf"
    node = shutil.which("node")
    if not node:
        bundled = Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node"
        node = str(bundled) if bundled.is_file() else None
    if not node or not converter.is_file() or not font.is_file():
        raise RuntimeError("Use the existing local Node, lv_font_conv and SourceHanSansCN; no download is needed")
    with tempfile.TemporaryDirectory(prefix="chameleon-font-") as temporary:
        output = Path(temporary) / "chameleon_font.c"
        subprocess.run([node, str(converter), "--size", "18", "--bpp", "4", "--font", str(font),
                        "-r", "0x20-0x7e", "--symbols", symbols, "--format", "lvgl", "--no-compress",
                        "--output", str(output), "--lv-font-name", "chameleon_font_18"], check=True, cwd=ROOT)
        source = output.read_text(encoding="utf-8")
    # Remove workstation/temp paths from the generator banner for byte-for-byte
    # reproducibility; keep the precise converter options in this script.
    source = re.sub(r"#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n.*?#endif\n", '#include "lvgl.h"\n',
                    source, count=1, flags=re.S)
    return re.sub(r" \* Opts:.*", " * Opts: scripts/build_board_assets.py; lv_font_conv 1.5.3; SourceHanSansCN; 18px 4bpp no-compress", source)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check generated files without modifying them")
    parser.add_argument("--skip-font", action="store_true", help="Rebuild/check image resources only")
    args = parser.parse_args()
    source, audit = build_resources()
    outputs = {ROOT / "main/chameleon_assets.h": header_source(), ROOT / "main/chameleon_assets.c": source}
    if not args.skip_font:
        symbols = font_symbols()
        font_source = build_font(symbols)
        outputs[ROOT / "main/chameleon_font.c"] = font_source
        bitmap = re.search(r"glyph_bitmap\[\] = \{(.*?)\n\};", font_source, re.S)
        bitmap_bytes = len(re.findall(r"0x[0-9a-f]+", bitmap[1])) if bitmap else 0
        audit["font"] = {"name": "chameleon_font_18", "size_px": 18, "bpp": 4,
                         "converter": "lv_font_conv 1.5.3", "symbols": symbols,
                         "symbol_count": len(symbols) + 95, "bitmap_bytes": bitmap_bytes,
                         "source_sha256": sha256(font_source.encode())}
    else:
        old_manifest = ROOT / "main/chameleon_assets_manifest.json"
        if old_manifest.is_file():
            old = json.loads(old_manifest.read_text(encoding="utf-8"))
            if "font" in old:
                audit["font"] = old["font"]
    outputs[ROOT / "main/chameleon_assets_manifest.json"] = json.dumps(audit, ensure_ascii=False, indent=2) + "\n"
    mismatches = []
    for path, text in outputs.items():
        if args.check:
            if not path.is_file() or path.read_text(encoding="utf-8") != text:
                mismatches.append(str(path.relative_to(ROOT)))
        else:
            path.write_text(text, encoding="utf-8")
    if mismatches:
        print("Generated resources differ: " + ", ".join(mismatches))
        return 1
    print(json.dumps({"ok": True, "check": args.check, "default_stage": audit["default_stage"],
                      "stages": list(STAGES), "actions_per_stage": len(ACTIVE_ACTIONS),
                      "frames": audit["frame_count"], "backgrounds": len(BACKGROUNDS),
                      "items": len(audit["items"]), "item_bytes": audit["item_bytes"],
                      "sprite_bytes": audit["sprite_bytes"], "background_bytes": audit["background_bytes"],
                      "image_bytes": audit["resource_bytes"],
                      "font_glyphs": audit.get("font", {}).get("symbol_count")}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
