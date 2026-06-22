#include "paddle_score.h"
#include "config.h"
#include "time_sync.h"  // getUserZodiac()
#include <string.h>

// ============================================================
// 字符串匹配辅助（UTF-8 中文字符串）
// ============================================================

static bool inList(const char* const* list, int count, const char* item) {
    for (int i = 0; i < count; i++) {
        if (strcmp(list[i], item) == 0) return true;
    }
    return false;
}

// ============================================================
// 特殊凶日判断
// ============================================================

// 四离日：立春/立夏/立秋/立冬前一天
// 节气日期每年略有浮动（1-2 天），这里用近 5 年精确日期
// 立春: 2/3-2/5, 立夏: 5/5, 立秋: 8/7-8/8, 立冬: 11/7
// 四离日 = 立春/夏/秋/冬 的前一天
static bool isFourLi(int y, int m, int d) {
    // 立春前一天：2/3 或 2/4（用 2/3 兜底，2/4 罕见）
    if (m == 2 && d == 3) return true;
    if (m == 2 && d == 4 && y != 2029) return true;  // 2029 立春 2/3
    // 立夏前一天：5/4
    if (m == 5 && d == 4) return true;
    // 立秋前一天：8/6 或 8/7
    if (m == 8 && d == 6) return true;
    if (m == 8 && d == 7 && y == 2027) return true;  // 2027 立秋 8/8
    // 立冬前一天：11/6
    if (m == 11 && d == 6) return true;
    return false;
}

// 四绝日：春分/夏至/秋分/冬至前一天
// 春分 3/20, 夏至 6/21, 秋分 9/22-23, 冬至 12/21-22
// 四绝日 = 春分/秋分/夏至/冬至 的前一天
static bool isFourJue(int y, int m, int d) {
    // 春分前一天：3/19
    if (m == 3 && d == 19) return true;
    // 夏至前一天：6/20
    if (m == 6 && d == 20) return true;
    // 秋分前一天：9/22 (一般)，9/21（秋分 9/22 时）
    if (m == 9 && d == 22) return true;  // 秋分 9/23 时前一天是 9/22
    if (m == 9 && d == 21 && y == 2028) return true;  // 2028 秋分 9/22
    // 冬至前一天：12/21
    if (m == 12 && d == 21) return true;
    if (m == 12 && d == 20 && (y == 2028 || y == 2029)) return true;
    return false;
}

// 杨公忌日（13 个固定农历日）
static bool isYangGong(int lunarMonth, int lunarDay, bool isLeap) {
    if (isLeap) return false;  // 闰月不算
    static const struct { uint8_t m, d; } dates[] = {
        {1, 13}, {2, 11}, {3, 9}, {4, 7}, {5, 5}, {6, 3},
        {7, 1}, {7, 29}, {8, 27}, {9, 25}, {10, 23}, {11, 21}, {12, 19}
    };
    for (const auto& dt : dates) {
        if (dt.m == lunarMonth && dt.d == lunarDay) return true;
    }
    return false;
}

// ============================================================
// 主计算函数
// ============================================================

