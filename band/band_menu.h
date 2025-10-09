#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

// ��֤��Щ�˵� ID ��������һ�£����Ѷ������ظ���
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

// ----- �ⲿ�������� band.cpp / ����ģ���ж��壩-----
extern bool  g_alwaysOnTop;
extern HWND  g_mainWnd;
extern BYTE  g_glowColorR;
extern BYTE  g_glowColorG;
extern BYTE  g_glowColorB;
extern bool  g_userColorOverride;
extern const UINT WM_APP_REFRESH;

void AppendLog(const std::wstring& line);

// ----- �˵��ӿ� -----
// ��ʼ�����ӳ�ע�ᴰ����ȣ��ɶ�ε��ã�
void InitMenuSubsystem();

// ��ʾ�Ҽ��˵�����Ļ���꣩
void ShowContextMenu(HWND owner, POINT screenPt);

// Owner-draw ֧��
bool HandleMenuMeasure(LPMEASUREITEMSTRUCT mis);
bool HandleMenuDraw(LPDRAWITEMSTRUCT dis);

// WM_COMMAND �ַ������� true ��ʾ�Ѵ���
bool HandleMenuCommand(HWND owner, WORD id);

// ������С�������������ṩ��
void MinimizeToTray();

// �Զ����ޱ߿�˵����ڲ�ʹ�ã�
void ShowCustomContextMenu(HWND owner, POINT screenPt);