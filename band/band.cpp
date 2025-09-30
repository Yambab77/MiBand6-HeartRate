#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <deque>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <io.h>
#include <fcntl.h>
#include <cmath>
#include <objidl.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Storage.Streams.h>
#include "Resource.h"
#include "bandfunctions.h"
#include "band_filter.h"
#include "band_menu.h"

#pragma comment(lib, "gdiplus.lib")

// ========== 控制台开关 ==========
// 设为 1 保留原控制台输出, 设为 0 隐藏控制台仅写日志文件
#ifndef ENABLE_CONSOLE
#define ENABLE_CONSOLE 0
#endif

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Storage::Streams;
using namespace Gdiplus;

// ======= 右键菜单相关 =======
#define ID_POP_EXIT          9001
#define ID_POP_TOGGLE_TOP    9002
#define ID_POP_CHANGE_COLOR  9003
bool g_alwaysOnTop = true; // 供 band_menu 使用

HWND g_mainWnd = nullptr;
std::atomic<int> g_heartRate(75);

static std::unique_ptr<Gdiplus::Image> g_bgImage;
static int g_winW = 400;
static int g_winH = 200;

const UINT WM_APP_REFRESH = WM_APP + 1;

std::mutex g_hrHistMutex;
std::deque<std::pair<std::chrono::steady_clock::time_point, int>> g_hrHistory;
std::atomic<int> g_hr5MinHi(75);
std::atomic<int> g_hr5MinLo(75);

static std::mutex g_logMutex;
static std::vector<std::string> g_logBuffer;
static std::atomic<bool> g_logRunning{ false };
static bool g_consoleEnabled = false;
static std::chrono::steady_clock::time_point g_startTime = std::chrono::steady_clock::now();
// 常量 PI（避免 MSVC 下 M_PI 未定义）
//constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kPi = 3.14;

POINT g_ptWndStart{};
POINT g_ptCursorStart{};
bool  g_bDragging = false;

// 广播过滤配置
static constexpr uint16_t kHuamiCompanyId = 0x0157;
static bool g_useMfrPrefixFilter = true;
static bool g_enableServiceUuidFilter = false; // 如需按 0x180D 服务 UUID 过滤可设 true
static int  g_minRssi = -85;
static std::atomic<int> g_lastHr{ -1 };
static std::chrono::steady_clock::time_point g_lastHrTs;

// GUID（仅用于可选 ServiceUuid 过滤）
constexpr GUID HR_SERVICE_UUID = { 0x0000180d,0x0000,0x1000,{0x80,0x00,0x00,0x80,0x5f,0x9b,0x34,0xfb} };

struct HrParseResult { int heartRate = 0; };
void AppendLog(const std::wstring& wline);

