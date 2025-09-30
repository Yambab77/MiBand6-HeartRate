#include "band_filter.h"
#include <gdiplus.h>
#include <fstream>
#include <cwctype>
#include <vector>
#include <algorithm>

using namespace Gdiplus;

std::atomic<uint64_t> g_filterAddr{ 0 }; // 定义

// ---------- 工具 ----------
static std::wstring FormatBtAddrLocal(uint64_t a) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%02x:%02x:%02x:%02x:%02x:%02x",
        (unsigned)((a >> 40) & 0xFF),
        (unsigned)((a >> 32) & 0xFF),
        (unsigned)((a >> 24) & 0xFF),
        (unsigned)((a >> 16) & 0xFF),
        (unsigned)((a >> 8) & 0xFF),
        (unsigned)(a & 0xFF));
    return buf;
}

static std::wstring GetFilterFilePath() {
    return GetExeDir() + L"\\bt_filter.txt";
}

static bool ParseBtAddrString(const std::wstring& input, uint64_t& out) {
    std::wstring s;
    s.reserve(input.size());
    for (wchar_t c : input) {
        if (iswxdigit(c)) s.push_back((wchar_t)towlower(c));
    }
    if (s.size() != 12) return false;
    uint64_t v = 0;
    for (size_t i = 0; i < 12; i += 2) {
        int byteVal = (int)wcstol(s.substr(i, 2).c_str(), nullptr, 16);
        if (byteVal < 0 || byteVal > 255) return false;
        v = (v << 8) | (uint64_t)byteVal;
    }
    out = v;
    return v != 0;
}

static void SaveFilter(uint64_t v) {
    try {
        std::wofstream ofs(GetFilterFilePath(), std::ios::trunc);
        if (!ofs) return;
        ofs << FormatBtAddrLocal(v) << L"\n";
    } catch (...) {}
}

static void DeleteFilterFile() {
    _wremove(GetFilterFilePath().c_str());
}