PaddleScore calculatePaddleScore(const LunarInfo& lunar,
                                  int sy, int sm, int sd) {
    PaddleScore r;
    memset(&r, 0, sizeof(r));
    r.score = 0;
    r.vetoed = false;

    // -------- 1. 特殊凶日（一票否决） --------

    // 破日（月破，建除十二神中的"破"）
    if (lunar.godIndex == 6) {
        r.vetoed = true;
        r.vetoReason = "Po Day (Moon clash)";
        r.score = -10;
        r.level = ScoreLevel::VETOED;
        r.symbol = "X";
        r.description = "Po day, avoid water";
        return r;
    }

    if (isFourLi(sy, sm, sd)) {
        r.vetoed = true;
        r.vetoReason = "Si Li (4-Leaving)";
        r.score = -10;
        r.level = ScoreLevel::VETOED;
        r.symbol = "X";
        r.description = "Si Li, avoid water";
        return r;
    }

    if (isFourJue(sy, sm, sd)) {
        r.vetoed = true;
        r.vetoReason = "Si Jue (4-Ending)";
        r.score = -10;
        r.level = ScoreLevel::VETOED;
        r.symbol = "X";
        r.description = "Si Jue, avoid water";
        return r;
    }

    if (isYangGong(lunar.lunarMonth, lunar.lunarDay, lunar.isLeap)) {
        r.vetoed = true;
        r.vetoReason = "Yang Gong Ji";
        r.score = -8;
        r.level = ScoreLevel::VETOED;
        r.symbol = "!";
        r.description = "Yang Gong, no go";
        return r;
    }

    // -------- 2. 字段权重累加 --------

    static const struct { const char* name; int weight; bool posOnly; } fields[] = {
        // 正向：宜 +weight，忌 -weight
        {"出行", 5, false},
        {"开池", 4, false},
        {"造桥", 4, false},
        {"祭祀", 2, false},
        {"开市", 1, false},
        {"交易", 1, false},
        {"立券", 1, false},
        {"移徙", 1, false},
        {"赴任", 1, false},
        {"沐浴", 1, false},
        // 仅负向：忌破土扣 3 分，宜破土不加分
        {"破土", 3, true},
    };

    int score = 0;
    for (const auto& f : fields) {
        if (f.posOnly) {
            // 仅忌时扣分
            if (inList(lunar.ji, MAX_JI_ITEMS, f.name)) {
                score -= f.weight;
            }
        } else {
            // 双向
            if (inList(lunar.yi, MAX_YI_ITEMS, f.name)) score += f.weight;
            if (inList(lunar.ji, MAX_JI_ITEMS, f.name)) score -= f.weight;
        }
    }

    // -------- 3. 生肖冲煞（运行时从 NVS 读取用户生肖） --------

    int userZodiac = getUserZodiac();
    if (userZodiac >= 0 && userZodiac < 12) {
        // 六冲：dayZhi + 6 = userZodiac（mod 12）
        if ((lunar.dayZhi + 6) % 12 == userZodiac) {
            score -= 3;
        }
    }

    // -------- 4. 分级 --------
    r.score = score;
    if (score >= 6) {
        r.level = ScoreLevel::EXCELLENT;
        r.symbol = "*";  // ★
        r.description = "Excellent, go!";
    } else if (score >= 3) {
        r.level = ScoreLevel::GOOD;
        r.symbol = "v";  // ✓
        r.description = "Good, paddle ok";
    } else if (score >= 0) {
        r.level = ScoreLevel::OK;
        r.symbol = "o";  // ◯
        r.description = "OK, check wx";
    } else if (score >= -3) {
        r.level = ScoreLevel::CAUTION;
        r.symbol = "!";  // ⚠
        r.description = "Caution";
    } else {
        r.level = ScoreLevel::BAD;
        r.symbol = "X";  // ✗
        r.description = "Avoid today";
    }

    return r;
}

const char* levelName(ScoreLevel level) {
    switch (level) {
        case ScoreLevel::EXCELLENT: return "Excellent";
        case ScoreLevel::GOOD:      return "Good";
        case ScoreLevel::OK:        return "OK";
        case ScoreLevel::CAUTION:   return "Caution";
        case ScoreLevel::BAD:       return "Bad";
        case ScoreLevel::VETOED:    return "Vetoed";
    }
    return "?";
}

int getNormalizedScore(const PaddleScore& s) {
    if (s.vetoed) return 0;
    int n = s.score + 5;  // 偏移到 0-10 范围
    if (n < 1) n = 1;     // 否决以外的最差 = 1
    if (n > 10) n = 10;
    return n;
}

const char* getConclusionCN(const PaddleScore& s) {
    if (s.vetoed) return "忌下水";
    int n = getNormalizedScore(s);
    if (n <= 3) return "忌下水";
    if (n <= 6) return "可下水";
    return "宜下水";
}

// ============================================================
// 调试
// ============================================================

void printPaddleScoreDebug(const LunarInfo& lunar, int sy, int sm, int sd) {
    PaddleScore r = calculatePaddleScore(lunar, sy, sm, sd);
    Serial.println("\n========== PADDLE SCORE ==========");
    Serial.printf("Solar: %04d-%02d-%02d\n", sy, sm, sd);
    if (r.vetoed) {
        Serial.printf("VETOED! reason=%s  (%s)\n",
                      r.vetoReason, r.description);
    } else {
        Serial.printf("Score: %+d  Level: %s  [%s] %s\n",
                      r.score, levelName(r.level),
                      r.symbol, r.description);
    }
    Serial.println("==================================\n");
}
