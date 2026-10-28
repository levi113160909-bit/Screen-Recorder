// =============================================================
//  Replay.h - إعادة آخر لحظات (Replay Buffer)
//
//  البرنامج بيضل يحفظ آخر كم ثانية (مضغوطة) بالذاكرة،
//  ولما تكبس "حفظ" منكتبهم بملف MP4 فوراً بدون إعادة ضغط.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "Encoders.h"

class ReplayBuffer
{
public:
    void SetSeconds(int seconds);
    int  Seconds() const { return m_seconds; }

    void Push(const std::shared_ptr<const EncodedPacket>& pkt);
    void SetVideoInfo(const std::vector<uint8_t>& avcConfig, int width, int height);
    void Clear();

    // كم ثانية محفوظة هلق
    double BufferedSeconds();

    bool Save(const std::wstring& path, std::wstring& error);

private:
    std::mutex m_mutex;
    std::deque<std::shared_ptr<const EncodedPacket>> m_packets;
    std::vector<uint8_t> m_avcConfig;
    int m_width = 0, m_height = 0;
    int m_seconds = 30;
};

// كاتب MP4 بسيط (فيديو H.264 + صوت AAC جاهزين مضغوطين)
bool WriteMp4(const std::wstring& path, const std::vector<std::shared_ptr<const EncodedPacket>>& packets,
              const std::vector<uint8_t>& avcConfig, int width, int height);
