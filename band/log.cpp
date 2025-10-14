#include "log.h"
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>

// 状态
static std::mutex            s_logMutex;
static std::vector<std::string> s_appBuffer; // 应用合并日志缓冲
static std::vector<std::string> s_hrBuffer;  // 心率日志缓冲（按天切分）
static std::atomic<bool>     s_running{ false };
static bool                  s_consoleEnabled = false;

// 辅助：获取 EXE 目录
static std::wstring GetExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : p.substr(0, pos);
}

static std::wstring GetLogsDir() {
    return GetExeDir() + L"\\logs";
}
static void EnsureLogsDir() {
    auto dir = GetLogsDir();
    CreateDirectoryW(dir.c_str(), nullptr); // 已存在则忽略
}

static std::wstring TodayStr() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    struct tm tmv{};
    localtime_s(&tmv, &t);
    wchar_t buf[16]{};
    swprintf(buf, 16, L"%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return buf;
}

static std::wstring GetTimestampW() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    struct tm tmv{};
    localtime_s(&tmv, &t);
    wchar_t buf[64]{};
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

static void EnsureTodayHrLogExists() {
    EnsureLogsDir();
    auto path = GetLogsDir() + L"\\hr_" + TodayStr() + L".txt";
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

void Log_SetConsoleEnabled(bool enabled) {
    s_consoleEnabled = enabled;
}

void AppendLog(const std::wstring& wline) {
    std::wstring line = GetTimestampW() + L" | " + wline;

    // 控制台/调试器输出
    if (s_consoleEnabled) {
        fwprintf(stdout, L"%s\n", line.c_str());
    } else {
        OutputDebugStringW((line + L"\n").c_str());
    }

    const bool isHr = (wline.find(L"[ADV-HR]") != std::wstring::npos);
    std::lock_guard<std::mutex> lk(s_logMutex);
    if (isHr) {
        s_hrBuffer.emplace_back(Narrow(line));
    } else {
        s_appBuffer.emplace_back(Narrow(line));
    }
}

static void FlushNow() {
    // 交换缓冲，尽量缩短持锁时间
    std::vector<std::string> appSwap, hrSwap;
    {
        std::lock_guard<std::mutex> lk(s_logMutex);
        if (!s_appBuffer.empty()) appSwap.swap(s_appBuffer);
        if (!s_hrBuffer.empty())  hrSwap.swap(s_hrBuffer);
        if (appSwap.empty() && hrSwap.empty()) return;
    }

    EnsureLogsDir();

    // 应用合并日志
    if (!appSwap.empty()) {
        try {
            std::ofstream ofs(Narrow(GetLogsDir() + L"\\app.log"), std::ios::app | std::ios::binary);
            if (ofs) for (auto& l : appSwap) ofs << l << "\n";
        } catch (...) {}
    }
    // 当天心率日志
    if (!hrSwap.empty()) {
        auto hrPath = GetLogsDir() + L"\\hr_" + TodayStr() + L".txt";
        try {
            std::ofstream ofs(Narrow(hrPath), std::ios::app | std::ios::binary);
            if (ofs) for (auto& l : hrSwap) ofs << l << "\n";
        } catch (...) {}
    }
}

void Log_Init() {
    if (s_running.exchange(true)) return;
    EnsureLogsDir();
    EnsureTodayHrLogExists();

    std::thread([] {
        while (s_running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            FlushNow();
        }
    }).detach();
}

void Log_Shutdown() {
    s_running.store(false);
    FlushNow();
}