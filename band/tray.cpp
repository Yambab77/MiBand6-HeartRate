#include "tray.h"
#include <windows.h>   // 确保 GDI 函数声明（CreateRoundRectRgn 等）
#include <shellapi.h>
#include <gdiplus.h>
#include <algorithm>
#include <vector>
#include <atomic>
#include <windowsx.h>

#pragma comment(lib, "Shell32.lib")

using namespace Gdiplus;

static bool  s_trayActive = false;
static HICON s_trayIcon = nullptr;
static HWND  s_hwndMain = nullptr;
static bool  s_menuShowing = false; // 防止重复弹出多个菜单实例
static std::atomic<UINT> s_trayMenuCmd{ 0 }; // 菜单选择结果（0 表示取消）

// 若常量在此前版本丢失，这里统一定义一次
#ifndef TRAY_MENU_CONSTANTS_DEFINED
#define TRAY_MENU_CONSTANTS_DEFINED 1
static constexpr int kTrayItemHeight = 30;
static constexpr int kTrayWidth      = 120;
static constexpr int kTrayRadius     = 8;
#endif

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

// 托盘菜单宿主窗口过程：处理 Owner-Draw
static LRESULT CALLBACK TrayMenuHostProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MEASUREITEM: {
        auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (mis && mis->CtlType == ODT_MENU) {
            mis->itemHeight = 30;   // 固定高度
            mis->itemWidth  = 90;  // 固定宽度（仅用于系统估算）
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis && dis->CtlType == ODT_MENU) {
            RECT rc = dis->rcItem;
            // 背景：黑色，选中时深灰
            COLORREF bg = (dis->itemState & ODS_SELECTED) ? RGB(24,24,24) : RGB(0,0,0);
            HBRUSH hbr = CreateSolidBrush(bg);
            FillRect(dis->hDC, &rc, hbr);
            DeleteObject(hbr);

            // 文本：白色
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255,255,255));

            // 根据 itemID 决定文本
            const wchar_t* text = L"";
            if (dis->itemID == ID_TRAY_RESTORE) text = L"还原窗口";
            else if (dis->itemID == ID_TRAY_EXIT) text = L"退出程序";

            RECT trc = rc; trc.left += 10; // 左侧内边距
            DrawTextW(dis->hDC, text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        }
        break;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// 创建一个临时菜单宿主窗口（可见但 1x1，无边框），用于可靠显示右键菜单（旧 HMENU 方案保留但不再使用）
