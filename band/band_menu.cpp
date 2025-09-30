#include "band_menu.h"
#include "band_filter.h" // 复用过滤菜单项
#include <gdiplus.h>
#include <algorithm>
#include <cwctype>
#include <chrono>
#include <atomic>

using namespace Gdiplus;

// ===== 颜色输入对话框 & 颜色解析 =====
static bool SetGlowColorFromHex(const std::wstring& hex) {
    if (hex.size() != 6) return false;
    for (wchar_t c : hex) if (!iswxdigit(c)) return false;
    auto hexToByte = [](std::wstring const& s)->BYTE {
        return (BYTE)wcstol(s.c_str(), nullptr, 16);
    };
    BYTE r = hexToByte(hex.substr(0, 2));
    BYTE g = hexToByte(hex.substr(2, 2));
    BYTE b = hexToByte(hex.substr(4, 2));
    g_glowColorR = r; g_glowColorG = g; g_glowColorB = b;
    g_userColorOverride = true;
    return true;
}

static LRESULT CALLBACK ColorInputWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    static HWND hEdit = nullptr;
    switch (m) {
    case WM_CREATE: {
        // 动态布局（与过滤地址窗口方式一致，避免底部裁剪）
        const int padding = 10;
        const int labelH  = 18;
        const int editH   = 22;
        const int btnH    = 26;
        RECT crc{}; GetClientRect(w, &crc);
        int clientW = crc.right - crc.left;
        int clientH = crc.bottom - crc.top;
        int labelY = padding;
        int editY  = labelY + labelH + 4;
        int btnY   = clientH - padding - btnH;
        if (btnY < editY + editH + 8) btnY = editY + editH + 8;

        CreateWindowExW(0, L"STATIC", L"输入颜色编码 (例:39C5BB)",
                        WS_CHILD | WS_VISIBLE,
                        padding, labelY, clientW - padding * 2, labelH,
                        w, (HMENU)100, nullptr, nullptr);

        hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                padding, editY, clientW - padding * 2, editH,
                                w, (HMENU)101, nullptr, nullptr);
        SendMessageW(hEdit, EM_SETLIMITTEXT, 6, 0);

        int gap = 14;
        int btnW = 70;
        int totalBtnW = btnW * 2 + gap;
        int startX = (clientW - totalBtnW) / 2;
        if (startX < padding) startX = padding;

        CreateWindowExW(0, L"BUTTON", L"确定",
                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        startX, btnY, btnW, btnH, w, (HMENU)1, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消",
                        WS_CHILD | WS_VISIBLE,
                        startX + btnW + gap, btnY, btnW, btnH, w, (HMENU)2, nullptr, nullptr);

        SetFocus(hEdit);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == 1) {
            wchar_t buf[16]{}; GetWindowTextW(hEdit, buf, 16);
            std::wstring s(buf);
            if (!s.empty() && s[0] == L'#') s.erase(0, 1);
            std::transform(s.begin(), s.end(), s.begin(), ::towupper);
            if (SetGlowColorFromHex(s)) {
                AppendLog(L"[UI] GlowColor -> #" + s);
                if (g_mainWnd) PostMessageW(g_mainWnd, WM_APP_REFRESH, 0, 0);
                DestroyWindow(w);
            } else {
                MessageBoxW(w, L"无效的 6 位十六进制颜色", L"错误", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        if (id == 2) { DestroyWindow(w); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(w); return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static void ShowColorInputDialog(HWND owner) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = ColorInputWndProc;
        wc.hInstance   = GetModuleHandleW(nullptr);
        wc.hCursor     = LoadCursor(nullptr, IDC_IBEAM);
        wc.lpszClassName = L"GlowColorInputWnd";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
        reg = true;
    }
    // 期望客户区大小
    int clientW = 230;
    int clientH = 120; // 比原来略高，留出按钮安全区
    RECT wr{ 0,0,clientW,clientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION, FALSE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
    int w = wr.right - wr.left;
    int h = wr.bottom - wr.top;

    RECT prc{};
    if (owner) GetWindowRect(owner, &prc);
    int x = owner ? prc.left + (prc.right - prc.left - w) / 2 : CW_USEDEFAULT;
    int y = owner ? prc.top + (prc.bottom - prc.top - h) / 2 : CW_USEDEFAULT;

    HWND win = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                               L"GlowColorInputWnd", L"变更心跳颜色",
                               WS_POPUP | WS_CAPTION,
                               x, y, w, h,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (win) { ShowWindow(win, SW_SHOW); UpdateWindow(win); }
}

// ===== Owner Draw 通用绘制 =====
static void DrawOwnerMenuItem(LPDRAWITEMSTRUCT ds,
                              const std::wstring& text,
                              bool selected) {
    Graphics g(ds->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    RectF rc((REAL)ds->rcItem.left, (REAL)ds->rcItem.top,
             (REAL)(ds->rcItem.right - ds->rcItem.left),
             (REAL)(ds->rcItem.bottom - ds->rcItem.top));
    // 纯色块：选中更亮，未选中半透明
    Color bg = selected ? Color(255, 180, 70, 90)  // 选中
                        : Color(255, 70, 70, 95);  // 未选
    SolidBrush b(bg);
    g.FillRectangle(&b, rc);
    // 不绘制边框（去除任何边缘线条）

    FontFamily ff(L"微软雅黑");
    Font ft(&ff, 14.f, FontStyleBold, UnitPixel);
    SolidBrush txtBrush(Color(255, 255, 255, 255));
    RectF textRc(rc.X + 12.f, rc.Y, rc.Width - 14.f, rc.Height);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text.c_str(), -1, &ft, textRc, &sf, &txtBrush);
}

// ===== 菜单接口实现 =====
void InitMenuSubsystem() {
    // 目前仅依赖懒注册，不需要预处理
}

void ShowContextMenu(HWND owner, POINT screenPt) {
    HMENU hMenu = CreatePopupMenu();
    AddFilterMenuItem(hMenu); // 过滤菜单
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_OWNERDRAW, ID_POP_CHANGE_COLOR, L"变更心跳颜色");
    // 去掉所有系统分隔符，保证纯色连续
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_OWNERDRAW, ID_POP_EXIT, L"退出");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_OWNERDRAW, ID_POP_TOGGLE_TOP, L"切换置顶");

    SetForegroundWindow(owner);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN,
                   screenPt.x, screenPt.y, 0, owner, nullptr);
    DestroyMenu(hMenu);
}

