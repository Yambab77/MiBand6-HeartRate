#include "tray.h"
#include <shellapi.h>
#include <gdiplus.h>
#include <algorithm>

#pragma comment(lib, "Shell32.lib")

using namespace Gdiplus;

static bool  s_trayActive = false;
static HICON s_trayIcon = nullptr;
static HWND  s_hwndMain = nullptr;
static bool  s_menuShowing = false; // ��ֹ�ظ���������˵�ʵ��

static HICON CreateHrTrayIcon(int hr) {
    const int cx = GetSystemMetrics(SM_CXSMICON);
    const int cy = GetSystemMetrics(SM_CYSMICON);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cx;
    bi.bmiHeader.biHeight = -cy; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc = CreateCompatibleDC(nullptr);
    HBITMAP hbmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmp) { DeleteDC(hdc); return nullptr; }
    HGDIOBJ old = SelectObject(hdc, hbmp);

    {
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

        SolidBrush clear(Color(0, 0, 0, 0));
        g.FillRectangle(&clear, 0, 0, cx, cy);

        std::wstring s = std::to_wstring(std::clamp(hr, 0, 999));
        FontFamily ff(L"Segoe UI");
        REAL fs = (REAL)std::max(10, (int)(cy * 0.7));
        Font f(&ff, fs, FontStyleBold, UnitPixel);

        RectF layout;
        g.MeasureString(s.c_str(), -1, &f, PointF(0, 0), &layout);
        REAL x = (cx - layout.Width) / 2.0f;
        REAL y = (cy - layout.Height) / 2.0f - 1.0f;

        SolidBrush shadow(Color(160, 0, 0, 0));
        g.DrawString(s.c_str(), -1, &f, PointF(x + 1, y + 1), &shadow);

        SolidBrush fg(Color(255, g_glowColorR, g_glowColorG, g_glowColorB));
        g.DrawString(s.c_str(), -1, &f, PointF(x, y), &fg);
    }

    SelectObject(hdc, old);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = hbmp;
    HBITMAP hMask = CreateBitmap(cx, cy, 1, 1, nullptr);
    ii.hbmMask = hMask;
    HICON hico = CreateIconIndirect(&ii);

    DeleteObject(hbmp);
    DeleteObject(hMask);
    DeleteDC(hdc);
    return hico;
}

static void UpdateTrayIconInternal() {
    if (!s_trayActive || !s_hwndMain) return;
    int hr = g_heartRate.load();
    HICON hNew = CreateHrTrayIcon(hr);
    if (!hNew) return;

    NOTIFYICONDATA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = s_hwndMain;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = hNew;
    std::wstring tip = L"����: " + std::to_wstring(hr);
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIcon(NIM_MODIFY, &nid);

    if (s_trayIcon) DestroyIcon(s_trayIcon);
    s_trayIcon = hNew;
}

void Tray_Init(HWND hwndMain) {
    s_hwndMain = hwndMain;
}

void MinimizeToTray() {
    if (!s_hwndMain || s_trayActive) return;

    int hr = g_heartRate.load();
    s_trayIcon = CreateHrTrayIcon(hr);
    if (!s_trayIcon) return;

    NOTIFYICONDATA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = s_hwndMain;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = s_trayIcon;
    std::wstring tip = L"����: " + std::to_wstring(hr) + L"���Ҽ��˵��ɻ�ԭ��";
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        DestroyIcon(s_trayIcon); s_trayIcon = nullptr; return;
    }
    // ʹ�� v3 �������յ� WM_RBUTTONUP/WM_CONTEXTMENU �ص�
    nid.uVersion = NOTIFYICON_VERSION; // v3
    Shell_NotifyIcon(NIM_SETVERSION, &nid);

    s_trayActive = true;
    ShowWindow(s_hwndMain, SW_HIDE);
    AppendLog(L"[UI] ��С��������");
}

void Tray_OnUiTick() {
    UpdateTrayIconInternal();
}

static void RestoreFromTray(HWND hWnd) {
    NOTIFYICONDATA nid{}; nid.cbSize = sizeof(nid); nid.hWnd = hWnd; nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
    if (s_trayIcon) { DestroyIcon(s_trayIcon); s_trayIcon = nullptr; }
    s_trayActive = false;
    // ��ԭ��ǰ��
    ShowWindow(hWnd, SW_SHOWNORMAL);
    UpdateWindow(hWnd);
    SetForegroundWindow(hWnd);
}

