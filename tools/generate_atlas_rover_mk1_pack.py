from __future__ import annotations

import csv
import math
from pathlib import Path
from textwrap import dedent
from urllib.parse import quote


ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / "Atlas_Rover_Mk1_制造图纸包_V1.0"
DRAWINGS = PACK / "图纸"
PDF_OUT = PACK / "输出" / "PDF"


BOM = [
    {
        "class": "核心",
        "item": "ESP32-S3-DualEye-Touch-LCD-1.28",
        "spec": "Waveshare 双 1.28 寸 240x240 圆形触摸屏开发板，M2 安装孔",
        "qty": "1",
        "required": "已有",
        "notes": "官方宽度 93.5 mm。可作为车头双眼显示；可通过 GPIO/I2C/PWM 扩展控制电机驱动，但不能直接带电机。板上已有麦克风、音频编解码/功放和喇叭接口；未集成可做车灯的 RGB/WS2812 灯。",
        "est_cny": "0",
    },
    {
        "class": "运动",
        "item": "N20 金属减速电机",
        "spec": "6 V，100-150 RPM，3 mm D 形轴，轴长 8-10 mm",
        "qty": "4",
        "required": "必买",
        "notes": "四电机四轮方案使用 4 个 N20；100 RPM 更适合桌面慢速巡游，150 RPM 必须用 PWM 限速。Mk.1 不需要编码器版本。",
        "est_cny": "30-50",
    },
    {
        "class": "运动/驱动",
        "item": "DRV8833 双路 H 桥电机驱动板（必备）",
        "spec": "电机供电 2.7-10.8 V，双直流电机通道，常见模块约 18.5 x 16 mm",
        "qty": "2",
        "required": "必买",
        "notes": "四电机方案前后各一块 DRV8833：前板驱动左前/右前，后板驱动左后/右后；XIAO D2-D5 同时接到两块板输入。不要用一块 DRV8833 长期并联四个电机。",
        "est_cny": "10-20",
    },
    {
        "class": "控制/扩展",
        "item": "PCA9685 I2C PWM 扩展板（推荐）",
        "spec": "16 通道，12 位 PWM，默认 I2C 地址 0x40，优先迷你板",
        "qty": "1",
        "required": "推荐",
        "notes": "这是控制信号扩展板，不是电机驱动板。它负责给 DRV8833 提供多路 PWM/方向控制信号；如果确认主控板有 4 个可用 GPIO，可省略。",
        "est_cny": "8-18",
    },
    {
        "class": "运动",
        "item": "N20 轮胎",
        "spec": "优先 34 mm 直径，3 mm D 形孔，橡胶胎",
        "qty": "4",
        "required": "备选",
        "notes": "若 64T 齿轮轮方案打滑或偏摆严重，可回退成 4 个 N20 橡胶轮；43 mm 轮会增加速度和外宽。",
        "est_cny": "12-24",
    },
    {
        "class": "运动/备选",
        "item": "64T 钢齿轮轮芯（备选）",
        "spec": "外径 30-36 mm 优先；最好 3 mm D 孔；厚 3-6 mm",
        "qty": "4",
        "required": "必买",
        "notes": "当前采用 64T 齿轮四轮方案。必须确认 3 mm D 孔可直接压入 N20 轴；外圈建议套 O 型圈或热缩胶圈增加摩擦。",
        "est_cny": "16-40",
    },
    {
        "class": "运动/备选",
        "item": "3 mm D 孔轮毂或夹紧适配器",
        "spec": "适配 N20 3 mm D 轴；可用 M2 螺丝固定齿轮",
        "qty": "4",
        "required": "备选",
        "notes": "若 64T 齿轮不是 3 mm D 孔直连，必须用轮毂/夹片转接；不建议只用胶水粘。",
        "est_cny": "16-36",
    },
    {
        "class": "运动/备选",
        "item": "齿轮外圈防滑圈",
        "spec": "O 型圈或热缩管，适配 30-36 mm 外径",
        "qty": "4-8",
        "required": "必买",
        "notes": "钢齿轮直接接触桌面会滑、响、伤桌面；四个齿轮外圈都要加橡胶圈或热缩圈。",
        "est_cny": "6-16",
    },
    {
        "class": "运动",
        "item": "迷你万向轮",
        "spec": "10-20 mm 球形万向轮或小型万向轮",
        "qty": "1",
        "required": "备选",
        "notes": "四电机四轮方案不把万向轮作为主支撑；可保留用于临时两轮回退方案或台架调试。",
        "est_cny": "3-8",
    },
    {
        "class": "电源",
        "item": "18650 锂电池",
        "spec": "1S，标称 3.7 V，2600-3500 mAh，建议持续放电 >= 8 A，优先高倍率/带保护电芯",
        "qty": "1",
        "required": "必买",
        "notes": "裸电芯约 18 x 65 mm；带保护板或尖头电芯可能更长。避免虚标容量电池，电机启动电流会拉低电压。",
        "est_cny": "10-35",
    },
    {
        "class": "电源",
        "item": "单节 18650 电池盒带线",
        "spec": "约 76 x 21 x 20 mm",
        "qty": "1",
        "required": "必买",
        "notes": "沿底层长度方向安装。双节电池盒不属于 Mk.1 方案。",
        "est_cny": "3-8",
    },
    {
        "class": "电源",
        "item": "5 V 升压模块",
        "spec": "1S 锂电输入，稳压 5 V 输出，建议 2-3 A 连续 / 4-5 A 峰值能力",
        "qty": "1",
        "required": "必买",
        "notes": "只给电机支路使用。不要让电机电流经过 ESP32 的 3.3 V 电源轨。若四个 N20 启动掉压，优先换更大电流模块。",
        "est_cny": "5-12",
    },
    {
        "class": "电源",
        "item": "电源开关与保护",
        "spec": "迷你拨动开关，加 1S 保护板或带保护 18650",
        "qty": "1 套",
        "required": "必买",
        "notes": "只保留一条充电路径。除非电池路径隔离，不要把 TP4056 与板载充电并联。",
        "est_cny": "3-10",
    },
    {
        "class": "音频",
        "item": "小喇叭",
        "spec": "4 欧 3 W 或 8 欧 1-2 W，外形尺寸不超过 40 mm",
        "qty": "1",
        "required": "推荐",
        "notes": "主控板已有麦克风、音频编解码、功放和喇叭接口；但通常仍需外接一个小喇叭才能外放。Mk.1 不必购买 MAX98357A 和 INMP441。",
        "est_cny": "5-12",
    },
    {
        "class": "灯光/外设",
        "item": "WS2812B 灯条或灯环（外接车灯模块）",
        "spec": "5 V，8 灯灯环 28-32 mm，或短灯条",
        "qty": "1",
        "required": "推荐",
        "notes": "这就是解决“没有集成车灯/RGB”的外接灯光模块。需要 5 V、GND 和 1 个数据 GPIO；8 颗灯全白约 0.48 A，建议限亮度并加 330 欧数据电阻和 470-1000 uF 电容。",
        "est_cny": "5-12",
    },
    {
        "class": "灯光/信号",
        "item": "WS2812B 数据电平转换器（推荐）",
        "spec": "SN74AHCT1G125、74AHCT125 或 74HCT14，3.3 V 转 5 V，单路即可",
        "qty": "1",
        "required": "推荐",
        "notes": "WS2812B 用 5 V 供电时，数据高电平按规格需要接近 3.5 V；ESP32 的 3.3 V 短线常能跑，但不够稳。推荐用 AHCT/HCT 电平转换：A 接 ESP32 数据 GPIO，Y 经 330 欧到 DIN，芯片 VCC 接 5 V，GND 共地。",
        "est_cny": "2-8",
    },
    {
        "class": "结构",
        "item": "黄铜丝/黄铜棒",
        "spec": "2.0 mm 黄铜棒约 3 m；1.5 mm 黄铜棒约 2 m；可选 0.8-1.0 mm 细铜/黄铜丝约 1 m",
        "qty": "1 套",
        "required": "必买",
        "notes": "2.0 mm 做底框、护栏、立柱和顶部提手；1.5 mm 做护眼圈、斜撑和灯架；1.0 mm 只用于绑扎焊点或装饰，不承担主结构。焊接前预上锡，铜架必须与带电焊盘绝缘。",
        "est_cny": "10-25",
    },
    {
        "class": "结构",
        "item": "M2 铜柱套装",
        "spec": "M2 双通铜柱：6、8、10、12、15、20 mm；配 M2 螺丝",
        "qty": "1 套",
        "required": "必买",
        "notes": "用 M2，不用 M3。双眼主控板官方安装孔为 M2。",
        "est_cny": "10-20",
    },
    {
        "class": "结构",
        "item": "层板材料",
        "spec": "1.5-2.0 mm FR4 洞洞板或亚克力；底层 110 x 72 mm，中层 84 x 58 mm",
        "qty": "2",
        "required": "必买",
        "notes": "FR4 洞洞板更适合手工钻孔；亚克力更好看，但加工过急容易裂。",
        "est_cny": "8-20",
    },
    {
        "class": "线材",
        "item": "JST/杜邦线套装",
        "spec": "MX1.25 2P 电池线；SH1.0 14P/FPC 转接线；26-28 AWG 信号线",
        "qty": "1 套",
        "required": "必买",
        "notes": "主控板外露接口是小间距 SH1.0/FPC 形态，建议准备转接线或转接板；否则手工接线会很难。",
        "est_cny": "10-25",
    },
    {
        "class": "线材",
        "item": "热缩管与绝缘材料",
        "spec": "1-5 mm 混装热缩管，Kapton 胶带",
        "qty": "1 套",
        "required": "必买",
        "notes": "铜架穿线处和电池引线处必须做绝缘。",
        "est_cny": "5-12",
    },
]


BUY_QUERIES = {
    "ESP32-S3-DualEye-Touch-LCD-1.28": "ESP32-S3-DualEye-Touch-LCD-1.28 Waveshare 双眼 屏 开发板",
    "N20 金属减速电机": "N20 金属减速电机 6V 100RPM 3mm D轴 轴长10mm",
    "DRV8833 双路 H 桥电机驱动板（必备）": "DRV8833 双路 H桥 电机驱动模块 迷你",
    "PCA9685 I2C PWM 扩展板（推荐）": "PCA9685 16路 PWM 舵机驱动模块 I2C 迷你",
    "N20 轮胎": "N20 轮胎 34mm 3mm D形孔 橡胶轮",
    "64T 钢齿轮轮芯（备选）": "64T 钢齿轮 3mm D孔 30mm 机器人 小车",
    "3 mm D 孔轮毂或夹紧适配器": "3mm D孔 轮毂 N20 电机 联轴器 M2 固定",
    "齿轮外圈防滑圈": "橡胶 O型圈 热缩管 30mm 35mm 轮胎 防滑",
    "迷你万向轮": "迷你万向轮 10mm 15mm 球形 小车",
    "18650 锂电池": "18650 锂电池 3.7V 2600mAh 带保护板 持续放电3A",
    "单节 18650 电池盒带线": "单节 18650 电池盒 带线 1节",
    "5 V 升压模块": "锂电池升压模块 3.7V转5V 2A 3A",
    "电源开关与保护": "迷你拨动开关 1S 18650 保护板 3.7V",
    "小喇叭": "4欧3W 小喇叭 直径40mm 以下",
    "WS2812B 灯条或灯环（外接车灯模块）": "WS2812B 8位 灯环 5V 30mm",
    "WS2812B 数据电平转换器（推荐）": "SN74AHCT1G125 74AHCT125 74HCT14 3.3V转5V 电平转换",
    "黄铜丝/黄铜棒": "黄铜棒 2mm 3米 1.5mm 2米 1mm 黄铜丝 模型 DIY",
    "M2 铜柱套装": "M2 铜柱套装 双通 6 8 10 12 15 20mm 螺丝",
    "层板材料": "FR4 洞洞板 2mm 110x72 84x58 亚克力板",
    "JST/杜邦线套装": "MX1.25 2P 电池线 SH1.0 14P 转接线 28AWG 杜邦线",
    "热缩管与绝缘材料": "热缩管 1mm 2mm 3mm 5mm Kapton 高温胶带",
}


FIT_CHECKS = [
    ("ESP32 双眼板宽度", "官方 93.5 mm", "概念图写 90 mm 宽", "修正后可用", "把 90 mm 定义为车架/底盘宽；双眼板和护栏允许外凸到约 100-104 mm。"),
    ("安装螺丝规格", "官方图标注 Phi M2", "曾考虑 M3 铜柱", "冲突已规避", "主控板固定全部使用 M2 铜柱和 M2 螺丝，M3 不匹配孔位。"),
    ("N20 轮毂孔径", "常见为 3 mm D 形轴", "部分轮子是 2 mm 圆孔", "采购风险", "下单时明确选择 3 mm D 形孔轮胎。"),
    ("N20 电机本体", "约 34 x 12 x 10 mm，含输出轴", "两个电机要放入 90 mm 车架", "可用", "输出轴朝外安装，中间预留 18-22 mm 走线空间。"),
    ("18650 电池盒", "约 76 x 21 x 20 mm", "底盘目标长度 120 mm", "可用", "沿底层长度方向安装，并保留 USB-C 访问空间。"),
    ("主电源", "主控板有 3.7 V 电池充放电接口", "电机启动电流较大", "分支供电可用", "电池分别供给主控电池接口和独立电机升压支路，所有 GND 共地。"),
    ("电机驱动能力", "ESP32 只能输出控制信号", "不能直接驱动 N20 电机", "已配置", "采购清单已列 DRV8833 双路 H 桥电机驱动板，这是必备功率驱动板。"),
    ("电机控制引脚", "主控板外露备用引脚有限", "DRV8833 直连需 4 个 GPIO", "风险已处理", "Mk.1 使用 PCA9685 通过 I2C 输出 PWM；若省略，必须先确认 4 个安全 GPIO。"),
    ("双板 UART 控制", "DualEye 可作为语音/HMI 主控，底盘板负责电机执行", "若采用成品底盘或第二块开发板，需要明确通信和供电边界", "可选方案已补充", "官方接口表确认 LCD1-Board SH1.0 14PIN 的 Pin 9 为 UART_RXD、Pin 10 为 UART_TXD；DualEye TX/RX/GND 与底盘板 RX/TX/GND 交叉连接，电机 VM 不从 DualEye 取电。"),
    ("车灯/RGB", "主控板没有集成车灯或装饰 RGB", "无法直接实现前灯/状态灯效果", "已配置", "采购清单已列 WS2812B 灯条或灯环，作为外接灯光模块。"),
    ("WS2812B 数据电平", "WS2812B 灯板按 5 V 供电", "ESP32 3.3 V 数据高电平低于严格规格裕量", "推荐补强", "推荐加 SN74AHCT1G125/74AHCT125/74HCT14 电平转换器，短线直连只作为临时测试。"),
    ("外设接线接口", "官方仅外露 UART、I2C 和部分 I/O，小间距接口为主", "直接插杜邦线不现实", "需要补强", "采购清单中把 SH1.0/FPC 转接线列为建议必备，先转成常规排针再接驱动板。"),
    ("音频模块", "主控板已有音频编解码、功放、麦克风和喇叭接口", "MAX98357A/INMP441 功能重复", "不需要", "Mk.1 只买小喇叭；除非板载麦克风效果不足，再另加外置麦克风。"),
    ("轮径", "优先 34 mm，可用 43 mm", "影响前保险杠高度和速度", "带备注可用", "图纸按 34 mm 设计；若用 43 mm，抬高保险杠并限制 PWM。"),
    ("充电模块", "主控板已有充电管理", "TP4056 与板载充电重复", "避免使用", "Mk.1 不加 TP4056，除非电池与主控板充电路径隔离。"),
]


