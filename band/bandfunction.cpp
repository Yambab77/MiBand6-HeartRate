#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "bandfunctions.h"
#include <algorithm>
#include <cmath>
#include <objidl.h>

using namespace winrt;
using namespace Windows::Storage::Streams;
using namespace Gdiplus;

// 清洗 PNG 透明边缘
void SanitizeTransparentPixels(Image* img) {
    if (!img) return;
    if (img->GetType() != ImageTypeBitmap) return;
    auto* bmp = static_cast<Bitmap*>(img);
    auto pf = bmp->GetPixelFormat();
    if (pf != PixelFormat32bppARGB && pf != PixelFormat32bppPARGB) return;
    const UINT w = bmp->GetWidth();
    const UINT h = bmp->GetHeight();
    Rect rect(0, 0, w, h);
    BitmapData data{};
    if (bmp->LockBits(&rect, ImageLockModeRead | ImageLockModeWrite, pf, &data) != Ok) return;
    auto* base = static_cast<uint8_t*>(data.Scan0);
    for (UINT y = 0; y < h; ++y) {
        uint8_t* row = base + y * data.Stride;
        for (UINT x = 0; x < w; ++x) {
            uint8_t* px = row + x * 4;
            uint8_t a = px[3];
            if (a < 8) {
                px[0] = px[1] = px[2] = 0;
            } else if (pf == PixelFormat32bppARGB && a < 255) {
                px[0] = (uint8_t)((px[0] * a + 127) / 255);
                px[1] = (uint8_t)((px[1] * a + 127) / 255);
                px[2] = (uint8_t)((px[2] * a + 127) / 255);
            }
        }
    }
    bmp->UnlockBits(&data);
}

void PreMultiplySurface(void* bits, int width, int height, int strideBytes) {
    if (!bits || width <= 0 || height <= 0 || strideBytes < width * 4) return;
    auto* base = static_cast<uint8_t*>(bits);
    for (int y = 0; y < height; ++y) {
        uint8_t* row = base + y * strideBytes;
        for (int x = 0; x < width; ++x) {
            uint8_t* px = row + x * 4;
            uint8_t a = px[3];
            if (a == 0) {
                px[0] = px[1] = px[2] = 0;
            } else if (a < 255) {
                px[0] = (uint8_t)((px[0] * a + 127) / 255);
                px[1] = (uint8_t)((px[1] * a + 127) / 255);
                px[2] = (uint8_t)((px[2] * a + 127) / 255);
            }
        }
    }
}

// 资源 PNG 加载
std::unique_ptr<Image> LoadPngFromResource(HINSTANCE hInst, int resId) {
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(resId), L"PNG");
    if (!hRes) return nullptr;
    DWORD size = SizeofResource(hInst, hRes);
    HGLOBAL hData = LoadResource(hInst, hRes);
    if (!hData) return nullptr;
    void* pData = LockResource(hData);
    if (!pData || size == 0) return nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return nullptr;
    void* pMem = GlobalLock(hMem);
    if (!pMem) { GlobalFree(hMem); return nullptr; }
    memcpy(pMem, pData, size);
    GlobalUnlock(hMem);
    IStream* pStream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) != S_OK) { GlobalFree(hMem); return nullptr; }
    std::unique_ptr<Image> img(new Image(pStream, FALSE));
    pStream->Release();
    if (img->GetLastStatus() != Ok) img.reset();
    return img;
}

// IBuffer helpers
IBuffer MakeBuffer(const std::vector<uint8_t>& v) {
    DataWriter w;
    w.WriteBytes(winrt::array_view<const uint8_t>(v.data(), v.data() + v.size()));
    return w.DetachBuffer();
}

std::vector<uint8_t> BufferToVec(IBuffer const& buf) {
    auto r = DataReader::FromBuffer(buf);
    std::vector<uint8_t> v(r.UnconsumedBufferLength());
    for (auto& b : v) b = r.ReadByte();
    return v;
}

// 厂商 HR 解析
bool ParseHuamiMfrHeartRate(const std::vector<uint8_t>& raw, int& hrOut) {
    if (raw.size() < 6) return false;
    if (!(raw[0] == 0x57 && raw[1] == 0x01)) return false;
    for (size_t i = 2; i + 3 < raw.size(); ++i) {
        if (raw[i] == 0x02 && raw[i + 1] == 0x02 && raw[i + 2] == 0x01) {
            uint8_t hr = raw[i + 3];
            if (hr >= 10 && hr <= 250) { hrOut = hr; return true; }
        }
    }
    return false;
}

// 峰值亮度
double ComputePeakAlpha(int hr) {
    if (hr <= 0) return 0.0;
    double norm = std::clamp((hr - 50.0) / 80.0, 0.0, 1.0);
    double peak = g_glowAlphaBase + g_glowAlphaRange * norm;
    if (peak > 255.0) peak = 255.0;
    return peak;
}

// 随机颜色
void RandomizeGlowColorIfAllowed() {
    if (g_userColorOverride) return;
    uint32_t x = g_rngState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_rngState = x;
    BYTE r = (BYTE)(x & 0xFF);
    BYTE g = (BYTE)((x >> 8) & 0xFF);
    BYTE b = (BYTE)((x >> 16) & 0xFF);
    if ((int)r + g + b < 90) { r |= 0x40; g |= 0x40; b |= 0x40; }
    g_glowColorR = r; g_glowColorG = g; g_glowColorB = b;
}

// 步进衰减
void StepGlow100ms() {
    using namespace std::chrono;
    int hr = g_heartRate.load(std::memory_order_relaxed);
    if (hr <= 0) { g_glowAlpha.store(0.0, std::memory_order_relaxed); return; }
    double period = g_beatPeriodSec.load(std::memory_order_relaxed);
    if (period <= 0) period = 1.0;
    const int64_t nowNs = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    int64_t lastBeatNs = g_lastBeatNsForCycle.load(std::memory_order_relaxed);
    if (lastBeatNs == 0) {
        g_lastBeatNsForCycle.store(nowNs, std::memory_order_relaxed);
        RandomizeGlowColorIfAllowed();
        g_glowAlpha.store(ComputePeakAlpha(hr), std::memory_order_relaxed);
        return;
    }
    double elapsedSec = (nowNs - lastBeatNs) * 1e-9;
    if (elapsedSec >= period) {
        double cycles = std::floor(elapsedSec / period);
        lastBeatNs += static_cast<int64_t>(cycles * period * 1e9);
        g_lastBeatNsForCycle.store(lastBeatNs, std::memory_order_relaxed);
        RandomizeGlowColorIfAllowed();
        g_glowAlpha.store(ComputePeakAlpha(hr), std::memory_order_relaxed);
    } else {
        double a = g_glowAlpha.load(std::memory_order_relaxed);
        a *= 0.9;
        if (a < 1.0) a = 0.0;
        g_glowAlpha.store(a, std::memory_order_relaxed);
    }
}

// 历史 HR 更新
void UpdateHrHistory(int hr) {
    using clock = std::chrono::steady_clock;
    auto now = clock::now();
    std::lock_guard<std::mutex> lk(g_hrHistMutex);
    g_hrHistory.emplace_back(now, hr);
    auto cutoff = now - std::chrono::minutes(5);
    while (!g_hrHistory.empty() && g_hrHistory.front().first < cutoff)
        g_hrHistory.pop_front();
    int hi = hr, lo = hr;
    for (auto& p : g_hrHistory) {
        hi = std::max(hi, p.second);
        lo = std::min(lo, p.second);
    }
    g_hr5MinHi = hi;
    g_hr5MinLo = lo;
}