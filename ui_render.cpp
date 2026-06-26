#include "ui_render.h"
#include "config.h"
#include "time_sync.h"
#include "tide_predict.h"
#include "lunar_calc.h"
#include "paddle_score.h"

#include <M5Unified.h>   // 先 include，定义 M5GFX 的 GFXfont/GFXglyph 类型
#include <time.h>

#ifdef USE_CHINESE_FONT
#include "cnfont.h"      // cnfont.h 用 GFXfont/GFXglyph（M5GFX 已定义）
#endif

// 显示访问宏
#define dsp M5.Display

// 屏幕尺寸（rotation=0 竖屏）
static int32_t s_W = 0;  // 135
static int32_t s_H = 0;  // 240

// 中文字体尺寸（从 cnfont.h 的 Glyph 数据提取，16pt 霞鹜文楷子集）
// 用于布局规划，确保汉字不重叠、不超出屏幕
#define CN_CHAR_W    31   // 每个汉字的 xAdvance（像素）
#define CN_CHAR_H    30   // 字形高度（像素）
#define CN_LINE_H    37   // 行高 yAdvance（像素）
#define CN_MAX_CHARS (s_W / CN_CHAR_W)  // 每行最多汉字数（约 4 个）

// 状态
static int s_currentScreen = SCREEN_CLOCK;
static bool s_forceRedraw = true;

// 中文字体可用性（编译时确定）
#ifdef USE_CHINESE_FONT
static const bool s_cnFontAvailable = true;
#else
static const bool s_cnFontAvailable = false;
#endif

// 屏 1 时钟防闪烁缓存
static long s_lastHiMin = -1;  // 缓存到分钟级
static long s_lastLoMin = -1;
static int  s_lastDay    = -1;  // 跨日强制重绘

// 潮汐极值缓存：避免每秒重算 3 天搜索
// ponytail: 事件粒度本就是分钟级，过期才重算，省 ~5000 次 cosf/秒
static time_t s_cachedNextHi = 0;
static time_t s_cachedNextLo = 0;

// 黄历缓存（一天计算一次）
static LunarInfo s_lunar = {0};
static int s_lunarCalcDay = -1;

// 打分缓存
static PaddleScore s_score = {0};
static int s_scoreCalcDay = -1;

// ============================================================
// 辅助：居中绘制字符串（含中文）
// ============================================================

// 用中文字体居中绘制（在调用前 loadFont，在调用后 unloadFont）
static void drawCenteredH_CN(const char *str, int y,
                              uint16_t fg, uint16_t bg = TFT_BLACK) {
    dsp.setTextColor(fg, bg);
    int16_t w = dsp.textWidth(str);
    dsp.setCursor((s_W - w) / 2, y);
    dsp.print(str);
}

// ============================================================
// 农历数字转中文
// ============================================================

static const char* lunarMonthNameCN(int m) {
    static const char* names[] = {
        "正月", "二月", "三月", "四月", "五月", "六月",
        "七月", "八月", "九月", "十月", "冬月", "腊月"
    };
    if (m < 1 || m > 12) return "?月";
    return names[m - 1];
}

// 农历日 → 中文（初一/初二...廿九/三十）
static void lunarDayNameCN(int d, char *buf, size_t bufSize) {
    static const char* digits[] = {
        "〇", "一", "二", "三", "四", "五", "六", "七", "八", "九"
    };
    if (d < 1 || d > 30) { snprintf(buf, bufSize, "?"); return; }
    if (d == 10)      { snprintf(buf, bufSize, "初十"); return; }
    if (d == 20)      { snprintf(buf, bufSize, "二十"); return; }
    if (d == 30)      { snprintf(buf, bufSize, "三十"); return; }
    if (d < 10)       { snprintf(buf, bufSize, "初%s", digits[d]); return; }
    if (d < 20)       { snprintf(buf, bufSize, "十%s", digits[d - 10]); return; }
    /* d 21..29 */      snprintf(buf, bufSize, "廿%s", digits[d - 20]);
}

// 打分等级 → 中文
static const char* scoreLevelCN(ScoreLevel level) {
    switch (level) {
        case ScoreLevel::EXCELLENT: return "极佳";
        case ScoreLevel::GOOD:      return "宜行";
        case ScoreLevel::OK:        return "尚可";
        case ScoreLevel::CAUTION:   return "慎行";
        case ScoreLevel::BAD:       return "不宜";
        case ScoreLevel::VETOED:    return "大忌";
    }
    return "?";
}