SOURCES = [
    ("Waveshare ESP32-S3-DualEye-Touch-LCD-1.28 文档", "https://docs.waveshare.com/ESP32-S3-DualEye-Touch-LCD-1.28", "用于核对主控板功能、板载电池接口、音频资源、LCD1 SH1.0 14PIN UART 针位和尺寸图。"),
    ("Waveshare 官方尺寸图", "https://docs.waveshare.com/assets/images/ESP32-S3-DualEye-Touch-LCD-1.28-details-size-0c9d4ea9bb5aeb50a666969698fec9e4.webp", "用于核对 93.50 mm 宽度、36.50 mm 显示高度和 M2 安装孔。"),
    ("TI DRV8833 数据手册", "https://www.ti.com/lit/gpn/DRV8833", "用于核对 DRV8833 供电范围和电流能力。"),
    ("Handson Technology GA12-N20 数据手册", "https://www.handsontec.com/dataspecs/motor_fan/GA12-N20.pdf", "用于核对 N20 典型尺寸和 3 mm D 形轴。"),
    ("NXP PCA9685 产品页", "https://www.nxp.com/products/power-drivers/lighting-driver-and-controller-ics/led-drivers/16-channel-12-bit-pwm-fm-plus-ic-bus-led-driver%3APCA9685", "用于核对 I2C 16 通道 12 位 PWM 控制特性。"),
    ("Worldsemi WS2812B 数据手册", "https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf", "用于核对 WS2812B 5 V 供电、数据输入高电平和单线数据特性。"),
    ("TI SN74AHCT1G125 数据手册", "https://www.ti.com/lit/gpn/sn74ahct1g125", "用于核对 AHCT 单路缓冲器可作为 3.3 V 到 5 V 数据电平转换。"),
    ("18650 电池尺寸参考", "https://www.neware.net/news/18650-battery-everything-you-need-to-know/230/46.html", "用于核对 18650 标称 18 mm 直径和 65 mm 长度。"),
    ("MimiClaw GitHub", "https://github.com/memovai/mimiclaw", "用于核对 ESP32-S3 上运行 OpenClaw-like AI assistant 的基础思路、ESP-IDF 和 16 MB flash / 8 MB PSRAM 需求。"),
    ("MimiClaw 项目页", "https://mimiclaw.io/", "用于核对 MimiClaw 的纯 C、Telegram/WebSocket、工具调用、GPIO/传感器/执行器控制定位。"),
]


FILE_INDEX = [
    ("说明.md", "项目总览、设计定版、能力缺口", "先读"),
    ("采购清单_Atlas_Rover_Mk1_V1.0.csv", "可导入表格的 BOM", "下单核价"),
    ("采购清单_Atlas_Rover_Mk1_V1.0.md", "可阅读版 BOM", "下单核对"),
    ("采购执行表_Atlas_Rover_Mk1_V1.0.xlsx", "Excel 版全量采购执行表，含三平台 URL", "下单执行"),
    ("采买执行清单_Atlas_Rover_Mk1_V1.0.md", "平台搜索词、优先级、下单前检查", "下单执行"),
    ("规格核对_Atlas_Rover_Mk1_V1.0.md", "尺寸、接口、电源、模块冲突复审", "开工前复核"),
    ("装配指南_Atlas_Rover_Mk1_V1.0.md", "层级结构、黄铜下料、机械步骤", "制作车架"),
    ("接线说明_Atlas_Rover_Mk1_V1.0.md", "电源拓扑、接线作业表、极性安全", "焊线和通电"),
    ("详细组装与接线手册_Atlas_Rover_Mk1_V1.0.md", "完整施工手册：下料、弯焊、走线、调试", "实操主文档"),
    ("程序设计_Atlas_Rover_Mk1_V1.0.md", "双目表情、多主题页面、MimiClaw 语音与运动意图设计", "固件开发"),
    ("语音与双板UART控制方案_Atlas_Rover_Mk1_V1.0.md", "DualEye 语音入口、UART 下发运动指令、底盘板执行", "双板/成品底盘"),
    ("组装模拟与效果确认_Atlas_Rover_Mk1_V1.0.md", "按参考图模拟三层装配、外观还原度和风险点", "开工前推演"),
    ("一致性复审报告_Atlas_Rover_Mk1_V1.0.md", "跨文件一致性结论和仍需实测项", "最终复查"),
    ("资料来源_Atlas_Rover_Mk1_V1.0.md", "官方/数据手册来源链接", "溯源"),
    ("图纸/*.svg", "可编辑矢量图纸", "打印/修改"),
    ("输出/PDF/Atlas_Rover_Mk1_制造图纸包_V1.0.pdf", "可打印整包", "施工打印"),
]


DECISION_MATRIX = [
    ("主控板", "ESP32-S3-DualEye-Touch-LCD-1.28", "已有/核心", "所有文件一致"),
    ("紧固件", "M2 铜柱和 M2 螺丝", "必买", "所有文件禁止 M3"),
    ("驱动形式", "4 个 N20 + 4 个 64T 齿轮轮，左右两侧差速", "必买", "参考图四轮姿态可落地，但不做四轮独立闭环"),
    ("电机功率驱动", "DRV8833 双路 H 桥", "必买", "ESP32/PCA9685 不直接带电机"),
    ("控制信号扩展", "PCA9685 I2C PWM", "推荐", "减少占用 GPIO；若省略必须确认 4 个安全 GPIO"),
    ("双目程序", "表情主页 + 时钟/状态/语音/设置多主题页面", "必做", "DualEye 是项目第一视觉入口"),
    ("MimiClaw", "本地安全意图层 + agent 工具调用适配层", "推荐", "运动命令先经规则和超时保护，再下发底盘"),
    ("双板语音控制", "DualEye 通过 UART 控制底盘板", "可选", "成品底盘/履带底盘优先采用，电机电流不走 DualEye"),
    ("车灯/RGB", "WS2812B 灯条或灯环", "推荐", "主控板无集成装饰 RGB"),
    ("WS2812B 数据可靠性", "AHCT/HCT 电平转换器", "推荐", "5 V 灯板下推荐把 ESP32 3.3 V 数据提升到 5 V 逻辑"),
    ("音频", "外接小喇叭", "推荐", "主控已有音频链路，不再买 MAX98357A"),
    ("充电", "使用主控板电池接口/充电路径", "已有", "不并联 TP4056"),
    ("结构", "2.0 mm 黄铜约 3 m + 1.5 mm 黄铜约 2 m", "必买", "与下料表、采买清单一致"),
]


OPEN_ITEMS = [
    ("最终 GPIO", "官方文档已确认 LCD1 SH1.0 14PIN 上有 UART_RXD/UART_TXD；I2C 和额外安全 GPIO 仍需要按你手上板子的例程/引脚图确认。"),
    ("电池接口极性", "MX1.25/JST 线序不同商家可能相反，第一次插电池前必须万用表确认。"),
    ("模块孔位", "DRV8833、PCA9685、升压模块、电池盒来自不同商家，最终钻孔前必须实测。"),
    ("WS2812B 数据线", "短线可直连测试，但正式版建议使用 AHCT/HCT 电平转换器。"),
    ("黄铜焊接", "酸性助焊剂只能用于结构件，焊后清洗，电子板附近补焊需拆电池并隔热。"),
]


BRASS_CUTS = [
    ("2.0 mm", "底部侧梁", "2", "120", "240", "直线", "左右各一根，先做底部 120 x 90 mm 矩形框"),
    ("2.0 mm", "底部前/后横梁", "2", "90", "180", "直线", "与侧梁搭接焊，角位留 2-3 mm 搭接余量"),
    ("2.0 mm", "中层侧护栏", "2", "112", "224", "轻微外弯", "位于电子层外侧，避开 USB-C 和开关"),
    ("2.0 mm", "中层前/后横梁", "2", "86", "172", "直线", "作为 PCA9685、DRV8833 外侧保护框"),
    ("2.0 mm", "上层侧梁", "2", "105", "210", "直线", "车头向上收窄，参考图里的上笼架"),
    ("2.0 mm", "上层前/后横梁", "2", "82", "164", "直线", "连接顶部四角立柱"),
    ("2.0 mm", "四角竖柱", "4", "58", "232", "直线", "底框到中层/上层过渡，先点焊再校正垂直"),
    ("2.0 mm", "前 A 柱斜撑", "2", "72", "144", "轻弯", "从前保险杠上扬到双眼板两侧，形成参考图前脸"),
    ("2.0 mm", "后部竖柱/喇叭护位", "2", "58", "116", "直线", "给后部小喇叭和线束留保护边"),
    ("2.0 mm", "前下保险杠 U 形杆", "1", "150", "150", "U 形弯", "绕 18-22 mm 圆柱弯两端，装在车灯下方"),
    ("2.0 mm", "前灯条横杆", "1", "92", "92", "直线", "固定 WS2812B 灯条或灯环支架"),
    ("2.0 mm", "后保险杠 U 形杆", "1", "135", "135", "U 形弯", "保护后部喇叭和充电/烧录线束"),
    ("2.0 mm", "顶部提手", "1", "170", "170", "U 形弯", "绕 30-35 mm 圆柱弯，腿部焊在上层横梁"),
    ("2.0 mm", "左右轮拱护栏", "2", "170", "340", "大弧弯", "绕 34-40 mm 圆柱弯，确保轮胎不擦"),
    ("1.5 mm", "双眼护圈", "2", "145", "290", "圆环", "绕 42-46 mm 圆柱弯，护圈只保护外缘，不压触摸屏"),
    ("1.5 mm", "护眼圈连接耳", "4", "25", "100", "短直线", "连接护圈到车头小立柱，可最后微调"),
    ("1.5 mm", "侧面斜撑", "6", "55", "330", "斜撑", "参考图侧面 X/斜杆效果，左右对称布置"),
    ("1.5 mm", "走线/扎线桥", "6", "35", "210", "短 U 形", "给电机线、灯线、喇叭线做固定点"),
    ("1.5 mm", "前灯小支架", "2", "35", "70", "L 形", "灯条背后支撑，灯板与黄铜之间加绝缘"),
    ("1.5 mm", "后喇叭护条", "4", "45", "180", "直线", "做参考图后部圆形喇叭格栅效果"),
]


BRASS_SUMMARY = [
    ("2.0 mm 黄铜棒", "约 2.57 m", "建议买 3 m", "主承力框架、保险杠、顶部提手、轮拱"),
    ("1.5 mm 黄铜棒", "约 1.18 m", "建议买 2 m", "护眼圈、斜撑、灯架、喇叭护条"),
    ("0.8-1.0 mm 细铜/黄铜丝", "约 0.6 m", "可选买 1 m", "绑扎焊点、装饰绕线；不承担主结构"),
]


ASSEMBLY_STEPS = [
    ("0", "准备基准", "打印或照着 `AR-MK1-003_底板模板.svg` 在纸上画 110 x 72 mm 底板和 120 x 90 mm 外框。所有黄铜件先在纸样上比对，再剪切。", "底板中心线、轮轴线、电池位置都标好。"),
    ("1", "底板开孔", "切 110 x 72 mm FR4/亚克力底板，钻 M2 铜柱孔、电池扎带孔、万向轮孔和电机固定孔。", "底板边缘打磨，M2 螺丝能顺利穿过。"),
    ("2", "先装动力底层", "固定 18650 电池盒、四个 N20 电机、四个 64T 齿轮轮、5 V 大电流升压模块和电机支路开关。先不装黄铜笼架。", "四轮不擦底板，四轮能同时接触桌面，电池能取出，开关能从侧面拨到。"),
    ("3", "焊底部黄铜矩形框", "用 2.0 mm 黄铜棒焊 120 x 90 mm 底框。四角可用 0.8-1.0 mm 细铜丝绕 2-3 圈后再上锡，强度更好。", "底框放桌面不翘，四角基本方正。"),
    ("4", "做中层保护框", "焊中层侧护栏和前后横梁，高度约 Z = 42-48 mm。该层保护 PCA9685、DRV8833 和线束。", "模块上方至少留 3 mm 空隙，USB-C 不被挡。"),
    ("5", "安装电子中层", "把 PCA9685、DRV8833 固定在中层板或底板上，先插线再最终锁紧。", "驱动板焊盘不碰黄铜，信号线有余量。"),
    ("6", "做前脸和双眼支架", "安装 ESP32 双眼板，使用 M2 铜柱。再做前 A 柱、双眼护圈和前灯条横杆。护圈最后焊，避免压屏。", "双眼板宽 93.5 mm 可轻微外凸，护圈不遮挡触摸区。"),
    ("7", "做顶部和侧面笼架", "焊上层侧梁、横梁、顶部提手和左右轮拱护栏。侧面斜撑用 1.5 mm 黄铜棒，左右对称。", "整车高度控制在约 110 mm，轮子能自由转。"),
    ("8", "走线固定", "电池线、电机线沿底部内侧走；I2C 和灯光数据线沿中层走；喇叭线走车尾或车头内侧。每 30-40 mm 用热缩管或扎线桥固定。", "线束不碰轮胎，不被黄铜边缘磨破。"),
    ("9", "分阶段通电", "先只给主控上电；再接 PCA9685；再接 DRV8833 但悬空车轮；最后接 WS2812B 和喇叭。", "没有发热、异味、重启和异常电机抖动。"),
]


