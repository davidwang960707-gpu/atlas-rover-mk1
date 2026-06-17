# Atlas Rover Mk.1 V1.0 采买执行清单

说明：核心板 `ESP32-S3-DualEye-Touch-LCD-1.28` 在原清单中标记为“已有”，本清单默认不重复购买。实际下单前请按“下单校验”逐项确认规格，尤其是电池、电机和驱动板。
全量 BOM 文件 `采购清单_Atlas_Rover_Mk1_V1.0.csv` 与 `采购清单_Atlas_Rover_Mk1_V1.0.md` 已包含已有、必买、推荐物料的京东、淘宝、拼多多搜索链接。

## 采购优先级

| 优先级 | 范围 | 估算总价 |
| --- | --- | --- |
| 必买 | 底盘控制板、四个 N20、两块 DRV8833、64T 齿轮轮、电源、结构、线材基础物料 | 约 160-350 元 |
| 推荐一起买 | 必买 + PCA9685 + 小喇叭 + WS2812B 外接车灯 + 数据电平转换器 | 约 180-400 元 |
| 暂缓购买 | 摄像头、外置麦克风、外置功放、TP4056、四轮独立控制扩展 | 第一版不建议做四轮独立闭环 |

## 配置关系确认

| 需求 | 解决模块 | 是否已在清单中 | 备注 |
| --- | --- | --- | --- |
| DualEye 不能直接负责电机实时安全 | Seeed Studio XIAO ESP32C3 底盘控制板 | 已列入必买 | 接收 UART 指令，执行 DRV8833 PWM、限速和超时停车 |
| ESP32 不能直接驱动 N20 电机 | DRV8833 双路 H 桥电机驱动板 x 2 | 已列入必买 | 前后各一块，负责四个 N20 的功率输出 |
| 底盘板可用 PWM/GPIO 可能不够 | PCA9685 I2C PWM 扩展板 | 已列入推荐一起买 | 这是底盘板侧可选控制信号扩展板，不是电机驱动板 |
| 采用 64T 齿轮轮方案 | 64T 钢齿轮 + 防滑外圈，必要时加 3mm D 孔轮毂/夹片 | 已列入必买/备选 | 当前主方案是四个 64T 齿轮直接压入四个 N20，逻辑上左右两侧差速，不做贯穿通轴 |
| 主控板没有车灯/RGB | WS2812B 灯条或灯环 | 已列入推荐一起买 | 这是外接车灯/状态灯模块，不是主控板自带 |
| WS2812B 5V 数据电平裕量不足 | SN74AHCT1G125/74AHCT125/74HCT14 电平转换器 | 已列入推荐一起买 | 正式版建议加，短线直连只适合临时测试 |
| 主控板有音频链路但没有喇叭本体 | 小喇叭 | 已列入推荐一起买 | 接主控板喇叭接口即可，不需要 MAX98357A |

## 已有/核心物料链接