// ---------- 对话框 ----------
static LRESULT CALLBACK FilterInputWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    static HWND hEdit = nullptr;
    switch (m) {
    case WM_CREATE: {
            // 动态布局，避免因不同窗口非客户区高度导致按钮被裁剪
            const int padding = 10;
        const int labelH = 18;
        const int editH = 22;
        const int btnH = 26;
        RECT crc{};
        GetClientRect(w, &crc);
        int clientW = crc.right - crc.left;
        int clientH = crc.bottom - crc.top;
            int labelY = padding;
        int editY = labelY + labelH + 4;
        int btnY = clientH - padding - btnH;  // 底部对齐
        if (btnY < editY + editH + 8) btnY = editY + editH + 8; // 最小间距保护
            CreateWindowExW(0, L"STATIC",
                L"输入蓝牙地址 (fa:cc:cb:6d:08:55)",
                WS_CHILD | WS_VISIBLE,
                padding, labelY, clientW - padding * 2, labelH,
                w, (HMENU)10, nullptr, nullptr);
            hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                padding, editY, clientW - padding * 2, editH,
                w, (HMENU)11, nullptr, nullptr);
        SendMessageW(hEdit, EM_SETLIMITTEXT, 32, 0);
            // 按钮横向布局
            int gap = 10;
        int btnW = 60;
        int totalBtnW = btnW * 3 + gap * 2;
        int startX = (clientW - totalBtnW) / 2;
        if (startX < padding) startX = padding;
        CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            startX, btnY, btnW, btnH, w, (HMENU)1, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"清除",
            WS_CHILD | WS_VISIBLE,
            startX + btnW + gap, btnY, btnW, btnH, w, (HMENU)2, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE,
            startX + (btnW + gap) * 2, btnY, btnW, btnH, w, (HMENU)3, nullptr, nullptr);
        if (g_filterAddr.load()) {
            auto cur = FormatBtAddrLocal(g_filterAddr.load());
            SetWindowTextW(hEdit, cur.c_str());
        }
        SetFocus(hEdit);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == 1) { // OK
            wchar_t buf[64]{}; GetWindowTextW(hEdit, buf, 63);
            uint64_t v;
            if (ParseBtAddrString(buf, v)) {
                g_filterAddr.store(v, std::memory_order_relaxed);
                SaveFilter(v);
                AppendLog(L"[FILTER] 设置过滤地址: " + FormatBtAddrLocal(v));
                DestroyWindow(w);
            }
            else {
                MessageBoxW(w, L"无效地址，需 12 个十六进制字符，可含冒号", L"错误", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        if (id == 2) { // 清除
            g_filterAddr.store(0, std::memory_order_relaxed);
            DeleteFilterFile();
            AppendLog(L"[FILTER] 过滤已清除");
            DestroyWindow(w);
            return 0;
        }
        if (id == 3) { DestroyWindow(w); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(w); return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

void ShowFilterDialog(HWND owner) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = FilterInputWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
        wc.lpszClassName = L"BtFilterInputWnd";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
        reg = true;
    }
       // 期望客户区大小
       int clientW = 260;
    int clientH = 130; // 扩大客户区高度以留足按钮空间
    RECT wr{ 0,0,clientW,clientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION, FALSE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
    int w = wr.right - wr.left;
    int h = wr.bottom - wr.top;
    
       RECT orc{};
    if (owner) GetWindowRect(owner, &orc);
    int x = owner ? orc.left + (orc.right - orc.left - w) / 2 : CW_USEDEFAULT;
    int y = owner ? orc.top + (orc.bottom - orc.top - h) / 2 : CW_USEDEFAULT;
    HWND win = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            L"BtFilterInputWnd", L"设置过滤地址",
            WS_POPUP | WS_CAPTION,
            x, y, w, h,
            owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (win) { ShowWindow(win, SW_SHOW); UpdateWindow(win); }
}

// ---------- 接口实现 ----------
void InitFilter() {
    std::wifstream ifs(GetFilterFilePath());
    if (!ifs) return;
    std::wstring line;
    std::getline(ifs, line);
    uint64_t v;
    if (ParseBtAddrString(line, v)) {
        g_filterAddr.store(v, std::memory_order_relaxed);
        AppendLog(L"[FILTER] 加载过滤地址: " + FormatBtAddrLocal(v));
    }
    else {
        AppendLog(L"[FILTER] 过滤文件无效，忽略");
    }
}

bool ShouldAcceptAddress(uint64_t addr) {
    uint64_t f = g_filterAddr.load(std::memory_order_relaxed);
    return f == 0 || f == addr;
}

// ---------- 菜单集成 ----------
void AddFilterMenuItem(HMENU hMenu) {
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_OWNERDRAW, ID_POP_SET_FILTER, L"[filter]");
}

bool HandleFilterMeasureItem(LPMEASUREITEMSTRUCT mis) {
    if (mis->CtlType != ODT_MENU || mis->itemID != ID_POP_SET_FILTER) return false;
    mis->itemHeight = 34;
    mis->itemWidth = 180;
    return true;
}

bool HandleFilterDrawItem(LPDRAWITEMSTRUCT ds) {
    if (ds->CtlType != ODT_MENU || ds->itemID != ID_POP_SET_FILTER) return false;
    std::wstring text = g_filterAddr.load() ? L"设置过滤地址(已启用)" : L"设置过滤地址";
    Graphics g(ds->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    RectF rc((REAL)ds->rcItem.left, (REAL)ds->rcItem.top,
        (REAL)(ds->rcItem.right - ds->rcItem.left),
        (REAL)(ds->rcItem.bottom - ds->rcItem.top));
    bool sel = (ds->itemState & ODS_SELECTED) != 0;
    Color bg = sel ? Color(255, 180, 70, 90) : Color(255, 70, 70, 95);
    SolidBrush b(bg);
    g.FillRectangle(&b, rc);
    FontFamily ff(L"微软雅黑");
    Font ft(&ff, 14.f, FontStyleBold, UnitPixel);
    SolidBrush txtBrush(Color(255, 255, 255, 255));
    RectF textRc(rc.X + 12.f, rc.Y, rc.Width - 14.f, rc.Height);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text.c_str(), -1, &ft, textRc, &sf, &txtBrush);
    return true;
}

bool HandleFilterCommand(WORD id, HWND owner) {
    if (id != ID_POP_SET_FILTER) return false;
    ShowFilterDialog(owner);
    return true;
}