WIRING_ROWS = [
    ("电池主支路", "18650 电池盒 BAT+ / BAT-", "ESP32 电池接口 BAT+ / BAT-", "MX1.25 2P 或原厂电池线", "先用万用表确认极性。若做 Y 线，主控支路仍接电池口，不要把 5 V 升压输出接进电池口。"),
    ("电机电源支路", "18650 BAT+", "电机支路开关 -> 5 V 升压 IN+", "22-24 AWG 红线", "这一路给电机和车灯供电；建议只在电机支路加独立开关。"),
    ("电源共地", "电池 BAT- / 升压 OUT- / ESP32 GND", "PCA9685 GND / DRV8833 GND / WS2812B GND", "22-24 AWG 黑线或小分线板", "所有 GND 必须共地，否则电机和灯的控制信号会不稳定。"),
    ("升压到电机驱动", "5 V 升压 OUT+ / OUT-", "DRV8833 VM / GND", "22-24 AWG 红黑线", "VM 只接电机电源。不要从 ESP32 3.3 V 给电机供电。"),
    ("ESP32 到 PCA9685", "ESP32 SDA / SCL / 3V3 / GND", "PCA9685 SDA / SCL / VCC / GND", "SH1.0/FPC 转接后用 26-28 AWG", "PCA9685 逻辑电源用 3.3 V。常见舵机板上的 V+ 端子本项目可不接。"),
    ("PCA9685 到 DRV8833", "CH0 / CH1 / CH2 / CH3", "AIN1 / AIN2 / BIN1 / BIN2", "26-28 AWG 信号线", "PCA9685 只发控制信号，真正给电机电流的是 DRV8833。若模块有 SLEEP/STBY，引到 3V3 使能。"),
    ("DRV8833 到电机", "AOUT1/AOUT2；BOUT1/BOUT2", "左 N20；右 N20", "电机自带线或 24-26 AWG", "如果前进时某个轮子反转，交换该电机两根线即可。"),
    ("双板 UART 控制（可选）", "LCD1 SH1.0 14PIN：Pin10 UART_TXD / Pin9 UART_RXD / Pin2 或 Pin6 GND", "底盘控制板 RX / TX / GND", "26-28 AWG 信号线 + SH1.0 14P 转接线", "Pin10 接底盘板 RX，Pin9 接底盘板 TX，GND 必须共地。ESP32-S3 UART 是 3.3 V TTL，不要接 RS232；若底盘板 TX 是 5 V，先做分压或电平转换再进 DualEye RX。"),
    ("底盘板逻辑供电（可选）", "5 V 升压 OUT+ / GND", "底盘控制板 5V/VIN / GND", "24-26 AWG 红黑线", "仅在底盘板需要外部逻辑电源且确认可接 5 V 时使用。电机供电仍走底盘板电机电源端/DRV8833 VM，不要从 DualEye 板取电。"),
    ("WS2812B 数据转换", "ESP32 空闲数据 GPIO", "AHCT/HCT 电平转换器 A 输入", "26-28 AWG 信号线", "电平转换器 VCC 接 5 V，GND 共地；若使用 74AHCT1G125，OE 按模块说明使能。短线直连只适合临时测试。"),
    ("WS2812B 车灯", "5 V 升压 OUT+ / GND；电平转换器 Y 输出", "WS2812B 5V / GND / DIN", "电源 24-26 AWG；数据 26-28 AWG", "Y 输出经 330 欧电阻到 DIN，灯板 5 V/GND 旁并 470-1000 uF 电容。亮度限制在 20-40%。"),
    ("小喇叭", "ESP32 SPK+ / SPK-", "4 欧 3 W 或 8 欧 1-2 W 小喇叭", "细软双绞线", "不要把喇叭任一端接 GND，按主控板喇叭接口两端直接接。"),
]


WIRE_LENGTHS = [
    ("22-24 AWG 红线", "约 0.8-1.0 m", "电池正极、电机支路、升压到 DRV8833/WS2812B"),
    ("22-24 AWG 黑线", "约 0.8-1.0 m", "共地母线、电机支路负极"),
    ("26-28 AWG 多色信号线", "约 1.5-2.0 m", "I2C、PCA9685 到 DRV8833、WS2812B 数据和电平转换"),
    ("细软双绞线", "约 0.3 m", "小喇叭线，尽量远离电机线"),
    ("热缩管", "1-5 mm 混装", "每个焊点、线束穿过黄铜处、灯条背面都做绝缘"),
]


DUALEYE_UART_PIN_ROWS = [
    ("LCD1-Board SH1.0 14PIN Pin 10", "UART_TXD", "接底盘控制板 RX，DualEye 下发 AR1 运动/灯光指令"),
    ("LCD1-Board SH1.0 14PIN Pin 9", "UART_RXD", "接底盘控制板 TX；若底盘板 TX 是 5 V TTL，先分压或电平转换"),
    ("LCD1-Board SH1.0 14PIN Pin 2 或 Pin 6", "GND", "接底盘控制板 GND，必须共地"),
    ("LCD1-Board SH1.0 14PIN Pin 5", "3V3", "仅作 3.3 V 逻辑参考或低功耗外设供电；不要给底盘板/电机供电"),
]


VOICE_UART_ROWS = [
    ("角色分工", "DualEye 板", "语音入口、唤醒/命令词识别、双眼表情、Wi-Fi/BLE 交互、串口下发动作命令"),
    ("角色分工", "底盘控制板", "接收 UART 指令，做限速/超时保护，控制普通 N20 + DRV8833 开环短时差速底盘"),
    ("官方接口确认", "LCD1-Board SH1.0 14PIN", "Pin 9 = UART_RXD，Pin 10 = UART_TXD，Pin 2/Pin 6 = GND，Pin 5 = 3V3；施工建议用 SH1.0 14P 转杜邦/排针转接线。"),
    ("信号连接", "DualEye Pin10 UART_TXD -> XIAO D7/GPIO20/RX", "3.3 V TTL 串口，短线即可；不要接 RS232 电平"),
    ("信号连接", "DualEye Pin9 UART_RXD <- XIAO D6/GPIO21/TX", "XIAO 是 3.3 V TTL，可直连；若替换成 5 V TTL 底盘板才需要分压或电平转换"),
    ("信号连接", "DualEye Pin2/Pin6 GND <-> 底盘板 GND", "必须共地，否则串口和电机控制会不稳定"),
    ("逻辑供电", "5 V 支路 -> 底盘板 5V/VIN", "只在底盘板需要外部逻辑供电且接口允许 5 V 时使用；不要反灌 DualEye"),
    ("电机供电", "电池/升压/底盘板 VM -> 电机驱动", "电机电流不经过 DualEye；DualEye 只发命令"),
    ("协议防误触发", "命令必须以 AR1, 开头", "底盘板丢弃所有不带 AR1 前缀的串口内容，避免 DualEye 启动日志、调试打印或乱码误触发电机。"),
    ("安全策略", "底盘板按 duration_ms 截止停车", "每条运动命令都必须带持续时间；语音识别卡住、串口断开或 DualEye 重启时，小车也会到时自动停下"),
]


VOICE_COMMAND_ROWS = [
    ("AR1,MOVE,F,60,800", "前进，速度 60%，持续 800 ms"),
    ("AR1,MOVE,B,50,500", "后退，速度 50%，持续 500 ms"),
    ("AR1,TURN,L,30,350", "左轮后退/右轮前进或差速左转，速度 30%，持续 350 ms"),
    ("AR1,TURN,R,30,350", "左轮前进/右轮后退或差速右转，速度 30%，持续 350 ms"),
    ("AR1,STOP", "立即停车"),
    ("AR1,LIGHT,BREATH", "切换前灯/状态灯呼吸效果"),
]


PROGRAM_MODULES = [
    ("app_main", "系统启动、任务创建、全局事件总线", "初始化显示、触摸、音频、Wi-Fi、UART、NVS 配置"),
    ("ui_shell", "页面管理和主题切换", "管理双目主页、时钟页、状态页、语音页、设置页"),
    ("eye_engine", "双眼渲染和表情动画", "眨眼、瞳孔跟随、眼皮曲线、情绪过渡、低帧率省电"),
    ("theme_manager", "多主题资源管理", "赛博蓝、黄铜复古、夜间低亮、调试高对比等主题"),
    ("voice_service", "语音入口", "板载麦克风收音、唤醒/命令词、VAD、录音状态上报"),
    ("mimiclaw_adapter", "MimiClaw 适配层", "把自然语言/agent tool call 转成标准 RoverIntent，不直接驱动电机"),
    ("intent_router", "意图路由和安全裁剪", "优先处理 STOP；限制速度/时长；低置信度要求确认"),
    ("rover_link", "底盘通信", "通过 UART 文本协议向底盘板下发 AR1,MOVE/AR1,TURN/AR1,STOP/AR1,LIGHT"),
    ("safety_watchdog", "安全看门狗", "运动命令超时、串口断开、低电量、过流/异常复位时进入 STOP"),
    ("storage", "本地配置", "保存主题、音量、亮度、唤醒词开关、上次页面；不保存危险运动状态"),
]


UI_PAGES = [
    ("双目表情主页", "默认页面", "开心、思考、聆听、说话、惊讶、生气、困倦、睡眠、移动中、错误", "低延迟渲染，适合巡游时常驻"),
    ("时钟主题页", "桌面陪伴/待机", "双圆屏分别显示小时/分钟、模拟表盘、日期、电量、Wi-Fi", "触摸或语音“显示时间/回到眼睛”切换"),
    ("语音交互页", "聆听/思考/回答", "左眼显示输入波形，右眼显示状态环；回答时恢复说话表情", "MimiClaw 进入思考时切换 Thinking"),
    ("小车状态页", "调试和运行", "速度、方向、底盘板在线、UART 延迟、电池电压、灯光模式", "调试阶段常用，正式巡游可隐藏"),
    ("主题设置页", "触摸操作", "主题、亮度、音量、表情风格、时钟样式、语音开关", "长按或双击进入，避免误触"),
    ("错误/安全页", "异常状态", "串口断开、低电量、底盘超时、语音未联网、MimiClaw 不可用", "必须提供 STOP 和回到主页"),
]


EXPRESSION_STATES = [
    ("idle", "待机", "轻微呼吸、随机眨眼、瞳孔慢速漂移", "巡游空闲、桌面陪伴"),
    ("happy", "开心", "上弧笑眼、蓝色高光增强", "识别成功、完成指令"),
    ("listen", "聆听", "瞳孔放大、外圈脉冲、低亮波形", "唤醒词后录音"),
    ("thinking", "思考", "眼睛向上/侧看、加载弧线", "MimiClaw 推理中"),
    ("speaking", "说话", "眼皮随音量轻动、嘴形可用前灯辅助", "TTS 或提示音输出"),
    ("moving", "移动中", "瞳孔朝运动方向偏移，边缘流光", "前进/后退/转向"),
    ("surprised", "惊讶", "圆眼放大、短闪", "突发障碍/命令冲突"),
    ("angry", "生气", "斜眼皮、红/橙警示", "拒绝危险指令或错误"),
    ("sleepy", "困倦", "半闭眼、低亮、慢眨眼", "长时间待机"),
    ("error", "错误", "警示图标/断线符号", "底盘断开、低电量、语音服务异常"),
]


MIMICLAW_INTEGRATION_ROWS = [
    ("本地安全层", "优先级最高", "STOP、前进、后退、左转、右转、显示时间、切换表情等固定命令先由本地规则解析，保证离线可用。"),
    ("MimiClaw 端侧模式", "中期目标", "若确认 DualEye 具备足够 flash/PSRAM，可把 MimiClaw/OpenClaw-like agent 编进 ESP-IDF 固件，用 tool call 调用 rover.move/eyes.set_expression。"),
    ("外部宿主/调试桥模式", "备选/增强", "若暂时使用 Mac/本地宿主，DualEye 作为语音和显示终端，通过 Wi-Fi WebSocket/HTTP 与宿主通信，再由 DualEye 转 UART 控底盘。"),
    ("云端/Telegram 模式", "可选", "MimiClaw 原始思路支持 Telegram/LLM API；Mk.1 只把它作为远程调试/长文本理解，不把云端回答直接当运动命令。"),
    ("工具调用白名单", "必须", "只开放 rover.move、rover.turn、rover.stop、eyes.set_expression、ui.set_page、lights.set_mode；不开放任意串口写入。"),
    ("意图确认", "必须", "长距离、长时间、高速、离开桌面等指令必须二次确认；低置信度只回答澄清，不动车。"),
]


ROVER_INTENT_ROWS = [
    ("前进/往前走/向前一点", "AR1,MOVE,F,40,500", "默认低速短时，避免从桌面冲出"),
    ("后退/退一点", "AR1,MOVE,B,35,400", "默认更低速，防止后方线束或障碍"),
    ("左转/向左看/左拐", "AR1,TURN,L,30,350", "普通 N20 + 万向轮开环转向，只按时间/PWM 标定"),
    ("右转/向右看/右拐", "AR1,TURN,R,30,350", "普通 N20 + 万向轮开环转向，只按时间/PWM 标定"),
    ("停下/别动/急停", "AR1,STOP", "最高优先级，任何状态立即执行"),
    ("开心一点/生气/睡觉", "EXPR,happy / EXPR,angry / EXPR,sleepy", "只改表情，不动底盘"),
    ("显示时间/切换时钟", "PAGE,clock", "切换到时钟主题页"),
    ("回到眼睛/主页", "PAGE,eyes", "回到双目表情主页"),
]


PROGRAM_MILESTONES = [
    ("P0", "双屏点亮和页面框架", "双眼主页、时钟页、状态页能触摸/串口切换"),
    ("P1", "表情引擎", "idle/happy/listen/thinking/speaking/moving/error 八个核心表情可平滑切换"),
    ("P2", "UART 底盘协议", "DualEye 能发 AR1,MOVE/AR1,TURN/AR1,STOP，底盘板能超时停车"),
    ("P3", "本地语音命令", "固定命令词可离线或弱联网触发 STOP/移动/页面切换"),
    ("P4", "MimiClaw 适配", "自然语言转 RoverIntent，tool call 通过白名单和安全裁剪后下发"),
    ("P5", "整车联调", "语音、表情、灯光、底盘动作联动；低电量/断线/误识别进入安全状态"),
]


ASSEMBLY_SIMULATION = [
    ("底层：动力与移动", "18650 电池盒沿车身纵向放在底板中心偏后；四个 N20 电机布置在四角，3 mm D 轴朝外；四个 64T 齿轮轮同规格同高度。", "比两轮方案更接近参考图四轮姿态，底层电池横卧、四轮包围、低重心都能实现。", "四轮刚性落地会增加转向摩擦；四个轮径和电机高度要一致，转向先限制到 20-35% PWM。"),
    ("中层：主控与扩展", "中层板放 DRV8833、PCA9685、5 V 升压和分线点；黄铜中层护栏高度 42-48 mm，给模块上方留至少 3 mm。", "能做出参考图中间裸露电路板和黄铜护栏的效果。", "PCA9685/DRV8833 模块孔位不统一，钻孔前必须按实物摆位；不要让模块焊盘碰黄铜。"),
    ("驾驶舱：双眼显示", "ESP32 双眼板用 M2 铜柱固定在车头上层，板宽 93.5 mm，允许车头护栏外宽到 100-104 mm。", "能达到双圆屏“眼睛”居中的核心视觉。", "参考图 90 mm 宽不能按整车极限理解；否则双眼板会放不下。护眼圈只能护外缘，不能压触摸屏。"),
    ("前脸灯光", "WS2812B 短灯条放在双眼下方、保险杠上方；数据经 AHCT/HCT 电平转换，电源来自 5 V 支路。", "能实现参考图前部蓝白灯带/状态灯效果。", "主控板无集成车灯；灯板背面必须绝缘，亮度限制 20-40%，避免电流拉垮升压模块。"),
    ("后部声音", "小喇叭放车尾或后侧护栏内，接 ESP32 板载 SPK+ / SPK-。", "能近似参考图后部圆形喇叭格栅效果。", "不需要 MAX98357A；喇叭任一端不要接 GND。若喇叭直径接近 40 mm，车尾护栏需外扩。"),
    ("黄铜外观", "2.0 mm 黄铜做底框、上框、立柱、保险杠、轮拱和顶部提手；1.5 mm 黄铜做护眼圈、斜撑、灯架和喇叭护条。", "视觉风格可以接近参考图：黄铜笼架、双眼护圈、顶部提手、侧面轮拱都能实现。", "概念图为渲染效果，实际手工焊接会有焊点和轻微不对称；用纸样和夹具可显著改善。"),
    ("重心与行走", "重物集中在底层：电池、电机、升压模块；双眼板和黄铜上层较轻。", "桌面低速巡游可行，外观高度约 110 mm。", "顶部提手不要过重；首次落地只用低 PWM，若转向拖拽严重，降低防滑圈摩擦或缩短转向时间。"),
]


