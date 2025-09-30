#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <string>

// �˵����� ID�������ļ�δ�������壩
#ifndef ID_POP_SET_FILTER
#define ID_POP_SET_FILTER 9004
#endif

// ȫ�֣����˵�ַ��0 ��ʾδ���ã�
extern std::atomic<uint64_t> g_filterAddr;

// ��ʼ��������ʱ���ر���Ĺ��˵�ַ
void InitFilter();

// �жϵ�ַ�Ƿ�Ӧ������
bool ShouldAcceptAddress(uint64_t addr);

// �Ҽ��˵�����
void AddFilterMenuItem(HMENU hMenu);                         // ����˵���
bool HandleFilterMeasureItem(LPMEASUREITEMSTRUCT mis);       // ODT_MENU ����
bool HandleFilterDrawItem(LPDRAWITEMSTRUCT ds);              // ODT_MENU ����
bool HandleFilterCommand(WORD id, HWND owner);               // WM_COMMAND ����

// ��ֱ�ӵ��õ���
void ShowFilterDialog(HWND owner);

// �����������ṩ�ĺ������� band.cpp �ж��壩
std::wstring GetExeDir();
void AppendLog(const std::wstring& line);