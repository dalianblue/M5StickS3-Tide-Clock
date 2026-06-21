// ============================================================
// M5StickS3 潮汐 + 黄历固件
// 阶段 4：竖屏 3 屏切换（时钟 / 潮汐 / 黄历）
// ============================================================

#include <M5Unified.h>
#include "config.h"
#include "time_sync.h"
#include "tide_predict.h"
#include "lunar_calc.h"
#include "paddle_score.h"
#include "ui_render.h"

// ---------- 状态 ----------
static int  s_currentScreen = SCREEN_CLOCK;
static uint32_t s_lastBtnPress = 0;        // 上次按键时间（用于自动返回屏 1）
static uint32_t s_btnAPressStart = 0;
static bool     s_btnALongFired = false;
static uint32_t s_lastUITick = 0;

// ---------- 亮度档位（BtnB 长按循环） ----------
static const uint8_t BRIGHTNESS[] = {32, 128, 255};
static const int BRIGHTNESS_COUNT = sizeof(BRIGHTNESS) / sizeof(BRIGHTNESS[0]);
static int s_brightnessIdx = 1;  // 默认 128

// ============================================================
// 按键处理
// ============================================================

static void cycleBrightness() {
    s_brightnessIdx = (s_brightnessIdx + 1) % BRIGHTNESS_COUNT;
    M5.Display.setBrightness(BRIGHTNESS[s_brightnessIdx]);
    Serial.printf("[UI] Brightness: %d\n", BRIGHTNESS[s_brightnessIdx]);
}

static void handleButtons() {
    M5.update();
    s_lastBtnPress = millis();

    // ---- BtnA ----
    if (M5.BtnA.isPressed()) {
        if (s_btnAPressStart == 0) {
            s_btnAPressStart = millis();
            s_btnALongFired = false;
        } else if (!s_btnALongFired && (millis() - s_btnAPressStart) > 1500) {
            s_btnALongFired = true;
            // 长按：触发 NTP 重同步或 AP 配置
            M5.Display.fillScreen(TFT_BLACK);
            if (!isWifiConnected()) {
                startConfigPortal();
            } else {
                forceNTPResync();
            }
            forceRedraw();
            renderScreen(s_currentScreen);
        }
    }

    if (M5.BtnA.wasReleased()) {
        if (!s_btnALongFired && isTimeValid()) {
            // 短按：串口打印调试
            Serial.println("\n>>> BtnA clicked: trigger debug <<<");
            printTideDebug(getNow());
            struct tm tinfo;
            if (getLocalTime(&tinfo)) {
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
        }
        s_btnAPressStart = 0;
    }

    // ---- BtnB ----
    static uint32_t btnBPressStart = 0;
    static bool btnBLongFired = false;

    if (M5.BtnB.isPressed()) {
        if (btnBPressStart == 0) {
            btnBPressStart = millis();
            btnBLongFired = false;
        } else if (!btnBLongFired && (millis() - btnBPressStart) > 1500) {
            btnBLongFired = true;
            cycleBrightness();
        }
    }

    if (M5.BtnB.wasReleased()) {
        if (!btnBLongFired) {
            // 短按：切换屏幕
            s_currentScreen = cycleScreen(s_currentScreen);
            Serial.printf("[UI] Screen: %d\n", s_currentScreen);
            renderScreen(s_currentScreen);
        }
        btnBPressStart = 0;
    }
}

// ============================================================
// Arduino 入口
// ============================================================

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n\n========== M5StickS3 Tide Clock ==========");
    Serial.println("[Boot] Serial ready @ 115200");
    Serial.flush();

    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    // UI 初始化（竖屏 + 黑底）
    initUI();

    // 显示启动屏
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(15, 60);
    M5.Display.print("Tide Clock");
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setCursor(25, 110);
    M5.Display.print("Booting...");

    // 初始化时间同步（首次启动最长等 5 分钟 AP 配置）
    initTimeSync();

    // 时间就绪：打印调试 + 渲染首屏
    if (isTimeValid()) {
        printTideDebug(getNow());
        struct tm tinfo;
        if (getLocalTime(&tinfo)) {
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
    }

    s_currentScreen = SCREEN_CLOCK;
    renderScreen(s_currentScreen);
}

void loop() {
    handleButtons();
    updateTimeSync();

    // 自动返回屏 1（5 秒无操作）
    if (s_currentScreen != SCREEN_CLOCK &&
        (millis() - s_lastBtnPress) > SCREEN_AUTO_RETURN_MS) {
        s_currentScreen = SCREEN_CLOCK;
        renderScreen(s_currentScreen);
    }

    // 每 100ms 刷新屏 1 时钟（屏 2/3 不需要）
    if (s_currentScreen == SCREEN_CLOCK &&
        (millis() - s_lastUITick) > 100) {
        s_lastUITick = millis();
        uiLoopTick();
    }

    delay(10);
}
