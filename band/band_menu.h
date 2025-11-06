#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <atomic> // 原文件要求

// 确保菜单 ID 不会重复定义
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
// 新增：上传开关，使用未被占用的编号
#ifndef ID_POP_UPLOAD_TOGGLE
#define ID_POP_UPLOAD_TOGGLE 9007
#endif

// ----- 外部全局（来自 band.cpp / 其它模块） -----
extern bool  g_alwaysOnTop;
extern HWND  g_mainWnd;
extern BYTE  g_glowColorR;
extern BYTE  g_glowColorG;
extern BYTE  g_glowColorB;
extern bool  g_userColorOverride;
extern const UINT WM_APP_REFRESH;
extern std::atomic<int>  g_heartRate;
extern std::atomic<bool> g_simpleMode; // 简洁模式开关（若有）
extern std::atomic<bool> g_uploadEnabled; // 新增：是否上传到网站（由右键菜单控制）

void AppendLog(const std::wstring& line);

// ----- 菜单接口 -----
void InitMenuSubsystem();
void ShowContextMenu(HWND owner, POINT screenPt);

// Owner-draw 支持
bool HandleMenuMeasure(LPMEASUREITEMSTRUCT mis);
bool HandleMenuDraw(LPDRAWITEMSTRUCT dis);

// WM_COMMAND 回调，返回 true 表示已处理
bool HandleMenuCommand(HWND owner, WORD id);

// 托盘相关（由 tray 模块实现）
void MinimizeToTray();

// 自定义上下文菜单
void ShowCustomContextMenu(HWND owner, POINT screenPt);
