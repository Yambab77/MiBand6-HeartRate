#pragma once

#include <string>

// 初始化/关闭（启动/停止后台刷新线程，准备日志目录与当天 HR 日志）
void Log_Init();
void Log_Shutdown();

// 控制是否同时输出到控制台（由 InitConsole 调用）
void Log_SetConsoleEnabled(bool enabled);

// 记录一条日志（自动分流到心率日志或应用日志）
// 约定：包含 "[ADV-HR]" 的行写入当日 HR 日志，其余写入 app.log
void AppendLog(const std::wstring& wline);