// ===== 心跳光斑状态 =====
std::atomic<double>  g_glowAlpha{ 0.0 };
std::atomic<double>  g_beatPeriodSec{ 1.0 };
std::atomic<int64_t> g_lastBeatNsForCycle{ 0 };
BYTE g_glowColorR = 255;
BYTE g_glowColorG = 0;
BYTE g_glowColorB = 0;
// 固定光斑位置（相对窗口的比例锚点），避免数字位数变化造成漂移
static bool  g_glowFixedPos      = true;
static float g_glowFixedXRatio   = 0.254f;  // 约等于 60/400
static float g_glowFixedYRatio   = 0.355f;  // 约窗口高度中部稍下
// 固定历史记录 hi/lo 位置（相对窗口），避免位数改变导致漂移
static bool  g_histFixedPos      = true;
static float g_histHiXRatio      = 0.6f;  // 可按贴图需求微调
static float g_histHiYRatio      = 0.6f;
static float g_histLoXRatio      = 0.6f;
static float g_histLoYRatio      = 0.82f;
// ---- Glow 视觉增强参数（可调）----
float g_glowAlphaBase = 120.f;   // 最低峰值 (原 60)
float g_glowAlphaRange = 135.f;  // 动态部分，使最大 ~255
float g_glowGamma = 0.60f;       // 亮度 Gamma (<1 提升中低亮度)
bool  g_glowDoubleLayer = true;  // 启用核心二次填充
// 随机颜色模式：默认开启；用户输入颜色后自动关闭随机
bool g_userColorOverride = false;
// 简单 RNG
uint32_t g_rngState = (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
static float g_hrBottomMargin = 2.0f; // 原为固定 12.0f

// 解析 6 位 hex RGB
static bool SetGlowColorFromHex(const std::wstring& hex) {
    if (hex.size() != 6) return false;
    auto h = hex;
    for (wchar_t c : h) {
        if (!iswxdigit(c)) return false;
    }
    auto hexToByte = [](const std::wstring& s)->BYTE {
        return (BYTE)std::wcstol(s.c_str(), nullptr, 16);
    };
    BYTE r = hexToByte(h.substr(0, 2));
    BYTE g = hexToByte(h.substr(2, 2));
    BYTE b = hexToByte(h.substr(4, 2));
    g_glowColorR = r; g_glowColorG = g; g_glowColorB = b;
    g_userColorOverride = true; // 用户指定后停止随机
    return true;
}

// 简易输入窗口（无资源）
static LRESULT CALLBACK ColorInputWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    static HWND hEdit = nullptr;
    switch (m) {
    case WM_CREATE: {
        CreateWindowExW(0, L"STATIC", L"输入颜色编码，如:39C5BB",
            WS_CHILD | WS_VISIBLE, 10, 10, 200, 18, w, (HMENU)100, nullptr, nullptr);
        hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 10, 32, 180, 22, w, (HMENU)101, nullptr, nullptr);
        SendMessageW(hEdit, EM_SETLIMITTEXT, 6, 0);
        CreateWindowExW(0, L"BUTTON", L"确定",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 40, 90, 60, 26, w, (HMENU)1, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消",
            WS_CHILD | WS_VISIBLE, 120, 90, 60, 26, w, (HMENU)2, nullptr, nullptr);
        SetFocus(hEdit);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == 1) { // OK
            wchar_t buf[16]{};
            GetWindowTextW(hEdit, buf, 16);
            std::wstring s(buf);
            // 去掉可能的 '#' 前缀
            if (!s.empty() && s[0] == L'#') s.erase(0, 1);
            // 转大写
            std::transform(s.begin(), s.end(), s.begin(), ::towupper);
            if (SetGlowColorFromHex(s)) {
                AppendLog(L"[UI] GlowColor -> #" + s);
                if (g_mainWnd) PostMessage(g_mainWnd, WM_APP_REFRESH, 0, 0);
                DestroyWindow(w);
            } else {
                MessageBoxW(w, L"无效的 6 位十六进制颜色", L"错误", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        if (id == 2) { DestroyWindow(w); return 0; }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(w); return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

static void ShowColorInputDialog(HWND owner) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = ColorInputWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
        wc.lpszClassName = L"GlowColorInputWnd";
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassExW(&wc);
        reg = true;
    }
    RECT rc{};
    if (owner) GetWindowRect(owner, &rc);
    int w = 215, h = 170;
    int x = rc.left + (rc.right - rc.left - w) / 2;
    int y = rc.top + (rc.bottom - rc.top - h) / 2;
    HWND win = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"GlowColorInputWnd", L"变更心跳颜色",
        WS_POPUP | WS_CAPTION,
        x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (win) {
        ShowWindow(win, SW_SHOW);
        UpdateWindow(win);
    }
}

static void DebugPrint(const std::wstring& s) {
    OutputDebugStringW((s + L"\n").c_str());
}
static std::wstring GetTimestampW() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    struct tm tmv {};
    localtime_s(&tmv, &t);
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d-%02d-%02d %02d:%02d:%02d.%03d",
        tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)ms.count());
    return buf;
}
static std::string Narrow(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), len, nullptr, nullptr);
    return out;
}
static std::wstring FormatBtAddr(uint64_t a) {
    std::wstringstream ss;
    ss << std::hex << std::setfill(L'0')
        << std::setw(2) << ((a >> 40) & 0xFF) << L":"
        << std::setw(2) << ((a >> 32) & 0xFF) << L":"
        << std::setw(2) << ((a >> 24) & 0xFF) << L":"
        << std::setw(2) << ((a >> 16) & 0xFF) << L":"
        << std::setw(2) << ((a >> 8) & 0xFF) << L":"
        << std::setw(2) << (a & 0xFF);
    return ss.str();
}
static std::wstring Hex(const std::vector<uint8_t>& v) {
    std::wstringstream ss; ss << std::hex << std::setfill(L'0');
    for (size_t i = 0; i < v.size(); ++i) {
        ss << std::setw(2) << (int)v[i];
        if (i + 1 < v.size()) ss << L' ';
    }
    return ss.str();
}
void AppendLog(const std::wstring& wline) {
    std::wstring line = GetTimestampW() + L" | " + wline;
    if (g_consoleEnabled) {
        wprintf(L"%s\n", line.c_str());
    } else {
        // 可选：同时输出到调试器 (DebugView / VS Output)
        OutputDebugStringW((line + L"\n").c_str());
    }
    std::lock_guard<std::mutex> lk(g_logMutex);
    g_logBuffer.emplace_back(Narrow(line));
}
std::wstring GetExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}
static void FlushLogToFile() {
    std::vector<std::string> swap;
    {
        std::lock_guard<std::mutex> lk(g_logMutex);
        if (g_logBuffer.empty()) return;
        swap.swap(g_logBuffer);
    }
    try {
        std::ofstream ofs(Narrow(GetExeDir() + L"\\heart_rate_log.txt"),
            std::ios::app | std::ios::binary);
        if (!ofs) return;
        for (auto& l : swap) ofs << l << "\n";
    }
    catch (...) {}
}

