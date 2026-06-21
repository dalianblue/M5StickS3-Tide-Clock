#include "ui_render.h"
#include "config.h"
#include "time_sync.h"
#include "tide_predict.h"
#include "lunar_calc.h"
#include "paddle_score.h"

#include <M5Unified.h>
#include <time.h>

// 显示访问宏
#define dsp M5.Display

// 屏幕尺寸（rotation=0 竖屏）
static int32_t s_W = 0;  // 135
static int32_t s_H = 0;  // 240

// 状态
static int s_currentScreen = SCREEN_CLOCK;
static bool s_forceRedraw = true;

// 屏 1 时钟防闪烁缓存
static String s_lastClockHHMM = "";
static int s_lastClockSec = -1;
static long s_lastHiSec = -1;
static long s_lastLoSec = -1;

// 黄历缓存（一天计算一次）
static LunarInfo s_lunar = {0};
static int s_lunarCalcDay = -1;

// 打分缓存
static PaddleScore s_score = {0};
static int s_scoreCalcDay = -1;

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

    char hhmm[8];
    char dateStr[20];
    strftime(hhmm, sizeof(hhmm), "%H:%M", &tinfo);
    strftime(dateStr, sizeof(dateStr), "%Y/%m/%d %a", &tinfo);

    String clockHHMM = String(hhmm);
    int sec = tinfo.tm_sec;

    time_t now = getNow();
    time_t nextHi = getNextHighTide(now);
    time_t nextLo = getNextLowTide(now);
    long hiSec = nextHi > 0 ? (long)difftime(nextHi, now) : -1;
    long loSec = nextLo > 0 ? (long)difftime(nextLo, now) : -1;

    // 防闪烁：仅秒数和倒计时变化时刷新（每秒），整体翻页只在强制刷新时做
    bool needFullRedraw = s_forceRedraw || (s_lastClockHHMM != clockHHMM);
    bool needPartialUpdate = (sec != s_lastClockSec) ||
                             (hiSec / 60 != s_lastHiSec / 60) ||
                             (loSec / 60 != s_lastLoSec / 60);

    s_lastClockHHMM = clockHHMM;
    s_lastClockSec = sec;
    s_lastHiSec = hiSec;
    s_lastLoSec = loSec;

    if (needFullRedraw) {
        dsp.fillScreen(TFT_BLACK);

        // 顶部日期（小字）
        drawCenteredH(dateStr, 4, 1, TFT_WHITE);

        // 中央大字时钟 HH:MM
        drawCenteredH(clockHHMM, 35, 7, TFT_CYAN);

        // 秒数（小字）
        char secStr[8];
        snprintf(secStr, sizeof(secStr), ":%02d", sec);
        drawCenteredH(secStr, 110, 2, TFT_DARKGREY);

        // 中部：当前水位
        float level = getWaterLevel(now);
        bool flood = isFloodTide(now);
        char levelLine[24];
        snprintf(levelLine, sizeof(levelLine), "%.2fm  %s",
                 level, flood ? "Rising" : "Falling");
        drawCenteredH(levelLine, 140, 2,
                      flood ? TFT_GREEN : TFT_ORANGE);

        // 底部：距下次高潮/低潮
        if (hiSec > 0) {
            char hiLine[32];
            char hiTime[8];
            struct tm hitm;
            localtime_r(&nextHi, &hitm);
            strftime(hiTime, sizeof(hiTime), "%H:%M", &hitm);
            snprintf(hiLine, sizeof(hiLine), "Hi %s  %ldh%ldm",
                     hiTime, hiSec / 3600, (hiSec % 3600) / 60);
            dsp.setTextSize(1);
            dsp.setTextColor(TFT_WHITE, TFT_BLACK);
            dsp.setCursor(8, 180);
            dsp.print(hiLine);
        }

        if (loSec > 0) {
            char loLine[32];
            char loTime[8];
            struct tm lotm;
            localtime_r(&nextLo, &lotm);
            strftime(loTime, sizeof(loTime), "%H:%M", &lotm);
            snprintf(loLine, sizeof(loLine), "Lo %s  %ldh%ldm",
                     loTime, loSec / 3600, (loSec % 3600) / 60);
            dsp.setTextSize(1);
            dsp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            dsp.setCursor(8, 200);
            dsp.print(loLine);
        }

        // 底部站点名
        drawCenteredH(SITE_NAME, 222, 1, TFT_DARKGREY);
    } else if (needPartialUpdate) {
        // 仅刷新秒数和倒计时
        char secStr[8];
        snprintf(secStr, sizeof(secStr), ":%02d", sec);
        // 清掉旧的秒区域
        dsp.fillRect(0, 110, s_W, 20, TFT_BLACK);
        drawCenteredH(secStr, 110, 2, TFT_DARKGREY);

        if (hiSec > 0) {
            char hiLine[32];
            char hiTime[8];
            struct tm hitm;
            localtime_r(&nextHi, &hitm);
            strftime(hiTime, sizeof(hiTime), "%H:%M", &hitm);
            snprintf(hiLine, sizeof(hiLine), "Hi %s  %ldh%ldm",
                     hiTime, hiSec / 3600, (hiSec % 3600) / 60);
            dsp.fillRect(0, 178, s_W, 16, TFT_BLACK);
            dsp.setTextSize(1);
            dsp.setTextColor(TFT_WHITE, TFT_BLACK);
            dsp.setCursor(8, 180);
            dsp.print(hiLine);
        }

        if (loSec > 0) {
            char loLine[32];
            char loTime[8];
            struct tm lotm;
            localtime_r(&nextLo, &lotm);
            strftime(loTime, sizeof(loTime), "%H:%M", &lotm);
            snprintf(loLine, sizeof(loLine), "Lo %s  %ldh%ldm",
                     loTime, loSec / 3600, (loSec % 3600) / 60);
            dsp.fillRect(0, 198, s_W, 16, TFT_BLACK);
            dsp.setTextSize(1);
            dsp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            dsp.setCursor(8, 200);
            dsp.print(loLine);
        }
    }

    s_forceRedraw = false;
}

