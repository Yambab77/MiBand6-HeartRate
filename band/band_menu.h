#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

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

// ----- 外部依赖（在 band.cpp / 其它模块中定义）-----
extern bool  g_alwaysOnTop;
extern HWND  g_mainWnd;
extern BYTE  g_glowColorR;
extern BYTE  g_glowColorG;
extern BYTE  g_glowColorB;
extern bool  g_userColorOverride;
extern const UINT WM_APP_REFRESH;

void AppendLog(const std::wstring& line);

// ----- 菜单接口 -----
// 初始化（延迟注册窗口类等，可多次调用）
void InitMenuSubsystem();

// 显示右键菜单（屏幕坐标）
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