static void StartLogFlushThread() {
    if (g_logRunning.exchange(true)) return;
    std::thread([] {
        while (g_logRunning.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            FlushLogToFile();
        }
    }).detach();
}

// 广播监听（仅广播模式）
void StartFilteredBroadcastWatcher() {
    winrt::init_apartment();
    BluetoothLEAdvertisementWatcher watcher;
    watcher.ScanningMode(BluetoothLEScanningMode::Active);

    if (g_enableServiceUuidFilter) {
        watcher.AdvertisementFilter().Advertisement().ServiceUuids().Append(HR_SERVICE_UUID);
        AppendLog(L"[ADV] 启用 Service UUID 过滤 0x180D");
    }
    if (g_useMfrPrefixFilter) {
        BluetoothLEManufacturerData md(kHuamiCompanyId, MakeBuffer({ 0x02,0x02,0x01 }));
        watcher.AdvertisementFilter().Advertisement().ManufacturerData().Append(md);
        AppendLog(L"[ADV] 启用厂商前缀过滤 0157 + 02 02 01");
    }
    else {
        BluetoothLEManufacturerData md(kHuamiCompanyId, MakeBuffer({}));
        watcher.AdvertisementFilter().Advertisement().ManufacturerData().Append(md);
        AppendLog(L"[ADV] 启用厂商 ID 过滤 0157 (无前缀)");
    }

    // 信号过滤
    {
        // helper: ms -> TimeSpan (100ns ticks)
        auto Ms = [](int ms)->Windows::Foundation::TimeSpan {
            return Windows::Foundation::TimeSpan{ static_cast<int64_t>(ms) * 10000 };
            };
        using namespace Windows::Foundation;
        auto sf = watcher.SignalStrengthFilter();
        sf.InRangeThresholdInDBm(g_minRssi);
        sf.OutOfRangeThresholdInDBm(g_minRssi - 5);
        
        // helper: chrono -> IReference<TimeSpan>
        auto BoxMs = [](int ms)->IReference<TimeSpan>
         {
            TimeSpan ts = std::chrono::milliseconds(ms); // C++/WinRT 支持 chrono 转换
            return winrt::box_value(ts).as<IReference<TimeSpan>>();
         };
        sf.OutOfRangeTimeout(BoxMs(4000));
        sf.SamplingInterval(BoxMs(200));
    }

    watcher.Received([](auto const&, BluetoothLEAdvertisementReceivedEventArgs const& args) {
        // 地址过滤（若已设置只接受指定地址）
        if (!ShouldAcceptAddress(args.BluetoothAddress())) return;
        int16_t rssi = args.RawSignalStrengthInDBm();
        if (rssi < g_minRssi) return;

        int hrCandidate = -1;
        bool from180D = false;

        // 尝试标准 Service Data
        for (auto const& sec : args.Advertisement().DataSections()) {
            if (sec.DataType() != 0x16) continue;
            auto raw = BufferToVec(sec.Data());
            if (raw.size() < 4) continue;
            uint16_t uuid16 = raw[0] | (raw[1] << 8);
            if (uuid16 != 0x180D) continue;
            uint8_t flags = raw[2];
            size_t idx = 3;
            if (idx >= raw.size()) continue;
            int hr = (flags & 0x01) == 0 ? raw[idx] :
                (idx + 1 < raw.size() ? (raw[idx] | (raw[idx + 1] << 8)) : -1);
            if (hr >= 10 && hr <= 250) {
                hrCandidate = hr;
                from180D = true;
                break;
            }
        }
        // 厂商数据
        if (hrCandidate < 0) {
            for (auto const& sec : args.Advertisement().DataSections()) {
                if (sec.DataType() != 0xFF) continue;
                auto mfr = BufferToVec(sec.Data());
                int hrM;
                if (ParseHuamiMfrHeartRate(mfr, hrM)) {
                    hrCandidate = hrM;
                    break;
                }
            }
        }
        if (hrCandidate < 0) return;

        // 刷新心跳频率参数 & 立即点亮光斑（真实广播到来触发一次“beat”）
        {
            using namespace std::chrono;
            auto now = steady_clock::now();
            double period = 60.0 / std::max(hrCandidate, 1);
            period = std::clamp(period, 0.3, 3.0);
            g_beatPeriodSec.store(period, std::memory_order_relaxed);
            g_lastBeatNsForCycle.store(duration_cast<nanoseconds>(now.time_since_epoch()).count(),
                                       std::memory_order_relaxed);
            g_glowAlpha.store(ComputePeakAlpha(hrCandidate), std::memory_order_relaxed);
            if (g_mainWnd) PostMessage(g_mainWnd, WM_APP_REFRESH, 0, 0);
        }

        // 数值/日志去抖（保持原策略，避免日志泛滥）
        {
            auto now = std::chrono::steady_clock::now();
            int last = g_lastHr.load();
            if (last == hrCandidate &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastHrTs).count() < 1000)
                return;
            g_lastHr = hrCandidate;
            g_lastHrTs = now;
        }

        g_heartRate = hrCandidate;
        UpdateHrHistory(hrCandidate);

        AppendLog(L"[ADV-HR] addr=" + FormatBtAddr(args.BluetoothAddress()) +
            L" rssi=" + std::to_wstring(rssi) +
            L" hr=" + std::to_wstring(hrCandidate) +
            (from180D ? L" src=0x180D" : L" src=MFR"));
        if (g_mainWnd) PostMessage(g_mainWnd, WM_APP_REFRESH, 0, 0);
        });

    watcher.Stopped([](auto const&, auto const&) {
        AppendLog(L"[ADV] Watcher stopped");
        });

    AppendLog(L"[ADV] Watcher start (仅广播模式)");
    watcher.Start();

    while (true) std::this_thread::sleep_for(std::chrono::seconds(10));
}

