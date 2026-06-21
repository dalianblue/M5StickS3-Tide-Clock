# M5StickS3 潮汐 + 黄历固件

为 M5StickS3（ESP32-S3）开发的自定义固件：显示杭州富阳桐州岛潮汐、当日黄历，通过按键切换屏幕。

## Plan（实施方案）

### Context（背景）

利用 M5StickS3（ESP32-S3，1.14" 240×135 屏幕，BtnA/BtnB 两按键），开发一个自定义固件，烧录后实现：

1. 启动时通过 Wi-Fi NTP 同步当前时间（GMT+8）
2. 根据当前时间，预测桐洲岛（即浙江杭州富阳桐州岛，30.01°N, 119.98°E）的水位
3. 显示当日黄历（农历、干支、宜忌）和 4 个涨落潮时刻
4. 信息量较大，通过屏幕下方的蓝色按键（BtnB, GPIO12）切换屏幕显示

利用 Python 潮汐计算项目（`https://gitee.com/feathercraft/calculate-tides`），使用盐官校正法 + 调和分析模型，精度 ±8 分钟。该算法可移植到 ESP32-S3（浮点运算 <1ms，内存占用 ~5KB）。

---

### 1. 项目结构（多模块，Arduino sketch 同级目录）

```
M5StickS3/
├── M5StickS3.ino          # setup()/loop() 主入口，仅做调度
├── config.h               # ★新建 - Wi-Fi凭证、NTP服务器、坐标、开关
├── time_sync.h/.cpp       # ★新建 - NTP同步 + RTC读取 + 状态管理
├── tide_predict.h/.cpp    # ★新建 - 6分潮调和预报 + 极值搜索
├── lunar_calc.h/.cpp      # ★新建 - 农历转换 + 干支 + 建除十二神 + 宜忌
├── ui_render.h/.cpp       # ★新建 - 3屏布局、按键处理、状态机
└── data_tables.h          # ★新建 - 盐官时刻表/调和常数/宜忌表 (PROGMEM)
```

依赖：**M5Unified >= 0.2.12** + **M5GFX >= 0.2.18**（Arduino IDE 库管理器安装）。

---

### 2. UI 设计：3 屏切换

横屏 (`M5.Display.setRotation(1)` → 240×135)，使用 Sprite 双缓冲防闪烁。

#### 屏 1（默认）：时钟 + 当前水位 + 倒计时

```
┌──────────────────────────────────┐
│ 2026/06/20 周六  农历五月廿五     │ ← 顶部 8pt
│                                    │
│        14:32:07                    │ ← 中央 26pt 大字
│                                    │
│ 水位 +1.23m ▲涨  下次高潮 15:48   │ ← 底部 8pt
│ 倒计时 1h16m  ████████░░          │
└──────────────────────────────────┘
```

#### 屏 2：当日潮汐时刻表

```
┌──────────────────────────────────┐
│ 今日潮汐  岛 30.01N 119.98E   │
│                                    │
│  ▲ 高潮  03:12   +2.15m           │
│  ▼ 低潮  09:28   -0.83m           │
│  ▲ 高潮  15:48   +2.31m           │
│  ▼ 低潮  22:05   -0.91m           │
│                                    │
│ ⚡ NTP已同步 6h前  状态: 在线      │
└──────────────────────────────────┘
```

#### 屏 3：当日黄历

```
┌──────────────────────────────────┐
│ 丙午年 甲午月 庚申日  建: 定      │
│                                    │
│ 宜: 祭祀 祈福 开光 出行           │
│                                    │
│ 忌: 动土 破土 安葬                 │
│                                    │
│ 冲: 虎(甲寅)  煞: 南              │
└──────────────────────────────────┘
```

#### 按键分配

| 操作 | 效果 |
|------|------|
| **BtnB 短按** | 屏幕循环 1→2→3→1（用户主需求） |
| **BtnA 长按 >1.5s** | 强制 NTP 重新同步，屏幕闪烁提示 |
| **BtnB 长按 >1.5s** | 切换屏幕亮度（3 级循环），用于省电 |
| **屏 1 + BtnA 短按** | 切换水位显示模式（数字 / 迷你曲线） |

