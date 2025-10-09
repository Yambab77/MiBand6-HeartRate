#include "tray.h"
#include <windows.h>   // ȷ�� GDI ����������CreateRoundRectRgn �ȣ�
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
static bool  s_menuShowing = false; // ��ֹ�ظ���������˵�ʵ��
static std::atomic<UINT> s_trayMenuCmd{ 0 }; // �˵�ѡ������0 ��ʾȡ����

// �������ڴ�ǰ�汾��ʧ������ͳһ����һ��
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

// ���̲˵��������ڹ��̣����� Owner-Draw
static LRESULT CALLBACK TrayMenuHostProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MEASUREITEM: {
        auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (mis && mis->CtlType == ODT_MENU) {
            mis->itemHeight = 30;   // �̶��߶�
            mis->itemWidth  = 90;  // �̶���ȣ�������ϵͳ���㣩
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis && dis->CtlType == ODT_MENU) {
            RECT rc = dis->rcItem;
            // ��������ɫ��ѡ��ʱ���
            COLORREF bg = (dis->itemState & ODS_SELECTED) ? RGB(24,24,24) : RGB(0,0,0);
            HBRUSH hbr = CreateSolidBrush(bg);
            FillRect(dis->hDC, &rc, hbr);
            DeleteObject(hbr);

            // �ı�����ɫ
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255,255,255));

            // ���� itemID �����ı�
            const wchar_t* text = L"";
            if (dis->itemID == ID_TRAY_RESTORE) text = L"��ԭ����";
            else if (dis->itemID == ID_TRAY_EXIT) text = L"�˳�����";

            RECT trc = rc; trc.left += 10; // ����ڱ߾�
            DrawTextW(dis->hDC, text, -1, &trc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return TRUE;
        }
        break;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ����һ����ʱ�˵��������ڣ��ɼ��� 1x1���ޱ߿򣩣����ڿɿ���ʾ�Ҽ��˵����� HMENU ��������������ʹ�ã�
static HWND CreateTrayMenuHost(HWND owner) {
    static const wchar_t* kClass = L"TrayMenuHostWnd";
    static bool s_reg = false;
    if (!s_reg) {
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = TrayMenuHostProc; // ʹ���Զ�������Դ��� Owner-Draw
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

// ===== �Ի�Բ�Ǻڵ����̲˵� =====
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
        // Բ������ȷ�� 6 �� int ������
        RECT rc{}; GetClientRect(hWnd, &rc);
        HRGN rgn = CreateRoundRectRgn(
            (int)rc.left, (int)rc.top, (int)rc.right, (int)rc.bottom,
            (int)(kTrayRadius * 2), (int)(kTrayRadius * 2));
        SetWindowRgn(hWnd, rgn, FALSE); // rgn �ɴ��ڽӹ�
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
        // ͨ��ԭ�ӱ����ش�ѡ����
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
        // ����������Բ��
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
            FontFamily ff(L"΢���ź�");
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

// ͬ����ʾ�Ի����̲˵�������ѡ������� ID��0 ��ʾȡ����
static UINT ShowTrayMenu() {
    EnsureTrayMenuClass();
    auto* items = new std::vector<TrayMenuItem>{
        { ID_TRAY_RESTORE, L"��ԭ����" },
        { ID_TRAY_EXIT,    L"�˳�����" }
    };
    POINT pt{}; GetCursorPos(&pt);
    int w = kTrayWidth;
    int h = (int)items->size() * kTrayItemHeight;
    // ��Ļ��Ե����
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
    // ������Ϣѭ��ֱ����������
    UINT ret = 0;
    MSG msg;
    while (IsWindow(wnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // ��ԭ�ӱ���ȡ������
    ret = s_trayMenuCmd.exchange(0, std::memory_order_relaxed);
    return ret;
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