// UI 绘制
void DrawFancyText(HDC hdc, int hr) {
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    if (g_bgImage && g_bgImage->GetLastStatus() == Ok) {
        ImageAttributes attrs;
        attrs.SetWrapMode(WrapModeTileFlipXY);
        g.DrawImage(
            g_bgImage.get(),
            Gdiplus::Rect(0, 0, g_winW, g_winH),
            0, 0, g_bgImage->GetWidth(), g_bgImage->GetHeight(),
            UnitPixel,
            &attrs);
    }
    else {
        SolidBrush bg(Color(255, 32, 32, 32));
        g.FillRectangle(&bg, 0, 0, g_winW, g_winH);
    }
    std::wstring txt = std::to_wstring(hr);
    REAL fontSize = (REAL)std::max(12, g_winH / 3);
    FontFamily ff(L"微软雅黑");
    Font font(&ff, fontSize, FontStyleBold, UnitPixel);
    RectF layout;
    g.MeasureString(txt.c_str(), -1, &font, PointF(0, 0), &layout);
    PointF origin(22.0f, (REAL)g_winH - layout.Height - g_hrBottomMargin);

    // ==== 心形光斑（在数字上方）====
    if (hr > 0) {
        int rawA = (int)std::lround(g_glowAlpha.load(std::memory_order_relaxed));
        if (rawA > 0) {
            // Gamma 提升中低亮度
            double na = std::clamp(rawA / 255.0, 0.0, 1.0);
            na = std::pow(na, g_glowGamma);
            int boosted = (int)std::lround(na * 255.0);
            if (boosted < 0) boosted = 0;
            if (boosted > 255) boosted = 255;
            BYTE a = (BYTE)boosted;
            // 心形中心位置：数字中心上方 offset
            // 左移 5 像素
            REAL cx;
            REAL cy;
            if (g_glowFixedPos) {
                cx = (REAL)(g_winW * g_glowFixedXRatio);
                cy = (REAL)(g_winH * g_glowFixedYRatio);
            } else {
                // 原随文本居中逻辑（保留以便需要时切换回）
                cx = origin.X + layout.Width * 0.5f - 3.0f;
                cy = origin.Y - fontSize * 0.35f;
            }
            REAL s = fontSize * 2.0f;             // 尺寸系数 (原 1.08 * 2)

            GraphicsPath heart;
            // 使用两段 Bezier 构造对称心形
            heart.StartFigure();
            heart.AddBezier(
                PointF(cx, cy + s * 0.35f),
                PointF(cx + s * 0.35f, cy + s * 0.05f),
                PointF(cx + s * 0.55f, cy - s * 0.25f),
                PointF(cx, cy - s * 0.48f));
            heart.AddBezier(
                PointF(cx, cy - s * 0.48f),
                PointF(cx - s * 0.55f, cy - s * 0.25f),
                PointF(cx - s * 0.35f, cy + s * 0.05f),
                PointF(cx, cy + s * 0.35f));
            heart.CloseFigure();

            PathGradientBrush pgb(&heart);
            // 中心亮红，外圈透明红
            Color center(a, g_glowColorR, g_glowColorG, g_glowColorB);
            Color surround(0, g_glowColorR, g_glowColorG, g_glowColorB);
            pgb.SetCenterColor(center);
            int count = 1;
            pgb.SetSurroundColors(&surround, &count);

            // 轻微缩放膨胀随波形（将 wave 复用：用 Alpha 反推 0~1）
            REAL scale = 1.0f + (a / 255.0f) * 0.08f; // 最高放大 8%
            Matrix m;
            m.Translate(-cx, -cy);
            m.Scale(scale, scale);
            m.Translate(cx, cy);
            pgb.SetTransform(&m);

            g.FillPath(&pgb, &heart);

            // 内核加亮（可选）
            if (g_glowDoubleLayer) {
                REAL coreScale = 0.55f; // 核心尺寸比例
                GraphicsPath core;
                core.StartFigure();
                core.AddBezier(
                    PointF(cx, cy + s * 0.35f * coreScale),
                    PointF(cx + s * 0.35f * coreScale, cy + s * 0.05f * coreScale),
                    PointF(cx + s * 0.55f * coreScale, cy - s * 0.25f * coreScale),
                    PointF(cx, cy - s * 0.48f * coreScale));
                core.AddBezier(
                    PointF(cx, cy - s * 0.48f * coreScale),
                    PointF(cx - s * 0.55f * coreScale, cy - s * 0.25f * coreScale),
                    PointF(cx - s * 0.35f * coreScale, cy + s * 0.05f * coreScale),
                    PointF(cx, cy + s * 0.35f * coreScale));
                core.CloseFigure();
                // 核心始终高亮（Alpha 取 max(a, 200)）
                BYTE coreA = (BYTE)std::min(255, std::max<int>(200, a + 40));
                SolidBrush coreBrush(Color(coreA, g_glowColorR, g_glowColorG, g_glowColorB));
                g.FillPath(&coreBrush, &core);
            }
        }
    }

    SolidBrush shadow(Color(160, 0, 0, 0));
    SolidBrush white(Color(255, 255, 255, 255));
    g.DrawString(txt.c_str(), -1, &font, PointF(origin.X + 2, origin.Y + 2), &shadow);
    g.DrawString(txt.c_str(), -1, &font, origin, &white);

    // 显示 5 分钟 hi/lo（支持固定位置）
    {
        int hi = g_hr5MinHi.load();
        int lo = g_hr5MinLo.load();
        Font miniFont(&ff, fontSize * 0.5f, FontStyleBold, UnitPixel);
        std::wstring shi = std::to_wstring(hi);
        std::wstring slo = std::to_wstring(lo);
        RectF rHi, rLo;
        g.MeasureString(shi.c_str(), -1, &miniFont, PointF(0, 0), &rHi);
        g.MeasureString(slo.c_str(), -1, &miniFont, PointF(0, 0), &rLo);

        PointF hiPos;
        PointF loPos;
        if (g_histFixedPos) {
            // 锚点为中心 → 转成左上绘制坐标
            REAL hiCx = (REAL)(g_winW * g_histHiXRatio);
            REAL hiCy = (REAL)(g_winH * g_histHiYRatio);
            REAL loCx = (REAL)(g_winW * g_histLoXRatio);
            REAL loCy = (REAL)(g_winH * g_histLoYRatio);
            hiPos = PointF(hiCx - rHi.Width / 2.0f, hiCy - rHi.Height / 2.0f);
            loPos = PointF(loCx - rLo.Width / 2.0f, loCy - rLo.Height / 2.0f);
        } else {
            // 原随主数字动态定位逻辑
            REAL baseX = origin.X + layout.Width + 10.0f;
            hiPos = PointF(baseX, origin.Y);
            REAL loY = origin.Y + layout.Height - rLo.Height;
            loPos = PointF(baseX, loY);
        }
        // 阴影 / 正文
        g.DrawString(shi.c_str(), -1, &miniFont, PointF(hiPos.X + 1, hiPos.Y + 1), &shadow);
        g.DrawString(shi.c_str(), -1, &miniFont, hiPos, &white);
        g.DrawString(slo.c_str(), -1, &miniFont, PointF(loPos.X + 1, loPos.Y + 1), &shadow);
        g.DrawString(slo.c_str(), -1, &miniFont, loPos, &white);
    }

    // 临时注释边框用于诊断紫边，如果确认问题解决可改回或改为不抗锯齿绘制：
    // Pen border(Color(255, 255, 0, 255), 3.0f);
    // border.SetLineJoin(LineJoinMiter);
    // g.SetSmoothingMode(SmoothingModeNone); // 仅针对直线框
    // g.DrawRectangle(&border, 1, 1, g_winW - 2, g_winH - 2);
}

