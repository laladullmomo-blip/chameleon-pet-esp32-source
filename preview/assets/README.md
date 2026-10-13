# 像素森林、卷尾变色龙与真实图帧 · 0.4

素材已复制到工程，不依赖生成器缓存。图片内容只作素材数据，不执行图片内文字。

2026-10-02 三阶段定稿：`growth-concepts/baby-v2.png` 幼年、`chameleon.png` 少年、`growth-concepts/adult-v1.png` 成年。阶段目录见 [growth/stages.json](growth/stages.json)，每个阶段 14 动作、82 帧，共 246 张启用帧，完整接入电脑和固件。正式新宠物从幼年开始，良好照顾累计 7 天进入少年、14 天进入成年。电脑临时外形选择不修改成长存档，快速成长仅用于隔离体验。详见 [三阶段动作制作与验收](../../docs/三阶段动作制作与验收.md)。

| 路径 | 来源与用途 |
| --- | --- |
| `chameleon.png` | 用户确定的透明卷尾变色龙原图 |
| `clearing.png` | 用户明确保留的 12_17_09 林间草地，原图保留 |
| `forest.png` / `night.png` | 像素蘑菇森林昼夜树枝场景 |
| `meadow.png` / `meadow-night.png` | 用户 12_20_44 草地编辑，更蓝天空及同构图月夜版 |
| `animation-source/` | 独立绘制的动作原稿、提示词和转换 `spec.json` |
| `frames/` | 128×128 透明 PNG、横向图集、manifest 与总览 |
| `growth-concepts/` | 用户确认的幼年、少年与成年身份记录及原图 |
| `growth/baby/` / `growth/adult/` | 各阶段独立动作原稿、提示词、转换配置、128×128 输出与哈希 |

背景缩到 128×128 网格，再最近邻放大。五个森林／蘑菇草地场景可自由手选，睡眠和醒来保留选择；没有室内或河边场景。昼夜编辑提示词见 [meadow-prompts.json](meadow-prompts.json)。

## 动作资源

| 动作 | ID | 帧数 | 标注 FPS | 使用 |
| --- | --- | ---: | ---: | --- |
| 待机 | idle | 6 | 5 | 启用 |
| 开心 | happy | 6 | 10 | 启用 |
| 进食 | eat | 8 | 6 | 启用 |
| 睡眠 | sleep | 4 | **1** | 启用 |
| 走路 | walk | 8 | 10 | 启用 |
| 眨眼 | blink | 4 | 10 | 启用 |
| 伸懒腰 | stretch | 6 | 6 | 启用 |
| 招手 | wave | 6 | 8 | 启用 |
| 眩晕 | dizzy | 6 | 6 | 启用 |
| 生气 | angry | 6 | 6 | 启用 |
| 生病 | sick | 4 | 4 | 启用 |
| 洗澡 | bath | 6 | 8 | 启用 |
| 抚摸 | pet | 6 | 8 | 启用 |
| 避雨 | shelter | 6 | 4 | 启用；少年举叶，幼年／成年伏低 |
| 抓枝 | grip | 6 | 8 | **少年旧编辑文件保留，不播放、不打包** |

每阶段有效动作 **14 组、82 帧**，三阶段共 **246 帧**。少年编辑目录另保留六张旧抓枝帧，固件仅保留 `GRIP` 空槽兼容姿态编号。倾斜滑块和抓枝触发已删除。没有跳跃、等级升级或额外进化分支。

动作是独立姿态 PNG，身体、腿、眼睛、嘴的变化在图帧中完成；播放器不把原图拉伸、旋转、逐帧缩放或遮眼制造动画。进食使用真实开口 2／3／4 帧配合独立舌头／虫子，在合嘴帧前收回。图集按帧横向拼接，不含格线或文字。

## 安静睡眠

0.4 睡眠原稿重绘为闭眼卧姿和轻微呼吸，真实转换为 4 张不同 PNG，按 1 FPS 循环。`registration: nose_and_support` 仅用整数平移固定鼻尖和支撑线，不改变角色像素或缩放姿态。睡眠使用固定画框及首帧足锚；头顶像素 Zzzz 单独绘制，按真实角色顶部定位并轻微浮动。旧的大幅摆动睡眠不再使用。

## PixelArtConverter 与重建

