#include "lunar_calc.h"
#include "data_tables.h"
#include "config.h"
#include <stdio.h>

// ============================================================
// 公历 → 儒略日号 (Julian Day Number)
// Fliegel-Van Flandern 算法
// ============================================================

static long gregorianToJDN(int y, int m, int d) {
    long a = (14 - m) / 12;
    long yy = (long)y + 4800 - a;
    long mm = (long)m + 12 * a - 3;
    return (long)d + (153 * mm + 2) / 5 + 365 * yy +
           yy / 4 - yy / 100 + yy / 400 - 32045;
}

// 1900-01-31 的 JDN（农历 1900-01-01 = 春节）
#define JDN_LUNAR_BASE 2415051L

// ============================================================
// 农历年份信息查询
// ============================================================

// 农历 y 年某月的天数 (29 或 30)
// bit 15 = 1 月, bit 4 = 12 月，1 = 大月(30天)
static int lunarMonthDays(int y, int m) {
    if (y < LUNAR_BASE_YEAR || y > LUNAR_MAX_YEAR) return 29;
    uint32_t info = LUNAR_INFO[y - LUNAR_BASE_YEAR];
    return (info >> (16 - m)) & 0x1 ? 30 : 29;
}

// 农历 y 年闰月月份 (0=无)
static int lunarLeapMonth(int y) {
    if (y < LUNAR_BASE_YEAR || y > LUNAR_MAX_YEAR) return 0;
    uint32_t info = LUNAR_INFO[y - LUNAR_BASE_YEAR];
    return info & 0xF;
}

// 农历 y 年闰月的天数 (29 或 30)
static int lunarLeapMonthDays(int y) {
    if (y < LUNAR_BASE_YEAR || y > LUNAR_MAX_YEAR) return 0;
    uint32_t info = LUNAR_INFO[y - LUNAR_BASE_YEAR];
    return (info & 0x10000) ? 30 : 29;
}

// 农历 y 年总天数
static int lunarYearDays(int y) {
    int sum = 348;  // 12 个月 × 29 = 348
    int leap = lunarLeapMonth(y);
    // 加上每个大月多出的 1 天
    for (int i = 0; i < 12; i++) {
        sum += (lunarMonthDays(y, i + 1) == 30) ? 1 : 0;
    }
    if (leap > 0) {
        sum += lunarLeapMonthDays(y);
    }
    return sum;
}

// ============================================================
// 公历 → 农历
// ============================================================

static void solarToLunar(int sy, int sm, int sd,
                         int &ly, int &lm, int &ld, bool &isLeap) {
    long offset = gregorianToJDN(sy, sm, sd) - JDN_LUNAR_BASE;
    isLeap = false;

    // 找农历年
    int y = LUNAR_BASE_YEAR;
    while (y <= LUNAR_MAX_YEAR) {
        int yearDays = lunarYearDays(y);
        if (offset < yearDays) break;
        offset -= yearDays;
        y++;
    }
    ly = y;

    // 找农历月（12 个月 + 可能的闰月）
    int leap = lunarLeapMonth(y);
    int m = 1;
    bool inLeap = false;

    while (m <= 12) {
        int monthDays;
        if (inLeap) {
            monthDays = lunarLeapMonthDays(y);  // 当前是闰月
        } else {
            monthDays = lunarMonthDays(y, m);   // 普通月
        }

        if (offset < monthDays) break;

        offset -= monthDays;

        if (inLeap) {
            // 闰月结束，进入下一普通月
            inLeap = false;
            m++;
        } else if (leap > 0 && m == leap) {
            // 当前是 leap 月的普通月，下次进入它的闰月
            inLeap = true;
        } else {
            m++;
        }
    }

    isLeap = inLeap;
    lm = m;
    ld = (int)offset + 1;
}

// ============================================================
// 干支计算
// ============================================================

// 日干支：基于 JDN，公式已验证
//   dayGan = (JDN + 9) % 10
//   dayZhi = (JDN + 1) % 12
static void calcDayGanZhi(long jdn, int &gan, int &zhi) {
    int g = (int)((jdn + 9) % 10); if (g < 0) g += 10;
    int z = (int)((jdn + 1) % 12); if (z < 0) z += 12;
    gan = g;
    zhi = z;
}

