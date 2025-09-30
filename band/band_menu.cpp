#include "band_menu.h"
#include "band_filter.h" // ���ù��˲˵���
#include <gdiplus.h>
#include <algorithm>
#include <cwctype>
#include <chrono>
#include <atomic>
#include <windowsx.h> // �ṩ GET_X_LPARAM / GET_Y_LPARAM�����Ѱ����ɺ��ԣ�

#ifndef GET_Y_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

using namespace Gdiplus;

// ===== ��ɫ����Ի��� & ��ɫ���� =====
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
        // ��̬���֣�����˵�ַ���ڷ�ʽһ�£�����ײ��ü���
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

        CreateWindowExW(0, L"STATIC", L"������ɫ���� (��:39C5BB)",
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

        CreateWindowExW(0, L"BUTTON", L"ȷ��",
                        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                        startX, btnY, btnW, btnH, w, (HMENU)1, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"ȡ��",
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
                MessageBoxW(w, L"��Ч�� 6 λʮ��������ɫ", L"����", MB_OK | MB_ICONERROR);
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
    // �����ͻ�����С
    int clientW = 230;
    int clientH = 120; // ��ԭ���Ըߣ�������ť��ȫ��
    RECT wr{ 0,0,clientW,clientH };
    AdjustWindowRectEx(&wr, WS_POPUP | WS_CAPTION, FALSE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
    int w = wr.right - wr.left;
    int h = wr.bottom - wr.top;

    RECT prc{};
    if (owner) GetWindowRect(owner, &prc);
    int x = owner ? prc.left + (prc.right - prc.left - w) / 2 : CW_USEDEFAULT;
    int y = owner ? prc.top + (prc.bottom - prc.top - h) / 2 : CW_USEDEFAULT;

    HWND win = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                               L"GlowColorInputWnd", L"���������ɫ",
                               WS_POPUP | WS_CAPTION,
                               x, y, w, h,
                               owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (win) { ShowWindow(win, SW_SHOW); UpdateWindow(win); }
}

// ===== Owner Draw ͨ�û��� =====
static void DrawOwnerMenuItem(LPDRAWITEMSTRUCT ds,
                              const std::wstring& text,
                              bool selected) {
    Graphics g(ds->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    RectF rc((REAL)ds->rcItem.left, (REAL)ds->rcItem.top,
             (REAL)(ds->rcItem.right - ds->rcItem.left),
             (REAL)(ds->rcItem.bottom - ds->rcItem.top));
    // ��ɫ�飺ѡ�и�����δѡ�а�͸��
    Color bg = selected ? Color(255, 180, 70, 90)  // ѡ��
                        : Color(255, 70, 70, 95);  // δѡ
    SolidBrush b(bg);
    g.FillRectangle(&b, rc);
    // �����Ʊ߿�ȥ���κα�Ե������

    FontFamily ff(L"΢���ź�");
    Font ft(&ff, 14.f, FontStyleBold, UnitPixel);
    SolidBrush txtBrush(Color(255, 255, 255, 255));
    RectF textRc(rc.X + 12.f, rc.Y, rc.Width - 14.f, rc.Height);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(text.c_str(), -1, &ft, textRc, &sf, &txtBrush);
}

// ===== �˵��ӿ�ʵ�� =====
void InitMenuSubsystem() {
    // Ŀǰ��������ע�ᣬ����ҪԤ����
}

void ShowContextMenu(HWND owner, POINT screenPt) {
    ShowCustomContextMenu(owner, screenPt);
}

bool HandleMenuMeasure(LPMEASUREITEMSTRUCT mis) {
    if (mis->CtlType != ODT_MENU) return false;
    // ���ù���ģ�鴦��
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
    if (ds->itemID == ID_POP_CHANGE_COLOR) text = L"���������ɫ";
    else if (ds->itemID == ID_POP_EXIT) text = L"�˳�����";
    else if (ds->itemID == ID_POP_TOGGLE_TOP) text = g_alwaysOnTop ? L"ȡ���ö�" : L"��Ϊ�ö�";
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
        AppendLog(L"[UI] �˵��˳�");
        PostMessageW(owner, WM_CLOSE, 0, 0);
        return true;
    case ID_POP_TOGGLE_TOP:
        g_alwaysOnTop = !g_alwaysOnTop;
        SetWindowPos(owner,
                     g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        AppendLog(std::wstring(L"[UI] �ö�״̬ -> ") + (g_alwaysOnTop ? L"ON" : L"OFF"));
        return true;
    default:
        break;
    }
    return false;
}

// ================= �Զ����ޱ߿�˵�ʵ�� =================
struct MenuItemDef {
    UINT id;
    std::wstring text;
};

static const int kMenuItemHeight = 34;
static const int kMenuWidth      = 200;
static const int kMenuPadX       = 12;
static const wchar_t* kMenuWndClass = L"HrCtxMenuWnd";

static std::vector<MenuItemDef> BuildMenuItems() {
    std::vector<MenuItemDef> v;
    v.push_back({ ID_POP_SET_FILTER,  g_filterAddr.load() ? L"���ù��˵�ַ(������)" : L"���ù��˵�ַ" });
    v.push_back({ ID_POP_CHANGE_COLOR, L"���������ɫ" });
    v.push_back({ ID_POP_TOGGLE_TOP,   g_alwaysOnTop ? L"ȡ���ö�" : L"��Ϊ�ö�" });
    v.push_back({ ID_POP_EXIT,         L"�˳�����" });
    return v;
}

static LRESULT CALLBACK MenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* items = reinterpret_cast<std::vector<MenuItemDef>*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    static int hotIndex = -1;
    switch (msg) {
    case WM_NCCALCSIZE:
        return 0; // �ޱ߿�����
    case WM_CREATE: {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        SetCapture(hWnd); // ������������ⲿ����ر�
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!items) break;
        int y = GET_Y_LPARAM(lParam);
        int idx = y / kMenuItemHeight;
        if (idx < 0 || idx >= (int)items->size()) idx = -1;
        if (idx != hotIndex) {
            hotIndex = idx;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!items) { DestroyWindow(hWnd); return 0; }
        int y = GET_Y_LPARAM(lParam);
        int idx = y / kMenuItemHeight;
        if (idx >= 0 && idx < (int)items->size()) {
            UINT id = (*items)[idx].id;
            HWND owner = GetParent(hWnd);
            DestroyWindow(hWnd);
            if (owner) HandleMenuCommand(owner, (WORD)id);
        } else {
            DestroyWindow(hWnd);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }
    case WM_CAPTURECHANGED: {
        // �������ת�Ƶ��ⲿ -> �ر�
        if ((HWND)lParam != hWnd) {
            DestroyWindow(hWnd);
        }
        return 0;
    }
    case WM_KILLFOCUS:
        DestroyWindow(hWnd); return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        RECT rc; GetClientRect(hWnd, &rc);
        // ������ɫ
        SolidBrush bg(Color(230, 40, 40, 40));
        g.FillRectangle(&bg,
                        (INT)rc.left,
                        (INT)rc.top,
                        (INT)(rc.right - rc.left),
                        (INT)(rc.bottom - rc.top));

        if (items) {
            FontFamily ff(L"΢���ź�");
            Font ft(&ff, 14.f, FontStyleBold, UnitPixel);
            SolidBrush txt(Color(255, 255, 255, 255));
            for (size_t i = 0; i < items->size(); ++i) {
                int top = (int)i * kMenuItemHeight;
                bool hot = ((int)i == hotIndex);
                if (hot) {
                    SolidBrush hb(Color(255, 180, 70, 90));
                    g.FillRectangle(&hb, (INT)0, (INT)top, (INT)kMenuWidth, (INT)kMenuItemHeight);
                } else {
                    SolidBrush nb(Color(255, 70, 70, 95));
                    g.FillRectangle(&nb, (INT)0, (INT)top, (INT)kMenuWidth, (INT)kMenuItemHeight);
                }
                RectF tr((REAL)kMenuPadX, (REAL)top, (REAL)(kMenuWidth - kMenuPadX * 2), (REAL)kMenuItemHeight);
                StringFormat sf;
                sf.SetAlignment(StringAlignmentNear);
                sf.SetLineAlignment(StringAlignmentCenter);
                g.DrawString((*items)[i].text.c_str(), -1, &ft, tr, &sf, &txt);
            }
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (items) {
            delete items;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
        ReleaseCapture();
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void EnsureMenuClass() {
    static bool reg = false;
    if (reg) return;
    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc = MenuWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kMenuWndClass;
    wc.hbrBackground = nullptr;
    wc.style = CS_DROPSHADOW; // ��ȥ����Ӱ����Ҳ������Ӱ��ע��
    RegisterClassExW(&wc);
    reg = true;
}

void ShowCustomContextMenu(HWND owner, POINT screenPt) {
    EnsureMenuClass();
    auto* items = new std::vector<MenuItemDef>(BuildMenuItems());
    int h = (int)items->size() * kMenuItemHeight;
    // �����ޱ߿򴰿�
    HWND wnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kMenuWndClass, L"",
        WS_POPUP,
        screenPt.x, screenPt.y, kMenuWidth, h,
        owner, nullptr, GetModuleHandleW(nullptr), items);
    if (!wnd) {
        delete items;
        return;
    }
    ShowWindow(wnd, SW_SHOWNOACTIVATE);
    UpdateWindow(wnd);
}