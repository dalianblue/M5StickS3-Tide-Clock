#pragma once

#include <Arduino.h>

// ============================================================
// UI 渲染模块：3 屏切换（竖屏 135×240）
// ============================================================

#define SCREEN_CLOCK  0  // 屏 1：时钟 + 当前水位 + 距下次高潮/低潮
#define SCREEN_TIDES  1  // 屏 2：当日 4 个潮汐时刻表
#define SCREEN_LUNAR  2  // 屏 3：当日黄历
#define SCREEN_COUNT  3

// 初始化 UI（在 setup 中调用一次）
void initUI();

// 渲染指定屏（每次切换或内容变化时调用）
void renderScreen(int screenIndex);

// 屏幕循环切换：屏 N → 屏 N+1（循环）
int cycleScreen(int currentScreen);

// 在 loop 中调用：每秒刷新当前屏（仅当时钟屏时刷新时钟数字）
void uiLoopTick();

// 强制下一次渲染（强制全屏重绘）
void forceRedraw();