// 年柱：1984 年甲子年开始，60 年循环
static void calcYearGanZhi(int year, int &gan, int &zhi) {
    int g = (year - 4) % 10; if (g < 0) g += 10;
    int z = (year - 4) % 12; if (z < 0) z += 12;
    gan = g;
    zhi = z;
}

// 月柱：基于年干和农历月
//   月支: lunarMonth → (lunarMonth + 1) % 12
//   月干: 五虎遁年起月诀
//     甲/己年: 正月起丙(2)
//     乙/庚年: 正月起戊(4)
//     丙/辛年: 正月起庚(6)
//     丁/壬年: 正月起壬(8)
//     戊/癸年: 正月起甲(0)
static void calcMonthGanZhi(int yearGan, int lunarMonth,
                            int &gan, int &zhi) {
    int firstGan = ((yearGan % 5) * 2 + 2) % 10;
    gan = (firstGan + (lunarMonth - 1)) % 10;
    zhi = (lunarMonth + 1) % 12;
}

// ============================================================
// 公开接口
// ============================================================

// 静态缓冲区：存放拼接的干支字符串（单线程安全）
static char s_yearGZ[8];
static char s_monthGZ[8];
static char s_dayGZ[8];

LunarInfo calcLunar(int sy, int sm, int sd) {
    LunarInfo info;
    memset(&info, 0, sizeof(info));

    // 农历
    solarToLunar(sy, sm, sd,
                 info.lunarYear, info.lunarMonth, info.lunarDay,
                 info.isLeap);

    // 干支
    long jdn = gregorianToJDN(sy, sm, sd);
    calcYearGanZhi(info.lunarYear, info.yearGan, info.yearZhi);
    calcMonthGanZhi(info.yearGan, info.lunarMonth,
                    info.monthGan, info.monthZhi);
    calcDayGanZhi(jdn, info.dayGan, info.dayZhi);

    // 拼接字符串
    snprintf(s_yearGZ,  sizeof(s_yearGZ),  "%s%s",
             TIAN_GAN[info.yearGan],  DI_ZHI[info.yearZhi]);
    snprintf(s_monthGZ, sizeof(s_monthGZ), "%s%s",
             TIAN_GAN[info.monthGan], DI_ZHI[info.monthZhi]);
    snprintf(s_dayGZ,   sizeof(s_dayGZ),   "%s%s",
             TIAN_GAN[info.dayGan],   DI_ZHI[info.dayZhi]);
    info.yearGanZhi  = s_yearGZ;
    info.monthGanZhi = s_monthGZ;
    info.dayGanZhi   = s_dayGZ;

    // 生肖（从年地支）
    info.shengXiao = SHENG_XIAO[info.yearZhi];

    // 建除十二神：日支对月支
    // godIndex = (dayZhi - monthZhi + 12) % 12, 0=建
    info.godIndex = (info.dayZhi - info.monthZhi + 12) % 12;
    info.jianChu  = JIAN_CHU[info.godIndex];

    // 宜忌（指向 data_tables.h 的表）
    info.yi = YI_TABLE[info.godIndex];
    info.ji = JI_TABLE[info.godIndex];

    return info;
}

// ============================================================
// 调试
// ============================================================

void printLunarDebug(int sy, int sm, int sd) {
    LunarInfo i = calcLunar(sy, sm, sd);
    Serial.println("\n========== LUNAR DEBUG ==========");
    Serial.printf("Solar:  %04d-%02d-%02d\n", sy, sm, sd);
    Serial.printf("Lunar:  %04d-%s%d-%d\n",
                  i.lunarYear, i.isLeap ? "leap " : "",
                  i.lunarMonth, i.lunarDay);
    Serial.printf("GanZhi: %s年 %s月 %s日\n",
                  i.yearGanZhi, i.monthGanZhi, i.dayGanZhi);
    Serial.printf("ShengXiao: %s   JianChu: %s\n",
                  i.shengXiao, i.jianChu);
    Serial.print("Yi:    ");
    for (int k = 0; k < MAX_YI_ITEMS; k++) {
        Serial.printf("%s ", i.yi[k]);
    }
    Serial.println();
    Serial.print("Ji:    ");
    for (int k = 0; k < MAX_JI_ITEMS; k++) {
        Serial.printf("%s ", i.ji[k]);
    }
    Serial.println();
    Serial.println("=================================\n");
}