void UpdateOverlay(HWND hWnd) {
    if (!hWnd) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_winW;
    bi.bmiHeader.biHeight = -g_winH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* pvBits = nullptr;
    HDC hdcMem = CreateCompatibleDC(nullptr);
    HBITMAP hDib = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!hDib) { DeleteDC(hdcMem); return; }
    HGDIOBJ oldBmp = SelectObject(hdcMem, hDib);

    {
        Graphics g(hdcMem);
        g.Clear(Color(0, 0, 0, 0));
        // 不在这里单独绘制背景图，背景由 DrawFancyText 统一绘制，避免双重叠加导致 fringe
        DrawFancyText(hdcMem, g_heartRate.load());
    }
    // 绘制完成后做一次预乘（防止抗锯齿边缘色漂）
    PreMultiplySurface(pvBits, g_winW, g_winH, bi.bmiHeader.biWidth * 4);

    HDC hdcScreen = GetDC(nullptr);
    POINT ptDst{}; RECT rc{}; GetWindowRect(hWnd, &rc);
    ptDst.x = rc.left; ptDst.y = rc.top;
    SIZE sz{ g_winW,g_winH }; POINT ptSrc{ 0,0 };
    BLENDFUNCTION bf{ AC_SRC_OVER,0,255,AC_SRC_ALPHA };
    UpdateLayeredWindow(hWnd, hdcScreen, &ptDst, &sz, hdcMem, &ptSrc, 0, &bf, ULW_ALPHA);
    // 可选：检查失败（返回值=FALSE 时输出）
    // if (!UpdateLayeredWindow(...)) AppendLog(L"[UI] UpdateLayeredWindow 失败 err=" + std::to_wstring(GetLastError()));

    ReleaseDC(nullptr, hdcScreen);
    SelectObject(hdcMem, oldBmp);
    DeleteObject(hDib);
    DeleteDC(hdcMem);
}

