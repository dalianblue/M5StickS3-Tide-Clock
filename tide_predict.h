#pragma once

#include <Arduino.h>
#include <time.h>

// ============================================================
// 潮汐预测模块（6 分潮简化调和分析）
// ============================================================

// 潮汐事件
typedef struct {
    time_t at;        // 时刻 (epoch 秒)
    bool   isHigh;    // true=高潮, false=低潮
    float  level;     // 水位 (米)
} TideEvent;

// 任意时刻水位 (米)
// 公式: h(t) = baseline(t) + z0 + Σᵢ ampᵢ × cos(omegaᵢ·t - phaseᵢ)
float getWaterLevel(time_t t);

// 当前处于涨潮还是落潮
// true=涨潮(water rising), false=落潮(water falling)
bool isFloodTide(time_t t);

// 查找指定日的极值
// dayStartLocal: 当天本地 0 点的 epoch（调用者用 localtime 计算）
// out: 输出数组（至少 4 个元素）
// count: 实际找到的极值数量（通常 4 个：2 高潮 + 2 低潮）
void findDailyExtrema(time_t dayStartLocal, TideEvent* out, int& count);

// 距当前最近的下次高潮 / 低潮时刻
// 搜索范围：从 t 起 48 小时
time_t getNextHighTide(time_t t);
time_t getNextLowTide(time_t t);

// 调试：串口打印当日潮汐信息
void printTideDebug(time_t t);
