# PixelArtConverter 核心来源

本目录保留 [ahmetolcum/PixelArtConverter](https://github.com/ahmetolcum/PixelArtConverter) 的真实 `core.py` 和 MIT 许可证，固定提交为 `e6fac947bfdebec3b5e12a7856f95997a934ac07`。两份上游文件原样保存，核心声明版本为 `2.0.0`；源码地址和调用记录见 `provenance.json`。

构建脚本调用实际的 `make_pixel_art_animation()`，按固定调色板量化、块多数色缩小、二值透明度输出。真实 API 名称没有 `convert_frames()`。`split_sprite_sheet()` 按行切分原始动作图集，`compose_sprite_sheet()` 组成横向输出图集；必要时调用 `remove_bg_by_color()` 做纯色背景抠除。

没有调用 GUI、rembg、AI 背景模型或更新接口。固定调色板路径不依赖 scikit-learn。`requirements.txt` 列出实际使用的处理库；安装到工程忽略的 `.tools/pixel-art-deps/` 即可。工程已有的 Python 3.12、Pillow、NumPy 可以继续使用。

调色板统一为 24 个参考变色龙颜色、4 个舌头粉色及 4 个蓝色效果颜色。脚本禁止复制完全相同的帧充数，每动作必须提供独立画出的姿态；相邻帧若经转换变成完全一致也会报错，要求修正原始动作稿。
