#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <string>

// ���̻ص���Ϣ���� WndProc �ַ�����Ϊ�����ڳ������� case��
inline constexpr UINT WM_APP_TRAY = WM_APP + 2;

// �������ṩ�ķ���
extern HWND g_mainWnd;
extern std::atomic<int> g_heartRate;
extern BYTE g_glowColorR, g_glowColorG, g_glowColorB;
void AppendLog(const std::wstring& line);

// ���̽ӿ�
void Tray_Init(HWND hwndMain);                        // �ڴ��ڴ��������һ��
void MinimizeToTray();                               // ������С��
void Tray_OnUiTick();                                // UI ˢ��/���ʱ仯ʱ���ã�����ͼ��
bool Tray_HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam); // �������̻ص�
void Tray_Shutdown();                                // WM_DESTROY ����

// ���̲˵�����ڲ�ʹ�ã�
#ifndef ID_TRAY_RESTORE
#define ID_TRAY_RESTORE  9101
#endif
#ifndef ID_TRAY_EXIT
#define ID_TRAY_EXIT     9102
#endif