def ensure_dirs() -> None:
    for path in [PACK, DRAWINGS, PDF_OUT]:
        path.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, text: str) -> None:
    normalized = dedent(text).strip()
    # Interpolated Markdown tables can prevent textwrap.dedent from seeing the
    # template indentation. Strip the template's leading 8 spaces per line so
    # headings and tables render as Markdown instead of code blocks.
    normalized = "\n".join(line[8:] if line.startswith("        ") else line for line in normalized.splitlines())
    path.write_text(normalized + "\n", encoding="utf-8")


def purchase_links(item_name: str) -> tuple[str, str, str, str]:
    query = BUY_QUERIES[item_name]
    encoded = quote(query)
    return (
        query,
        f"https://search.jd.com/Search?keyword={encoded}",
        f"https://s.taobao.com/search?q={encoded}",
        f"https://mobile.yangkeduo.com/search_result.html?search_key={encoded}",
    )


def md_link(label: str, url: str) -> str:
    return f"[{label}]({url})"


def write_csv() -> None:
    with (PACK / "采购清单_Atlas_Rover_Mk1_V1.0.csv").open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.writer(f)
        writer.writerow(["分类", "名称", "规格", "数量", "是否必需", "备注", "估算价格_元", "推荐搜索词", "京东", "淘宝", "拼多多"])
        for item in BOM:
            query, jd, taobao, pdd = purchase_links(item["item"])
            writer.writerow([item["class"], item["item"], item["spec"], item["qty"], item["required"], item["notes"], item["est_cny"], query, jd, taobao, pdd])


def md_table(headers: list[str], rows: list[list[str] | tuple[str, ...]]) -> str:
    out = ["| " + " | ".join(headers) + " |", "| " + " | ".join(["---"] * len(headers)) + " |"]
    for row in rows:
        out.append("| " + " | ".join(str(x).replace("\n", "<br>") for x in row) + " |")
    return "\n".join(out)