// 否决原因 → 中文
static const char* vetoReasonCN(const PaddleScore &s) {
    if (!s.vetoed || !s.vetoReason) return "";
    if (strstr(s.vetoReason, "Po Day"))    return "月破日 大凶";
    if (strstr(s.vetoReason, "Si Li"))     return "四离日 大凶";
    if (strstr(s.vetoReason, "Si Jue"))    return "四绝日 大凶";
    if (strstr(s.vetoReason, "Yang Gong")) return "杨公忌日";
    return s.vetoReason;
}

// ============================================================
// 辅助：居中绘制字符串
// ============================================================

static void drawCenteredH(const char *str, int y, int size,
                          uint16_t fg, uint16_t bg = TFT_BLACK) {
    dsp.setTextSize(size);
    dsp.setTextColor(fg, bg);
    int16_t w = dsp.textWidth(str);
    dsp.setCursor((s_W - w) / 2, y);
    dsp.print(str);
}

static void drawCenteredH(const String &str, int y, int size,
                          uint16_t fg, uint16_t bg = TFT_BLACK) {
    drawCenteredH(str.c_str(), y, size, fg, bg);
}

// ============================================================
// 屏 1：时钟 + 当前水位 + 倒计时
// ============================================================

static void renderScreen0_Clock() {
    if (!isTimeValid()) {
        // 未同步状态
        dsp.fillScreen(TFT_BLACK);
        SyncState st = getSyncState();
        uint16_t c = (st == SyncState::AP_PORTAL_WAIT) ? TFT_YELLOW :
                     (st == SyncState::WIFI_FAILED) ? TFT_RED : TFT_CYAN;
        drawCenteredH(syncStateString(st), 100, 2, c);
        drawCenteredH("Long press BtnA", 140, 1, TFT_DARKGREY);
        drawCenteredH("to setup WiFi", 155, 1, TFT_DARKGREY);
        return;
    }

    struct tm tinfo;
    if (!getLocalTime(&tinfo)) return;

    char dateStr[20];
    strftime(dateStr, sizeof(dateStr), "%Y/%m/%d %a", &tinfo);

    // 计算农历（每天一次，与屏 3 共享缓存）
    int day = tinfo.tm_mday;
    if (day != s_lunarCalcDay) {
        s_lunar = calcLunar(tinfo.tm_year + 1900,
                            tinfo.tm_mon + 1,
                            tinfo.tm_mday);
        s_lunarCalcDay = day;
    }

    time_t now = getNow();

    // 缓存下次高潮/低潮：now 超过缓存值才重算
    if (s_cachedNextHi <= now) s_cachedNextHi = getNextHighTide(now);
    if (s_cachedNextLo <= now) s_cachedNextLo = getNextLowTide(now);
    time_t nextHi = s_cachedNextHi;
    time_t nextLo = s_cachedNextLo;

    long hiSec = nextHi > 0 ? (long)difftime(nextHi, now) : -1;
    long loSec = nextLo > 0 ? (long)difftime(nextLo, now) : -1;
    long hiMin = hiSec / 60;
    long loMin = loSec / 60;

    bool flood = isFloodTide(now);

    // 防闪烁：分钟变化、跨日、或强制重绘
    bool needRedraw = s_forceRedraw ||
                      (hiMin != s_lastHiMin) ||
                      (loMin != s_lastLoMin) ||
                      (day != s_lastDay);

    s_lastHiMin = hiMin;
    s_lastLoMin = loMin;
    s_lastDay   = day;

    if (!needRedraw) {
        s_forceRedraw = false;
        return;
    }

    dsp.fillScreen(TFT_BLACK);

    // 顶部状态栏：日期 + 同步状态
    dsp.setTextSize(1);
    dsp.setTextColor(TFT_WHITE, TFT_BLACK);
    dsp.setCursor(4, 4);
    dsp.print(dateStr);

    const char *stStr = syncStateString(getSyncState());
    uint16_t stColor = (getSyncState() == SyncState::SYNCED) ? TFT_GREEN :
                       (getSyncState() == SyncState::RTC_ONLY) ? TFT_ORANGE :
                       TFT_RED;
    dsp.setTextColor(stColor, TFT_BLACK);
    int16_t stW = dsp.textWidth(stStr);
    dsp.setCursor(s_W - stW - 4, 4);
    dsp.print(stStr);

    // 农历日期（五月初七）
#ifdef USE_CHINESE_FONT
    dsp.setFont(&cnfont_subset16pt8b);
    char dayCN[12];
    lunarDayNameCN(s_lunar.lunarDay, dayCN, sizeof(dayCN));
    char ldateCN[20];
    snprintf(ldateCN, sizeof(ldateCN), "%s%s%s",
             s_lunar.isLeap ? "闰" : "",
             lunarMonthNameCN(s_lunar.lunarMonth), dayCN);
    drawCenteredH_CN(ldateCN, 18, TFT_YELLOW);

    // 站点名（桐洲岛）- 农历下方，间距 = CN_LINE_H（37px）
    drawCenteredH_CN(SITE_NAME, 18 + CN_LINE_H, TFT_CYAN);
    dsp.setTextFont(0);
#else
    char ldateASCII[20];
    snprintf(ldateASCII, sizeof(ldateASCII), "Lunar %d/%d",
             s_lunar.lunarMonth, s_lunar.lunarDay);
    drawCenteredH(ldateASCII, 18, 1, TFT_YELLOW);
    drawCenteredH(SITE_NAME, 18 + CN_LINE_H, 1, TFT_CYAN);
#endif

    // 大字涨落状态（中文字体优先，回退 ASCII）
    const char *tideStatus;
#ifdef USE_CHINESE_FONT
    dsp.setFont(&cnfont_subset16pt8b);
    tideStatus = flood ? "涨潮" : "落潮";
#else
    dsp.setTextSize(4);
    tideStatus = flood ? "Rise" : "Fall";
#endif
    dsp.setTextColor(flood ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
    int16_t tw = dsp.textWidth(tideStatus);
    dsp.setCursor((s_W - tw) / 2, 92);
    dsp.print(tideStatus);
#ifdef USE_CHINESE_FONT
    dsp.setTextFont(0);
#endif

    // 分隔线
    dsp.drawLine(8, 132, s_W - 8, 132, TFT_DARKGREY);

    // 距下次高潮（两行：时间 + 倒计时）
    if (hiSec > 0) {
        char hiTime[8];
        struct tm hitm;
        localtime_r(&nextHi, &hitm);
        strftime(hiTime, sizeof(hiTime), "%H:%M", &hitm);

        // 第一行：Hi 时间（白色）
        char hiLine1[16];
        snprintf(hiLine1, sizeof(hiLine1), "Hi %s", hiTime);
        dsp.setTextSize(2);
        dsp.setTextColor(TFT_WHITE, TFT_BLACK);
        int16_t w = dsp.textWidth(hiLine1);
        dsp.setCursor((s_W - w) / 2, 140);
        dsp.print(hiLine1);

        // 第二行：倒计时（绿色）
        char hiLine2[16];
        snprintf(hiLine2, sizeof(hiLine2), "%ldh%ldm",
                 hiSec / 3600, (hiSec % 3600) / 60);
        dsp.setTextColor(TFT_GREEN, TFT_BLACK);
        w = dsp.textWidth(hiLine2);
        dsp.setCursor((s_W - w) / 2, 160);
        dsp.print(hiLine2);
    }

    // 距下次低潮（两行：时间 + 倒计时）
    if (loSec > 0) {
        char loTime[8];
        struct tm lotm;
        localtime_r(&nextLo, &lotm);
        strftime(loTime, sizeof(loTime), "%H:%M", &lotm);

        // 第一行：Lo 时间（浅灰色）
        char loLine1[16];
        snprintf(loLine1, sizeof(loLine1), "Lo %s", loTime);
        dsp.setTextSize(2);
        dsp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        int16_t w = dsp.textWidth(loLine1);
        dsp.setCursor((s_W - w) / 2, 184);
        dsp.print(loLine1);

        // 第二行：倒计时（橙色）
        char loLine2[16];
        snprintf(loLine2, sizeof(loLine2), "%ldh%ldm",
                 loSec / 3600, (loSec % 3600) / 60);
        dsp.setTextColor(TFT_ORANGE, TFT_BLACK);
        w = dsp.textWidth(loLine2);
        dsp.setCursor((s_W - w) / 2, 204);
        dsp.print(loLine2);
    }

    s_forceRedraw = false;
}

// ============================================================
// 屏 2：当日潮汐时刻表
// ============================================================

static void renderScreen1_Tides() {
    if (!isTimeValid()) return;

    dsp.fillScreen(TFT_BLACK);

    // 大标题
    drawCenteredH("Tides Today", 6, 2, TFT_YELLOW);
    drawCenteredH(SITE_NAME, 32, 1, TFT_DARKGREY);

    // 分隔线
    dsp.drawLine(8, 48, s_W - 8, 48, TFT_DARKGREY);

    // 当日 4 个极值
    struct tm tinfo;
    if (!getLocalTime(&tinfo)) return;
    struct tm dayStart = tinfo;
    dayStart.tm_hour = 0;
    dayStart.tm_min = 0;
    dayStart.tm_sec = 0;
    time_t dayStartT = mktime(&dayStart);

    TideEvent events[4];
    int count = 0;
    findDailyExtrema(dayStartT, events, count);

    // 每事件 2 行（Hi/Lo+时间 一行，水位 另一行），紧凑布局
    int y = 58;
    for (int i = 0; i < count && y < s_H - 30; i++) {
        char timeBuf[8];
        struct tm evtTm;
        localtime_r(&events[i].at, &evtTm);
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &evtTm);

        bool isHigh = events[i].isHigh;

        // 第一行：Hi/Lo（标签色：绿/橙）
        dsp.setTextSize(2);
        dsp.setTextColor(isHigh ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
        dsp.setCursor(4, y);
        dsp.print(isHigh ? "Hi" : "Lo");

        // 第一行：时间（白色，突出）
        dsp.setTextColor(TFT_WHITE, TFT_BLACK);
        dsp.setCursor(34, y);
        dsp.print(timeBuf);

        // 第二行：水位（高潮青色 / 低潮品红——跟 Lo 标签 ORANGE 拉开层次）
        char levelStr[16];
        snprintf(levelStr, sizeof(levelStr), "%.2fm", events[i].level);
        dsp.setTextColor(isHigh ? TFT_CYAN : TFT_MAGENTA, TFT_BLACK);
        dsp.setCursor(34, y + 16);
        dsp.print(levelStr);

        y += 38;
    }

    // 底部同步状态
    const char *st = syncStateString(getSyncState());
    drawCenteredH(st, s_H - 12, 1,
                  getSyncState() == SyncState::SYNCED ? TFT_GREEN : TFT_ORANGE);

    s_forceRedraw = false;
}

// ============================================================
// 屏 3：当日黄历
// ============================================================

static void renderScreen2_Lunar() {
    if (!isTimeValid()) return;

    // 字体未启用：显示英文提示
    if (!s_cnFontAvailable) {
        dsp.fillScreen(TFT_BLACK);
        drawCenteredH("Chinese font disabled", 50, 2, TFT_YELLOW);
        drawCenteredH("To enable:", 90, 1, TFT_WHITE);
        drawCenteredH("1. Generate cnfont.h", 105, 1, TFT_WHITE);
        drawCenteredH("2. #define USE_CHINESE_FONT", 120, 1, TFT_WHITE);
        drawCenteredH("(see README)", 145, 1, TFT_DARKGREY);
        s_forceRedraw = false;
        return;
    }

    struct tm tinfo;
    if (!getLocalTime(&tinfo)) return;
    int day = tinfo.tm_mday;

    if (day != s_lunarCalcDay) {
        s_lunar = calcLunar(tinfo.tm_year + 1900,
                            tinfo.tm_mon + 1,
                            tinfo.tm_mday);
        s_lunarCalcDay = day;
    }
    if (day != s_scoreCalcDay) {
        s_score = calculatePaddleScore(s_lunar,
                                       tinfo.tm_year + 1900,
                                       tinfo.tm_mon + 1,
                                       tinfo.tm_mday);
        s_scoreCalcDay = day;
    }

    dsp.fillScreen(TFT_BLACK);

#ifdef USE_CHINESE_FONT
    dsp.setFont(&cnfont_subset16pt8b);
#endif

    // 评分颜色（红/黄/绿）
    int normScore = getNormalizedScore(s_score);
    uint16_t scoreColor;
    if (normScore <= 3)      scoreColor = TFT_RED;
    else if (normScore <= 6) scoreColor = TFT_YELLOW;
    else                     scoreColor = TFT_GREEN;

    // ====== 6 行简洁布局 ======

    // 行1：评分 X/10（大字居中，色编码）
    char scoreLine[16];
    snprintf(scoreLine, sizeof(scoreLine), "%d/10", normScore);
    drawCenteredH_CN(scoreLine, 8, scoreColor);

    // 行2：结论（宜下水/可下水/忌下水）
    drawCenteredH_CN(getConclusionCN(s_score), 46, scoreColor);

    // 分隔线
    dsp.drawLine(8, 80, s_W - 8, 80, TFT_DARKGREY);

    // 行3：农历（五月初七）
    char dayCN[12];
    lunarDayNameCN(s_lunar.lunarDay, dayCN, sizeof(dayCN));
    char ldate[20];
    snprintf(ldate, sizeof(ldate), "%s%s",
             lunarMonthNameCN(s_lunar.lunarMonth), dayCN);
    drawCenteredH_CN(ldate, 88, TFT_YELLOW);

    // 行4：干支 4 字（年柱 + 日柱，如"丙午丙寅"）
    char gzLine[16];
    snprintf(gzLine, sizeof(gzLine), "%s%s",
             s_lunar.yearGanZhi, s_lunar.dayGanZhi);
    drawCenteredH_CN(gzLine, 122, TFT_WHITE);

    // 分隔线
    dsp.drawLine(8, 156, s_W - 8, 156, TFT_DARKGREY);

    // 行5：宜 + 3首字（每个宜忌词取第1字）
    // UTF-8 中文每字3字节，取前3个词的首字
    char yiFirst[10];
    char *p = yiFirst;
    for (int i = 0; i < 3 && i < MAX_YI_ITEMS; i++) {
        strncpy(p, s_lunar.yi[i], 3);
        p += 3;
    }
    *p = '\0';
    char yiLine[16];
    snprintf(yiLine, sizeof(yiLine), "%s%s", "宜", yiFirst);
    drawCenteredH_CN(yiLine, 164, TFT_GREEN);

    // 行6：忌 + 3首字
    char jiFirst[10];
    p = jiFirst;
    for (int i = 0; i < 3 && i < MAX_JI_ITEMS; i++) {
        strncpy(p, s_lunar.ji[i], 3);
        p += 3;
    }
    *p = '\0';
    char jiLine[16];
    snprintf(jiLine, sizeof(jiLine), "%s%s", "忌", jiFirst);
    drawCenteredH_CN(jiLine, 198, TFT_RED);

#ifdef USE_CHINESE_FONT
    dsp.setTextFont(0);  // 恢复默认字体
#endif

    s_forceRedraw = false;
}

// ============================================================
// 公开接口
// ============================================================

void initUI() {
    dsp.setRotation(SCREEN_ROTATION);  // 竖屏 135×240
    dsp.setBrightness(BRIGHTNESS_BOOT);
    dsp.fillScreen(TFT_BLACK);
    s_W = dsp.width();
    s_H = dsp.height();
    Serial.printf("[UI] Screen %dx%d (rotation=%d)\n",
                  s_W, s_H, SCREEN_ROTATION);
#ifdef USE_CHINESE_FONT
    Serial.println("[UI] Chinese font ENABLED");
#else
    Serial.println("[UI] Chinese font DISABLED (define USE_CHINESE_FONT to enable)");
#endif
    s_forceRedraw = true;
}

void renderScreen(int screenIndex) {
    s_currentScreen = screenIndex;
    s_forceRedraw = true;  // 切屏强制重绘
    switch (screenIndex) {
        case SCREEN_CLOCK: renderScreen0_Clock(); break;
        case SCREEN_TIDES: renderScreen1_Tides(); break;
        case SCREEN_LUNAR: renderScreen2_Lunar(); break;
    }
}

int cycleScreen(int currentScreen) {
    return (currentScreen + 1) % SCREEN_COUNT;
}

void uiLoopTick() {
    // 仅屏 1 需要每秒刷新（时钟）
    if (s_currentScreen == SCREEN_CLOCK) {
        renderScreen0_Clock();
    }
}

void forceRedraw() {
    s_forceRedraw = true;
    // NTP 重同步 / 切屏后时间可能跳变，缓存失效
    s_cachedNextHi = 0;
    s_cachedNextLo = 0;
}

void printAllDebug() {
    if (!isTimeValid()) return;
    printTideDebug(getNow());
    struct tm tinfo;
    if (!getLocalTime(&tinfo)) return;
    LunarInfo li = calcLunar(tinfo.tm_year + 1900,
                             tinfo.tm_mon + 1,
                             tinfo.tm_mday);
    printLunarDebug(tinfo.tm_year + 1900,
                    tinfo.tm_mon + 1,
                    tinfo.tm_mday);
    printPaddleScoreDebug(li,
                          tinfo.tm_year + 1900,
                          tinfo.tm_mon + 1,
                          tinfo.tm_mday);
}
