#pragma once

// ============================================================
// 全局配置（改参数只动这一个文件）
// ============================================================

// ---------- Wi-Fi（AP 模式 Captive Portal，手机直连配置） ----------
#define WIFI_CONNECT_TIMEOUT_MS     15000   // 已保存凭证时的连接超时
#define CONFIG_PORTAL_TIMEOUT_MS    300000  // AP 模式等待手机配置超时（5 分钟）
#define WIFI_RETRY_DELAY_MS         500
#define WIFI_RECONNECT_INTERVAL_MS  60000   // 运行中断线重连检查间隔

// ---------- AP 热点（配置阶段） ----------
#define AP_SSID         "M5StickS3-Config"  // 配置时发出的热点名
#define AP_PASSWORD     ""                  // 空字符串=开放热点（便于手机直连弹出页面）

// ---------- NTP 时间同步 ----------
#define NTP_SERVER1 "ntp.aliyun.com"
#define NTP_SERVER2 "cn.pool.ntp.org"
#define NTP_SERVER3 "pool.ntp.org"
#define GMT_OFFSET_SEC   (8 * 3600)      // GMT+8 中国时区
#define DAYLIGHT_OFFSET_SEC  0           // 无夏令时
#define NTP_RESYNC_INTERVAL_MS (6UL * 3600UL * 1000UL)  // 每 6 小时重新同步

// ---------- NVS 命名空间 ----------
#define NVS_NAMESPACE "tide_clock"

// ---------- 位置（铜州岛 = 杭州富阳桐州岛） ----------
#define SITE_NAME     "桐洲岛"
#define SITE_LATITUDE 30.01
#define SITE_LONGITUDE 119.98

// ---------- 屏幕 ----------
#define SCREEN_ROTATION   0              // 0=竖屏(135×240)，BtnB 在下方
#define BRIGHTNESS_BOOT   128            // 启动亮度 (0-255)

// ---------- UI ----------
#define SCREEN_AUTO_RETURN_MS  5000      // 5 秒无操作回到屏 1
#define SCREEN_TRANSITION_MS   200       // 屏幕切换过渡（暂留作扩展）

// ---------- 省电优化 ----------
#define POWER_CPU_FREQ_MHZ     80       // CPU 频率（80MHz 足够，省电）
#define POWER_WIFI_OFF_AFTER_SYNC  1    // NTP 同步后关闭 Wi-Fi（1=开启省电）
#define POWER_SCREEN_DIM_MS    30000    // 30s 无操作降低亮度
#define POWER_SCREEN_OFF_MS    60000    // 60s 无操作息屏
#define POWER_SCREEN_DIM_LEVEL 20       // 降低亮度级别（0-255）
#define POWER_LOOP_DELAY_MS    100      // loop 延迟（10Hz，省电）

// ---------- 中文字体（可选，启用后屏 3 显示中文） ----------
// 步骤：
//   1. 用 Adafruit fontconvert 从 TTF 生成 .h 头文件（详见 README）
//   2. 复制为 cnfont.h（字体名 cnfont_subset16pt8b）
//   3. 下面这行已启用
#define USE_CHINESE_FONT

// ---------- 潮汐计算（阶段 2 使用） ----------
// 6 分潮简化调和分析（M2/S2/N2/K1/O1/P1）
#define NUM_CONSTITUENTS  6

// ---------- 黄历（阶段 3 使用） ----------
#define MAX_YI_ITEMS 5    // 精简版：宜 5 条
#define MAX_JI_ITEMS 5    // 精简版：忌 5 条

// ---------- 用户配置（皮划艇打分用） ----------
// 用户生肖地支索引，首次配网时在 Web 页面下拉选择，存到 NVS
// 0=子/鼠  1=丑/牛  2=寅/虎  3=卯/兔  4=辰/龙  5=巳/蛇
// 6=午/马  7=未/羊  8=申/猴  9=酉/鸡  10=戌/狗  11=亥/猪
// -1=关闭生肖冲煞
// 这是首次配网前 / NVS 未设置时的默认值
#define USER_ZODIAC_DEFAULT  1  // 默认：牛