5 秒无操作自动回到屏 1（用户主关注屏）。

---

### 3. 时间策略

```
启动:
  ├─ WiFi.begin() 重试3次, 每次超时10s
  ├─ configTime(8*3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org")
  ├─ 阻塞等待 getLocalTime() 成功 (最多15s)
  └─ configTime() 自动写入 ESP32-S3 内部 RTC

运行:
  ├─ 每 1s: 读 RTC, 更新屏 1 时钟显示
  ├─ 每 6h: 后台非阻塞触发 NTP 重同步 (放 Core0, 不卡 UI)
  └─ NTP 失败: 继续用 RTC, 状态栏标注 "RTC"

断网:
  └─ ESP32-S3 RTC 每天漂移 10-30 秒, 状态栏显示距上次同步时间
```

注意：`configTime()` 内部自动管理 SNTP 并写硬件 RTC，无需手动调 `rtc_set_time()`。NTP 重同步放 FreeRTOS task 到 Core0，避免阻塞 UI（默认 `loop()` 在 Core1）。

---

### 4. 潮汐移植清单（从 Python → C++）

核心算法用 **6 分潮简化调和分析**（M2/S2/N2/K1/O1/P1，覆盖 80% 振幅）：

```cpp
// tide_predict.cpp
float getWaterLevel(time_t t);                              // 任意时刻水位
void  findDailyExtrema(time_t dayStart, TideEvent out[4]);  // 当日4极值
```

**水位公式**：

```
h(t) = z0 + Σᵢ Hᵢ × cos(σᵢ × t - φᵢ),  i ∈ {M2,S2,N2,K1,O1,P1}
```

**极值搜索**：5 分钟步长采样全天 288 点 → 检测极值 → 抛物线插值精确定位（公式：`offset = -(y_next-y_prev)/(2×((y_prev+y_next)/2-y_curr))`）。

**数据表（`data_tables.h`, PROGMEM, ~156 字节）**：

- 6 分潮角速度 σ（标准天文常数）
- 桐洲岛调和常数（H 振幅、φ 相位）— 从 `tide_harmonic_constants.json` 提取
- 盐官农历高潮表（农历 1-30 日对应分钟数，作为后备方案）

**数据来源**：直接读 `https://gitee.com/feathercraft/calculate-tides` 仓库里的 `tide_harmonic_constants.json`，取前 6 个分潮的 amplitude 和 phase 字段，写成 C 数组。

---

### 5. 黄历移植

**农历 + 干支**：从 `https://github.com/Bill-Gray/lunar` 的 `date.cpp` 中提取核心函数（农历转换、六十甲子），裁剪掉无关天文历表，目标编译后 <15KB Flash。

**宜忌**：实时计算建除十二神（建除满平定执破危成收开闭），公式：

```
godIndex = (月地支索引 + 日干支序号) % 12
宜/忌 = YIJI_TABLE[godIndex]
```

宜忌表用查表法存储（12 神 × 平均 30 项 ≈ 1KB），用 2 字节编码（宜/忌 + 索引）。

**接口**：

```cpp
// lunar_calc.cpp
LunarInfo getLunarInfo(int year, int month, int day);
// 返回: 农历月日、天干地支、建除神、宜[5]、忌[5]
```

---

### 6. 实现阶段（每阶段有可验证产物）

| 阶段 | 目标 | 验证方式 |
|------|------|---------|
| **1. 框架 + 时间** | M5Unified 初始化、Wi-Fi 连接、NTP 同步、屏 1 显示时钟 | 烧录后屏幕显示正确时间，断电重启自动同步 |
| **2. 潮汐引擎** | 移植 6 分潮预报 + 极值搜索 | 串口打印当日 4 个潮汐时刻，与 Python 原版对照 |
| **3. 黄历引擎** | 移植农历 + 干支 + 建除 + 宜忌 | 串口打印当日黄历，与在线黄历网站对照 |
| **4. UI 整合** | 3 屏切换、按键、布局排版 | 完整 3 屏显示，BtnB 切换流畅，无闪烁 |
| **5. 打磨** | 省电、NTP 容错、亮度调节 | 电池续航 >8h，断网自动恢复 |

