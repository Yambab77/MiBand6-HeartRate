#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include <vector>
#include <atomic>
#include <string>
#include <deque>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <utility>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Storage::Streams;
using namespace Gdiplus;

// -------- 全局变量（仅声明，定义在 band.cpp）--------
extern std::atomic<int>     g_heartRate;

extern std::atomic<double>  g_glowAlpha;
extern std::atomic<double>  g_beatPeriodSec;
extern std::atomic<int64_t> g_lastBeatNsForCycle;

extern float  g_glowAlphaBase;
extern float  g_glowAlphaRange;
extern float  g_glowGamma;
extern bool   g_glowDoubleLayer;
extern bool   g_userColorOverride;
extern uint32_t g_rngState;

extern BYTE g_glowColorR;
extern BYTE g_glowColorG;
extern BYTE g_glowColorB;

extern std::mutex g_hrHistMutex;
extern std::deque<std::pair<std::chrono::steady_clock::time_point,int>> g_hrHistory;
extern std::atomic<int> g_hr5MinHi;
extern std::atomic<int> g_hr5MinLo;

// -------- 函数 --------
void SanitizeTransparentPixels(Gdiplus::Image* img);
void PreMultiplySurface(void* bits, int width, int height, int strideBytes);
std::unique_ptr<Gdiplus::Image> LoadPngFromResource(HINSTANCE hInst, int resId);

IBuffer MakeBuffer(const std::vector<uint8_t>& v);
std::vector<uint8_t> BufferToVec(IBuffer const& buf);

bool   ParseHuamiMfrHeartRate(const std::vector<uint8_t>& raw, int& hrOut);
double ComputePeakAlpha(int hr);
void   RandomizeGlowColorIfAllowed();
void   StepGlow100ms();
void   UpdateHrHistory(int hr);

// 日志（band.cpp 实现）
void AppendLog(const std::wstring& line);