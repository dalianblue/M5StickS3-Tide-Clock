#include "time_sync.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>     // ESP32 Arduino Core 自带
#include <DNSServer.h>     // ESP32 Arduino Core 自带
#include <Preferences.h>   // ESP32 Arduino Core 自带
#include "time.h"
#include <M5Unified.h>

// ============================================================
// 内部状态
// ============================================================

static SyncState s_state = SyncState::BOOTING;
static time_t s_lastNTPSync = 0;
static uint32_t s_lastResyncAttempt = 0;
static uint32_t s_lastWifiCheck = 0;
static bool s_resyncRequested = false;

// AP 配置阶段的状态
static DNSServer s_dnsServer;
static WebServer s_webServer(80);
static volatile bool s_configSubmitted = false;
static String s_submittedSSID = "";
static String s_submittedPass = "";

// ============================================================
// NVS 凭证读写
// ============================================================

static bool loadCredentials(String &ssid, String &pass) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false;
    ssid = prefs.getString("ssid", "");
    pass = prefs.getString("pass", "");
    prefs.end();
    return ssid.length() > 0;
}

static void saveCredentials(const String &ssid, const String &pass) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
}

static void clearCredentials() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.end();
}

// ---------- 用户生肖持久化 ----------

static const char* ZODIAC_NAMES[12] = {
    "鼠", "牛", "虎", "兔", "龙", "蛇",
    "马", "羊", "猴", "鸡", "狗", "猪"
};

const char* zodiacName(int idx) {
    if (idx < 0 || idx > 11) return "?";
    return ZODIAC_NAMES[idx];
}

int getUserZodiac() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return USER_ZODIAC_DEFAULT;
    int z = prefs.getInt("zodiac", USER_ZODIAC_DEFAULT);
    prefs.end();
    return z;
}

void setUserZodiac(int zodiac) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.putInt("zodiac", zodiac);
    prefs.end();
}

// ============================================================
// UI 辅助：在阻塞调用期间直接刷新屏幕
// ============================================================

static void showAPPortalScreen() {
    auto &d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.setTextSize(2);
    d.setCursor(8, 6);
    d.print("Setup WiFi");

    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextSize(1);
    d.setCursor(3, 36);
    d.print("1. Phone WiFi ->");
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.setCursor(10, 50);
    d.print(AP_SSID);

    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setCursor(3, 68);
    d.print("2. Browser visit:");
    d.setTextColor(TFT_GREEN, TFT_BLACK);
    d.setCursor(10, 82);
    d.print("192.168.4.1");

    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setCursor(3, 100);
    d.print("3. Enter WiFi pass");
    d.setCursor(3, 112);
    d.print("   tap Save");
}

static void showConnectingScreen(const char *ssid) {
    auto &d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextSize(1);
    d.setCursor(10, 30);
    d.print("Connecting:");
    d.setTextColor(TFT_CYAN, TFT_BLACK);
    d.setCursor(10, 48);
    d.setTextSize(2);
    d.print(ssid);
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.setTextSize(1);
    d.setCursor(10, 90);
    d.print("Please wait...");
}

static void showNTPScreen() {
    auto &d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_YELLOW, TFT_BLACK);
    d.setTextSize(2);
    d.setCursor(15, 50);
    d.print("NTP Sync");
    d.setTextColor(TFT_DARKGREY, TFT_BLACK);
    d.setTextSize(1);
    d.setCursor(25, 80);
    d.print("Getting time...");
}

static void showFailedScreen(const char *msg) {
    auto &d = M5.Display;
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.setTextSize(2);
    d.setCursor(15, 40);
    d.print("FAILED");
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextSize(1);
    d.setCursor(15, 75);
    d.print(msg);
    d.setCursor(15, 95);
    d.print("Long press BtnA");
    d.setCursor(15, 108);
    d.print("to retry setup");
}

// ============================================================
// Captive Portal：HTML 配置页面
// ============================================================

