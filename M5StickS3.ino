// ============================================================
// M5StickS3 潮汐 + 黄历固件
// 阶段 4：竖屏 3 屏切换（时钟 / 潮汐 / 黄历）
// ============================================================

#include <M5Unified.h>
#include "config.h"
#include "time_sync.h"
#include "tide_predict.h"
#include "ui_render.h"

// ---------- 状态 ----------
static int  s_currentScreen = SCREEN_CLOCK;
static uint32_t s_lastBtnPress = 0;
static uint32_t s_btnAPressStart = 0;
static bool     s_btnALongFired = false;
static uint32_t s_lastUITick = 0;

// 屏幕超时状态（省电）
static bool s_screenDimmed = false;
static bool s_screenOff = false;

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

    // 屏幕关闭时，按键仅唤醒（不切屏）
    if (s_screenOff) {
        M5.Display.setBrightness(BRIGHTNESS[s_brightnessIdx]);
        s_screenOff = false;
        s_screenDimmed = false;
        s_lastBtnPress = millis();
        forceRedraw();
        renderScreen(s_currentScreen);
        return;
    }

    // 屏幕降亮时，恢复亮度
    if (s_screenDimmed) {
        M5.Display.setBrightness(BRIGHTNESS[s_brightnessIdx]);
        s_screenDimmed = false;
    }

    s_lastBtnPress = millis();

    // ---- BtnA ----（切屏 + 长按 NTP 重同步）
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
        if (!s_btnALongFired) {
            // 短按：切屏
            s_currentScreen = cycleScreen(s_currentScreen);
            Serial.printf("[UI] Screen: %d\n", s_currentScreen);
            renderScreen(s_currentScreen);

            // 屏 3 时同时打印调试
            if (s_currentScreen == SCREEN_LUNAR && isTimeValid()) {
                printAllDebug();
            }
        }
        s_btnAPressStart = 0;
    }

    // ---- BtnB ----（仅长按调亮度）
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
        printAllDebug();
    }

    s_currentScreen = SCREEN_CLOCK;
    renderScreen(s_currentScreen);

    // NTP 同步完成后降低 CPU 频率省电
    setCpuFrequencyMhz(POWER_CPU_FREQ_MHZ);
    Serial.printf("[Power] CPU 降至 %d MHz\n", POWER_CPU_FREQ_MHZ);
}

void loop() {
    uint32_t idle = millis() - s_lastBtnPress;

    // 屏幕超时管理
    if (idle > POWER_SCREEN_OFF_MS && !s_screenOff) {
        M5.Display.setBrightness(0);
        s_screenOff = true;
        Serial.println("[Power] 屏幕关闭");
    } else if (idle > POWER_SCREEN_DIM_MS && !s_screenDimmed && !s_screenOff) {
        M5.Display.setBrightness(POWER_SCREEN_DIM_LEVEL);
        s_screenDimmed = true;
    }

    handleButtons();
    updateTimeSync();

    // 息屏时跳过 UI 刷新
    if (!s_screenOff) {
        // 自动返回屏 1
        if (s_currentScreen != SCREEN_CLOCK &&
            (millis() - s_lastBtnPress) > SCREEN_AUTO_RETURN_MS) {
            s_currentScreen = SCREEN_CLOCK;
            renderScreen(s_currentScreen);
        }

        // 刷新屏 1（降亮时也刷新，息屏时不刷新）
        if (s_currentScreen == SCREEN_CLOCK &&
            (millis() - s_lastUITick) > POWER_LOOP_DELAY_MS) {
            s_lastUITick = millis();
            uiLoopTick();
        }
    }

    delay(POWER_LOOP_DELAY_MS);
}
