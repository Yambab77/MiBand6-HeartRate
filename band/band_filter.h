#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <string>

// 菜单命令 ID（若主文件未定义则定义）
#ifndef ID_POP_SET_FILTER
#define ID_POP_SET_FILTER 9004
#endif

// 全局：过滤地址（0 表示未启用）
extern std::atomic<uint64_t> g_filterAddr;

// 初始化：启动时加载保存的过滤地址
void InitFilter();

// 判断地址是否应当接受
bool ShouldAcceptAddress(uint64_t addr);

// 右键菜单集成
void AddFilterMenuItem(HMENU hMenu);                         // 插入菜单项
bool HandleFilterMeasureItem(LPMEASUREITEMSTRUCT mis);       // ODT_MENU 测量
bool HandleFilterDrawItem(LPDRAWITEMSTRUCT ds);              // ODT_MENU 绘制
bool HandleFilterCommand(WORD id, HWND owner);               // WM_COMMAND 处理

// 可直接调用弹窗
void ShowFilterDialog(HWND owner);

// 依赖主程序提供的函数（在 band.cpp 中定义）
std::wstring GetExeDir();
void AppendLog(const std::wstring& line);