static const char CONFIG_PAGE_TEMPLATE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>M5StickS3 Setup</title>
<style>
body{font-family:system-ui,sans-serif;padding:24px;max-width:420px;margin:auto;background:#0c1424;color:#fff}
h2{color:#00d4ff;margin:0 0 20px}
label{display:block;margin:14px 0 4px;font-size:14px;color:#9fb3c8}
input,select{width:100%;padding:12px;font-size:16px;box-sizing:border-box;border:1px solid #2a3f5f;border-radius:8px;background:#1a2538;color:#fff}
button{width:100%;padding:14px;margin-top:20px;background:#00d4ff;color:#000;border:none;border-radius:8px;font-size:16px;font-weight:bold}
.note{font-size:12px;color:#9fb3c8;margin-top:16px;text-align:center}
</style>
</head>
<body>
<h2>M5StickS3 Setup</h2>
<form action="/save" method="post">
<label>WiFi Name (SSID)</label>
<input type="text" name="ssid" placeholder="Your WiFi name" required autocomplete="off">
<label>WiFi Password</label>
<input type="password" name="pass" placeholder="Password" autocomplete="off">
<label>Your Zodiac (for paddle scoring)</label>
<select name="zodiac">
__ZODIAC_OPTIONS__
</select>
<button type="submit">Save & Connect</button>
</form>
<div class="note">Credentials + zodiac saved on device. WiFi setup takes ~10s after save.</div>
</body>
</html>
)HTML";

// 动态生成配置页面：把生肖下拉的当前选项回显
static String buildConfigPage() {
    String page = FPSTR(CONFIG_PAGE_TEMPLATE);
    String options;
    int currentZodiac = getUserZodiac();
    if (currentZodiac < 0 || currentZodiac > 11) currentZodiac = USER_ZODIAC_DEFAULT;
    for (int i = 0; i < 12; i++) {
        char buf[80];
        snprintf(buf, sizeof(buf),
                 "<option value='%d'%s>%s</option>",
                 i, i == currentZodiac ? " selected" : "",
                 zodiacName(i));
        options += buf;
    }
    page.replace("__ZODIAC_OPTIONS__", options);
    return page;
}

static void handleRoot() {
    s_webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    s_webServer.sendHeader("Pragma", "no-cache");
    s_webServer.sendHeader("Expires", "-1");
    String page = buildConfigPage();
    s_webServer.send(200, "text/html", page);
}

static void handleSave() {
    if (!s_webServer.hasArg("ssid")) {
        s_webServer.send(400, "text/plain", "Missing SSID");
        return;
    }
    s_submittedSSID = s_webServer.arg("ssid");
    s_submittedPass = s_webServer.hasArg("pass") ? s_webServer.arg("pass") : "";

    // 解析生肖（如果用户选了）
    if (s_webServer.hasArg("zodiac")) {
        int z = s_webServer.arg("zodiac").toInt();
        if (z >= 0 && z <= 11) {
            setUserZodiac(z);
            Serial.printf("[Config] 生肖已保存: %d (%s)\n", z, zodiacName(z));
        }
    }

    s_webServer.send(200, "text/html",
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>body{font-family:system-ui;padding:30px;text-align:center;color:#0c1424}"
        "h2{color:#00a86b}</style></head><body>"
        "<h2>Saved!</h2><p>Connecting to WiFi...</p>"
        "<p>Device will restart clock automatically.</p>"
        "</body></html>");

    s_configSubmitted = true;
}

// Captive Portal：把所有未知 URL 重定向到首页
static void handleRedirect() {
    s_webServer.sendHeader("Location", "http://192.168.4.1/", true);
    s_webServer.send(302, "text/plain", "");
}

// ============================================================
// AP 模式配置（阻塞）：启动 AP + DNS + Web 等用户提交
// ============================================================

static bool runConfigPortal(uint32_t timeoutMs) {
    s_state = SyncState::AP_PORTAL_WAIT;
    s_configSubmitted = false;
    showAPPortalScreen();

    Serial.println("[WiFi] 启动 AP 配置模式");
    WiFi.mode(WIFI_AP);
    bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD);
    if (!apOk) {
        Serial.println("[WiFi] softAP 失败");
        return false;
    }
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[WiFi] AP IP: %s\n", apIP.toString().c_str());

    // DNS 劫持：所有域名都解析到 AP IP（Captive Portal 自动弹窗的关键）
    s_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    s_dnsServer.start(53, "*", apIP);

    // 路由
    s_webServer.on("/", HTTP_GET, handleRoot);
    s_webServer.on("/save", HTTP_POST, handleSave);
    // Captive Portal 探测路径（iOS/Android/macOS/Win）
    s_webServer.on("/generate_204", handleRedirect);          // Android
    s_webServer.on("/gen_204", handleRedirect);                // Chrome
    s_webServer.on("/hotspot-detect.html", handleRedirect);    // iOS/macOS
    s_webServer.on("/library/test/success.html", handleRedirect);  // iOS旧版
    s_webServer.on("/connecttest.txt", handleRedirect);        // Windows
    s_webServer.on("/ncsi.txt", handleRedirect);               // Windows
    s_webServer.on("/fwlink", handleRedirect);                 // Windows
    s_webServer.onNotFound(handleRedirect);
    s_webServer.begin();

    // 等待用户提交或超时
    uint32_t start = millis();
    while (!s_configSubmitted && (millis() - start < timeoutMs)) {
        s_dnsServer.processNextRequest();
        s_webServer.handleClient();
        delay(10);
    }

    s_webServer.stop();
    s_dnsServer.stop();
    WiFi.softAPdisconnect(true);

    if (!s_configSubmitted) {
        Serial.println("[WiFi] 配置超时");
        return false;
    }

    // 保存凭证
    saveCredentials(s_submittedSSID, s_submittedPass);
    Serial.printf("[WiFi] 凭证已保存: %s\n", s_submittedSSID.c_str());

    // 切换到 STA 模式连接
    showConnectingScreen(s_submittedSSID.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(s_submittedSSID.c_str(), s_submittedPass.c_str());

    uint32_t connStart = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - connStart > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("[WiFi] 连接新凭证超时");
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.printf("[WiFi] 已连接: %s, IP: %s\n",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
}

// ============================================================
// 用已保存凭证连接 Wi-Fi（阻塞）
// ============================================================

static bool connectWithSavedCredentials(uint32_t timeoutMs) {
    String ssid, pass;
    if (!loadCredentials(ssid, pass)) return false;

    Serial.printf("[WiFi] 用已保存凭证连接: %s\n", ssid.c_str());
    showConnectingScreen(ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) {
            Serial.println("[WiFi] 连接超时");
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.printf("[WiFi] 已连接: %s, IP: %s\n",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
}

// ============================================================
// NTP 同步
// ============================================================

static bool syncNTP(uint32_t timeoutMs = 10000) {
    s_state = SyncState::NTP_SYNCING;
    showNTPScreen();
    Serial.println("[TimeSync] NTP 同步中...");

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
               NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

    struct tm timeinfo = {0};
    uint32_t start = millis();
    while (!getLocalTime(&timeinfo, 5000)) {
        if (millis() - start > timeoutMs) {
            Serial.println("[TimeSync] NTP 同步失败");
            return false;
        }
    }

    s_lastNTPSync = mktime(&timeinfo);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("[TimeSync] NTP 同步成功: %s\n", buf);
    return true;
}

// ============================================================
// 公开接口
// ============================================================

void initTimeSync() {
    s_state = SyncState::BOOTING;
    Serial.println("\n[TimeSync] === 时间同步初始化 ===");
    Serial.flush();

    // 1. 优先尝试已保存凭证
    s_state = SyncState::WIFI_CONNECTING;
    if (connectWithSavedCredentials(WIFI_CONNECT_TIMEOUT_MS)) {
        if (syncNTP()) {
            s_state = SyncState::SYNCED;
#if POWER_WIFI_OFF_AFTER_SYNC
            Serial.println("[Power] NTP 同步完成，关闭 Wi-Fi 省电");
            WiFi.disconnect(true, true);
            WiFi.mode(WIFI_OFF);
#endif
            return;
        }
        s_state = SyncState::RTC_ONLY;
#if POWER_WIFI_OFF_AFTER_SYNC
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
#endif
        return;
    }

    // 2. 没凭证或连不上，启动 AP 配置
    bool ok = runConfigPortal(CONFIG_PORTAL_TIMEOUT_MS);
    if (!ok) {
        s_state = SyncState::WIFI_FAILED;
        showFailedScreen("WiFi setup failed");
        delay(2000);
        return;
    }

    // 3. AP 配置成功，NTP 同步
    if (syncNTP()) {
        s_state = SyncState::SYNCED;
    } else {
        s_state = SyncState::RTC_ONLY;
    }
#if POWER_WIFI_OFF_AFTER_SYNC
    Serial.println("[Power] 关闭 Wi-Fi 省电");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
#endif
}

void updateTimeSync() {
    uint32_t now = millis();

    bool needResync = s_resyncRequested ||
                      (now - s_lastResyncAttempt > NTP_RESYNC_INTERVAL_MS);
    if (!needResync) return;

    s_resyncRequested = false;
    s_lastResyncAttempt = now;

    // 重新打开 Wi-Fi 进行同步
    Serial.println("[Power] 重新打开 Wi-Fi 进行 NTP 同步");
    String ssid, pass;
    if (!loadCredentials(ssid, pass)) {
        Serial.println("[Power] 无保存凭证，跳过");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    // 等待连接
    uint32_t connStart = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - connStart < WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        if (syncNTP()) {
            s_state = SyncState::SYNCED;
            Serial.println("[Power] NTP 重同步成功");
        }
    } else {
        Serial.println("[Power] Wi-Fi 连接失败，继续用 RTC");
    }

#if POWER_WIFI_OFF_AFTER_SYNC
    // 同步完成后关闭 Wi-Fi
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[Power] Wi-Fi 已关闭");
#endif
}

SyncState getSyncState() { return s_state; }

bool isWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool isTimeValid() {
    return s_state == SyncState::SYNCED || s_state == SyncState::RTC_ONLY;
}

time_t getNow() {
    return time(nullptr);
}

time_t getLastNTPSyncTime() {
    return s_lastNTPSync;
}

void startConfigPortal() {
    clearCredentials();
    WiFi.disconnect(true, true);
    delay(100);

    bool ok = runConfigPortal(CONFIG_PORTAL_TIMEOUT_MS);
    if (ok) {
        if (syncNTP()) {
            s_state = SyncState::SYNCED;
        } else {
            s_state = SyncState::RTC_ONLY;
        }
    } else {
        s_state = SyncState::WIFI_FAILED;
        showFailedScreen("WiFi setup failed");
        delay(2000);
    }
}

void forceNTPResync() {
    s_resyncRequested = true;
    Serial.println("[TimeSync] 用户请求 NTP 重同步");
}

const char* syncStateString(SyncState s) {
    switch (s) {
        case SyncState::BOOTING:          return "Boot";
        case SyncState::AP_PORTAL_WAIT:   return "Setup";
        case SyncState::WIFI_CONNECTING:  return "Conn";
        case SyncState::WIFI_FAILED:      return "FAIL";
        case SyncState::NTP_SYNCING:      return "NTP";
        case SyncState::SYNCED:           return "NTP OK";
        case SyncState::RTC_ONLY:         return "RTC";
    }
    return "?";
}
