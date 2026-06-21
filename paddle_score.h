#pragma once

#include <Arduino.h>
#include "lunar_calc.h"

// ============================================================
// 皮划艇 / 水上运动适宜度打分
// 基于 README 中的"黄历打分系统"
// ============================================================

enum class ScoreLevel {
    EXCELLENT,  // ≥ 6
    GOOD,       // 3 ~ 5
    OK,         // 0 ~ 2
    CAUTION,    // -3 ~ -1
    BAD,        // ≤ -4
    VETOED,     // 特殊凶日一票否决
};

struct PaddleScore {
    int score;
    ScoreLevel level;

    // 显示用的符号（ASCII，M5GFX 默认字体不支持 emoji）
    // EXCELLENT: "*", GOOD: "v", OK: "o", CAUTION: "!", BAD: "X", VETOED: "X"
    const char* symbol;

    // 简短描述（英文，节省屏幕空间）
    const char* description;

    // 否决信息（vetoed=true 时有效）
    bool vetoed;
    const char* vetoReason;
};

// 计算当日皮划艇适宜度打分
// lunar: 来自 calcLunar() 的黄历信息
// sy/sm/sd: 公历日期（用于四离/四绝判断）
PaddleScore calculatePaddleScore(const LunarInfo& lunar,
                                  int sy, int sm, int sd);

// 等级转中文名（用于调试和将来 UI）
const char* levelName(ScoreLevel level);

// 调试：串口打印打分详情
void printPaddleScoreDebug(const LunarInfo& lunar, int sy, int sm, int sd);
