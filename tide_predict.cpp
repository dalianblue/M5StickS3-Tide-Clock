#include "tide_predict.h"
#include "data_tables.h"
#include "config.h"
#include <math.h>

// ============================================================
// 水位预测（调和叠加 + 基线）
// ============================================================

float getWaterLevel(time_t t) {
    // 相对参考时间的小时数
    double h = difftime(t, TIDE_REFERENCE_TIME) / 3600.0;

    // 基线：使用多项式常数项（约 1.9 米，桐州岛径流基线）
    // 多项式只在 h ∈ [0, 984] 校准期内有效，超出会漂移，故只取常数项
    // 这会让绝对水位有 ±几厘米误差，但极值时刻精度不受影响
    double baseline = TIDE_BASELINE_COEFFS[3];

    // 调和叠加：6 个主要分潮
    double tidal = (double)TIDE_Z0;
    for (int i = 0; i < NUM_CONSTITUENTS; i++) {
        tidal += (double)TIDE_CONSTITUENTS[i].amplitude *
                 cos((double)TIDE_CONSTITUENTS[i].omega * h -
                     (double)TIDE_CONSTITUENTS[i].phase);
    }

    return (float)(baseline + tidal);
}

// ============================================================
// 涨潮/落潮判断
// ============================================================

bool isFloodTide(time_t t) {
    float l_now = getWaterLevel(t);
    float l_future = getWaterLevel(t + 300);  // 5 分钟后
    return l_future > l_now;
}

// ============================================================
// 抛物线插值找极值（亚步长精度）
// ============================================================

// 给定三个等间隔采样点 (y_{i-1}, y_i, y_{i+1})，拟合抛物线
// 返回极值点的偏移量 (offset ∈ [-0.5, +0.5] 个步长) 和极值
static float parabolicPeak(float y0, float y1, float y2, float &offset) {
    float a = (y0 + y2) * 0.5f - y1;
    float b = (y2 - y0) * 0.5f;
    if (fabsf(a) < 1e-9f) {
        offset = 0.0f;
        return y1;
    }
    offset = -b / (2.0f * a);
    if (offset > 0.5f)  offset = 0.5f;
    if (offset < -0.5f) offset = -0.5f;
    return y1 - b * b / (4.0f * a);
}

// ============================================================
// 查找当日极值
// ============================================================

void findDailyExtrema(time_t dayStartLocal, TideEvent* out, int& count) {
    const int STEP_SEC = 300;   // 5 分钟步长
    const int N = 288 + 4;      // 24h/5min = 288 点，前后各加 2 个边界点

    // 采样全天（从 dayStart - 10 分钟 到 dayStart + 24h + 10 分钟）
    float levels[N];
    for (int i = 0; i < N; i++) {
        levels[i] = getWaterLevel(dayStartLocal + (time_t)(i - 2) * STEP_SEC);
    }

    // 检测局部极值
    count = 0;
    for (int i = 2; i < N - 2; i++) {
        bool isHigh = levels[i] > levels[i-1] && levels[i] > levels[i+1];
        bool isLow  = levels[i] < levels[i-1] && levels[i] < levels[i+1];

        if (!isHigh && !isLow) continue;

        // 抛物线插值精确定位
        float offset;
        float peakVal = parabolicPeak(levels[i-1], levels[i], levels[i+1], offset);

        // 精确时间戳
        time_t t = dayStartLocal +
                   (time_t)(i - 2) * STEP_SEC +
                   (time_t)llroundf(offset * STEP_SEC);

        // 限制在当天 [00:00, 24:00) 内
        if (t < dayStartLocal || t >= dayStartLocal + 86400) continue;

        out[count].at    = t;
        out[count].isHigh = isHigh;
        out[count].level = peakVal;
        count++;

        if (count >= 4) break;  // 最多 4 个极值
    }
}

// ============================================================
// 下次高潮 / 低潮（48 小时内搜索）
// ============================================================

// 计算本地当天 0 点的 epoch
static time_t localDayStart(time_t t) {
    struct tm tinfo;
    localtime_r(&t, &tinfo);
    tinfo.tm_hour = 0;
    tinfo.tm_min  = 0;
    tinfo.tm_sec  = 0;
    return mktime(&tinfo);
}

time_t getNextHighTide(time_t t) {
    time_t dayStart = localDayStart(t);
    TideEvent events[4];
    // 搜 3 天足够覆盖下次高潮（半日潮周期 ~12.42h）
    for (int day = 0; day < 3; day++) {
        int count = 0;
        findDailyExtrema(dayStart + (time_t)day * 86400, events, count);
        for (int i = 0; i < count; i++) {
            if (events[i].isHigh && events[i].at > t) {
                return events[i].at;
            }
        }
    }
    return 0;
}

time_t getNextLowTide(time_t t) {
    time_t dayStart = localDayStart(t);
    TideEvent events[4];
    for (int day = 0; day < 3; day++) {
        int count = 0;
        findDailyExtrema(dayStart + (time_t)day * 86400, events, count);
        for (int i = 0; i < count; i++) {
            if (!events[i].isHigh && events[i].at > t) {
                return events[i].at;
            }
        }
    }
    return 0;
}

// ============================================================
// 调试：串口打印当日潮汐信息
// ============================================================

void printTideDebug(time_t t) {
    Serial.println("\n========== TIDE DEBUG ==========");

    // 当前水位
    float level = getWaterLevel(t);
    Serial.printf("[Now]   Water level: %.3f m  (%s)\n",
                  level, isFloodTide(t) ? "Flood/rising" : "Ebb/falling");

    // 下次高潮
    time_t nextHigh = getNextHighTide(t);
    if (nextHigh > 0) {
        struct tm tinfo;
        localtime_r(&nextHigh, &tinfo);
        char buf[32];
        strftime(buf, sizeof(buf), "%H:%M", &tinfo);
        long diffSec = (long)difftime(nextHigh, t);
        Serial.printf("[Next]  High tide: %s  (in %ldh %ldm)\n",
                      buf, diffSec / 3600, (diffSec % 3600) / 60);
    }

    // 下次低潮
    time_t nextLow = getNextLowTide(t);
    if (nextLow > 0) {
        struct tm tinfo;
        localtime_r(&nextLow, &tinfo);
        char buf[32];
        strftime(buf, sizeof(buf), "%H:%M", &tinfo);
        long diffSec = (long)difftime(nextLow, t);
        Serial.printf("[Next]  Low tide:  %s  (in %ldh %ldm)\n",
                      buf, diffSec / 3600, (diffSec % 3600) / 60);
    }

    // 当日 4 个极值
    time_t dayStart = localDayStart(t);
    TideEvent events[4];
    int count = 0;
    findDailyExtrema(dayStart, events, count);

    struct tm dayinfo;
    localtime_r(&dayStart, &dayinfo);
    char dayBuf[32];
    strftime(dayBuf, sizeof(dayBuf), "%Y-%m-%d", &dayinfo);
    Serial.printf("[Today %s]  %d extrema:\n", dayBuf, count);

    for (int i = 0; i < count; i++) {
        struct tm tinfo;
        localtime_r(&events[i].at, &tinfo);
        char buf[32];
        strftime(buf, sizeof(buf), "%H:%M", &tinfo);
        Serial.printf("  %s  %s  %.3f m\n",
                      events[i].isHigh ? "HIGH" : "LOW ",
                      buf, events[i].level);
    }

    Serial.println("================================\n");
}
