#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <atomic> // 新增：声明原子开关

// 保证这些菜单 ID 与主程序一致（若已定义则不重复）
#ifndef ID_POP_EXIT
#define ID_POP_EXIT          9001
#endif
#ifndef ID_POP_TOGGLE_TOP
#define ID_POP_TOGGLE_TOP    9002
#endif
#ifndef ID_POP_CHANGE_COLOR
#define ID_POP_CHANGE_COLOR  9003
#endif
#ifndef ID_POP_SET_FILTER
#define ID_POP_SET_FILTER    9004
#endif
#ifndef ID_POP_MINIMIZE
#define ID_POP_MINIMIZE      9005
#endif
#ifndef ID_POP_TOGGLE_SIMPLE
#define ID_POP_TOGGLE_SIMPLE 9006
#endif

// ----- 外部依赖（在 band.cpp / 其它模块中定义）-----
extern bool  g_alwaysOnTop;
extern HWND  g_mainWnd;
extern BYTE  g_glowColorR;
extern BYTE  g_glowColorG;
extern BYTE  g_glowColorB;
extern bool  g_userColorOverride;
extern const UINT WM_APP_REFRESH;
extern std::atomic<int>  g_heartRate;
extern std::atomic<bool> g_simpleMode; // 新增：简洁模式开关

void AppendLog(const std::wstring& line);

// ----- 菜单接口 -----
void InitMenuSubsystem();
void ShowContextMenu(HWND owner, POINT screenPt);

// Owner-draw 支持
bool HandleMenuMeasure(LPMEASUREITEMSTRUCT mis);
bool HandleMenuDraw(LPDRAWITEMSTRUCT dis);

// WM_COMMAND 分发（返回 true 表示已处理）
bool HandleMenuCommand(HWND owner, WORD id);

// 托盘最小化（由主程序提供）
void MinimizeToTray();

// 自定义无边框菜单（内部使用）
void ShowCustomContextMenu(HWND owner, POINT screenPt);