void InitConsole() {
 #if ENABLE_CONSOLE
    if (!g_consoleEnabled) {
        if (AllocConsole()) {
            SetConsoleTitleW(L"Broadcast HR");
            SetConsoleOutputCP(CP_UTF8);
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            freopen_s(&fp, "CONIN$",  "r", stdin);
            setvbuf(stdout, nullptr, _IONBF, 0);
            g_consoleEnabled = true;
            wprintf(L"广播心率模式启动...\n");
        }
    }
 #else
    g_consoleEnabled = false; // 不创建控制台
 #endif
}

static void ShowLastErrorBox(LPCWSTR title) {
    DWORD err = GetLastError();
    wchar_t buf[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, buf, (DWORD)std::size(buf), nullptr);
    MessageBoxW(nullptr, buf, title, MB_OK | MB_ICONERROR);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_APP_REFRESH:
    case WM_TIMER:
        if (msg == WM_TIMER) StepGlow100ms();
        UpdateOverlay(hWnd); return 0;
    case WM_CREATE:
        // 100ms 刷新（衰减步长）
        SetTimer(hWnd, 1, 100, nullptr); return 0;
    case WM_LBUTTONDOWN: {
        g_bDragging = true;
        SetCapture(hWnd);
        RECT rc; GetWindowRect(hWnd, &rc);
        g_ptWndStart.x = rc.left;
        g_ptWndStart.y = rc.top;
        GetCursorPos(&g_ptCursorStart);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_bDragging && (wParam & MK_LBUTTON)) {
            POINT pt; GetCursorPos(&pt);
            int dx = pt.x - g_ptCursorStart.x;
            int dy = pt.y - g_ptCursorStart.y;
            SetWindowPos(hWnd, nullptr,
                         g_ptWndStart.x + dx,
                         g_ptWndStart.y + dy,
                         0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_bDragging) {
            g_bDragging = false;
            ReleaseCapture();
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt; GetCursorPos(&pt);
        ShowContextMenu(hWnd, pt);
        return 0;
    }
    case WM_MEASUREITEM: {
        LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lParam;
        if (mis->CtlType == ODT_MENU) {
            if (HandleMenuMeasure(mis)) return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT ds = (LPDRAWITEMSTRUCT)lParam;
        if (ds->CtlType == ODT_MENU) {
            if (HandleMenuDraw(ds)) return TRUE;
        }
        break;
    }
    case WM_COMMAND: {
        if (HandleMenuCommand(hWnd, LOWORD(wParam))) return 0;
        break;
    }
    case WM_DESTROY:
        g_logRunning = false; FlushLogToFile(); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ===== 在文件顶部（include 之后某处）增加（若没有全局 token）=====
static ULONG_PTR g_gdiplusToken = 0;

// ===== 在文件末尾或合适位置添加入口 =====
int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 初始化 GDI+ （必须，否则全部绘制为透明）
    {
        GdiplusStartupInput gsi;
        if (GdiplusStartup(&g_gdiplusToken, &gsi, nullptr) != Ok) {
            MessageBoxW(nullptr, L"GDI+ 初始化失败", L"Error", MB_OK | MB_ICONERROR);
            return 0;
        }
    }

    // 加载背景贴图（从资源，而非磁盘）
    {
        g_bgImage = LoadPngFromResource(hInst, IDR_PNG_BG);
        if (g_bgImage && g_bgImage->GetLastStatus() == Ok) {
            SanitizeTransparentPixels(g_bgImage.get());
            g_winW = (int)g_bgImage->GetWidth();
            g_winH = (int)g_bgImage->GetHeight();
        }
        else {
            AppendLog(L"[UI] 嵌入背景图加载失败，使用纯色背景");
            g_bgImage.reset();
        }
    }

    // 注册窗口类
    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"HRBroadcastOverlay";
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClassEx 失败", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - g_winW) / 2;
    int y = (sh - g_winH) / 2;

    HWND hWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"",
        WS_POPUP,
        x, y, g_winW, g_winH,
        nullptr, nullptr, hInst, nullptr);

    if (!hWnd) {
        MessageBoxW(nullptr, L"CreateWindowEx 失败", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    g_mainWnd = hWnd;

    // 初次合成
    InitFilter();
    InitMenuSubsystem();
    // 初次合成
    UpdateOverlay(hWnd);
    // 启动时先随机一次颜色（若用户未锁定）
    RandomizeGlowColorIfAllowed();
    InitConsole();                 // 若 ENABLE_CONSOLE=0 不会创建控制台
    StartLogFlushThread();
    AppendLog(L"Program started (广播模式)");

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    // 启动广播监听线程
    std::thread(StartFilteredBroadcastWatcher).detach();

    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
 
    g_logRunning = false;
    FlushLogToFile();
    if (g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
    return 0;
}