// ============================================================
// 屏 2：当日潮汐时刻表
// ============================================================

static void renderScreen1_Tides() {
    if (!isTimeValid()) return;

    dsp.fillScreen(TFT_BLACK);

    // 标题
    drawCenteredH("Tides Today", 4, 1, TFT_YELLOW);
    char siteLine[32];
    snprintf(siteLine, sizeof(siteLine), "%s", SITE_NAME);
    drawCenteredH(siteLine, 16, 1, TFT_DARKGREY);

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

    // 从 y=35 开始绘制，每个事件 50 像素高
    int y = 35;
    for (int i = 0; i < count && y < s_H - 15; i++) {
        char timeBuf[8];
        struct tm evtTm;
        localtime_r(&events[i].at, &evtTm);
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &evtTm);

        // 标志：▲ 高潮 / ▼ 低潮
        dsp.setTextSize(2);
        if (events[i].isHigh) {
            dsp.setTextColor(TFT_GREEN, TFT_BLACK);
            dsp.setCursor(8, y);
            dsp.print("Hi");
        } else {
            dsp.setTextColor(TFT_ORANGE, TFT_BLACK);
            dsp.setCursor(8, y);
            dsp.print("Lo");
        }

        // 时间（大字）
        dsp.setTextColor(TFT_WHITE, TFT_BLACK);
        dsp.setCursor(35, y);
        dsp.print(timeBuf);

        // 水位
        char levelStr[16];
        snprintf(levelStr, sizeof(levelStr), "%.2fm", events[i].level);
        dsp.setTextSize(1);
        dsp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        dsp.setCursor(95, y + 5);
        dsp.print(levelStr);

        y += 28;
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

    struct tm tinfo;
    if (!getLocalTime(&tinfo)) return;
    int day = tinfo.tm_mday;

    // 一天只计算一次黄历和打分
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

    // ========= 顶部：打分卡（最显眼） =========
    uint16_t scoreColor;
    switch (s_score.level) {
        case ScoreLevel::EXCELLENT: scoreColor = TFT_GREEN;      break;
        case ScoreLevel::GOOD:      scoreColor = TFT_GREEN;      break;
        case ScoreLevel::OK:        scoreColor = TFT_CYAN;       break;
        case ScoreLevel::CAUTION:   scoreColor = TFT_YELLOW;     break;
        case ScoreLevel::BAD:       scoreColor = TFT_RED;        break;
        case ScoreLevel::VETOED:    scoreColor = TFT_RED;        break;
        default:                    scoreColor = TFT_WHITE;
    }

    // 大字符号 + 分数
    char scoreHeadline[16];
    if (s_score.vetoed) {
        snprintf(scoreHeadline, sizeof(scoreHeadline), "X %d", s_score.score);
    } else {
        snprintf(scoreHeadline, sizeof(scoreHeadline), "%s %+d",
                 s_score.symbol, s_score.score);
    }
    drawCenteredH(scoreHeadline, 2, 3, scoreColor);

    // 描述
    drawCenteredH(s_score.description, 32, 1, TFT_WHITE);

    // 否决原因（如果有）
    if (s_score.vetoed) {
        drawCenteredH(s_score.vetoReason, 46, 1, TFT_ORANGE);
    }

    // 分隔线
    dsp.drawLine(4, 60, s_W - 4, 60, TFT_DARKGREY);

    // ========= 中部：黄历基础信息 =========

    // 农历日期
    char ldate[20];
    snprintf(ldate, sizeof(ldate), "Lunar %d-%s%d-%d",
             s_lunar.lunarYear,
             s_lunar.isLeap ? "*" : "",
             s_lunar.lunarMonth, s_lunar.lunarDay);
    dsp.setTextSize(1);
    dsp.setTextColor(TFT_YELLOW, TFT_BLACK);
    dsp.setCursor(4, 64);
    dsp.print(ldate);

    // 干支
    char gzLine[32];
    snprintf(gzLine, sizeof(gzLine), "%s %s %s",
             s_lunar.yearGanZhi, s_lunar.monthGanZhi, s_lunar.dayGanZhi);
    dsp.setTextColor(TFT_WHITE, TFT_BLACK);
    dsp.setCursor(4, 76);
    dsp.print(gzLine);

    // 生肖 + 建除
    char infoLine[40];
    snprintf(infoLine, sizeof(infoLine), "%s  | JianChu: %s",
             s_lunar.shengXiao, s_lunar.jianChu);
    dsp.setTextColor(TFT_CYAN, TFT_BLACK);
    dsp.setCursor(4, 88);
    dsp.print(infoLine);

    // 分隔线
    dsp.drawLine(4, 102, s_W - 4, 102, TFT_DARKGREY);

    // ========= 下部：宜/忌（横向排列） =========

    // 宜（标题 + 5 条横排）
    dsp.setTextColor(TFT_GREEN, TFT_BLACK);
    dsp.setCursor(4, 106);
    dsp.print("Yi:");
    dsp.setTextColor(TFT_WHITE, TFT_BLACK);
    int x = 28;
    for (int i = 0; i < MAX_YI_ITEMS; i++) {
        dsp.setCursor(x, 106);
        dsp.print(s_lunar.yi[i]);
        x += dsp.textWidth(s_lunar.yi[i]) + 4;
        // 换行保护
        if (x > s_W - 20 && i < MAX_YI_ITEMS - 1) {
            x = 4;
            dsp.setCursor(x, 118);
            dsp.print("   ");
            x = 28;
        }
    }

    // 分隔线
    dsp.drawLine(4, 130, s_W - 4, 130, TFT_DARKGREY);

    // 忌
    dsp.setTextColor(TFT_RED, TFT_BLACK);
    dsp.setCursor(4, 134);
    dsp.print("Ji:");
    dsp.setTextColor(TFT_WHITE, TFT_BLACK);
    x = 28;
    for (int i = 0; i < MAX_JI_ITEMS; i++) {
        dsp.setCursor(x, 134);
        dsp.print(s_lunar.ji[i]);
        x += dsp.textWidth(s_lunar.ji[i]) + 4;
        if (x > s_W - 20 && i < MAX_JI_ITEMS - 1) {
            x = 4;
            dsp.setCursor(x, 146);
            dsp.print("   ");
            x = 28;
        }
    }

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
}
