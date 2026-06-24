# Atlas Rover Mk.1 可视化组装器方案

> 状态说明：本文是早期 MVP 示例方案。平台级产品方向已经升级为“机器人积木化设计器”，Atlas Rover Mk.1 只作为第一个示例包和验证 Demo。平台级需求与架构见 `docs/机器人积木化设计器_产品需求与功能架构.md` 和 `docs/机器人积木化设计器_平台技术架构.md`。

## 目标

基于 `tiny-world-builder` 的思路，为 Atlas Rover Mk.1 做一个面向硬件设计评审的分层可视化组装器。

它不是完整 CAD，也不是纯展示页。第一版要解决三个具体问题：

- 好看地摆位评审：按层显示底盘、四轮、开发板、灯光、喇叭、黄铜笼架和线束避让。
- 装配步骤生成：根据当前配置输出从底板、动力、电子、中层、车头到黄铜外观的施工顺序。
- BOM/检查清单：从当前模型导出采购项、数量、风险项和装配验收清单。

## 当前基准方案

Mk.1 默认采用四轮、四电机、64T 齿轮轮方案：

```text
4 x N20 金属减速电机
  -> 4 x 64T 3 mm D 孔齿轮轮芯
  -> 4 x O 型圈/热缩胶圈
  -> 2 x DRV8833，前桥一块、后桥一块
  -> XIAO ESP32C3 仍按左右两侧差速控制
```

默认不做四轮独立控制。前后同侧电机共享同一组逻辑控制信号，保持 Mk.1 固件和接线复杂度可控。

## 产品 Brief

- 产品：Atlas Rover Mk.1 Layer Builder。
- 视觉来源：Atlas Rover Mk.1 现有黄铜笼架、黑色电子板、蓝绿双眼、桌面机器人风格；交互模型参考 `tiny-world-builder` 的工具箱、画布、保存和导出思路。
- 交互等级：第一版以可用为先，组件拖放/开关层级/选择 variant/导出清单需要可工作；精细 CAD、真实物理仿真和自由曲面建模不进入 MVP。

## MVP 范围

### 1. 分层画布

画布使用 Three.js 或同等 3D 渲染方案，默认提供正交/透视/侧视/俯视四种视角。

建议层级：

| 层级 | 内容 | 默认 Z 高度 |
| --- | --- | ---: |
| 底板层 | 110 x 78 mm FR4/亚克力底板、安装孔、长槽 | 0-2 mm |
| 动力层 | 18650 电池盒、升压模块、电机支路开关 | 2-24 mm |
| 驱动层 | 四个 N20、64T 齿轮轮、两块 DRV8833 | 8-36 mm |
| 中层电子 | XIAO ESP32C3、分线点、PCA9685 可选 | 42-52 mm |
| 车头交互 | DualEye、M2 铜柱、WS2812B 灯环/灯条、喇叭 | 55-100 mm |
| 黄铜外观 | 底框、护栏、A 柱、轮拱、护眼圈、顶部提手 | 0-110 mm |
| 线束安全 | 电池线、电机线、UART、灯光数据线、绝缘/避让区 | 跟随相关层 |

底板宽度在四轮方案中默认从 72 mm 放宽到 78 mm，仍允许用户切回 72 mm 做拥挤度评估。

### 2. 组件库

组件库先用参数化几何体表达，不要求第一版就有真实 GLB/CAD 模型。

每个组件需要包含：

- `dimensions_mm`：长宽高或直径/厚度。
- `mounting`：安装孔、长槽、铜柱、夹具或绑扎方式。
- `ports`：电源、信号、接口方向和需要预留的插拔空间。
- `clearance_mm`：必须保留的安全间隙。
- `bom`：采购名称、数量、必需性、搜索词和来源。
- `rules`：碰撞、供电、共地、绝缘、方向、左右对称等约束。
- `variant`：默认件、备选件、装饰件或 Mk.2 预留件。

### 3. 约束检查

第一版至少实现这些检查：

| 检查 | 规则 |
| --- | --- |
| 四轮干涉 | 齿轮/胶圈与底板侧边、黄铜轮拱间隙 >= 2 mm |
| 四轮落地 | 四个轮心高度误差建议 <= 1 mm |
| 电机方向 | 四个 N20 输出轴朝外，线束朝内 |
| 驱动板数量 | 四电机正式方案必须使用 2 块 DRV8833 |
| 禁止单板并联四电机 | 单块 DRV8833 并联四电机只允许标记为临时空载测试 |
| DualEye 宽度 | 车架 90 mm 不是整车外宽，DualEye 可外凸到 100-104 mm |
| USB-C 可达 | DualEye 和 XIAO 的 USB-C 插拔方向保留操作空间 |
| 黄铜绝缘 | 黄铜件不得触碰电池正极、DRV8833 焊盘、灯板背面和裸露电源线 |
| WS2812B 电平 | 正式配置建议包含 AHCT/HCT 电平转换和 330 欧数据电阻 |
| 电源支路 | 电机电流不得经过 DualEye；DualEye、XIAO、DRV8833 必须共地 |
| 重心 | 18650、四个 N20、升压模块尽量放底层，顶部提手不作为承重把手默认项 |