static HWND CreateTrayMenuHost(HWND owner) {
    static const wchar_t* kClass = L"TrayMenuHostWnd";
    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = TrayMenuHostProc; // 使用自定义过程以处理 Owner-Draw
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

// ===== 自绘圆角黑底托盘菜单 =====
struct TrayMenuItem { UINT id; const wchar_t* text; };
static const wchar_t* kTrayMenuClass = L"TrayCtxMenuWnd";

static LRESULT CALLBACK TrayCtxMenuProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* items = reinterpret_cast<std::vector<TrayMenuItem>*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    static int hot = -1;
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        s_trayMenuCmd.store(0, std::memory_order_relaxed);
        // 圆角区域（确保 6 个 int 参数）
        RECT rc{}; GetClientRect(hWnd, &rc);
        HRGN rgn = CreateRoundRectRgn(
            (int)rc.left, (int)rc.top, (int)rc.right, (int)rc.bottom,
            (int)(kTrayRadius * 2), (int)(kTrayRadius * 2));
        SetWindowRgn(hWnd, rgn, FALSE); // rgn 由窗口接管
        SetCapture(hWnd);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINTS pts = MAKEPOINTS(lParam);
        int idx = pts.y / kTrayItemHeight;
        if (!items || idx < 0 || idx >= (int)items->size()) idx = -1;
        if (idx != hot) { hot = idx; InvalidateRect(hWnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!items) { DestroyWindow(hWnd); return 0; }
        int idx = HIWORD(lParam) / kTrayItemHeight;
        UINT ret = 0;
        if (idx >= 0 && idx < (int)items->size()) ret = (*items)[idx].id;
        ReleaseCapture();
        // 通过原子变量回传选择结果
        s_trayMenuCmd.store(ret, std::memory_order_relaxed);
        DestroyWindow(hWnd);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { ReleaseCapture(); s_trayMenuCmd.store(0, std::memory_order_relaxed); DestroyWindow(hWnd); return 0; }
        break;
    case WM_KILLFOCUS:
    case WM_CAPTURECHANGED:
        ReleaseCapture(); s_trayMenuCmd.store(0, std::memory_order_relaxed); DestroyWindow(hWnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT rc{}; GetClientRect(hWnd, &rc);
        // 背景：纯黑圆角
        {
            GraphicsPath path;
            RectF rcf((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top));
            REAL r = (REAL)kTrayRadius;
            path.AddArc(rcf.X, rcf.Y, r*2, r*2, 180, 90);
            path.AddArc(rcf.GetRight()-r*2, rcf.Y, r*2, r*2, 270, 90);
            path.AddArc(rcf.GetRight()-r*2, rcf.GetBottom()-r*2, r*2, r*2,   0, 90);
            path.AddArc(rcf.X, rcf.GetBottom()-r*2, r*2, r*2,  90, 90);
            path.CloseFigure();
            SolidBrush bg(Color(255, 0, 0, 0));
            g.FillPath(&bg, &path);
        }
        if (items) {
            FontFamily ff(L"微软雅黑");
            Font ft(&ff, 14.f, FontStyleBold, UnitPixel);
            SolidBrush white(Color(255, 255, 255, 255));
            for (size_t i = 0; i < items->size(); ++i) {
                int top = (int)i * kTrayItemHeight;
                if ((int)i == hot) {
                    SolidBrush sel(Color(255, 24, 24, 24));
                    g.FillRectangle(&sel, 0, top, kTrayWidth, kTrayItemHeight);
                }
                RectF trc(0, (REAL)top, (REAL)kTrayWidth, (REAL)kTrayItemHeight);
                StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
                g.DrawString((*items)[i].text, -1, &ft, trc, &sf, &white);
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (items) { delete items; SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0); }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void EnsureTrayMenuClass() {
    static bool reg = false;
    if (reg) return;
    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = TrayCtxMenuProc;
    wc.hInstance   = GetModuleHandleW(nullptr);
    wc.hCursor     = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kTrayMenuClass;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);
    reg = true;
}

// 同步显示自绘托盘菜单，返回选择的命令 ID（0 表示取消）
static UINT ShowTrayMenu() {
    EnsureTrayMenuClass();
    auto* items = new std::vector<TrayMenuItem>{
        { ID_TRAY_RESTORE, L"还原窗口" },
        { ID_TRAY_EXIT,    L"退出程序" }
    };
    POINT pt{}; GetCursorPos(&pt);
    int w = kTrayWidth;
    int h = (int)items->size() * kTrayItemHeight;
    // 屏幕边缘修正
    RECT sr{ 0,0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    int x = pt.x, y = pt.y;
    if (x + w > sr.right)  x = sr.right - w - 2;
    if (y + h > sr.bottom) y = sr.bottom - h - 2;
    HWND wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                               kTrayMenuClass, L"", WS_POPUP,
                               x, y, w, h,
                               s_hwndMain, nullptr, GetModuleHandleW(nullptr), items);
    if (!wnd) { delete items; return 0; }
    ShowWindow(wnd, SW_SHOWNOACTIVATE);
    UpdateWindow(wnd);
    ForceForeground(wnd);
    // 阻塞消息循环直到窗口销毁
    UINT ret = 0;
    MSG msg;
    while (IsWindow(wnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // 从原子变量取回命令
    ret = s_trayMenuCmd.exchange(0, std::memory_order_relaxed);
    return ret;
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