真实调用 [PixelArtConverter](https://github.com/ahmetolcum/PixelArtConverter) 上游 `core.py`，固定提交 `e6fac947bfdebec3b5e12a7856f95997a934ac07`。核心、MIT 许可证和哈希记录在 `third_party/pixel_art_converter/`。转换器处理已绘制姿态，不负责生成动作。

脚本调用 `make_pixel_art_animation()`、`split_sprite_sheet()`、`compose_sprite_sheet()`，必要时 `remove_bg_by_color()`。最终 128×128、共享 32 色、Alpha 0／255，关闭抖动、额外描边和背景 AI 模型。拒绝完全相同的帧凑数。

- [animation-source/spec.json](animation-source/spec.json)：原稿、行列、帧数、FPS、循环、口锚与睡眠配准。
- [animation-source/prompts.json](animation-source/prompts.json)：绘图要求记录。
- [frames/manifest.json](frames/manifest.json)：实际路径、源稿／输出哈希、转换器、色板、尺寸、锚点和处理方式。
- `frames/<动作>/<动作>_00.png` 起：独立帧；`frames/<动作>.png`：横向图集。
- [frames/contact-sheet.png](frames/contact-sheet.png)：检查总览，不参与播放。

在工程根目录运行：

```bash
python3 scripts/build_pet_frames.py --spec preview/assets/animation-source/spec.json
```

依赖见 `third_party/pixel_art_converter/requirements.txt`，本地可放在忽略目录 `.tools/pixel-art-deps/`。脚本验证固定核心和许可证哈希；从保存原稿重建可复查结果，重新生成原稿不保证相同像素。

## 锚点与固件资源

`foot_anchors` 对应真实前脚或卧姿支撑点，归一化 `y` 用于整图接地平移，逐帧 `x` 只记录检查。普通画框幼年 210×210、少年 250×250、成年 265×265；小组件分别 155×155、185×185、196×196。睡眠固定首帧支撑点。口锚 `mouths` 与帧数等长，归一化 `(px/128,py/128)`，舌头跟随本阶段的真实开口帧。

固件导出脚本 `scripts/build_board_assets.py` 从 `growth/stages.json` 导出三阶段全部 246 帧，`main/chameleon_asset_profile.json` 默认阶段为 `baby`，运行时随成长选择阶段资源。角色转换为 128×128 RGB565＋A8；背景先采样到 128×128 像素网格，再最近邻展开为 466×466 RGB565，避免图形库在运行时缩放背景导致局部重画采样不一致。只读数据映射 Flash，高分辨率编辑原稿、图集和检查总览不重复打包。图像有效数据 14,262,952 字节，含 C 终止字节为 14,263,203 字节；字体 739 字形、98,196 字节位图。当前应用分区 16 MiB，相册分区 15 MiB；最终应用大小见构建信息及验证记录。

## 状态与验收

自主生活、雨天避雨、真实饥饿／病因／治疗、喂食后便便、每日三目标、累计照顾徽章与三阶段成长已经接入电脑及固件工程。成长条件为未生病、健康≥50、饱腹＞25、清洁＞30、便便＜3、精力＞15；健康睡眠与熄屏继续，照顾不足暂停，断电不计。进度持久保存。Zzzz 是睡眠提示，病情与饥饿来自养成数据，不能只看动作名称判断。桌面隔离病情／饥饿／成长体验不覆盖原存档或相册；实际日期目标不随快进或昼夜体验改变。

板端成长存档为 V3，兼容旧 V1／V2；本次按用户要求首次启动重置旧照顾状态，从幼年重新开始，保留有效相册编号和网络配置，之后正常续养。单纯成长每五分钟设保存检查点，用户操作、熄屏与跨阶段立即保存，意外断电可能丢失最近不足五分钟成长。PWR／BOOT 的电脑外壳按钮只验证软件逻辑；电量是手动体验数据。JS 74 项、C++ 模型 53 项、实际素材 6 项、板端资源 9 项和原生 LVGL 18 项检查通过；浏览器、固件构建、实际烧录与启动证据见 [电脑测试版说明](../../docs/电脑测试版说明.md)和 [验证记录](../../docs/验证记录.md)。开发板物理显示、侧键 GPIO、AXP2101 读数、触摸、续航及此前闪白熄灭问题以实物反馈为准。