### 4. 导出物

当前模型应该能导出三类文件：

- `assembly_steps.md`：装配步骤，按层和阶段排序。
- `bom.csv`：当前方案的 BOM，含必需/推荐/备选。
- `checklist.md`：装配前、上电前、悬空测试、落地测试检查清单。

后续可以增加：

- `layout.json`：保存当前摆位。
- `drawings.svg`：底板孔位和黄铜弯折模板。
- `preview.png`：评审截图。

## 建议数据结构

```json
{
  "project": "atlas-rover-mk1",
  "variant": "mk1-four-wheel-64t",
  "units": "mm",
  "board": {
    "length": 110,
    "width": 78,
    "thickness": 2
  },
  "components": [
    {
      "id": "n20-front-left",
      "type": "motor",
      "ref": "n20-ga12",
      "pose": { "x": 37, "y": 34, "z": 8, "rotation": [0, 0, 0] },
      "side": "left",
      "axle": "front"
    }
  ],
  "rules": ["four_wheel_clearance", "dual_drv8833_required", "brass_insulation"],
  "exports": ["bom", "assembly_steps", "checklist"]
}
```

## 初始组件尺寸来源

部分常见模块没有统一型号，第一版按“可靠默认候选 + 可替换参数”处理。

| 组件 | 默认尺寸 | 说明 |
| --- | --- | --- |
| DualEye | 宽 93.5 mm，双 1.28 寸 240 x 240 圆屏 | 本地文档已锁定宽度；Waveshare 文档确认双 1.28 寸 LCD、Type-C、ESP32-S3R8、16 MB Flash/8 MB PSRAM |
| XIAO ESP32C3 | 21 x 17.5 mm | Seeed XIAO 系列官方资料 |
| N20 | 总长约 34 mm，齿箱 15 x 12 x 10 mm，3 mm D 轴 x 10 mm | GA12-N20 常见规格 |
| DRV8833 | 默认 18.5 x 16 mm；Pololu 载板约 12.7 x 20.3 mm | 市售模块差异较大，组件库保留 variant |
| 18650 电池盒 | 76 x 21 x 20 mm | 常见单节带线电池盒 |
| WS2812B 8 灯环 | 外径 28 mm，内径 16 mm，5 V | 适合前脸状态灯或装饰灯 |
| 小喇叭 | 40 x 40 x 20 mm，4 欧 3 W | 适合后部或车头下方，若空间紧张可换 28-36 mm 薄喇叭 |
| 64T 齿轮轮 | 外径 30-36 mm，厚 3-6 mm，3 mm D 孔优先 | 采购前必须确认孔型和外径 |

## 实施路线

### Phase 0：数据先行

- 建立 `atlas_rover_mk1.components.json`。
- 建立 `atlas_rover_mk1.layout.json` 示例。
- 把现有 BOM、尺寸、接线规则映射到组件库。

### Phase 1：静态评审器

- Three.js 渲染参数化盒体、圆柱、杆件。
- 支持层级开关、视角切换、选择组件查看尺寸和风险。
- 内置四轮/64T 默认布局。

### Phase 2：规则检查器

- 实现碰撞盒、间隙、绝缘、供电、驱动板数量、USB-C 访问空间检查。
- 风险以红/黄/绿标记，不阻止用户探索。

### Phase 3：导出器

- 从当前模型导出 `bom.csv`、`assembly_steps.md`、`checklist.md`。
- 输出内容与制造包术语保持一致，便于复制回正式文档。

### Phase 4：资产升级

- 为 DualEye、N20、DRV8833、XIAO、18650、喇叭、灯环逐步补 GLB 或更精确参数模型。
- 增加底板孔位 SVG 和黄铜弯折模板导出。

## 需要继续补齐的信息

不阻塞 MVP，但会影响精度：

- 实买 64T 齿轮的外径、厚度、孔型和端面摆动。
- 实买 N20 电机夹的尺寸和安装孔。
- 实买升压模块尺寸、输入/输出端子方向。
- DualEye 实板 USB-C、SH1.0/FPC、喇叭接口周围可操作空间。
- 灯条还是灯环作为默认前脸灯光。

## 外部资料

- Tiny World Builder: https://github.com/jasonkneen/tiny-world-builder
- Waveshare DualEye 文档: https://docs.waveshare.com/ESP32-S3-DualEye-Touch-LCD-1.28
- Seeed XIAO 系列资料: https://files.seeedstudio.com/wiki/XIAO/Seeed-Studio-XIAO-Series-SOM-Datasheet.pdf
- Pololu DRV8833 载板资料: https://www.pololu.com/product-info-merged/2130
- Botland WS2812B 8 灯环资料: https://botland.store/led-strings-chains-matrices/6248-rgb-led-ring-ws2812b-5050-x-8-leds-28mm-5904422375065.html
