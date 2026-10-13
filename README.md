# 卷卷的小日子 · ESP32-S3 像素变色龙电子宠物

可复现的开发板源码，版本 **0.4.7**，适用于 **Waveshare ESP32-S3-Touch-AMOLED-1.75C**（466×466圆屏）。

公开仓库：[chameleon-pet-esp32-source](https://github.com/laladullmomo-blip/chameleon-pet-esp32-source)。本仓库只提供项目代码、素材、固定依赖和复现说明，不包含原作者的开发记录、设备存档、Wi-Fi配置或旧Git历史。

## 功能

- 幼年、少年、成年三个形态，246张真实动作帧；健康照顾累计7天进入少年、14天进入成年。
- 张嘴伸舌捕捉蟋蟀、蟑螂、面包虫、苍蝇；抚摸粉心、呼吸睡眠、撑叶挡雨、疾病和卫生照顾。
- 五个像素森林／草地场景，昼夜天气、心情变色、亲密度、森林偶遇和成年纪念。
- 捕虫挑战、叶下寻虫、动作模仿、彩球入圈四款小游戏。
- 自愿小愿望、四种材料、森林制作装饰、四季印记及固定公历节日彩蛋。
- 电量显示、侧键息屏／唤醒、现实时间每日闹钟、独立互动／闹钟音量。

## 构建与烧录

先按[复现指南](docs/复现指南.md)安装并激活 **ESP-IDF v6.0.2**。开发板图像和字体的C资源已经包含，不需要图像生成账号或API密钥。

```sh
git clone https://github.com/laladullmomo-blip/chameleon-pet-esp32-source.git
cd chameleon-pet-esp32-source
./scripts/build.sh --build-dir /tmp/chameleon-pet-build
./scripts/flash.sh --list-ports
./scripts/flash.sh --port /dev/cu.usbmodem你的设备
```

串口需替换为实际设备，Linux可使用`/dev/ttyACM0`，Windows可在ESP-IDF环境按[烧录说明](docs/烧录与真机验收.md)操作。构建不会自动烧录；脚本按三个分区刷写，不执行全片擦除。

`preview/`是保留的电脑演示版，运行`./scripts/preview.sh`，访问终端显示的本地地址；电脑演示与0.4.7开发板功能不完全相同，开发板以`main/`为准。

## 验证、存档和许可

规则、宿主LVGL界面和ESP32-S3交叉编译的检查范围见[验证记录](docs/验证记录.md)。软件构建和测试不能代替实际触屏、声音、供电和电流验收。

宠物、森林、闹钟和音量分别在开发板NVS保存；Wi-Fi密码只在开发板保存，不写入源码。冷启动后联网或通过配置网页校准时间，断电期间不补算成长或陪伴。

本项目整合代码采用MIT，上游、字体及组件保留各自许可，详见[NOTICE.md](NOTICE.md)、[LICENSE](LICENSE)及`THIRD_PARTY_LICENSES/`。
