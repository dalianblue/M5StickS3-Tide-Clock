#pragma once

#include <Arduino.h>

// ============================================================
// 农历 / 黄历 模块
// 农历数据范围: 1900-2099
// ============================================================

// 黄历信息（一次计算的完整输出）
struct LunarInfo {
    // 农历
    int  lunarYear;     // 农历年（如 2026）
    int  lunarMonth;    // 农历月 1-12
    bool isLeap;        // 是否闰月
    int  lunarDay;      // 农历日 1-30

    // 干支（字符串指针，指向模块内静态缓冲区）
    const char* yearGanZhi;    // 年柱 "甲子"
    const char* monthGanZhi;   // 月柱 "丙寅"
    const char* dayGanZhi;     // 日柱 "戊午"
    const char* shengXiao;     // 生肖 "龙"
    const char* jianChu;       // 建除十二神 "建" / "除" / ...

    // 宜忌（指向 YI_TABLE / JI_TABLE 中的 5 条）
    const char* const* yi;     // 宜，共 MAX_YI_ITEMS 条
    const char* const* ji;     // 忌，共 MAX_JI_ITEMS 条

    // 索引（用于内部调试）
    int  yearGan, yearZhi;
    int  monthGan, monthZhi;
    int  dayGan, dayZhi;
    int  godIndex;
};

// 公历 → 农历 + 干支 + 建除 + 宜忌
// sy/sm/sd: 公历年月日（如 2026, 6, 21）
LunarInfo calcLunar(int sy, int sm, int sd);

// 调试：串口打印当日黄历
void printLunarDebug(int sy, int sm, int sd);