每阶段结束做编译验证 + 串口对照测试。

---

### 7. 关键文件路径

| 文件 | 状态 | 职责 |
|------|------|------|
| `M5StickS3.ino` | 已存在（空壳） | setup/loop 主入口 |
| `config.h` | 新建 | Wi-Fi 凭证、NTP、坐标、开关 |
| `time_sync.{h,cpp}` | 新建 | NTP + RTC 时间管理 |
| `tide_predict.{h,cpp}` | 新建 | 6 分潮调和预报（核心算法） |
| `lunar_calc.{h,cpp}` | 新建 | 农历 + 干支 + 建除 + 宜忌 |
| `ui_render.{h,cpp}` | 新建 | 3 屏布局 + 按键状态机 |
| `data_tables.h` | 新建 | 所有 PROGMEM 数据表 |

---

### 8. 风险与对策

1. **ESP32-S3 RTC 漂移**：每天 10-30 秒。对策：每 6h NTP 重同步；状态栏标注上次同步时间。
2. **135 像素高度排版极限**：横屏只能放 3-4 行。对策：屏 1 用 26pt 大字时钟 + 8pt 信息栏；用 Sprite 双缓冲避免闪烁；选 M5GFX 内置小点阵字体。
3. **Wi-Fi 阻塞 loop**：对策：NTP 重同步放 FreeRTOS task 到 Core0。
4. **Bill-Gray/lunar 体积**：完整库可能 >100KB Flash。对策：只提取 `date.cpp` 农历转换函数，裁剪到 <15KB。
5. **桐洲岛调和常数精度**：内河潮汐受径流影响，纯天文潮预报汛期误差大。对策：状态栏标注"仅供参考"；后续可加水位实测校准。

---

### 9. 验证方式（端到端）

1. **编译验证**：Arduino IDE 选择开发板 "M5StickS3"，编译通过无错误
2. **时间同步**：烧录后串口看 NTP 同步日志，屏幕显示正确北京时间
3. **潮汐正确性**：C++ 输出与 Python `calculate-tides` 原版对照，4 个时刻误差 <15 分钟
4. **黄历正确性**：与 `wannianrili.bjtime.cn` 等在线黄历对照，农历、干支、建除完全一致
5. **按键交互**：BtnB 短按循环切屏，长按调亮度，BtnA 长按触发 NTP 重同步
6. **续航**：满电状态运行 >8 小时，断网自动恢复

---

### 10. 待确认的细节

实施前需要确认：

- **Wi-Fi 凭证**：硬编码到 `config.h`（最简单，需重新编译才能改）vs SmartConfig 手机配置（复杂但灵活）
- **宜忌详细度**：完整宜忌（每项平均 30 条）vs 精简版（前 5 条最常用，如"嫁娶、出行、安葬、动土、开市"）
- **桐洲岛地名**：研究中确认是杭州富阳"桐州岛"，请确认这是同一个地方

# 中文字体安装（屏 3 黄历显示中文需要）

M5GFX 默认字体不含 CJK，屏 3 的黄历、打分、农历等中文需要单独加载字体。

**方案**：用 Adafruit fontconvert 工具生成 .h 头文件（C 数组），#include 到代码中。不需要烧录 LittleFS。

## 步骤

### 1. 准备 TTF 字体（任选一个开源字体）

推荐（古韵风格适合黄历场景）：

- **霞鹜文楷**（手写古风）：https://github.com/lxgw/LxgwWenKai
- **Noto Sans SC**（思源黑体）：https://github.com/notofonts/noto-cjk
- **Source Han Sans**（思源宋体）：https://github.com/adobe-fonts/source-han-sans

下载 TTF/OTF 文件到本地（如 `LXGWWenKai-Regular.ttf`）。

### 2. 安装 Adafruit fontconvert 工具

```bash
git clone https://github.com/adafruit/Adafruit-GFX-Library.git
cd Adafruit-GFX-Library/fontconvert
make
```

依赖：`gcc`、`freetype-dev`（macOS：`brew install freetype`）