bool HandleMenuMeasure(LPMEASUREITEMSTRUCT mis) {
    if (mis->CtlType != ODT_MENU) return false;
    // 先让过滤模块处理
    if (HandleFilterMeasureItem(mis)) return true;

    if (mis->itemID == ID_POP_EXIT ||
        mis->itemID == ID_POP_TOGGLE_TOP ||
        mis->itemID == ID_POP_CHANGE_COLOR) {
        mis->itemHeight = 34;
        mis->itemWidth  = 160;
        return true;
    }
    return false;
}

bool HandleMenuDraw(LPDRAWITEMSTRUCT ds) {
    if (ds->CtlType != ODT_MENU) return false;
    if (HandleFilterDrawItem(ds)) return true;

    std::wstring text;
    if (ds->itemID == ID_POP_CHANGE_COLOR) text = L"变更心跳颜色";
    else if (ds->itemID == ID_POP_EXIT) text = L"退出程序";
    else if (ds->itemID == ID_POP_TOGGLE_TOP) text = g_alwaysOnTop ? L"取消置顶" : L"设为置顶";
    else return false;

    bool sel = (ds->itemState & ODS_SELECTED) != 0;
    DrawOwnerMenuItem(ds, text, sel);
    return true;
}

bool HandleMenuCommand(HWND owner, WORD id) {
    switch (id) {
    case ID_POP_SET_FILTER:
        if (HandleFilterCommand(id, owner)) return true;
        return true;
    case ID_POP_CHANGE_COLOR:
        ShowColorInputDialog(owner);
        return true;
    case ID_POP_EXIT:
        AppendLog(L"[UI] 菜单退出");
        PostMessageW(owner, WM_CLOSE, 0, 0);
        return true;
    case ID_POP_TOGGLE_TOP:
        g_alwaysOnTop = !g_alwaysOnTop;
        SetWindowPos(owner,
                     g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        AppendLog(std::wstring(L"[UI] 置顶状态 -> ") + (g_alwaysOnTop ? L"ON" : L"OFF"));
        return true;
    default:
        break;
    }
    return false;
}