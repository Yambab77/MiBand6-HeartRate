#include "tray.h"
#include <shellapi.h>
#include <gdiplus.h>
#include <algorithm>

#pragma comment(lib, "Shell32.lib")

using namespace Gdiplus;

static bool  s_trayActive = false;
static HICON s_trayIcon = nullptr;
static HWND  s_hwndMain = nullptr;
static bool  s_menuShowing = false; // 防止重复弹出多个菜单实例

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
    std::wstring tip = L"心率: " + std::to_wstring(hr);
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
    std::wstring tip = L"心率: " + std::to_wstring(hr) + L"（右键菜单可还原）";
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        DestroyIcon(s_trayIcon); s_trayIcon = nullptr; return;
    }
    // 使用 v3 更容易收到 WM_RBUTTONUP/WM_CONTEXTMENU 回调
    nid.uVersion = NOTIFYICON_VERSION; // v3
    Shell_NotifyIcon(NIM_SETVERSION, &nid);

    s_trayActive = true;
    ShowWindow(s_hwndMain, SW_HIDE);
    AppendLog(L"[UI] 最小化到托盘");
}

void Tray_OnUiTick() {
    UpdateTrayIconInternal();
}

static void RestoreFromTray(HWND hWnd) {
    NOTIFYICONDATA nid{}; nid.cbSize = sizeof(nid); nid.hWnd = hWnd; nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
    if (s_trayIcon) { DestroyIcon(s_trayIcon); s_trayIcon = nullptr; }
    s_trayActive = false;
    // 还原并前置
    ShowWindow(hWnd, SW_SHOWNORMAL);
    UpdateWindow(hWnd);
    SetForegroundWindow(hWnd);
}

// 将某窗口可靠地置前（临时附加输入队列，避免 SetForegroundWindow 失败）
static void ForceForeground(HWND target) {
    HWND fg = GetForegroundWindow();
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD selfTid = GetCurrentThreadId();
    if (fgTid && fgTid != selfTid) AttachThreadInput(fgTid, selfTid, TRUE);
    SetForegroundWindow(target);
    if (fgTid && fgTid != selfTid) AttachThreadInput(fgTid, selfTid, FALSE);
}

// 创建一个临时菜单宿主窗口（可见但 1x1，无边框），用于可靠显示右键菜单
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
    // 在鼠标附近创建一个 1x1 的弹出窗口作为菜单宿主
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
    InsertMenuW(h, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_RESTORE, L"还原窗口");
    InsertMenuW(h, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT,    L"退出程序");

    // 菜单位置：鼠标当前位置
    POINT pt{}; GetCursorPos(&pt);
    // 使用临时菜单宿主，确保前台与消息队列正确
    HWND host = CreateTrayMenuHost(s_hwndMain);
    // 再次确保前台
    if (host) ForceForeground(host);

    UINT cmd = TrackPopupMenu(h,
                              TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                              pt.x, pt.y, 0, host ? host : s_hwndMain, nullptr);
    DestroyMenu(h);

    // 让菜单正确关闭（MS 推荐）：向宿主发送一个空消息
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

    // 记录收到的回调，便于排查
    AppendLog(L"[TRAY] cb lParam=" + std::to_wstring((ULONG_PTR)lParam));

    switch (lParam) {
        // 仅在 WM_CONTEXTMENU 触发右键菜单，避免重复（不再响应 DOWN/UP/DBLCLK/NIN_POPUPOPEN）
    case WM_CONTEXTMENU: {
        if (s_menuShowing) return true;
        s_menuShowing = true;
        UINT cmd = ShowTrayMenu();
        s_menuShowing = false;
        if (cmd == ID_TRAY_RESTORE) {
            RestoreFromTray(hWnd);
            AppendLog(L"[UI] 托盘菜单：还原窗口");
        } else if (cmd == ID_TRAY_EXIT) {
            AppendLog(L"[UI] 托盘菜单：退出程序");
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        return true;
    }
    // 可选：左键也弹菜单（如不需要可移除此分支）
    case NIN_SELECT:
    case WM_LBUTTONUP: {
        if (s_menuShowing) return true;
        s_menuShowing = true;
        UINT cmd = ShowTrayMenu();
        s_menuShowing = false;
        if (cmd == ID_TRAY_RESTORE) {
            RestoreFromTray(hWnd);
            AppendLog(L"[UI] 托盘菜单(左键)：还原窗口");
        } else if (cmd == ID_TRAY_EXIT) {
            AppendLog(L"[UI] 托盘菜单(左键)：退出程序");
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        return true;
    }
    // 其余托盘回调忽略（避免多次弹菜单）
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