### 3. 生成字体头文件

把 TTF 文件**重命名为 `cnfont.ttf`**（这样输出文件名和字体名都是 cnfont）：

```bash
cp /path/to/LXGWWenKai-Regular.ttf ./cnfont.ttf
./fontconvert cnfont.ttf 16 0x20 0xFFFF
```

参数说明：
- `16`：字号（pt），135×240 竖屏推荐 16（紧凑）或 20（清晰）
- `0x20`：起始 Unicode（空格）
- `0xFFFF`：结束 Unicode（覆盖 ASCII + CJK 统一汉字 + 部分）

输出：`cnfont16pt7b.h`（约 100-500KB，包含 CJK 字符点阵）

### 4. 替换 sketch 中的 cnfont.h

```bash
cp cnfont16pt7b.h /path/to/M5StickS3/cnfont.h
```

替换项目里的 placeholder 文件。

### 5. 启用中文字体

编辑 `config.h`，取消注释：

```c
#define USE_CHINESE_FONT
```

### 6. 编译烧录

Arduino IDE 编译烧录。代码会自动 #include cnfont.h 并用 `cnfont16pt7b` 字体显示中文。

## 验证

固件启动时串口应显示：
```
[UI] Chinese font ENABLED
```

切到屏 3（短按 BtnA）应看到完整中文：农历日期、干支、生肖、值神、宜/忌、打分等级。

如果未启用（USE_CHINESE_FONT 未定义），屏 3 会显示英文提示：
```
Chinese font disabled
To enable:
1. Generate cnfont.h
2. #define USE_CHINESE_FONT
```

## 字号调整

如果想用其他字号（如 20pt），需要：
1. 重新运行 fontconvert，参数改为 `20`：`./fontconvert cnfont.ttf 20 0x20 0xFFFF`
2. 替换 cnfont.h（注意字体名变成 `cnfont20pt7b`）
3. 修改 ui_render.cpp 里的 `cnfont16pt7b` 为 `cnfont20pt7b`（搜索 `dsp.setFont(&cnfont` 全部替换）



# 黄历打分系统

## 完整字段映射表

| 黄历字段 | 与水上活动的关系 | 权重 | 宜(+) | 忌(-) |
|---------|----------------|------|-------|-------|
| **出行** | 核心：出远门（含水路） | ⭐⭐⭐⭐⭐ | +5 | -5 |
| **祭祀** | 间接：古代祭天求晴，代表天气适宜 | ⭐⭐⭐ | +2 | -2 |
| **开光** | 无关 | 0 | 0 | 0 |
| **祈福** | 无关 | 0 | 0 | 0 |
| **求嗣** | 无关 | 0 | 0 | 0 |
| **嫁娶** | 无关 | 0 | 0 | 0 |
| **纳采** | 无关 | 0 | 0 | 0 |
| **订盟** | 无关 | 0 | 0 | 0 |
| **入宅** | 无关 | 0 | 0 | 0 |
| **移徙** | 轻微相关：搬家搬迁，也涉及路途 | ⭐ | +1 | -1 |
| **安床** | 无关 | 0 | 0 | 0 |
| **开市** | 间接：重大事项启动日，宜则整日气场顺 | ⭐⭐ | +1 | -1 |
| **交易** | 间接：同上 | ⭐⭐ | +1 | -1 |
| **立券** | 间接：同上 | ⭐⭐ | +1 | -1 |
| **纳财** | 无关 | 0 | 0 | 0 |
| **栽种** | 无关 | 0 | 0 | 0 |
| **牧养** | 无关 | 0 | 0 | 0 |
| **纳畜** | 无关 | 0 | 0 | 0 |
| **破土** | ⚠️ 危险关联："破"字头，动土破地，不吉 | ⭐⭐⭐ | - | -3 |
| **安葬** | 无关 | 0 | 0 | 0 |
| **启攒** | 无关 | 0 | 0 | 0 |
| **修造** | 无关（除非你在江边施工） | 0 | 0 | 0 |
| **动土** | 无关 | 0 | 0 | 0 |
| **上梁** | 无关 | 0 | 0 | 0 |
| **竖柱** | 无关 | 0 | 0 | 0 |
| **盖屋** | 无关 | 0 | 0 | 0 |
| **求医** | 无关 | 0 | 0 | 0 |
| **治病** | 无关 | 0 | 0 | 0 |
| **赴任** | 轻微相关：上任途中也涉及出行 | ⭐ | +1 | -1 |
| **解除** | 无关 | 0 | 0 | 0 |
| **沐浴** | ⚠️ 沾水相关，但"沐浴"是洗身，不吉时可作负面参考 | ⭐ | +1 | -1 |
| **理发** | 无关 | 0 | 0 | 0 |
| **整容** | 无关 | 0 | 0 | 0 |
| **会亲友** | 无关 | 0 | 0 | 0 |
| **安机械** | 无关 | 0 | 0 | 0 |
| **开池** | ✅ 强相关：开挖水池，与"水"直接相关 | ⭐⭐⭐⭐ | +4 | -4 |
| **开厕** | 无关（虽然带"开"但实际是厕所） | 0 | 0 | 0 |
| **造桥** | ✅ 强相关：水上工程，与水体直接相关 | ⭐⭐⭐⭐ | +4 | -4 |

