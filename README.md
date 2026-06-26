# M5StickS3 潮汐 + 黄历时钟 / M5StickS3 Tide + Almanac Clock

[中文](#中文) | [English](#english)

---

<a id="中文"></a>

## 中文

为 M5StickS3（ESP32-S3）开发的潮汐/黄历/水位时钟固件，专为水上运动比如皮划艇下水前的综合判断设计。

### 功能

**三屏切换（短按 BtnA）**：

| 屏 | 内容 |
|----|------|
| 屏 1 | 农历日期、桐洲岛站名、涨潮/落潮状态（大字）、距下次高潮/低潮倒计时 |
| 屏 2 | 当日 4 个潮汐时刻表（2 高潮 + 2 低潮，含水位值） |
| 屏 3 | 皮划艇适宜度评分（X/10）+ 结论 + 农历 + 干支 + 宜忌首字 |

**打分系统**：基于黄历字段权重 + 凶日否决 + 生肖冲煞，归一化为 0-10 评分：
- 7-10：宜下水（绿）
- 4-6：可下水（黄）
- 0-3：忌下水（红）

### 硬件

- M5StickS3（ESP32-S3, 8MB Flash + 8MB PSRAM, 1.14" 135×240 屏幕）
- BtnA（GPIO11）+ BtnB（GPIO12）

### 安装

#### 1. Arduino IDE 环境

- 安装 **ESP32 Arduino Core >= 3.2.5**（开发板管理器）
- 安装 **M5Unified >= 0.2.12** + **M5GFX >= 0.2.18**（库管理器）
- 开发板选 **M5StickS3**
- 工具 → **USB CDC On Boot**: Enabled
- 工具 → **Partition Scheme**: 8M with SPIFFS (3MB APP/1.5MB SPIFFS)

#### 2. 首次烧录

烧录后屏幕显示 AP 配置引导：
1. 手机连接热点 **M5StickS3-Config**
2. 浏览器访问 **192.168.4.1**
3. 输入 Wi-Fi 密码 + 选择生肖（打分用）
4. 点 Save & Connect

凭证和生肖自动保存到 NVS，后续开机直接连接。

#### 3. 中文字体（可选，屏 3 中文需要）

中文字体已包含在项目中（`cnfont.h`，霞鹜文楷 16pt 子集）。

如需重新生成：
1. 确保已启用：`config.h` 中 `#define USE_CHINESE_FONT`
2. 字体生成流程见 `rebuild_cnfont.py`（从完整 TTF 提取项目字符集）

#### 4. 串口调试

波特率 **115200**，短按 BtnA 切到屏 3 时自动打印潮汐/黄历/打分调试信息。

### 按键操作

| 操作 | 效果 |
|------|------|
| BtnA 短按 | 切换屏幕（1→2→3→1） |
| BtnA 长按 >1.5s | 重新触发 Wi-Fi 配置（换路由器时用） |
| BtnB 长按 >1.5s | 切换屏幕亮度（3 级） |
| 5 秒无操作 | 自动回到屏 1 |

### 项目结构

```
M5StickS3/
├── M5StickS3.ino          # 主入口（setup/loop + 按键状态机）
├── config.h               # 全局配置（Wi-Fi、NTP、坐标、开关）
├── time_sync.h/.cpp       # AP 配置 + NTP 同步 + 生肖 NVS 存储
├── tide_predict.h/.cpp    # 6 分潮调和预报 + 极值搜索
├── lunar_calc.h/.cpp      # 农历转换 + 干支 + 建除十二神
├── paddle_score.h/.cpp    # 皮划艇打分（权重 + 凶日否决 + 归一化）
├── ui_render.h/.cpp       # 3 屏绘制 + 中文渲染
├── data_tables.h          # 调和常数 + 农历数据 + 宜忌表 (PROGMEM)
├── cnfont.h               # 中文字体（霞鹜文楷 16pt 子集）
├── chars.txt              # 字符集（字体生成用）
├── rebuild_cnfont.py      # 从完整字体提取精简 cnfont.h
├── subset_font.py         # TTF 子集化脚本
└── fix_surgical.py        # 字体 bitmap 精确修复脚本
```

### 数据来源

- **潮汐调和常数**：来自 [calculate-tides](https://gitee.com/feathercraft/calculate-tides)（桐洲岛实测数据）
- **农历数据**：经典 1900-2099 农历查表算法
- **黄历宜忌表**：依《[钦定协纪辨方书](https://zh.wikisource.org/zh-hans/%E6%AC%BD%E5%AE%9A%E5%8D%94%E7%B4%80%E8%BE%A8%E6%96%B9%E6%9B%B8_(%E5%9B%9B%E5%BA%AB%E5%85%A8%E6%9B%B8%E6%9C%AC)/%E5%85%A8%E8%A6%BD2)》卷四·义例二校准 12 值神宜忌
- **字体**：[霞鹜文楷](https://github.com/lxgw/LxgwWenKai)（开源手写古韵字体）
- **位置**：杭州富阳桐洲岛（30.01°N, 119.98°E）

### 打分系统

#### 字段权重

| 黄历字段 | 权重 | 宜(+) | 忌(-) |
|---------|------|-------|-------|
| 出行 | 5 | +5 | -5 |
| 开池 | 4 | +4 | -4 |
| 造桥 | 4 | +4 | -4 |
| 祭祀 | 2 | +2 | -2 |
| 开市/交易/立券 | 1 | +1 | -1 |
| 移徙/赴任/沐浴 | 1 | +1 | -1 |
| 破土 | 3 | - | -3 |

#### 凶日否决（一票否决）

| 类型 | 判断 | 分数 |
|------|------|------|
| 破日 | 建除十二神"破"（godIndex=6） | 0/10 |
| 四离日 | 立春/夏/秋/冬前一天 | 0/10 |
| 四绝日 | 春分/夏至/秋分/冬至前一天 | 0/10 |
| 杨公忌日 | 13 个固定农历日 | 1/10 |
| 生肖冲煞 | 日支六冲用户生肖 | -3 |

#### 归一化

```
normalized = max(1, min(10, rawScore + 5))
否决 = 0
```

| 归一化 | 结论 |
|--------|------|
| 7-10 | 宜下水 |
| 4-6 | 可下水 |
| 0-3 | 忌下水 |

---

<a id="english"></a>

## English

Firmware for the M5StickS3 (ESP32-S3) that works as a tide / almanac / water-level clock, designed to help with go/no-go decisions before water sports such as kayaking.

### Features

**Three-screen toggle (short press BtnA)**:

| Screen | Content |
|--------|---------|
| Screen 1 | Lunar date, Tongzhou Island station name, rising/falling tide status (large), countdown to next high/low tide |
| Screen 2 | Today's 4 tide times (2 high + 2 low, with water levels) |
| Screen 3 | Kayak suitability score (X/10) + verdict + lunar calendar + Ganzhi + first char of Yi/Ji (auspicious/inauspicious) |

**Scoring system**: based on almanac field weights + inauspicious-day veto + zodiac clash, normalized to 0-10:
- 7-10: suitable (green)
- 4-6: acceptable (yellow)
- 0-3: avoid (red)

### Hardware

- M5StickS3 (ESP32-S3, 8MB Flash + 8MB PSRAM, 1.14" 135×240 display)
- BtnA (GPIO11) + BtnB (GPIO12)

### Installation

#### 1. Arduino IDE setup

- Install **ESP32 Arduino Core >= 3.2.5** (Boards Manager)
- Install **M5Unified >= 0.2.12** + **M5GFX >= 0.2.18** (Library Manager)
- Board: **M5StickS3**
- Tools → **USB CDC On Boot**: Enabled
- Tools → **Partition Scheme**: 8M with SPIFFS (3MB APP/1.5MB SPIFFS)

#### 2. First flash

After flashing, the device shows an AP configuration portal:
1. Connect your phone to hotspot **M5StickS3-Config**
2. Open **192.168.4.1** in a browser
3. Enter Wi-Fi password + select your Chinese zodiac (used by scoring)
4. Click Save & Connect

Credentials and zodiac are saved to NVS automatically; subsequent boots connect directly.

#### 3. Chinese font (optional, needed for Chinese on Screen 3)

The font is bundled (`cnfont.h`, LXGW WenKai 16pt subset).

To regenerate:
1. Enable `#define USE_CHINESE_FONT` in `config.h`
2. See `rebuild_cnfont.py` (extracts the project's character set from the full TTF)

#### 4. Serial debug

Baud rate **115200**. Short-press BtnA to Screen 3 auto-prints tide/almanac/score debug info.

### Button operations

| Action | Effect |
|--------|--------|
| BtnA short press | Switch screen (1→2→3→1) |
| BtnA long press >1.5s | Re-trigger Wi-Fi config (when changing router) |
| BtnB long press >1.5s | Toggle brightness (3 levels) |
| 5s idle | Auto-return to Screen 1 |

### Project structure

```
M5StickS3/
├── M5StickS3.ino          # Entry (setup/loop + button state machine)
├── config.h               # Global config (Wi-Fi, NTP, coords, toggles)
├── time_sync.h/.cpp       # AP config + NTP sync + zodiac NVS storage
├── tide_predict.h/.cpp    # 6-component harmonic prediction + extrema search
├── lunar_calc.h/.cpp      # Lunar conversion + Ganzhi + Jianchu 12 gods
├── paddle_score.h/.cpp    # Kayak scoring (weights + veto + normalization)
├── ui_render.h/.cpp       # 3-screen drawing + Chinese rendering
├── data_tables.h          # Harmonic constants + lunar data + Yi/Ji tables (PROGMEM)
├── cnfont.h               # Chinese font (LXGW WenKai 16pt subset)
├── chars.txt              # Character set (for font generation)
├── rebuild_cnfont.py      # Build slim cnfont.h from full font
├── subset_font.py         # TTF subsetting script
└── fix_surgical.py        # Font bitmap surgical fix script
```

### Data sources

- **Tide harmonic constants**: [calculate-tides](https://gitee.com/feathercraft/calculate-tides) (Tongzhou Island field data)
- **Lunar data**: classic 1900-2099 lunar lookup-table algorithm
- **Almanac Yi/Ji tables**: calibrated against the *Qinding Xieji Bianfang Shu* (Imperial Almanac, Qianlong era), Vol. 4
- **Font**: [LXGW WenKai](https://github.com/lxgw/LxgwWenKai) (open-source handwritten-style font)
- **Location**: Tongzhou Island, Fuyang, Hangzhou (30.01°N, 119.98°E)

### Scoring system

#### Field weights

| Almanac field | Weight | Yi (+) | Ji (-) |
|---------------|--------|--------|--------|
| Travel | 5 | +5 | -5 |
| Dig pond | 4 | +4 | -4 |
| Build bridge | 4 | +4 | -4 |
| Sacrifice | 2 | +2 | -2 |
| Open market / trade / sign | 1 | +1 | -1 |
| Move / take office / bathe | 1 | +1 | -1 |
| Break ground | 3 | - | -3 |

#### Inauspicious-day veto (one-strike veto)

| Type | Rule | Score |
|------|------|-------|
| Po day | Jianchu 12 gods "Po" (godIndex=6) | 0/10 |
| Si Li | Day before start of spring/summer/autumn/winter | 0/10 |
| Si Jue | Day before spring/summer solstice / autumn/winter equinox | 0/10 |
| Yang Gong taboo | 13 fixed lunar dates | 1/10 |
| Zodiac clash | Day branch six-clashes user zodiac | -3 |

#### Normalization

```
normalized = max(1, min(10, rawScore + 5))
veto = 0
```

| Normalized | Verdict |
|-----------|---------|
| 7-10 | Suitable |
| 4-6 | Acceptable |
| 0-3 | Avoid |

## License

MIT — see [LICENSE](LICENSE)
