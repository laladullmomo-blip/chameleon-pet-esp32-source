# 开发板三阶段生病动作（2026-10-03）

本目录只替换开发板的 `CH_SICK`，电脑端 `preview/` 未修改。幼年、少年、成年各 4 张独立病姿，2 FPS 循环；替换原来的 12 帧，不另行打包旧病姿或增加总帧数。

每阶段使用相应定稿作为参考，由内置 `image_gen` 分别生成一张 2×2 图集，保存未经改画的透明原稿 `source/sick-sheet.png`、完整提示词 `source/prompt.txt`、转换规格 `source/spec.json` 和来源哈希 `source/generation.json`。生病外观重新绘制为伏低身体、疲惫半闭眼/慢眨眼、小幅头垂及浅蓝冷敷包，保留三阶段头冠、身体比例和成年青绿宽纹；原稿保持绿色，运行时生病体色统一调为批准的灰青色。

转换实际调用 `scripts/build_pet_frames.py` 和固定版本 PixelArtConverter `make_pixel_art_animation`，128×128、32 色共享参考调色板、二值透明、不抖动。四格按原图坐标共同缩放及填充，不拉伸、不旋转、不补画、不复制姿势来制造帧。每张脚部支撑位置由原始输出测量，运行时按固定支撑线显示；四张 PNG 均有不同哈希。

重建命令（使用项目现有包含 Pillow、NumPy、SciPy 和 scikit-image 的 Python）：

```sh
python3 scripts/build_pet_frames.py --spec main/assets/sick-redesign/baby/source/spec.json --output main/assets/sick-redesign/baby/frames
python3 scripts/build_pet_frames.py --spec main/assets/sick-redesign/teen/source/spec.json --output main/assets/sick-redesign/teen/frames
python3 scripts/build_pet_frames.py --spec main/assets/sick-redesign/adult/source/spec.json --output main/assets/sick-redesign/adult/frames
python3 scripts/build_board_assets.py
python3 tests/board_assets_test.py
```

板端 `main/chameleon_asset_profile.json` 的 `action_overrides` 按阶段指定这三份病姿 manifest。打包器检查定稿和调色板、转换器来源、完整提示词/原稿/规格哈希及 4 帧唯一性；打包审计保留 `board_override` 记录。其他动作、旧电脑病姿和定稿原图保持原文件。

PixelArtConverter 固定来源、MIT 许可和源文件校验详见 `third_party/pixel_art_converter/provenance.json` 与 `LICENSE`。