// ��ĳ���ڿɿ�����ǰ����ʱ����������У����� SetForegroundWindow ʧ�ܣ�
static void ForceForeground(HWND target) {
    HWND fg = GetForegroundWindow();
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD selfTid = GetCurrentThreadId();
    if (fgTid && fgTid != selfTid) AttachThreadInput(fgTid, selfTid, TRUE);
    SetForegroundWindow(target);
    if (fgTid && fgTid != selfTid) AttachThreadInput(fgTid, selfTid, FALSE);
}

// ����һ����ʱ�˵��������ڣ��ɼ��� 1x1���ޱ߿򣩣����ڿɿ���ʾ�Ҽ��˵�
static HWND CreateTrayMenuHost(HWND owner) {
    static const wchar_t* kClass = L"TrayMenuHostWnd";
    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance   = GetModuleHandleW(nullptr);
        wc.hCursor     = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        wc.hbrBackground = nullptr;
        s_reg = RegisterClassExW(&wc) != 0;
    }
    POINT pt{}; GetCursorPos(&pt);
    // ����긽������һ�� 1x1 �ĵ���������Ϊ�˵�����
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW,
                                kClass, L"", WS_POPUP,
                                pt.x, pt.y, 1, 1,
                                owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (host) {
        ShowWindow(host, SW_SHOWNOACTIVATE);
        UpdateWindow(host);
        ForceForeground(host);
    }
    return host;
}

static UINT ShowTrayMenu() {
    HMENU h = CreatePopupMenu();
    InsertMenuW(h, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_RESTORE, L"��ԭ����");
    InsertMenuW(h, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT,    L"�˳�����");

    // �˵�λ�ã���굱ǰλ��
    POINT pt{}; GetCursorPos(&pt);
    // ʹ����ʱ�˵�������ȷ��ǰ̨����Ϣ������ȷ
    HWND host = CreateTrayMenuHost(s_hwndMain);
    // �ٴ�ȷ��ǰ̨
    if (host) ForceForeground(host);

    UINT cmd = TrackPopupMenu(h,
                              TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                              pt.x, pt.y, 0, host ? host : s_hwndMain, nullptr);
    DestroyMenu(h);

    // �ò˵���ȷ�رգ�MS �Ƽ���������������һ������Ϣ
    if (host) {
        PostMessageW(host, WM_NULL, 0, 0);
        DestroyWindow(host);
    } else if (s_hwndMain) {
        PostMessageW(s_hwndMain, WM_NULL, 0, 0);
    }
    return cmd;
}

bool Tray_HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg != WM_APP_TRAY) return false;

    // ��¼�յ��Ļص��������Ų�
    AppendLog(L"[TRAY] cb lParam=" + std::to_wstring((ULONG_PTR)lParam));

    switch (lParam) {
        // ���� WM_CONTEXTMENU �����Ҽ��˵��������ظ���������Ӧ DOWN/UP/DBLCLK/NIN_POPUPOPEN��
    case WM_CONTEXTMENU: {
        if (s_menuShowing) return true;
        s_menuShowing = true;
        UINT cmd = ShowTrayMenu();
        s_menuShowing = false;
        if (cmd == ID_TRAY_RESTORE) {
            RestoreFromTray(hWnd);
            AppendLog(L"[UI] ���̲˵�����ԭ����");
        } else if (cmd == ID_TRAY_EXIT) {
            AppendLog(L"[UI] ���̲˵����˳�����");
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        return true;
    }
    // ��ѡ�����Ҳ���˵����粻��Ҫ���Ƴ��˷�֧��
    case NIN_SELECT:
    case WM_LBUTTONUP: {
        if (s_menuShowing) return true;
        s_menuShowing = true;
        UINT cmd = ShowTrayMenu();
        s_menuShowing = false;
        if (cmd == ID_TRAY_RESTORE) {
            RestoreFromTray(hWnd);
            AppendLog(L"[UI] ���̲˵�(���)����ԭ����");
        } else if (cmd == ID_TRAY_EXIT) {
            AppendLog(L"[UI] ���̲˵�(���)���˳�����");
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        return true;
    }
    // �������̻ص����ԣ������ε��˵���
    default:
        return true;
    }
}

void Tray_Shutdown() {
    if (s_trayActive && s_hwndMain) {
        NOTIFYICONDATA nid{}; nid.cbSize = sizeof(nid); nid.hWnd = s_hwndMain; nid.uID = 1;
        Shell_NotifyIcon(NIM_DELETE, &nid);
        s_trayActive = false;
    }
    if (s_trayIcon) { DestroyIcon(s_trayIcon); s_trayIcon = nullptr; }
}