---

## 额外特殊规则（独立于“宜/忌”之外）

这些是黄历里独立标注的凶日类型，一旦命中，**直接否决**：

| 特殊规则 | 判断方式 | 惩罚 |
|---------|---------|------|
| **破日**（月破） | 日支与月支相冲 | -10（强烈不建议） |
| **四离日** | 立春/立夏/立秋/立冬前一天 | -10（大忌） |
| **四绝日** | 春分/秋分/夏至/冬至前一天 | -10（大忌） |
| **杨公忌日** | 农历正月十三、二月十一、三月初九、四月初七、五月初五、六月初三、七月初一、七月廿九、八月廿七、九月廿五、十月廿三、十一月廿一、十二月十九 | -8（大忌） |
| **冲自己生肖** | 日支冲用户生肖（如子日冲马） | -3（个人层面不建议） |

---

## 最终打分公式

```python
def calculate_water_score(almanac_today, user_zodiac=None):
    score = 0
    
    # 1. 遍历所有字段，累加权重
    field_scores = {
        '出行': 5, '开池': 4, '造桥': 4, '祭祀': 2,
        '开市': 1, '交易': 1, '立券': 1,
        '移徙': 1, '赴任': 1, '沐浴': 1,
        '破土': -3
    }
    
    for field, weight in field_scores.items():
        status = almanac_today.get(field)  # '宜' / '忌' / None
        if status == '宜':
            score += weight
        elif status == '忌':
            score -= weight
    
    # 2. 特殊凶日检查（一票否决）
    if almanac_today.get('is_po_day'):       # 破日
        return -10, "今日为破日，强烈不建议水上活动"
    if almanac_today.get('is_si_li'):        # 四离
        return -10, "今日为四离日，不宜出行涉水"
    if almanac_today.get('is_si_jue'):       # 四绝
        return -10, "今日为四绝日，不宜出行涉水"
    if almanac_today.get('is_yang_gong'):    # 杨公忌
        return -8, "今日为杨公忌日，传统上不宜远行"
    
    # 3. 生肖冲煞
    if user_zodiac and almanac_today.get('clashing_zodiac') == user_zodiac:
        score -= 3
    
    # 4. 输出等级
    if score >= 6:
        return score, "⭐ 极佳！非常适合水上活动"
    elif score >= 3:
        return score, "✅ 适宜，可以放心去划艇"
    elif score >= 0:
        return score, "👍 尚可，建议结合实时天气判断"
    elif score >= -3:
        return score, "⚠️ 略有不宜，谨慎考虑或改期"
    else:
        return score, "❌ 不建议，今日黄历不利水上活动"
```

---

## 使用示例

假设你今天黄历是：**宜出行、宜开市，忌破土，非破日、非四离四绝**：

- 出行 +5
- 开市 +1
- 破土 不触发（因为忌，已在上面扣了）
- 无特殊凶日
- 最终得分：**6分 → “极佳！非常适合水上活动”** ✅

---