| 项目 | 数量 | 推荐搜索词 | 下单校验 | 京东 | 淘宝 | 拼多多 |
| --- | ---: | --- | --- | --- | --- | --- |
| ESP32-S3-DualEye-Touch-LCD-1.28 | 1 | ESP32-S3-DualEye-Touch-LCD-1.28 Waveshare 双眼 屏 开发板 | 若你已经有这块板，不重复购买；若补买，确认是 Waveshare 双 1.28 寸圆形触摸屏版本，M2 安装孔，带电池/音频/喇叭接口 | [搜索](https://search.jd.com/Search?keyword=ESP32-S3-DualEye-Touch-LCD-1.28%20Waveshare%20%E5%8F%8C%E7%9C%BC%20%E5%B1%8F%20%E5%BC%80%E5%8F%91%E6%9D%BF) | [搜索](https://s.taobao.com/search?q=ESP32-S3-DualEye-Touch-LCD-1.28%20Waveshare%20%E5%8F%8C%E7%9C%BC%20%E5%B1%8F%20%E5%BC%80%E5%8F%91%E6%9D%BF) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=ESP32-S3-DualEye-Touch-LCD-1.28%20Waveshare%20%E5%8F%8C%E7%9C%BC%20%E5%B1%8F%20%E5%BC%80%E5%8F%91%E6%9D%BF) |

## 必买清单

| 项目 | 数量 | 推荐搜索词 | 下单校验 | 京东 | 淘宝 | 拼多多 |
| --- | ---: | --- | --- | --- | --- | --- |
| Seeed Studio XIAO ESP32C3 底盘控制板 | 1 | Seeed XIAO ESP32C3 开发板 | 优先买 Seeed 原版或引脚丝印一致版本；USB-C；3.3V GPIO；D2-D5 控 DRV8833，D6/D7 接 DualEye UART；ESP32-C3 SuperMini 可替代但必须重核引脚 | [搜索](https://search.jd.com/Search?keyword=Seeed%20XIAO%20ESP32C3%20%E5%BC%80%E5%8F%91%E6%9D%BF) | [搜索](https://s.taobao.com/search?q=Seeed%20XIAO%20ESP32C3%20%E5%BC%80%E5%8F%91%E6%9D%BF) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=Seeed%20XIAO%20ESP32C3%20%E5%BC%80%E5%8F%91%E6%9D%BF) |
| N20 金属减速电机 | 4 | N20 金属减速电机 6V 100RPM 3mm D轴 轴长10mm | 6V；100-150RPM；3mm D 形轴；轴长 8-10mm；四个尽量同规格同批次 | [搜索](https://search.jd.com/Search?keyword=N20%20%E9%87%91%E5%B1%9E%E5%87%8F%E9%80%9F%E7%94%B5%E6%9C%BA%206V%20100RPM%203mm%20D%E8%BD%B4%20%E8%BD%B4%E9%95%BF10mm) | [搜索](https://s.taobao.com/search?q=N20%20%E9%87%91%E5%B1%9E%E5%87%8F%E9%80%9F%E7%94%B5%E6%9C%BA%206V%20100RPM%203mm%20D%E8%BD%B4%20%E8%BD%B4%E9%95%BF10mm) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=N20%20%E9%87%91%E5%B1%9E%E5%87%8F%E9%80%9F%E7%94%B5%E6%9C%BA%206V%20100RPM%203mm%20D%E8%BD%B4%20%E8%BD%B4%E9%95%BF10mm) |
| DRV8833 双路 H 桥电机驱动板（必备） | 2 | DRV8833 双路 H桥 电机驱动模块 迷你 | 芯片/模块必须是 DRV8833；前后各一块；不要用一块板并联四个电机长期运行 | [搜索](https://search.jd.com/Search?keyword=DRV8833%20%E5%8F%8C%E8%B7%AF%20H%E6%A1%A5%20%E7%94%B5%E6%9C%BA%E9%A9%B1%E5%8A%A8%E6%A8%A1%E5%9D%97%20%E8%BF%B7%E4%BD%A0) | [搜索](https://s.taobao.com/search?q=DRV8833%20%E5%8F%8C%E8%B7%AF%20H%E6%A1%A5%20%E7%94%B5%E6%9C%BA%E9%A9%B1%E5%8A%A8%E6%A8%A1%E5%9D%97%20%E8%BF%B7%E4%BD%A0) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=DRV8833%20%E5%8F%8C%E8%B7%AF%20H%E6%A1%A5%20%E7%94%B5%E6%9C%BA%E9%A9%B1%E5%8A%A8%E6%A8%A1%E5%9D%97%20%E8%BF%B7%E4%BD%A0) |
| 64T 钢齿轮轮芯（备选） | 4 | 64T 钢齿轮 3mm D孔 30mm 机器人 小车 | 当前主轮方案；必须选 3mm D 孔，外径 30-36mm，四个同规格 | [搜索](https://search.jd.com/Search?keyword=64T%20%E9%92%A2%E9%BD%BF%E8%BD%AE%203mm%20D%E5%AD%94%2030mm%20%E6%9C%BA%E5%99%A8%E4%BA%BA%20%E5%B0%8F%E8%BD%A6) | [搜索](https://s.taobao.com/search?q=64T%20%E9%92%A2%E9%BD%BF%E8%BD%AE%203mm%20D%E5%AD%94%2030mm%20%E6%9C%BA%E5%99%A8%E4%BA%BA%20%E5%B0%8F%E8%BD%A6) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=64T%20%E9%92%A2%E9%BD%BF%E8%BD%AE%203mm%20D%E5%AD%94%2030mm%20%E6%9C%BA%E5%99%A8%E4%BA%BA%20%E5%B0%8F%E8%BD%A6) |
| 齿轮外圈防滑圈 | 4-8 | 橡胶 O型圈 热缩管 30mm 35mm 轮胎 防滑 | 四个齿轮外圈都要套；多买几个备件方便调同心和摩擦 | [搜索](https://search.jd.com/Search?keyword=%E6%A9%A1%E8%83%B6%20O%E5%9E%8B%E5%9C%88%20%E7%83%AD%E7%BC%A9%E7%AE%A1%2030mm%2035mm%20%E8%BD%AE%E8%83%8E%20%E9%98%B2%E6%BB%91) | [搜索](https://s.taobao.com/search?q=%E6%A9%A1%E8%83%B6%20O%E5%9E%8B%E5%9C%88%20%E7%83%AD%E7%BC%A9%E7%AE%A1%2030mm%2035mm%20%E8%BD%AE%E8%83%8E%20%E9%98%B2%E6%BB%91) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=%E6%A9%A1%E8%83%B6%20O%E5%9E%8B%E5%9C%88%20%E7%83%AD%E7%BC%A9%E7%AE%A1%2030mm%2035mm%20%E8%BD%AE%E8%83%8E%20%E9%98%B2%E6%BB%91) |
| 18650 锂电池 | 1 | 18650 锂电池 3.7V 2600mAh 带保护板 持续放电3A | 单节 3.7V/4.2V；优先高倍率，持续放电建议 >= 8A；不要买 7.4V 电池组 | [搜索](https://search.jd.com/Search?keyword=18650%20%E9%94%82%E7%94%B5%E6%B1%A0%203.7V%202600mAh%20%E5%B8%A6%E4%BF%9D%E6%8A%A4%E6%9D%BF%20%E6%8C%81%E7%BB%AD%E6%94%BE%E7%94%B53A) | [搜索](https://s.taobao.com/search?q=18650%20%E9%94%82%E7%94%B5%E6%B1%A0%203.7V%202600mAh%20%E5%B8%A6%E4%BF%9D%E6%8A%A4%E6%9D%BF%20%E6%8C%81%E7%BB%AD%E6%94%BE%E7%94%B53A) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=18650%20%E9%94%82%E7%94%B5%E6%B1%A0%203.7V%202600mAh%20%E5%B8%A6%E4%BF%9D%E6%8A%A4%E6%9D%BF%20%E6%8C%81%E7%BB%AD%E6%94%BE%E7%94%B53A) |
| 单节 18650 电池盒带线 | 1 | 单节 18650 电池盒 带线 1节 | 只买单节；尺寸接近 76 x 21 x 20mm；带线更方便 | [搜索](https://search.jd.com/Search?keyword=%E5%8D%95%E8%8A%82%2018650%20%E7%94%B5%E6%B1%A0%E7%9B%92%20%E5%B8%A6%E7%BA%BF%201%E8%8A%82) | [搜索](https://s.taobao.com/search?q=%E5%8D%95%E8%8A%82%2018650%20%E7%94%B5%E6%B1%A0%E7%9B%92%20%E5%B8%A6%E7%BA%BF%201%E8%8A%82) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=%E5%8D%95%E8%8A%82%2018650%20%E7%94%B5%E6%B1%A0%E7%9B%92%20%E5%B8%A6%E7%BA%BF%201%E8%8A%82) |
| 5V 升压模块 | 1 | 锂电池升压模块 3.7V转5V 2A 3A | 1S 锂电输入；稳定 5V 输出；四电机建议 2-3A 连续/4-5A 峰值 | [搜索](https://search.jd.com/Search?keyword=%E9%94%82%E7%94%B5%E6%B1%A0%E5%8D%87%E5%8E%8B%E6%A8%A1%E5%9D%97%203.7V%E8%BD%AC5V%202A%203A) | [搜索](https://s.taobao.com/search?q=%E9%94%82%E7%94%B5%E6%B1%A0%E5%8D%87%E5%8E%8B%E6%A8%A1%E5%9D%97%203.7V%E8%BD%AC5V%202A%203A) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=%E9%94%82%E7%94%B5%E6%B1%A0%E5%8D%87%E5%8E%8B%E6%A8%A1%E5%9D%97%203.7V%E8%BD%AC5V%202A%203A) |
| 电源开关与保护 | 1 套 | 迷你拨动开关 1S 18650 保护板 3.7V | 若电池已带保护板，可只买开关；不要把 TP4056 与主控板充电路径并联 | [搜索](https://search.jd.com/Search?keyword=%E8%BF%B7%E4%BD%A0%E6%8B%A8%E5%8A%A8%E5%BC%80%E5%85%B3%201S%2018650%20%E4%BF%9D%E6%8A%A4%E6%9D%BF%203.7V) | [搜索](https://s.taobao.com/search?q=%E8%BF%B7%E4%BD%A0%E6%8B%A8%E5%8A%A8%E5%BC%80%E5%85%B3%201S%2018650%20%E4%BF%9D%E6%8A%A4%E6%9D%BF%203.7V) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=%E8%BF%B7%E4%BD%A0%E6%8B%A8%E5%8A%A8%E5%BC%80%E5%85%B3%201S%2018650%20%E4%BF%9D%E6%8A%A4%E6%9D%BF%203.7V) |
| 黄铜丝/黄铜棒 | 1 套 | 黄铜棒 2mm 3米 1.5mm 2米 1mm 黄铜丝 模型 DIY | 2.0mm 买约 3m 做主框架；1.5mm 买约 2m 做护眼圈/斜撑；0.8-1.0mm 可选 1m 做绑扎焊点，不能当主结构 | [搜索](https://search.jd.com/Search?keyword=%E9%BB%84%E9%93%9C%E6%A3%92%202mm%203%E7%B1%B3%201.5mm%202%E7%B1%B3%201mm%20%E9%BB%84%E9%93%9C%E4%B8%9D%20%E6%A8%A1%E5%9E%8B%20DIY) | [搜索](https://s.taobao.com/search?q=%E9%BB%84%E9%93%9C%E6%A3%92%202mm%203%E7%B1%B3%201.5mm%202%E7%B1%B3%201mm%20%E9%BB%84%E9%93%9C%E4%B8%9D%20%E6%A8%A1%E5%9E%8B%20DIY) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=%E9%BB%84%E9%93%9C%E6%A3%92%202mm%203%E7%B1%B3%201.5mm%202%E7%B1%B3%201mm%20%E9%BB%84%E9%93%9C%E4%B8%9D%20%E6%A8%A1%E5%9E%8B%20DIY) |
| M2 铜柱套装 | 1 套 | M2 铜柱套装 双通 6 8 10 12 15 20mm 螺丝 | M2；双通铜柱；含 6/8/10/12/15/20mm 和 M2 螺丝 | [搜索](https://search.jd.com/Search?keyword=M2%20%E9%93%9C%E6%9F%B1%E5%A5%97%E8%A3%85%20%E5%8F%8C%E9%80%9A%206%208%2010%2012%2015%2020mm%20%E8%9E%BA%E4%B8%9D) | [搜索](https://s.taobao.com/search?q=M2%20%E9%93%9C%E6%9F%B1%E5%A5%97%E8%A3%85%20%E5%8F%8C%E9%80%9A%206%208%2010%2012%2015%2020mm%20%E8%9E%BA%E4%B8%9D) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=M2%20%E9%93%9C%E6%9F%B1%E5%A5%97%E8%A3%85%20%E5%8F%8C%E9%80%9A%206%208%2010%2012%2015%2020mm%20%E8%9E%BA%E4%B8%9D) |
| 层板材料 | 2 | FR4 洞洞板 2mm 110x72 84x58 亚克力板 | 1.5-2.0mm；底层至少 110 x 72mm；中层至少 84 x 58mm；FR4 更容易手工加工 | [搜索](https://search.jd.com/Search?keyword=FR4%20%E6%B4%9E%E6%B4%9E%E6%9D%BF%202mm%20110x72%2084x58%20%E4%BA%9A%E5%85%8B%E5%8A%9B%E6%9D%BF) | [搜索](https://s.taobao.com/search?q=FR4%20%E6%B4%9E%E6%B4%9E%E6%9D%BF%202mm%20110x72%2084x58%20%E4%BA%9A%E5%85%8B%E5%8A%9B%E6%9D%BF) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=FR4%20%E6%B4%9E%E6%B4%9E%E6%9D%BF%202mm%20110x72%2084x58%20%E4%BA%9A%E5%85%8B%E5%8A%9B%E6%9D%BF) |
| JST/杜邦线套装 | 1 套 | MX1.25 2P 电池线 SH1.0 14P 转接线 28AWG 杜邦线 | 至少包含 MX1.25 2P、SH1.0 14P/FPC 转接、26-28AWG 信号线 | [搜索](https://search.jd.com/Search?keyword=MX1.25%202P%20%E7%94%B5%E6%B1%A0%E7%BA%BF%20SH1.0%2014P%20%E8%BD%AC%E6%8E%A5%E7%BA%BF%2028AWG%20%E6%9D%9C%E9%82%A6%E7%BA%BF) | [搜索](https://s.taobao.com/search?q=MX1.25%202P%20%E7%94%B5%E6%B1%A0%E7%BA%BF%20SH1.0%2014P%20%E8%BD%AC%E6%8E%A5%E7%BA%BF%2028AWG%20%E6%9D%9C%E9%82%A6%E7%BA%BF) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=MX1.25%202P%20%E7%94%B5%E6%B1%A0%E7%BA%BF%20SH1.0%2014P%20%E8%BD%AC%E6%8E%A5%E7%BA%BF%2028AWG%20%E6%9D%9C%E9%82%A6%E7%BA%BF) |
| 热缩管与绝缘材料 | 1 套 | 热缩管 1mm 2mm 3mm 5mm Kapton 高温胶带 | 1-5mm 热缩管混装；Kapton 高温胶带 | [搜索](https://search.jd.com/Search?keyword=%E7%83%AD%E7%BC%A9%E7%AE%A1%201mm%202mm%203mm%205mm%20Kapton%20%E9%AB%98%E6%B8%A9%E8%83%B6%E5%B8%A6) | [搜索](https://s.taobao.com/search?q=%E7%83%AD%E7%BC%A9%E7%AE%A1%201mm%202mm%203mm%205mm%20Kapton%20%E9%AB%98%E6%B8%A9%E8%83%B6%E5%B8%A6) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=%E7%83%AD%E7%BC%A9%E7%AE%A1%201mm%202mm%203mm%205mm%20Kapton%20%E9%AB%98%E6%B8%A9%E8%83%B6%E5%B8%A6) |

## 推荐一起买

| 项目 | 数量 | 推荐搜索词 | 下单校验 | 京东 | 淘宝 | 拼多多 |
| --- | ---: | --- | --- | --- | --- | --- |
| PCA9685 I2C PWM 扩展板（推荐） | 1 | PCA9685 16路 PWM 舵机驱动模块 I2C 迷你 | 16 通道；I2C；默认地址 0x40；优先迷你板；注意它是 PWM 信号扩展板，不是电机驱动板 | [搜索](https://search.jd.com/Search?keyword=PCA9685%2016%E8%B7%AF%20PWM%20%E8%88%B5%E6%9C%BA%E9%A9%B1%E5%8A%A8%E6%A8%A1%E5%9D%97%20I2C%20%E8%BF%B7%E4%BD%A0) | [搜索](https://s.taobao.com/search?q=PCA9685%2016%E8%B7%AF%20PWM%20%E8%88%B5%E6%9C%BA%E9%A9%B1%E5%8A%A8%E6%A8%A1%E5%9D%97%20I2C%20%E8%BF%B7%E4%BD%A0) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=PCA9685%2016%E8%B7%AF%20PWM%20%E8%88%B5%E6%9C%BA%E9%A9%B1%E5%8A%A8%E6%A8%A1%E5%9D%97%20I2C%20%E8%BF%B7%E4%BD%A0) |
| 小喇叭 | 1 | 4欧3W 小喇叭 直径40mm 以下 | 4 欧 3W 或 8 欧 1-2W；外形不超过 40mm | [搜索](https://search.jd.com/Search?keyword=4%E6%AC%A73W%20%E5%B0%8F%E5%96%87%E5%8F%AD%20%E7%9B%B4%E5%BE%8440mm%20%E4%BB%A5%E4%B8%8B) | [搜索](https://s.taobao.com/search?q=4%E6%AC%A73W%20%E5%B0%8F%E5%96%87%E5%8F%AD%20%E7%9B%B4%E5%BE%8440mm%20%E4%BB%A5%E4%B8%8B) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=4%E6%AC%A73W%20%E5%B0%8F%E5%96%87%E5%8F%AD%20%E7%9B%B4%E5%BE%8440mm%20%E4%BB%A5%E4%B8%8B) |
| WS2812B 灯条或灯环（外接车灯模块） | 1 | WS2812B 8位 灯环 5V 30mm | 5V；8 灯灯环 28-32mm 或短灯条；需 1 个数据 GPIO；建议限亮度并加 330 欧数据电阻、470-1000uF 电容 | [搜索](https://search.jd.com/Search?keyword=WS2812B%208%E4%BD%8D%20%E7%81%AF%E7%8E%AF%205V%2030mm) | [搜索](https://s.taobao.com/search?q=WS2812B%208%E4%BD%8D%20%E7%81%AF%E7%8E%AF%205V%2030mm) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=WS2812B%208%E4%BD%8D%20%E7%81%AF%E7%8E%AF%205V%2030mm) |
| WS2812B 数据电平转换器（推荐） | 1 | SN74AHCT1G125 74AHCT125 74HCT14 3.3V转5V 电平转换 | AHCT/HCT 系列；VCC 可接 5V；A 接 ESP32 数据 GPIO，Y 经 330 欧到 WS2812B DIN；不要买只适合 I2C 的双向 MOS 小板当灯带数据转换 | [搜索](https://search.jd.com/Search?keyword=SN74AHCT1G125%2074AHCT125%2074HCT14%203.3V%E8%BD%AC5V%20%E7%94%B5%E5%B9%B3%E8%BD%AC%E6%8D%A2) | [搜索](https://s.taobao.com/search?q=SN74AHCT1G125%2074AHCT125%2074HCT14%203.3V%E8%BD%AC5V%20%E7%94%B5%E5%B9%B3%E8%BD%AC%E6%8D%A2) | [搜索](https://mobile.yangkeduo.com/search_result.html?search_key=SN74AHCT1G125%2074AHCT125%2074HCT14%203.3V%E8%BD%AC5V%20%E7%94%B5%E5%B9%B3%E8%BD%AC%E6%8D%A2) |

## 暂缓购买

| 项目 | 原因 |
| --- | --- |
| TP4056 充电模块 | 主控板已有 3.7V 锂电池充放电接口；除非做隔离电源路径，否则不要并联 |
| INMP441 麦克风 | 主控板已有麦克风，第一版不必重复买 |
| MAX98357A 功放 | 主控板已有音频编解码/功放链路，第一版不必重复买 |
| OV2640 摄像头 | 第一版先跑通底盘、表情和灯光；摄像头会显著增加接线和性能复杂度 |
| 四轮独立控制板/PWM 扩展闭环 | Mk.1 采用四个物理电机、左右两侧逻辑差速；暂不做四轮独立速度控制 |

## 平台建议

- 京东：优先买电池、升压模块、开关保护、喇叭这类需要售后和参数可信度的东西。
- 淘宝：优先买 N20 电机、轮胎、DRV8833/PCA9685 模块、线材、铜柱等电子 DIY 小件，型号更全。
- 拼多多：适合买热缩管、铜柱、黄铜棒、洞洞板等低风险耗材；电池不建议只按低价买。

## 下单前最后检查

- 电池只能是单节 18650：标称 3.7V，满电 4.2V。不要买 2 串 7.4V 电池组。
- 电机驱动优先 DRV8833：不要被搜索结果里的 L298N、TB6612FNG 混淆。
- PCA9685 不是电机驱动板：它只是 PWM/控制信号扩展板，真正带电机的是 DRV8833。
- WS2812B 是外接车灯模块：主控板没有集成装饰 RGB，想做车灯/状态灯就要买这一项。
- WS2812B 数据正式版建议加 AHCT/HCT 电平转换：不要把 PCA9685 当灯带数据输出；短线直连只能先测试。
- 齿轮孔径必须匹配电机：N20 常用 3mm D 形轴，64T 齿轮优先买 3mm D 形孔。
- 电机转速建议 100RPM：150RPM 也可用 PWM 限速；更高速会让桌面小车难控。
- 所有电源模块必须共地：ESP32、DRV8833、升压模块、电池负极需要公共 GND。
- 只保留一条充电路径：不要把外置 TP4056 与主控板板载充电并联，除非做了电池路径隔离。