def write_docs() -> None:
    bom_rows = []
    for b in BOM:
        query, jd, taobao, pdd = purchase_links(b["item"])
        bom_rows.append([
            b["class"],
            b["item"],
            b["spec"],
            b["qty"],
            b["required"],
            b["notes"],
            b["est_cny"],
            query,
            md_link("京东", jd),
            md_link("淘宝", taobao),
            md_link("拼多多", pdd),
        ])
    fit_rows = [[a, b, c, d, e] for a, b, c, d, e in FIT_CHECKS]
    src_rows = [[name, url, note] for name, url, note in SOURCES]
    file_rows = [[name, role, when] for name, role, when in FILE_INDEX]
    decision_rows = [[item, decision, priority, note] for item, decision, priority, note in DECISION_MATRIX]
    open_rows = [[item, note] for item, note in OPEN_ITEMS]
    simulation_rows = [[layer, layout, effect, risk] for layer, layout, effect, risk in ASSEMBLY_SIMULATION]
    dualeye_uart_pin_rows = [[pin, signal, use] for pin, signal, use in DUALEYE_UART_PIN_ROWS]
    voice_uart_rows = [[kind, item, note] for kind, item, note in VOICE_UART_ROWS]
    voice_command_rows = [[cmd, meaning] for cmd, meaning in VOICE_COMMAND_ROWS]
    program_module_rows = [[module, role, note] for module, role, note in PROGRAM_MODULES]
    ui_page_rows = [[name, use, content, note] for name, use, content, note in UI_PAGES]
    expression_rows = [[state, name, effect, scene] for state, name, effect, scene in EXPRESSION_STATES]
    mimiclaw_rows = [[mode, priority, note] for mode, priority, note in MIMICLAW_INTEGRATION_ROWS]
    rover_intent_rows = [[utterance, command, safety] for utterance, command, safety in ROVER_INTENT_ROWS]
    program_milestone_rows = [[stage, task, acceptance] for stage, task, acceptance in PROGRAM_MILESTONES]

    write_text(
        PACK / "说明.md",
        f"""
        # Atlas Rover Mk.1 制造图纸包 V1.0

        本图纸包定义的是第一版桌面巡游车，主控采用 ESP32-S3-DualEye-Touch-LCD-1.28。
        目标是一台可展示、可调试、可继续扩展的小型原型车：黄铜车架、四个 N20 物理电机、左右两侧差速、单节 18650 供电、双眼表情显示、板载音频，以及推荐外接 RGB 状态灯。

        ## 设计定版

        - 目标车身长度：120 mm
        - 黄铜车架/底盘宽度：90 mm
        - 实际最大外宽：100-104 mm，因为双眼板官方宽度为 93.5 mm
        - 目标高度：110 mm，包含顶部提手/笼架
        - 驱动形式：四个 N20 + 四个 64T 齿轮轮；控制上仍按左右两侧差速
        - 推荐轮径：优先 34 mm；43 mm 可用但需要调整前保险杠间隙
        - 紧固件标准：M2
        - 主控板安装：M2 铜柱，禁止使用 M3
        - 电机控制：DRV8833 加 PCA9685，通过 I2C 控制，布线最稳

        ## 文件说明

        - `采购清单_Atlas_Rover_Mk1_V1.0.csv`：下单采购清单
        - `采购执行表_Atlas_Rover_Mk1_V1.0.xlsx`：Excel 版全量采购执行表
        - `规格核对_Atlas_Rover_Mk1_V1.0.md`：尺寸与冲突核对
        - `装配指南_Atlas_Rover_Mk1_V1.0.md`：机械装配顺序
        - `接线说明_Atlas_Rover_Mk1_V1.0.md`：电源与信号接线
        - `详细组装与接线手册_Atlas_Rover_Mk1_V1.0.md`：黄铜下料、弯焊、走线和通电步骤
        - `程序设计_Atlas_Rover_Mk1_V1.0.md`：双目表情、多主题页面、MimiClaw 语音交互和运动意图设计
        - `语音与双板UART控制方案_Atlas_Rover_Mk1_V1.0.md`：DualEye 语音入口通过 UART 控制底盘板
        - `组装模拟与效果确认_Atlas_Rover_Mk1_V1.0.md`：按参考图推演能否达到效果
        - `一致性复审报告_Atlas_Rover_Mk1_V1.0.md`：跨文件一致性结论和仍需实测项
        - `资料来源_Atlas_Rover_Mk1_V1.0.md`：尺寸和规格核对来源
        - `图纸/*.svg`：可编辑矢量图纸
        - `输出/PDF/Atlas_Rover_Mk1_制造图纸包_V1.0.pdf`：可打印总览 PDF

        ## 制造备注

        原概念图标注车宽 90 mm，但真实双眼主控板宽度为 93.5 mm。因此 V1.0 将 90 mm 解释为底盘/黄铜车架宽度，允许双眼板和前护栏轻微外凸。

        ESP32 主控板已经包含电池充电、板载麦克风、音频编解码、功放和喇叭接口。Mk.1 中外置 TP4056、INMP441、MAX98357A 已从必买清单中移除。

        ## 能力缺口与解决方式

        | 能力缺口 | 解决方式 | 是否需要新增模块 | 采购清单对应项 |
        | --- | --- | --- | --- |
        | ESP32 不能直接驱动 N20 电机 | 加 DRV8833 双路 H 桥电机驱动板 | 必须 | `DRV8833 双路 H 桥电机驱动板（必备）` |
        | ESP32 可用 GPIO 可能不够 | 加 PCA9685 I2C PWM 扩展板 | 推荐 | `PCA9685 I2C PWM 扩展板（推荐）` |
        | 主控板没有车灯/RGB | 外接 WS2812B 灯条或灯环 | 推荐 | `WS2812B 灯条或灯环（外接车灯模块）` |
        | WS2812B 5 V 数据电平裕量不足 | 加 AHCT/HCT 数据电平转换器 | 推荐 | `WS2812B 数据电平转换器（推荐）` |
        | 主控板有音频但没有喇叭本体 | 外接小喇叭 | 推荐 | `小喇叭` |

        电机链路是：ESP32 发控制信号 -> PCA9685 分配 PWM/方向信号 -> DRV8833 输出电机电流 -> N20 电机转动。
        灯光链路是：ESP32 数据 GPIO -> AHCT/HCT 电平转换 -> 330 欧电阻 -> WS2812B DIN，5 V/GND 从电机/灯光电源支路提供。
        双板语音链路是：DualEye 板载麦克风/语音识别 -> UART 文本指令 -> 底盘控制板 -> 电机驱动。底盘板逻辑可另接 5 V，但电机供电不要从 DualEye 板上取。
        程序链路是：双目 UI 事件/语音事件 -> 本地安全意图解析 -> MimiClaw 工具调用适配 -> RoverIntent -> UART -> 底盘板/DRV8833 执行。
        """,
    )

    write_text(
        PACK / "规格核对_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 规格与适配核对

        ## 结论

        做完以下 V1.0 修正后，方案可以制造：

        - 使用 M2 紧固件，不使用 M3。
        - 将 90 mm 视为底盘/车架宽度，而不是整车绝对最大外宽，因为双眼板官方宽度为 93.5 mm。

        另一个实际修正是：除非已经确认主控板有 4 个安全可用 GPIO，否则建议加入 PCA9685 I2C PWM 模块。
        本轮复审还确认：电机供电支路需要更高电流余量，18650 需要关注持续放电能力，主控板小间距接口建议先转接再布线；WS2812B 正式版建议增加 AHCT/HCT 数据电平转换。

        ## 适配核对表

        {md_table(["核对项", "已确认/假设规格", "潜在问题", "状态", "V1.0 处理"], fit_rows)}

        ## 锁定尺寸

        - 整体视觉长度：120 mm
        - 底层安装板：110 x 72 mm
        - 黄铜车架/车体宽度：90 mm
        - 车头双眼板宽度：官方 93.5 mm
        - 最大外宽目标：100-104 mm
        - 目标高度：110 mm
        - 底层板材料：1.5-2.0 mm FR4 或亚克力
        - 中层板材料：1.5-2.0 mm FR4 或亚克力，84 x 58 mm
        - 铜柱：M2，10-20 mm 混装
        - 主黄铜棒：2.0 mm
        - 细节黄铜棒：1.5 mm
        - WS2812B 数据电平转换：SN74AHCT1G125、74AHCT125 或 74HCT14

        ## 从必买清单移除的部件

        - TP4056：除非电源路径隔离，否则与板载充电管理重复。
        - MAX98357A：主控板已有音频编解码、功放和喇叭接口。
        - INMP441：Mk.1 使用板载麦克风即可。
        - 额外 2.8 寸 TFT：会削弱双眼板造型，并增加供电/UI 复杂度。
        - 单块 DRV8833 并联四个电机：只适合空载短测，不适合作为正式方案。
        """,
    )

    write_text(
        PACK / "装配指南_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 装配指南

        ## 坐标系

        - X 轴：向前为正，向后为负
        - Y 轴：向左为正，向右为负
        - Z 轴：向上为正
        - 原点：底层安装板中心

        ## 层级结构

        1. 底层：18650 电池盒、电机支路开关、升压模块、线束固定。
        2. 驱动层：四个 N20 电机、两块 DRV8833、四个 64T 齿轮轮。
        3. 中层电子板：PCA9685、走线分配、推荐外接车灯/RGB。
        4. 车头层：ESP32 双眼主控板，使用 M2 铜柱固定；喇叭放在车头后方或下方。
        5. 黄铜笼架：2.0 mm 黄铜棒做主梁，1.5 mm 黄铜棒做斜撑。

        ## 参考图取舍

        参考图提供的是黄铜笼架、前灯、双眼护圈、顶部提手和后部喇叭位的造型方向。
        Mk.1 当前采用四个 N20 物理电机，但控制上仍是左右两侧差速，不追加四轮独立速度控制、TP4056、MAX98357A 或外置麦克风。
        黄铜轮拱和侧护栏可以做出四轮小车的视觉姿态，但四个 64T 齿轮轮都参与支撑和驱动。

        ## 黄铜材料总量

        {md_table(["材料", "理论用量", "建议购买", "用途"], BRASS_SUMMARY)}

        ## 黄铜下料表

        {md_table(["材料", "部位", "数量", "单根 mm", "小计 mm", "成形", "备注"], BRASS_CUTS)}

        ## 机械步骤

        {md_table(["阶段", "任务", "做法", "验收"], ASSEMBLY_STEPS)}

        ## 弯折与焊接方法

        1. 先把 120 x 90 mm 外框画在纸上，纸样贴在木板或切割垫上。
        2. 2.0 mm 黄铜棒用平口钳和圆柱辅助弯折，弯 U 形件时先从中心线开始，两边对称弯。
        3. 1.5 mm 护眼圈绕 42-46 mm 圆柱弯成圆环，切口留 3-5 mm 搭接。
        4. 焊接顺序是底框、竖柱、中层框、上层框、前脸、细节斜撑。每焊完一层都放桌面校平。
        5. 焊点可以先用 0.8-1.0 mm 细铜/黄铜丝绕 2-3 圈，再上锡或银焊，强度和视觉都会更接近参考图。
        6. 若使用酸性助焊剂，只能用于黄铜结构件；焊后必须清洗干净，不能把酸性助焊剂带到电子板上。
        7. 所有黄铜件尽量在电子板装车前焊完；必须补焊时，要拆下电池并用湿布/隔热片保护附近线束。

        ## 验收检查

        - 轮子能自由转动，不碰黄铜车架。
        - 前护眼圈不挤压双眼主控板。
        - USB-C 口可正常插入，用于充电和烧录。
        - 电池可拆卸，不需要拆焊。
        - 电机支路可以独立断电。
        - 任何裸露的电池正极线都不能接触黄铜车架。
        """,
    )

    write_text(
        PACK / "接线说明_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 接线说明

        ## 电源拓扑

        单节 18650 电池正极分为两条支路。推荐做一条短 Y 线，或者用小分线板分出主控支路和电机支路：

        - 主控支路：18650 接 ESP32 主控板电池接口，注意 MX1.25 极性。
        - 电机支路：18650 经电机支路开关进入 5 V 升压模块，升压后接 DRV8833 的 VM，并给 WS2812B 供 5 V。

        所有地线必须共地：

        - ESP32 GND
        - PCA9685 GND
        - DRV8833 GND
        - 升压模块 GND
        - 电池负极

        不要从 ESP32 的 3.3 V 给电机供电。不要把 5 V 升压输出接进主控板电池接口。

        ## 接线作业表

        {md_table(["线路", "从哪里来", "接到哪里", "线材", "注意事项"], WIRING_ROWS)}

        ## 线束长度建议

        {md_table(["线材", "建议准备", "用途"], WIRE_LENGTHS)}

        ## 推荐电机控制接线

        解决电机的新增板子是 DRV8833。ESP32 不能直接接 N20 电机，只能输出控制信号；DRV8833 才是给电机供电和换向的功率驱动板。
        PCA9685 是推荐的控制信号扩展板，不是必须的功率驱动板。

        ESP32 通过 I2C 总线连接 PCA9685：

        - ESP32 SDA -> PCA9685 SDA
        - ESP32 SCL -> PCA9685 SCL
        - ESP32 3V3 -> PCA9685 VCC
        - ESP32 GND -> PCA9685 GND

        常见 PCA9685 舵机模块会有 V+ 端子。Mk.1 不接舵机，不需要给 V+ 供电；本项目只用 VCC、GND、SDA、SCL 和 CH0-CH3 信号。

        PCA9685 连接 DRV8833：

        - PCA9685 CH0 -> DRV8833 AIN1
        - PCA9685 CH1 -> DRV8833 AIN2
        - PCA9685 CH2 -> DRV8833 BIN1
        - PCA9685 CH3 -> DRV8833 BIN2

        DRV8833 连接电机：

        - AOUT1/AOUT2 -> 左侧 N20 电机
        - BOUT1/BOUT2 -> 右侧 N20 电机
        - VM -> 升压模块输出的电机 5 V
        - GND -> 共地

        若 DRV8833 模块带 SLEEP、nSLEEP、STBY 或 EN 引脚，把它接到 3V3 或按模块说明拉高，否则电机可能不转。

        ## 可选双板 UART 语音控制

        如果后续使用成品轮式/履带底盘，或增加第二块底盘控制板，DualEye 建议只做语音入口和 HMI，不直接执行电机闭环。
        Waveshare 官方接口表确认 DualEye 外露 UART 位于 LCD1-Board SH1.0 14PIN 接口。施工时先用 SH1.0 14P 转杜邦/排针转接线引出，再接到底盘板。

        {md_table(["DualEye 接口针位", "信号", "接法/用途"], dualeye_uart_pin_rows)}

        双板连接使用 3.3 V TTL UART：

        - DualEye Pin10 UART_TXD -> XIAO D7 / GPIO20 / RX
        - DualEye Pin9 UART_RXD <- XIAO D6 / GPIO21 / TX
        - DualEye Pin2/Pin6 GND <-> XIAO GND

        若底盘板需要逻辑供电，可以从 5 V 升压支路给底盘板 5V/VIN 供电，但必须先确认该底盘板接口允许 5 V。
        电机供电仍接到底盘板电机电源端、DRV8833 VM 或成品底盘的电机电源端，不要从 DualEye 板取电，也不要让电机电流经过 DualEye。
        若底盘板 TX 是 5 V TTL，进入 DualEye RX 前要加分压或电平转换。

        推荐串口命令使用一行一条文本协议，并统一以 `AR1,` 开头，例如 `AR1,MOVE,F,60,800`、`AR1,TURN,L,30,350`、`AR1,STOP`。
        底盘板必须丢弃所有不带 `AR1,` 前缀的串口内容，避免 DualEye 启动日志、调试打印或乱码误触发电机；每条运动命令都必须带 `duration_ms`，底盘板按截止时间自动停车。

        ## 推荐外接车灯/RGB

        主控板没有集成车灯或装饰 RGB。若要实现图里的前灯/状态灯效果，需要外接 WS2812B 灯条或灯环。
        WS2812B 需要 5 V、GND 和 1 个 ESP32 数据 GPIO。PCA9685 不能生成 WS2812B 的单线数据协议，所以灯带数据线不要接 PCA9685。
        正式版建议加 SN74AHCT1G125、74AHCT125 或 74HCT14 这类 AHCT/HCT 电平转换器：ESP32 GPIO -> A 输入，Y 输出 -> 330 欧电阻 -> WS2812B DIN，电平转换器 VCC 接 5 V，GND 共地。
        最终引脚图确认前，先预留一个安全空闲 GPIO 给 WS2812B 数据输入。
        若使用多颗 LED，建议数据线串 330 欧电阻，并在 5 V/GND 间加 470-1000 uF 电容。
        8 颗 WS2812B 全白最大电流约 0.48 A，桌面车建议亮度限制在 20-40%。

        ## 音频

        Mk.1 直接使用 ESP32 主控板的喇叭接口和板载音频链路。
        除非你明确要绕过板载音频，否则不需要外置 MAX98357A。

        ## 极性与安全

        - 第一次插电池前，必须确认电池接口极性。
        - 第一次接入升压模块前，空载调到 5.0 V 左右，再接 DRV8833 和 WS2812B。
        - 电机支路必须加开关。
        - 使用带保护 18650，或外加 1S 保护板。
        - 电池线和电机线经过黄铜车架附近时必须绝缘。
        - 第一次电机测试时，把车抬离桌面。
        """,
    )

    write_text(
        PACK / "详细组装与接线手册_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 详细组装与接线手册

        ## 这张参考图怎么用

        参考图用于确定外观语言：黄铜笼架、前保险杠、双眼护圈、顶部提手、前灯条、后部喇叭位和侧面轮拱。
        电气方案仍以 V1.0 定版为准：四只 N20 电机、四个 64T 齿轮轮、两块 DRV8833 电机驱动、PCA9685 控制扩展、WS2812B 外接车灯、AHCT/HCT 数据电平转换、小喇叭外放。
        参考图表格里的 TP4056、MAX98357A、INMP441 不纳入 Mk.1 必买项。

        ## 黄铜材料怎么买

        {md_table(["材料", "理论用量", "建议购买", "用途"], BRASS_SUMMARY)}

        2.0 mm 是主结构，不建议低于 1.8 mm。1.5 mm 负责造型细节，比较好弯。0.8-1.0 mm 只用于绕焊点和装饰，不能替代主梁。

        ## 黄铜下料表

        {md_table(["材料", "部位", "数量", "单根 mm", "小计 mm", "成形", "备注"], BRASS_CUTS)}

        下料时每根多留 2-3 mm，弯好、试装后再修齐。左右对称件先切一根做样板，再按样板复制另一根。

        ## 推荐工具

        - 80 W 以上电烙铁或小型火焰焊枪。
        - 黄铜专用助焊剂、焊锡或银焊料。
        - 平口钳、尖嘴钳、斜口钳、小锉刀、砂纸。
        - 30-50 mm 圆柱体，用来弯双眼护圈、轮拱和顶部提手。
        - 万用表、热缩管、Kapton 胶带、扎带。
        - 纸样、胶带、直角尺，用来保证底框不歪。

        ## 组装总顺序

        {md_table(["阶段", "任务", "做法", "验收"], ASSEMBLY_STEPS)}

        ## 黄铜车架做法

        1. 先做底部 120 x 90 mm 矩形框。四根 2.0 mm 黄铜棒在纸样上对齐，四角搭接 2-3 mm 后点焊。
        2. 把底框放在桌面上压平，确认四角不翘，再焊满四角。
        3. 焊四角竖柱，先只点焊。竖柱垂直后，再焊中层侧护栏。
        4. 中层护栏高度建议 42-48 mm，用来保护电机驱动板、PCA9685 和线束。
        5. 前 A 柱向后微倾，形成参考图车头前脸。A 柱不要挡住双眼屏、USB-C 或触摸区域。
        6. 双眼护圈最后做。用 1.5 mm 黄铜棒绕 42-46 mm 圆柱成环，护圈只保护屏幕外缘，不压屏幕。
        7. 前灯条横杆装在双眼下方、保险杠上方。WS2812B 灯条背面必须贴绝缘胶带，再固定到支架。
        8. 顶部提手用 2.0 mm 黄铜棒弯 U 形，先在上层横梁临时绑扎，看高度和比例，再焊死。
        9. 侧面轮拱护栏绕 34-40 mm 圆柱弯弧，轮胎外侧至少留 2 mm 间隙。
        10. 斜撑最后补。斜撑是装饰和防扭，不要为了好看挡住拆电池、插 USB-C 或拨开关。

        ## 接线总原则

        先做电源线，再做共地，再做信号线，最后接灯和喇叭。
        所有经过黄铜车架附近的线都要套热缩管或贴 Kapton 胶带。黄铜车架只做结构和装饰，不作为电源负极或地线使用。

        ## 接线作业表

        {md_table(["线路", "从哪里来", "接到哪里", "线材", "注意事项"], WIRING_ROWS)}

        ## 线束长度建议

        {md_table(["线材", "建议准备", "用途"], WIRE_LENGTHS)}

        ## DualEye UART 接口针位

        {md_table(["DualEye 接口针位", "信号", "接法/用途"], dualeye_uart_pin_rows)}

        ## 走线建议

        - 电池线和电机电源线走底层内侧，尽量短，远离 ESP32 屏幕排线。
        - 电机线成对绞合，从左右两边贴底框走，进 DRV8833 前再汇合。
        - I2C 线从 ESP32 后方或侧面下到 PCA9685，SDA/SCL/GND/3V3 四根并行，长度尽量控制在 120 mm 内。
        - 若采用双板 UART，Pin10 UART_TXD、Pin9 UART_RXD、GND 三根线从 DualEye 后方下到底盘板，远离电机电源线；底盘板 TX 为 5 V 时先电平转换。
        - WS2812B 数据线从 ESP32 预留 GPIO 走到 AHCT/HCT 电平转换器，再经 330 欧电阻到前灯条 DIN。
        - 喇叭线走车尾或车头内侧，避开电机线，避免电机噪声串入音频。
        - 每隔 30-40 mm 做一个固定点，固定点可以用 1.5 mm 黄铜短 U 形扎线桥，但线和铜之间要有热缩管。

        ## 通电调试顺序

        1. 不装电池，先用万用表检查 BAT+ 和 GND 没有短路。
        2. 只接电池到 ESP32 主控板，确认双眼屏、触摸和 USB-C 正常。
        3. 空载接 5 V 升压模块，调到约 5.0 V，再断电。
        4. 接 PCA9685，运行 I2C 扫描或最小测试，确认地址能被识别。
        5. 接 DRV8833 控制线和电源线，但车轮悬空。用低 PWM 先测左轮，再测右轮。
        6. 若方向反了，只交换对应电机的 AOUT 或 BOUT 两根线。
        7. 接 AHCT/HCT 电平转换器和 WS2812B，亮度限制在 20-40%，先测红绿蓝三色。
        8. 接小喇叭，音量从低开始。喇叭两根线接 SPK+ 和 SPK-，不要接到 GND。
        9. 最后落地测试，先直行 30 cm，再测原地转向，再测低速巡游。

        ## 最容易出错的点

        - 把 M3 铜柱塞进 M2 孔：不要这样做，主控板固定统一用 M2。
        - 把 PCA9685 当电机驱动：它不是驱动板，DRV8833 才负责给电机输出电流。
        - 把 WS2812B 数据线接 PCA9685：不行，WS2812B 需要 ESP32 的单线数据 GPIO。
        - 5 V WS2812B 长期直接吃 ESP32 3.3 V 数据：短线可能能亮，但正式版建议加 AHCT/HCT 电平转换。
        - 把 5 V 升压输出接进主控板电池口：不要这样做。
        - 双板 UART 没有共地：不要这样做，GND 不共地会导致串口误码。
        - 把底盘板 5 V 或电机 VM 反灌到 DualEye：不要这样做，DualEye 只接自己的电池/USB/允许的逻辑接口。
        - 把小喇叭一端接 GND：不要这样做，按 SPK+ / SPK- 两端接。
        - 焊完黄铜不清洗助焊剂：残留会腐蚀铜件，也可能污染电子板。
        """,
    )

    write_text(
        PACK / "程序设计_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 程序设计

        ## 目标

        软件分成两个核心能力：

        1. 双目表情程序：两块圆屏显示眼睛、表情和动画效果，并支持切换时钟、状态、语音、设置等多主题页面。
        2. MimiClaw 语音交互：接入语音理解和自然语言指令，把“往前走一点”“停下”“显示时间”等话语转换成安全的 RoverIntent，再通过 UART 控制底盘板。

        注意：运动类命令必须先经过本地安全意图层。MimiClaw 或 LLM 只能产生结构化意图，不能直接写 UART、不能绕过 STOP/超时/限速保护。

        ## 总体架构

        ```text
        触摸/语音/Wi-Fi
            -> EventBus
            -> UI Shell + Eye Engine
            -> 本地命令词/MimiClaw Adapter
            -> Intent Router + Safety Watchdog
            -> Rover Link(UART 或本地 DRV8833/PCA9685)
            -> 底盘板/电机驱动
        ```

        ## 固件模块划分

        {md_table(["模块", "职责", "说明"], program_module_rows)}

        ## 多主题页面

        {md_table(["页面", "用途", "显示内容", "切换/备注"], ui_page_rows)}

        ## 双目表情状态

        {md_table(["状态 ID", "中文名", "视觉效果", "触发场景"], expression_rows)}

        ## MimiClaw 接入策略

        本项目优先适配 `memovai/mimiclaw`，不再按 MiniClaw 替代方案设计：

        - MimiClaw：ESP32-S3 端 OpenClaw-like 方案，适合未来直接嵌入固件；但直接集成前必须确认 DualEye 实际 flash/PSRAM 是否满足目标版本需求。
        - 外部宿主/调试桥：可作为端侧 MimiClaw 合并前的临时联调方式；DualEye 作为语音、表情和串口控制终端。

        {md_table(["模式/层级", "优先级", "设计说明"], mimiclaw_rows)}

        ## 自然语言到小车指令

        {md_table(["用户说法", "标准指令", "安全处理"], rover_intent_rows)}

        ## 工具调用白名单

        ```text
        rover.move(direction, speed_percent, duration_ms)
        rover.turn(direction, angle_deg)
        rover.stop()
        eyes.set_expression(expression_id)
        ui.set_page(page_id)
        lights.set_mode(mode_id)
        system.get_status()
        ```

        所有工具调用先进入 `intent_router`，再由 `safety_watchdog` 裁剪。任何超过 1000 ms 的连续运动、超过 60% 的速度、或语义不明确的移动指令，都要求二次确认。

        ## 页面与表情联动规则

        - 唤醒词触发：切到 `listen` 表情，语音页显示输入状态。
        - MimiClaw 推理中：切到 `thinking`，前灯可低亮呼吸。
        - 回答中：切到 `speaking`，眼皮或高光随音量变化。
        - 小车移动中：切到 `moving`，瞳孔偏向运动方向。
        - 收到 STOP：立即切 `idle` 或 `surprised`，底盘停车。
        - 底盘离线/低电量：切 `error`，拒绝运动指令。
        - 长时间无交互：切 `sleepy` 或时钟页。

        ## 里程碑

        {md_table(["阶段", "任务", "验收标准"], program_milestone_rows)}

        ## 实施建议

        第一版先做“表情 + 页面 + UART + 本地命令词”，确保小车能稳定听懂固定动作命令。
        第二版再接 MimiClaw，把自然语言、记忆和工具调用叠上去。这样研发风险最低：就算 agent 不在线，STOP、前进、后退、左转、右转、显示时间这些核心功能仍可用。
        """,
    )

    write_text(
        PACK / "语音与双板UART控制方案_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 语音与双板 UART 控制方案

        ## 结论

        若采用两块开发板或成品底盘，建议把 ESP32-S3-DualEye-Touch-LCD-1.28 定位为语音/HMI 主控：
        它负责板载麦克风收音、命令词识别、双眼表情和 Wi-Fi/BLE 交互；底盘控制板负责电机驱动、限速、转向和安全停车。
        两块板之间使用 3.3 V TTL UART 串口通信，指令用一行一条文本协议即可。
        官方资料确认 DualEye 外露 UART：LCD1-Board SH1.0 14PIN 的 Pin 9 为 UART_RXD，Pin 10 为 UART_TXD。

        ## 系统链路

        `语音命令 -> DualEye 解析 -> UART 文本指令 -> 底盘控制板 -> 电机驱动 -> 车轮/履带动作`

        {md_table(["类型", "项目", "说明"], voice_uart_rows)}

        ## DualEye UART 接口针位

        {md_table(["DualEye 接口针位", "信号", "接法/用途"], dualeye_uart_pin_rows)}

        ## 推荐接线

        ```text
        DualEye LCD1 Pin10 UART_TXD  -> XIAO D7 / GPIO20 / RX
        DualEye LCD1 Pin9  UART_RXD  <- XIAO D6 / GPIO21 / TX
        DualEye LCD1 Pin2/Pin6 GND   <-> XIAO GND
        5 V 升压 OUT+                -> XIAO 5V / DRV8833 VM / WS2812B 5V
        5 V 升压 OUT-                -> XIAO GND / DRV8833 GND / WS2812B GND
        ```

        注意：ESP32-S3 的 UART 是 3.3 V TTL。不要接 RS232 电平。若替换成 5 V TTL 底盘板，进入 DualEye RX 前必须加分压或电平转换。
        XIAO 可以另接 5 V 逻辑电源，但电机供电不要从 DualEye 板上取。

        ## 推荐串口命令

        {md_table(["命令示例", "含义"], voice_command_rows)}

        ## 底盘板必须实现的安全逻辑

        - 只解析以 `AR1,` 开头的命令；不带前缀的串口日志、调试打印、乱码全部丢弃。
        - 收到 `AR1,STOP` 立即停车。
        - 300-500 ms 内没有收到新的运动指令，自动停车。
        - 上电默认停车，不执行上一次残留命令。
        - 限制最大 PWM，首次调试建议不超过 40%。
        - 若语音识别误触发，DualEye 可以先要求二次确认，例如“开始巡游”后才允许连续运动。

        ## 与当前 Mk.1 单板方案的关系

        - 单板方案：DualEye 通过 I2C/PCA9685 控制 DRV8833，适合手工黄铜底盘。
        - 双板方案：DualEye 通过 UART 控制底盘板，适合成品轮式底盘、履带底盘或带编码器底盘。
        - 两种方案不要混接电机供电。无论哪种方案，电机电流都不从 DualEye 板取。
        """,
    )

    write_text(
        PACK / "组装模拟与效果确认_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 组装模拟与效果确认

        ## 总体判断

        按当前 V1.0 物料和结构方案，可以做出接近参考图的三层黄铜笼架效果：底层电池和动力、中层驱动/扩展电子、上层双眼显示与交互。
        需要接受的差异是：参考图更像工业渲染，真实手工版会看到焊点、线束和模块孔位差异；另外 Mk.1 是四个物理电机的左右两侧差速，不做四轮独立闭环控制。

        ## 模拟装配核对

        {md_table(["层/功能", "模拟摆放", "效果判断", "风险与调整"], simulation_rows)}

        ## 能达到的视觉特征

        - 双圆屏眼睛：可以达到，核心板本身就是双 1.28 寸圆屏，宽度按 93.5 mm 处理。
        - 黄铜笼架：可以达到，2.0 mm 做主框，1.5 mm 做护眼圈和斜撑。
        - 顶部提手：可以达到，2.0 mm 黄铜棒绕 30-35 mm 圆柱弯 U 形。
        - 前灯条：可以达到，但必须外接 WS2812B 灯条/灯环，主控板没有集成车灯。
        - 后部喇叭：可以达到，需要外接小喇叭，主控板已有功放/喇叭接口。
        - 桌面巡游：可以达到低速巡游，建议 N20 100 RPM，150 RPM 也要限 PWM。

        ## 达不到或不建议强求的点

        - 不建议做四驱。四驱会增加电流、驱动板数量、机械固定和空间压力。
        - 不建议把 90 mm 当整车最大宽度。双眼板官方宽度 93.5 mm，车头必须允许轻微外凸。
        - 不建议把黄铜车架当电源地。黄铜只做结构和装饰，所有电源必须独立走线并绝缘。
        - 不建议把电池、升压、灯光、电机全挂在主控板 3.3 V 上。电机和灯光走独立 5 V 支路。

        ## 开工前摆位演练

        1. 先把底板模板打印或照着画到纸上，放上电池盒、四个 N20、两块 DRV8833、升压模块，确认 120 x 90 mm 车架内能放下。
        2. 再把 ESP32 双眼板放到车头位置，确认最大外宽 100-104 mm 可接受，并标出 USB-C 插拔空间。
        3. 用纸条模拟黄铜护栏：底框 120 x 90 mm，中层护栏高度 42-48 mm，上层双眼支架到总高 110 mm。
        4. 用胶带临时固定线束路线，确认电池能取出、轮胎不擦线、开关能拨到。
        5. 通过以上演练后再切黄铜棒，避免先焊死后发现模块挡住接口。
        """,
    )

    write_text(
        PACK / "一致性复审报告_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 一致性复审报告

        ## 结论

        当前制造包已经按同一套 V1.0 决策重排：采购清单、采买执行清单、规格核对、装配指南、接线说明、详细手册、SVG 图纸和 PDF 的核心结论一致。
        主要复审修正是：WS2812B 正式接线增加 AHCT/HCT 数据电平转换器；所有文件继续保持 M2、四个 N20 物理电机、左右两侧差速、DRV8833、电机独立升压支路、板载充电和板载音频这几条定版原则。

        ## 文件索引

        {md_table(["文件", "内容", "什么时候看"], file_rows)}

        ## 定版决策矩阵

        {md_table(["项目", "定版", "优先级", "一致性说明"], decision_rows)}

        ## 跨文件一致性核对

        - BOM 与采买执行清单：已统一 DRV8833 为必买，PCA9685/WS2812B/小喇叭/AHCT-HCT 电平转换为推荐一起买。
        - 规格核对与接线说明：已统一 ESP32 不能直接驱动电机，PCA9685 不是电机驱动，WS2812B 数据不接 PCA9685。
        - 装配指南与详细手册：已统一黄铜用量、下料表、组装阶段、焊接顺序和线束固定方式。
        - 图纸与文字尺寸：已统一 120 mm 车身长度、90 mm 黄铜车架宽度、100-104 mm 车头最大外宽、110 mm 目标高度。
        - PDF 与 Markdown：PDF 从同一生成脚本重建，包含规格核对、图纸、黄铜下料、详细步骤、接线作业、BOM 和资料来源。

        ## 仍需实物确认

        {md_table(["项目", "需要确认的原因"], open_rows)}
        """,
    )

    write_text(
        PACK / "资料来源_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 资料来源

        {md_table(["资料", "链接", "用途"], src_rows)}

        ## 备注

        通用电商模块尺寸可能有差异。图纸使用典型模块尺寸并预留了安装余量。
        最终钻孔前，请用游标卡尺实测你手上的模块。
        """,
    )

    write_text(
        PACK / "采购清单_Atlas_Rover_Mk1_V1.0.md",
        f"""
        # Atlas Rover Mk.1 V1.0 采购清单

        {md_table(["分类", "名称", "规格", "数量", "是否必需", "备注", "估算价格_元", "推荐搜索词", "京东", "淘宝", "拼多多"], bom_rows)}
        """,
    )


def svg_header(w: int, h: int, title: str) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{h}mm" viewBox="0 0 {w} {h}">',
        "<defs>",
        '<marker id="arrow" markerWidth="6" markerHeight="6" refX="5" refY="3" orient="auto" markerUnits="strokeWidth">',
        '<path d="M0,0 L6,3 L0,6 z" fill="#222"/>',
        "</marker>",
        "</defs>",
        '<rect x="0" y="0" width="100%" height="100%" fill="#fbfaf6"/>',
        f'<text x="8" y="10" font-family="Arial, sans-serif" font-size="5" font-weight="700">{title}</text>',
        '<g stroke="#222" stroke-width="0.45" fill="none" font-family="Arial, sans-serif" font-size="3.2">',
    ]


def dim_line(x1: float, y1: float, x2: float, y2: float, label: str) -> str:
    mx = (x1 + x2) / 2
    my = (y1 + y2) / 2
    return (
        f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#333" marker-start="url(#arrow)" marker-end="url(#arrow)"/>'
        f'<text x="{mx}" y="{my - 2}" fill="#111" text-anchor="middle" stroke="none">{label}</text>'
    )


def write_svg_overall() -> None:
    parts = svg_header(220, 160, "AR-MK1-001 总体尺寸 - mm")
    parts += [
        '<text x="15" y="20" fill="#333" stroke="none">顶视图</text>',
        '<rect x="35" y="35" width="120" height="90" rx="4" stroke="#b87333" stroke-width="1.2"/>',
        '<rect x="40" y="44" width="110" height="72" rx="3" stroke="#333" stroke-dasharray="2 1"/>',
        '<circle cx="70" cy="125" r="17" stroke="#111" fill="#ddd"/>',
        '<circle cx="120" cy="125" r="17" stroke="#111" fill="#ddd"/>',
        '<circle cx="145" cy="80" r="6" stroke="#111" fill="#eee"/>',
        '<rect x="49" y="57" width="76" height="21" rx="2" stroke="#777" fill="#f1f1f1"/>',
        '<text x="87" y="70" fill="#333" stroke="none" text-anchor="middle">18650 电池盒</text>',
        '<rect x="62" y="84" width="62" height="25" rx="2" stroke="#777" fill="#eef6ff"/>',
        '<text x="93" y="99" fill="#333" stroke="none" text-anchor="middle">DRV8833 + PCA9685</text>',
        '<rect x="43" y="22" width="104" height="10" rx="5" stroke="#333" fill="#111"/>',
        '<circle cx="69" cy="27" r="18.25" stroke="#111" fill="#222"/>',
        '<circle cx="121.5" cy="27" r="18.25" stroke="#111" fill="#222"/>',
        '<text x="95" y="17" fill="#333" stroke="none" text-anchor="middle">ESP32 双眼板，官方宽度 93.5 mm</text>',
        dim_line(35, 132, 155, 132, "车身长度 120"),
        dim_line(162, 35, 162, 125, "黄铜车架宽 90"),
        dim_line(43, 14, 146.5, 14, "车头/护栏最大外宽 100-104"),
        '<text x="15" y="145" fill="#333" stroke="none">备注：90 mm 为车架/车体宽度。双眼板宽 93.5 mm，允许轻微外凸。</text>',
    ]
    parts += ["</g>", "</svg>"]
    (DRAWINGS / "AR-MK1-001_总体尺寸.svg").write_text("\n".join(parts), encoding="utf-8")


def write_svg_layer_stack() -> None:
    parts = svg_header(220, 150, "AR-MK1-002 层级结构 - mm")
    parts += [
        '<text x="20" y="25" fill="#333" stroke="none">侧视图</text>',
        '<line x1="25" y1="125" x2="175" y2="125" stroke="#444"/>',
        '<circle cx="65" cy="108" r="17" stroke="#111" fill="#ddd"/>',
        '<circle cx="148" cy="114" r="7" stroke="#111" fill="#eee"/>',
        '<rect x="35" y="86" width="120" height="4" stroke="#333" fill="#f5f5f5"/>',
        '<rect x="45" y="65" width="84" height="3" stroke="#333" fill="#f5f5f5"/>',
        '<rect x="55" y="92" width="76" height="20" rx="2" stroke="#777" fill="#f1f1f1"/>',
        '<text x="93" y="105" fill="#333" stroke="none" text-anchor="middle">电池</text>',
        '<rect x="62" y="70" width="60" height="13" rx="2" stroke="#777" fill="#eef6ff"/>',
        '<text x="92" y="79" fill="#333" stroke="none" text-anchor="middle">驱动板</text>',
        '<rect x="95" y="37" width="94" height="18" rx="5" stroke="#111" fill="#111"/>',
        '<circle cx="121" cy="46" r="18.25" stroke="#111" fill="#222"/>',
        '<circle cx="173.5" cy="46" r="18.25" stroke="#111" fill="#222"/>',
        '<path d="M38 88 C45 38, 150 25, 185 52" stroke="#b87333" stroke-width="1.2"/>',
        '<path d="M42 62 L42 88 M132 62 L132 88" stroke="#b87333" stroke-width="1.2"/>',
        dim_line(195, 125, 195, 15, "目标高度 110"),
        dim_line(35, 132, 155, 132, "长度 120"),
        '<text x="20" y="138" fill="#333" stroke="none">层高可微调。保留 USB-C 操作空间，并与黄铜车架保持绝缘间隙。</text>',
    ]
    parts += ["</g>", "</svg>"]
    (DRAWINGS / "AR-MK1-002_层级结构.svg").write_text("\n".join(parts), encoding="utf-8")


def write_svg_bottom_template() -> None:
    parts = svg_header(160, 115, "AR-MK1-003 底板模板 - 可按 1:1 打印核对")
    x0, y0 = 25, 22
    parts += [
        f'<rect x="{x0}" y="{y0}" width="110" height="72" rx="4" stroke="#111" fill="#fff"/>',
        '<text x="80" y="16" fill="#333" stroke="none" text-anchor="middle">110 x 72 mm 层板，居中放入 120 x 90 mm 黄铜车架</text>',
    ]
    # standoff holes
    for x in [x0 + 10, x0 + 100]:
        for y in [y0 + 10, y0 + 62]:
            parts.append(f'<circle cx="{x}" cy="{y}" r="1.1" stroke="#111" fill="none"/>')
            parts.append(f'<text x="{x+2.5}" y="{y+1}" fill="#333" stroke="none">M2</text>')
    # battery strap slots
    for x in [x0 + 34, x0 + 76]:
        parts.append(f'<rect x="{x-1.5}" y="{y0+25}" width="3" height="22" rx="1" stroke="#777" fill="none"/>')
    parts += [
        f'<rect x="{x0+17}" y="{y0+25.5}" width="76" height="21" rx="2" stroke="#777" stroke-dasharray="2 1"/>',
        '<text x="80" y="60" fill="#333" stroke="none" text-anchor="middle">18650 电池盒占位</text>',
    ]
    # motor zip slots
    for sx in [x0 + 28, x0 + 54]:
        for sy in [y0 + 4, y0 + 64]:
            parts.append(f'<rect x="{sx}" y="{sy}" width="12" height="3" rx="1" stroke="#1f4e79" fill="none"/>')
    parts += [
        '<text x="80" y="101" fill="#333" stroke="none" text-anchor="middle">蓝色槽位：电机固定/扎带区域</text>',
        f'<circle cx="{x0+92}" cy="{y0+36}" r="1.1" stroke="#111" fill="none"/>',
        f'<circle cx="{x0+100}" cy="{y0+31}" r="1.1" stroke="#111" fill="none"/>',
        f'<circle cx="{x0+100}" cy="{y0+41}" r="1.1" stroke="#111" fill="none"/>',
        '<text x="124" y="60" fill="#333" stroke="none">万向轮 M2 区域</text>',
        dim_line(x0, y0 + 100, x0 + 110, y0 + 100, "110"),
        dim_line(x0 + 124, y0, x0 + 124, y0 + 72, "72"),
    ]
    parts += ["</g>", "</svg>"]
    (DRAWINGS / "AR-MK1-003_底板模板.svg").write_text("\n".join(parts), encoding="utf-8")


def write_svg_wiring() -> None:
    parts = svg_header(220, 150, "AR-MK1-004 接线框图")
    nodes = {
        "bat": (20, 55, 34, 18, "1S 18650"),
        "board": (150, 22, 50, 28, "ESP32 双眼板"),
        "boost": (75, 82, 42, 20, "5 V 升压"),
        "drv": (145, 80, 42, 22, "DRV8833"),
        "pca": (75, 25, 42, 22, "PCA9685"),
        "mot": (145, 115, 42, 18, "N20 电机"),
        "spk": (150, 55, 42, 15, "喇叭"),
        "lvl": (75, 112, 42, 16, "AHCT/HCT"),
        "rgb": (20, 115, 42, 18, "WS2812B 车灯"),
    }
    for x, y, w, h, label in nodes.values():
        parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" stroke="#222" fill="#fff"/>')
        parts.append(f'<text x="{x+w/2}" y="{y+h/2+1}" fill="#111" stroke="none" text-anchor="middle">{label}</text>')

    def wire(a: str, b: str, label: str, color: str = "#333") -> None:
        ax, ay, aw, ah, _ = nodes[a]
        bx, by, bw, bh, _ = nodes[b]
        x1, y1 = ax + aw, ay + ah / 2
        x2, y2 = bx, by + bh / 2
        parts.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" marker-end="url(#arrow)"/>')
        parts.append(f'<text x="{(x1+x2)/2}" y="{(y1+y2)/2 - 2}" fill="{color}" stroke="none" text-anchor="middle">{label}</text>')

    parts.append('<polyline points="54,64 62,14 150,36" fill="none" stroke="#a33" marker-end="url(#arrow)"/>')
    parts.append('<text x="102" y="18" fill="#a33" stroke="none" text-anchor="middle">3.7 V 主控支路</text>')
    wire("bat", "boost", "电机开关支路", "#a33")
    wire("boost", "drv", "5 V VM", "#a33")
    wire("pca", "drv", "4 x PWM", "#1f4e79")
    wire("board", "pca", "I2C + 3V3", "#1f4e79")
    wire("drv", "mot", "A/B 输出", "#333")
    wire("board", "spk", "喇叭接口", "#333")
    wire("board", "lvl", "数据 GPIO", "#1f4e79")
    wire("lvl", "rgb", "5 V 数据", "#1f4e79")
    wire("boost", "rgb", "5 V + GND", "#a33")
    parts += [
        '<text x="20" y="140" fill="#333" stroke="none">所有 GND 共地。电机不能从 ESP32 3.3 V 取电。WS2812B 数据建议经 AHCT/HCT 电平转换。</text>',
    ]
    parts += ["</g>", "</svg>"]
    (DRAWINGS / "AR-MK1-004_接线框图.svg").write_text("\n".join(parts), encoding="utf-8")


def write_svgs() -> None:
    write_svg_overall()
    write_svg_layer_stack()
    write_svg_bottom_template()
    write_svg_wiring()


def build_pdf() -> None:
    from reportlab.lib import colors
    from reportlab.lib.pagesizes import A4, landscape
    from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
    from reportlab.lib.units import mm
    from reportlab.pdfbase import pdfmetrics
    from reportlab.pdfbase.cidfonts import UnicodeCIDFont
    from reportlab.pdfbase.ttfonts import TTFont
    from reportlab.platypus import (
        BaseDocTemplate,
        Frame,
        PageBreak,
        PageTemplate,
        Paragraph,
        Spacer,
        Table,
        TableStyle,
        Flowable,
    )

    font_name = "STSong-Light"
    font_path = Path("/System/Library/Fonts/Supplemental/Arial Unicode.ttf")
    try:
        if font_path.exists():
            pdfmetrics.registerFont(TTFont("AtlasCJK", str(font_path)))
            font_name = "AtlasCJK"
        else:
            pdfmetrics.registerFont(UnicodeCIDFont("STSong-Light"))
    except Exception:
        pdfmetrics.registerFont(UnicodeCIDFont("STSong-Light"))
        font_name = "STSong-Light"

    pdf_path = PDF_OUT / "Atlas_Rover_Mk1_制造图纸包_V1.0.pdf"
    doc = BaseDocTemplate(
        str(pdf_path),
        pagesize=landscape(A4),
        leftMargin=12 * mm,
        rightMargin=12 * mm,
        topMargin=10 * mm,
        bottomMargin=10 * mm,
        title="Atlas Rover Mk.1 制造图纸包 V1.0",
    )
    frame = Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="normal")

    def footer(canvas, _doc):
        canvas.saveState()
        canvas.setFont(font_name, 7)
        canvas.setFillColor(colors.HexColor("#666666"))
        canvas.drawString(12 * mm, 7 * mm, "Atlas Rover Mk.1 制造图纸包 V1.0")
        canvas.drawRightString(285 * mm, 7 * mm, f"第 {_doc.page} 页")
        canvas.restoreState()

    doc.addPageTemplates([PageTemplate(id="main", frames=[frame], onPage=footer)])

    styles = getSampleStyleSheet()
    styles.add(ParagraphStyle(name="AtlasTitle", fontName=font_name, fontSize=22, leading=28, spaceAfter=8))
    styles.add(ParagraphStyle(name="AtlasH1", fontName=font_name, fontSize=14, leading=18, spaceBefore=5, spaceAfter=5, textColor=colors.HexColor("#222222")))
    styles.add(ParagraphStyle(name="AtlasBody", fontName=font_name, fontSize=8.5, leading=12, spaceAfter=4))
    styles.add(ParagraphStyle(name="AtlasSmall", fontName=font_name, fontSize=7.2, leading=9))

    def p(text: str, style: str = "AtlasBody") -> Paragraph:
        return Paragraph(text, styles[style])

    def table(data, widths=None, small=True):
        para_data = []
        style_name = "AtlasSmall" if small else "AtlasBody"
        for row in data:
            para_data.append([p(str(cell), style_name) for cell in row])
        t = Table(para_data, colWidths=widths, repeatRows=1)
        t.setStyle(
            TableStyle(
                [
                    ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#222222")),
                    ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
                    ("FONTNAME", (0, 0), (-1, -1), font_name),
                    ("GRID", (0, 0), (-1, -1), 0.25, colors.HexColor("#c9c3b8")),
                    ("VALIGN", (0, 0), (-1, -1), "TOP"),
                    ("BACKGROUND", (0, 1), (-1, -1), colors.HexColor("#fffdf7")),
                    ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.HexColor("#fffdf7"), colors.HexColor("#f4f0e8")]),
                    ("LEFTPADDING", (0, 0), (-1, -1), 4),
                    ("RIGHTPADDING", (0, 0), (-1, -1), 4),
                    ("TOPPADDING", (0, 0), (-1, -1), 3),
                    ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
                ]
            )
        )
        return t

    class AtlasDrawing(Flowable):
        def __init__(self, kind: str, width_mm: float, height_mm: float):
            super().__init__()
            self.kind = kind
            self.width = width_mm * mm
            self.height = height_mm * mm
            self.logical_w = 220 if kind in {"overall", "layer", "wiring"} else 160
            self.logical_h = {"overall": 160, "layer": 150, "bottom": 115, "wiring": 150}[kind]

        def wrap(self, _avail_width, _avail_height):
            return self.width, self.height

        def draw(self):
            c = self.canv
            scale = min(self.width / self.logical_w, self.height / self.logical_h)
            c.saveState()
            c.scale(scale, scale)
            c.setFillColor(colors.HexColor("#fbfaf6"))
            c.rect(0, 0, self.logical_w, self.logical_h, stroke=0, fill=1)
            c.setFont(font_name, 5)
            c.setFillColor(colors.HexColor("#111111"))
            titles = {
                "overall": "AR-MK1-001 总体尺寸 - mm",
                "layer": "AR-MK1-002 层级结构 - mm",
                "bottom": "AR-MK1-003 底板模板",
                "wiring": "AR-MK1-004 接线框图",
            }
            c.drawString(8, self.logical_h - 10, titles[self.kind])
            if self.kind == "overall":
                self._overall(c)
            elif self.kind == "layer":
                self._layer(c)
            elif self.kind == "bottom":
                self._bottom(c)
            elif self.kind == "wiring":
                self._wiring(c)
            c.restoreState()

        def _set(self, c, stroke="#222222", fill=None, lw=0.45):
            c.setStrokeColor(colors.HexColor(stroke))
            c.setLineWidth(lw)
            if fill is None:
                c.setFillColor(colors.transparent)
            else:
                c.setFillColor(colors.HexColor(fill))

        def _text(self, c, x, y, text, size=3.2, anchor="start", color="#111111"):
            c.setFont(font_name, size)
            c.setFillColor(colors.HexColor(color))
            if anchor == "middle":
                c.drawCentredString(x, y, text)
            elif anchor == "end":
                c.drawRightString(x, y, text)
            else:
                c.drawString(x, y, text)

        def _rect(self, c, x, y, w, h, stroke="#222222", fill=None, lw=0.45, radius=0):
            self._set(c, stroke, fill, lw)
            if radius:
                c.roundRect(x, y, w, h, radius, stroke=1, fill=1 if fill else 0)
            else:
                c.rect(x, y, w, h, stroke=1, fill=1 if fill else 0)

        def _circle(self, c, x, y, r, stroke="#222222", fill=None, lw=0.45):
            self._set(c, stroke, fill, lw)
            c.circle(x, y, r, stroke=1, fill=1 if fill else 0)

        def _line(self, c, x1, y1, x2, y2, stroke="#222222", lw=0.45):
            c.setStrokeColor(colors.HexColor(stroke))
            c.setLineWidth(lw)
            c.line(x1, y1, x2, y2)

        def _dim(self, c, x1, y1, x2, y2, label):
            self._line(c, x1, y1, x2, y2, "#333333", 0.4)
            tick = 2.0
            if abs(y2 - y1) < abs(x2 - x1):
                self._line(c, x1, y1 - tick, x1, y1 + tick, "#333333", 0.4)
                self._line(c, x2, y2 - tick, x2, y2 + tick, "#333333", 0.4)
                self._text(c, (x1 + x2) / 2, y1 + 3, label, 3.1, "middle")
            else:
                self._line(c, x1 - tick, y1, x1 + tick, y1, "#333333", 0.4)
                self._line(c, x2 - tick, y2, x2 + tick, y2, "#333333", 0.4)
                self._text(c, x1 + 3, (y1 + y2) / 2, label, 3.1)

        def _overall(self, c):
            # Top-view style, coordinates mirror the SVG source.
            self._text(c, 15, 132, "顶视图", 3.4, color="#333333")
            self._rect(c, 35, 35, 120, 90, "#b87333", None, 1.2, 4)
            c.setDash(2, 1)
            self._rect(c, 40, 44, 110, 72, "#333333", None, 0.45, 3)
            c.setDash()
            self._circle(c, 70, 35, 17, "#111111", "#dddddd", 0.45)
            self._circle(c, 120, 35, 17, "#111111", "#dddddd", 0.45)
            self._circle(c, 145, 80, 6, "#111111", "#eeeeee", 0.45)
            self._rect(c, 49, 82, 76, 21, "#777777", "#f1f1f1", 0.45, 2)
            self._text(c, 87, 94, "18650 电池盒", 3.1, "middle", "#333333")
            self._rect(c, 62, 51, 62, 25, "#777777", "#eef6ff", 0.45, 2)
            self._text(c, 93, 64, "DRV8833 + PCA9685", 3.1, "middle", "#333333")
            self._rect(c, 43, 113, 104, 10, "#333333", "#111111", 0.45, 5)
            self._circle(c, 69, 118, 18.25, "#111111", "#222222", 0.45)
            self._circle(c, 121.5, 118, 18.25, "#111111", "#222222", 0.45)
            self._dim(c, 35, 12, 155, 12, "车身长度 120")
            self._dim(c, 162, 35, 162, 125, "车架宽度 90")
            self._dim(c, 43, 137, 146.5, 137, "车头宽 100-104，双眼板 93.5")
            self._text(c, 15, 4, "备注：90 mm 为车架/车体宽度。双眼板宽 93.5 mm，允许轻微外凸。", 3.0, color="#333333")

        def _layer(self, c):
            self._text(c, 20, 125, "侧视图", 3.4, color="#333333")
            self._line(c, 25, 25, 175, 25, "#444444", 0.45)
            self._circle(c, 65, 42, 17, "#111111", "#dddddd", 0.45)
            self._circle(c, 148, 36, 7, "#111111", "#eeeeee", 0.45)
            self._rect(c, 35, 60, 120, 4, "#333333", "#f5f5f5", 0.45)
            self._rect(c, 45, 82, 84, 3, "#333333", "#f5f5f5", 0.45)
            self._rect(c, 55, 65, 76, 20, "#777777", "#f1f1f1", 0.45, 2)
            self._text(c, 93, 75, "电池", 3.1, "middle", "#333333")
            self._rect(c, 62, 88, 60, 13, "#777777", "#eef6ff", 0.45, 2)
            self._text(c, 92, 95, "驱动板", 3.1, "middle", "#333333")
            self._rect(c, 95, 113, 94, 18, "#111111", "#111111", 0.45, 5)
            self._circle(c, 121, 122, 18.25, "#111111", "#222222", 0.45)
            self._circle(c, 173.5, 122, 18.25, "#111111", "#222222", 0.45)
            c.setStrokeColor(colors.HexColor("#b87333"))
            c.setLineWidth(1.2)
            pth = c.beginPath()
            pth.moveTo(38, 62)
            pth.curveTo(45, 112, 150, 125, 185, 98)
            c.drawPath(pth, stroke=1, fill=0)
            self._line(c, 42, 62, 42, 88, "#b87333", 1.2)
            self._line(c, 132, 62, 132, 88, "#b87333", 1.2)
            self._dim(c, 195, 25, 195, 135, "高度 110")
            self._dim(c, 35, 18, 155, 18, "长度 120")
            self._text(c, 20, 10, "层高可微调。保留 USB-C 操作空间，并与黄铜车架保持绝缘间隙。", 3.0, color="#333333")

        def _bottom(self, c):
            x0, y0 = 25, 20
            self._rect(c, x0, y0, 110, 72, "#111111", "#ffffff", 0.45, 4)
            self._text(c, 80, 100, "110 x 72 mm 层板，居中放入 120 x 90 mm 黄铜车架", 3.0, "middle", "#333333")
            for x in [x0 + 10, x0 + 100]:
                for y in [y0 + 10, y0 + 62]:
                    self._circle(c, x, y, 1.1, "#111111", None, 0.45)
                    self._text(c, x + 2.5, y - 1, "M2", 2.8, color="#333333")
            for x in [x0 + 34, x0 + 76]:
                self._rect(c, x - 1.5, y0 + 25, 3, 22, "#777777", None, 0.45, 1)
            c.setDash(2, 1)
            self._rect(c, x0 + 17, y0 + 25.5, 76, 21, "#777777", None, 0.45, 2)
            c.setDash()
            self._text(c, 80, 55, "18650 电池盒占位", 3.1, "middle", "#333333")
            for sx in [x0 + 28, x0 + 54]:
                for sy in [y0 + 4, y0 + 64]:
                    self._rect(c, sx, sy, 12, 3, "#1f4e79", None, 0.45, 1)
            self._text(c, 80, 8, "蓝色槽位：电机固定/扎带区域", 3.0, "middle", "#333333")
            self._circle(c, x0 + 92, y0 + 36, 1.1, "#111111", None, 0.45)
            self._circle(c, x0 + 100, y0 + 31, 1.1, "#111111", None, 0.45)
            self._circle(c, x0 + 100, y0 + 41, 1.1, "#111111", None, 0.45)
            self._text(c, 124, 54, "万向轮 M2 区域", 3.0, color="#333333")
            self._dim(c, x0, 12, x0 + 110, 12, "110")
            self._dim(c, x0 + 124, y0, x0 + 124, y0 + 72, "72")

        def _wiring(self, c):
            nodes = {
                "bat": (20, 77, 34, 18, "1S 18650"),
                "board": (150, 100, 50, 28, "ESP32 双眼板"),
                "boost": (75, 48, 42, 20, "5 V 升压"),
                "drv": (145, 48, 42, 22, "DRV8833"),
                "pca": (75, 103, 42, 22, "PCA9685"),
                "mot": (145, 17, 42, 18, "N20 电机"),
                "spk": (150, 80, 42, 15, "喇叭"),
                "lvl": (75, 37, 42, 16, "AHCT/HCT"),
                "rgb": (20, 17, 42, 18, "WS2812B 车灯"),
            }

            def wire(a, b, color="#333333"):
                ax, ay, aw, ah, _ = nodes[a]
                bx, by, _bw, bh, _ = nodes[b]
                x1, y1 = ax + aw, ay + ah / 2
                x2, y2 = bx, by + bh / 2
                self._line(c, x1, y1, x2, y2, color, 0.5)

            def wire_path(points, color="#333333"):
                for (x1, y1), (x2, y2) in zip(points, points[1:]):
                    self._line(c, x1, y1, x2, y2, color, 0.5)

            # Draw wires first so module boxes stay legible.
            wire_path([(54, 86), (62, 134), (150, 114)], "#aa3333")
            wire("bat", "boost", "#aa3333")
            wire("boost", "drv", "#aa3333")
            wire("pca", "drv", "#1f4e79")
            wire("board", "pca", "#1f4e79")
            wire("drv", "mot", "#333333")
            wire("board", "spk", "#333333")
            wire("board", "lvl", "#1f4e79")
            wire("lvl", "rgb", "#1f4e79")
            wire("boost", "rgb", "#aa3333")

            for x, y, w, h, label in nodes.values():
                self._rect(c, x, y, w, h, "#222222", "#ffffff", 0.45, 3)
                self._text(c, x + w / 2, y + h / 2 - 1, label, 3.0, "middle")

            self._text(c, 93, 93, "3.7 V 主控支路", 2.8, "middle", "#aa3333")
            self._text(c, 55, 68, "电机开关支路", 2.8, "middle", "#aa3333")
            self._text(c, 128, 63, "5 V VM", 2.8, "middle", "#aa3333")
            self._text(c, 123, 83, "4 x PWM", 2.8, "middle", "#1f4e79")
            self._text(c, 126, 121, "I2C + 3V3", 2.8, "middle", "#1f4e79")
            self._text(c, 128, 39, "A/B 输出", 2.8, "middle", "#333333")
            self._text(c, 139, 88, "喇叭接口", 2.8, "end", "#333333")
            self._text(c, 112, 71, "灯光数据", 2.8, "middle", "#1f4e79")
            self._text(c, 70, 33, "5 V 数据", 2.8, "middle", "#1f4e79")
            self._text(c, 84, 42, "5 V + GND", 2.8, "middle", "#aa3333")
            self._text(c, 20, 10, "所有 GND 共地。电机不能从 ESP32 3.3 V 取电。WS2812B 数据建议经 AHCT/HCT 电平转换。", 3.0, color="#333333")

    story = []
    story += [
        p("Atlas Rover Mk.1", "AtlasTitle"),
        p("制造图纸包 V1.0"),
        p("基于 ESP32-S3-DualEye-Touch-LCD-1.28 的桌面巡游车原型"),
        Spacer(1, 6),
        p("设计定版", "AtlasH1"),
        table(
            [
                ["项目", "取值"],
                ["车身长度", "目标视觉长度 120 mm"],
                ["车架/车体宽度", "黄铜车架/车体宽度 90 mm"],
                ["最大外宽", "允许 100-104 mm，因为双眼板宽度为 93.5 mm"],
                ["高度", "目标 110 mm，包含顶部提手/笼架"],
                ["驱动", "四个 N20 + 四个 64T 齿轮轮，左右两侧差速"],
                ["紧固件", "M2 铜柱和 M2 螺丝"],
                ["电源", "单节 18650；主控电池支路加独立电机开关升压支路"],
                ["电机驱动", "必备 DRV8833 双路 H 桥电机驱动板"],
                ["电机控制", "推荐 PCA9685 I2C PWM 模块连接 DRV8833"],
                ["车灯/RGB", "推荐外接 WS2812B 灯条或灯环，加 AHCT/HCT 数据电平转换"],
            ],
            widths=[48 * mm, 190 * mm],
            small=False,
        ),
        Spacer(1, 8),
        p("规格核对后的主要修正", "AtlasH1"),
        p("1. 双眼板安装孔必须使用 M2，M3 铜柱与主控板孔位不匹配。"),
        p("2. 原图中的 90 mm 宽度应视为车架/车体宽度；双眼板官方宽度是 93.5 mm，因此 V1.0 允许车头轻微外凸。"),
        p("3. 主控板已带电池充电、麦克风、音频编解码、功放和喇叭接口；Mk.1 不需要 TP4056、INMP441 和 MAX98357A。"),
        p("4. 复审后补强电机供电余量和接线可实施性：18650 建议持续放电 >= 8 A，升压模块建议 2-3 A 连续 / 4-5 A 峰值，并准备 SH1.0/FPC 转接线。"),
        p("5. 电机和车灯的解决方案已明确：电机靠 DRV8833 驱动板，车灯靠外接 WS2812B 灯条/灯环，正式版建议加 AHCT/HCT 数据电平转换。"),
        PageBreak(),
    ]

    story += [
        p("文件索引与定版决策", "AtlasH1"),
        table(
            [["文件", "内容", "什么时候看"]] + [list(row) for row in FILE_INDEX],
            widths=[72 * mm, 118 * mm, 65 * mm],
        ),
        Spacer(1, 6),
        table(
            [["项目", "定版", "优先级", "一致性说明"]] + [list(row) for row in DECISION_MATRIX],
            widths=[42 * mm, 82 * mm, 30 * mm, 102 * mm],
        ),
        PageBreak(),
    ]

    story += [
        p("规格与冲突核对", "AtlasH1"),
        table(
            [["核对项", "规格", "问题", "状态", "V1.0 处理"]] + [list(row) for row in FIT_CHECKS],
            widths=[35 * mm, 44 * mm, 43 * mm, 30 * mm, 105 * mm],
        ),
        PageBreak(),
    ]

    story += [
        p("程序设计：双目 UI 与多主题页面", "AtlasH1"),
        p("软件目标是让 DualEye 成为项目第一视觉入口：默认显示双目表情，支持时钟、语音、状态、设置等多主题页面；触摸、语音和底盘状态都通过事件总线驱动页面和表情变化。"),
        table(
            [["模块", "职责", "说明"]] + [list(row) for row in PROGRAM_MODULES],
            widths=[42 * mm, 58 * mm, 156 * mm],
        ),
        Spacer(1, 6),
        table(
            [["页面", "用途", "显示内容", "切换/备注"]] + [list(row) for row in UI_PAGES],
            widths=[32 * mm, 38 * mm, 116 * mm, 70 * mm],
        ),
        PageBreak(),
    ]

    story += [
        p("程序设计：表情、MimiClaw 与运动意图", "AtlasH1"),
        table(
            [["状态 ID", "中文名", "视觉效果", "触发场景"]] + [list(row) for row in EXPRESSION_STATES],
            widths=[28 * mm, 28 * mm, 118 * mm, 82 * mm],
        ),
        Spacer(1, 6),
        table(
            [["用户说法", "标准指令", "安全处理"]] + [list(row) for row in ROVER_INTENT_ROWS],
            widths=[60 * mm, 62 * mm, 132 * mm],
        ),
        Spacer(1, 6),
        p("MimiClaw 只产生结构化 RoverIntent；运动命令必须先经过本地规则、限速、限时、二次确认和 STOP/超时保护，不能直接写 UART。"),
        PageBreak(),
    ]

    story += [
        p("语音与双板 UART 控制：针位与接线", "AtlasH1"),
        p("双板/成品底盘方案中，DualEye 作为语音入口和 HMI，底盘控制板负责电机执行。官方资料确认 DualEye 的 LCD1-Board SH1.0 14PIN 接口提供 UART：Pin 9 为 UART_RXD，Pin 10 为 UART_TXD。两块板之间使用 3.3 V TTL UART，GND 必须共地。"),
        table(
            [["DualEye 接口针位", "信号", "接法/用途"]] + [list(row) for row in DUALEYE_UART_PIN_ROWS],
            widths=[78 * mm, 38 * mm, 138 * mm],
        ),
        Spacer(1, 6),
        table(
            [["类型", "项目", "说明"]] + [list(row) for row in VOICE_UART_ROWS],
            widths=[32 * mm, 58 * mm, 166 * mm],
        ),
        PageBreak(),
    ]

    story += [
        p("语音与双板 UART 控制：协议与安全", "AtlasH1"),
        p("DualEye 只发送带 AR1 前缀的动作意图，底盘控制板负责解析、限速、超时停车和驱动 DRV8833/TB6612。底盘板逻辑可另接 5 V，但电机供电不要从 DualEye 板上取。"),
        table(
            [["命令示例", "含义"]] + [list(row) for row in VOICE_COMMAND_ROWS],
            widths=[55 * mm, 198 * mm],
        ),
        Spacer(1, 6),
        p("底盘板必须丢弃所有不带 AR1 前缀的串口内容，避免 DualEye 启动日志、调试打印或乱码误触发电机。每条运动命令都必须带 duration_ms，到时自动停车；收到 AR1,STOP 立即停车；上电默认停车。XIAO ESP32C3 是 3.3 V TTL，若换成 5 V TTL 底盘板，进入 DualEye RX 前需要分压或电平转换。"),
        PageBreak(),
    ]

    story += [
        p("组装模拟与效果确认", "AtlasH1"),
        p("按当前 V1.0 物料和结构方案，可以做出接近参考图的三层黄铜笼架效果：底层电池和动力、中层驱动/扩展电子、上层双眼显示与交互。真实手工版会看到焊点、线束和模块孔位差异；Mk.1 是四个物理电机的左右两侧差速，不做四轮独立闭环控制。"),
        table(
            [["层/功能", "模拟摆放", "效果判断", "风险与调整"]] + [list(row) for row in ASSEMBLY_SIMULATION],
            widths=[35 * mm, 76 * mm, 58 * mm, 88 * mm],
        ),
        PageBreak(),
    ]

    story += [
        p("总体尺寸", "AtlasH1"),
        p("这些图纸同时以 SVG 源文件形式保存在“图纸”文件夹中，后续可以继续编辑。"),
        AtlasDrawing("overall", 230, 168),
        PageBreak(),
        p("层级结构", "AtlasH1"),
        AtlasDrawing("layer", 230, 157),
        PageBreak(),
        p("底板与接线", "AtlasH1"),
        AtlasDrawing("bottom", 130, 93),
        Spacer(1, 4),
        AtlasDrawing("wiring", 185, 126),
        PageBreak(),
    ]

    story += [
        p("黄铜下料施工单", "AtlasH1"),
        p("参考图用于确定黄铜笼架、前保险杠、双眼护圈、顶部提手、前灯条和侧面轮拱的造型。Mk.1 采用四个物理电机的左右两侧差速。"),
        table(
            [["材料", "理论用量", "建议购买", "用途"]] + [list(row) for row in BRASS_SUMMARY],
            widths=[38 * mm, 32 * mm, 36 * mm, 145 * mm],
            small=False,
        ),
        Spacer(1, 6),
        table(
            [["材料", "部位", "数量", "单根 mm", "小计 mm", "成形", "备注"]] + [list(row) for row in BRASS_CUTS],
            widths=[18 * mm, 35 * mm, 14 * mm, 20 * mm, 20 * mm, 28 * mm, 122 * mm],
        ),
        PageBreak(),
        p("详细组装步骤", "AtlasH1"),
        table(
            [["阶段", "任务", "做法", "验收"]] + [list(row) for row in ASSEMBLY_STEPS],
            widths=[12 * mm, 30 * mm, 138 * mm, 76 * mm],
        ),
        Spacer(1, 6),
        p("焊接顺序：底框、竖柱、中层框、上层框、前脸、细节斜撑。每焊完一层都放桌面校平。若使用酸性助焊剂，只能用于黄铜结构件，焊后必须清洗干净。"),
        PageBreak(),
        p("接线作业表", "AtlasH1"),
        table(
            [["线路", "从哪里来", "接到哪里", "线材", "注意事项"]] + [list(row) for row in WIRING_ROWS],
            widths=[27 * mm, 48 * mm, 52 * mm, 44 * mm, 85 * mm],
        ),
        Spacer(1, 6),
        table(
            [["线材", "建议准备", "用途"]] + [list(row) for row in WIRE_LENGTHS],
            widths=[50 * mm, 45 * mm, 158 * mm],
        ),
        PageBreak(),
    ]

    bom_pdf_rows = [["分类", "名称", "规格", "数量", "必需", "备注"]] + [
        [b["class"], b["item"], b["spec"], b["qty"], b["required"], b["notes"]] for b in BOM
    ]
    story += [
        p("采购清单", "AtlasH1"),
        table(bom_pdf_rows, widths=[19 * mm, 42 * mm, 68 * mm, 13 * mm, 23 * mm, 92 * mm]),
        PageBreak(),
        p("装配顺序", "AtlasH1"),
        p("1. 先制作并校正底层安装板，再焊接装饰性黄铜栏杆。"),
        p("2. 安装电池盒、开关/保护、升压模块和电机。电机线建议成对绞合，并做好应力释放。"),
        p("3. 加中层板、DRV8833 和 PCA9685。接信号线前先确认所有 GND 已共地。"),
        p("4. 用 M2 铜柱安装 ESP32 双眼板。前护眼圈不要压到触摸屏。"),
        p("5. 先给主控支路上电，再给电机支路上电。电机首次测试时把车抬离桌面，并用低 PWM。"),
        p("6. 电子部分测试通过后，再完成最终黄铜笼架，并在所有可能接触点加绝缘。"),
        Spacer(1, 8),
        p("资料来源", "AtlasH1"),
        table([["资料", "链接", "用途"]] + [list(row) for row in SOURCES], widths=[65 * mm, 115 * mm, 75 * mm]),
    ]

    doc.build(story)


def main() -> None:
    ensure_dirs()
    write_csv()
    write_docs()
    write_svgs()
    build_pdf()
    print(PACK)


if __name__ == "__main__":
    main()
