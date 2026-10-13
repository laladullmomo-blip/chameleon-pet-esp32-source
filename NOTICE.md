# 开源来源与移植边界

本项目新增界面、整合入口和脚本采用 MIT。导入的代码、字体和组件保留各自许可。

| 来源 | 锁定版本 | 本项目使用范围 | 许可 |
| --- | --- | --- | --- |
| [微雪官方 1.75C 示例](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C) | `6d19f7e16fb9a3be219e9eed43ca9eb56c88d01c` | 参考 `02_lvgl_demo_v9` 的初始化与配置；实际依赖官方托管 BSP `3.0.0` | Apache-2.0 |
| [Ellenp2p/esp32-pet-tamagotchi](https://github.com/Ellenp2p/esp32-pet-tamagotchi) | `28f01d20a061b465f0258f304f3ffc143f3e6e6c` | `pet_state`、`pet_save` 和六组宠物帧资源，已为本板修订与拆分 | MIT，Copyright (c) 2026 ellenp2p |
| [LVGL](https://github.com/lvgl/lvgl) | `9.5.0` | 图形、触摸事件、定时器 | MIT |
| [Espressif LVGL Adapter](https://github.com/espressif/esp-iot-solution/tree/master/components/display/tools/esp_lvgl_adapter) | `0.4.3` | 原BSP适配器；构建时生成两份省电调度修订源码，保留Espressif版权与Apache许可，不修改托管文件 | Apache-2.0 |
| [Source Han Sans](https://github.com/adobe-fonts/source-han-sans) | CN Normal，UI 文案字符子集 | 当前`main/chameleon_font.c`及历史`main/pet_font.c`，18px、4bpp | SIL Open Font License 1.1 |
| [PixelArtConverter](https://github.com/ahmetolcum/PixelArtConverter) | `e6fac947bfdebec3b5e12a7856f95997a934ac07`，核心2.0.0 | 原样保留核心，实际调用图集分格、动作帧像素转换和图集组合；不含GUI或AI背景模型 | MIT，Copyright (c) 2026 Ahmet Olcum |
| [ESP-IDF](https://github.com/espressif/esp-idf) | `v6.0.2` | 编译、FreeRTOS、NVS、硬件接口 | Apache-2.0 等，按各文件原许可 |

许可全文放在 `THIRD_PARTY_LICENSES/`，PixelArtConverter原样许可另保存在`third_party/pixel_art_converter/LICENSE`；托管组件的许可由组件随包提供，版本和哈希见 `dependencies.lock`。

## 与上游的差异

- 替换原立创开发板的屏幕、触摸、电源层，使用官方 1.75C BSP，不沿用 ST7789/FT5x06 引脚。
- 按 466×466 圆屏重新实现中文主页、商店和追光小游戏。
- 变色龙开发板版保留离线养成，联网只用于实际校时和 Open-Meteo 天气；不接入 BLE、AI 配额查询、语音聊天或麦克风。
- 本板公开原理图没有外置 RTC 芯片，使用 ESP32 系统时间；断电后通过联网 NTP 或设置网页同步。相册使用自定义 SPIFFS 分区，不依赖 SD 卡。
- 原 `pet_*` 状态文件和资源作为历史参考保留，当前 CMake 编译入口使用 `chameleon_*` 模块。
- 宠物状态操作与存档均由 LVGL 线程调用，避免多个任务同时修改状态。
- 修正上游阶段时间注释、重生、持续濒危状态与存档边界校验，保持原主要养成阈值。

上游图片的独立作者信息未单列；这里按仓库的 MIT 许可保留来源，未另行宣称原创。
