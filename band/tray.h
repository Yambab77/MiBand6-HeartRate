#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <string>

// 托盘回调消息（供 WndProc 分发，需为编译期常量用于 case）
inline constexpr UINT WM_APP_TRAY = WM_APP + 2;

// 主程序提供的符号
extern HWND g_mainWnd;
extern std::atomic<int> g_heartRate;
extern BYTE g_glowColorR, g_glowColorG, g_glowColorB;
void AppendLog(const std::wstring& line);

// 托盘接口
void Tray_Init(HWND hwndMain);                        // 在窗口创建后调用一次
void MinimizeToTray();                               // 触发最小化
void Tray_OnUiTick();                                // UI 刷新/心率变化时调用，更新图标
bool Tray_HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam); // 处理托盘回调
void Tray_Shutdown();                                // WM_DESTROY 清理

// 托盘菜单命令（内部使用）
#ifndef ID_TRAY_RESTORE
#define ID_TRAY_RESTORE  9101
#endif
#ifndef ID_TRAY_EXIT
#define ID_TRAY_EXIT     9102
#endif
