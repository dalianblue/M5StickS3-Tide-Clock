#pragma once

#include <Arduino.h>
#include <time.h>

// ============================================================
// 时间同步模块：SmartConfig Wi-Fi 配置 + NTP 同步 + RTC 管理
// ============================================================

// 时间同步状态
enum class SyncState {
    BOOTING,            // 启动中
    AP_PORTAL_WAIT,     // 等待手机连 AP 热点配置
    WIFI_CONNECTING,    // 已有凭证，连接中
    WIFI_FAILED,        // Wi-Fi 连接失败
    NTP_SYNCING,        // Wi-Fi 已连，NTP 同步中
    SYNCED,             // NTP 同步成功
    RTC_ONLY            // NTP 失败，仅用 RTC
};

// 初始化时间同步（启动时调用一次）。
// 内部流程：WiFiManager 检查已保存凭证 → 有凭证则直接连，无凭证则启动 AP 热点 → NTP 同步。
// 阻塞调用，最长 CONFIG_PORTAL_TIMEOUT_MS。
void initTimeSync();

// 在 loop 中周期调用，处理断线重连和定期 NTP 重同步。
void updateTimeSync();

// 状态查询
SyncState getSyncState();
bool isWifiConnected();
bool isTimeValid();      // SYNCED 或 RTC_ONLY 都算有效
time_t getNow();         // 当前 epoch 秒

// 上次成功 NTP 同步的时间戳（0 表示从未同步）
time_t getLastNTPSyncTime();

// 手动操作
void startConfigPortal(); // 清除已保存凭证并重新启动 AP 配置模式
void forceNTPResync();    // 强制重新触发一次 NTP 同步

// 状态转中文（用于 UI 显示）
const char* syncStateString(SyncState s);

// ============================================================
// 用户偏好（NVS 持久化）
// ============================================================

// 用户生肖（0-11，-1=关闭）。从 NVS 读取；未设置时返回 USER_ZODIAC_DEFAULT
int  getUserZodiac();

// 保存用户生肖到 NVS
void setUserZodiac(int zodiac);

// 生肖索引转中文名（"鼠"/"牛"/...）
const char* zodiacName(int idx);
