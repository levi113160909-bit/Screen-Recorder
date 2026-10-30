// =============================================================
//  StreamFile.h - "نسخة من البث على جهازك"
//
//  وانت عم تبث، منكتب نفس القطع المضغوطة اللي رايحة للمنصة بملف MP4.
//  يعني بلا أي ضغط إضافي: لا حمل على كرت الشاشة ولا على المعالج.
//  بعد البث بتقصّ منه مقاطع لليوتيوب.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "Encoders.h"

class StreamFileWriter
{
public:
    ~StreamFileWriter() { Close(); }

    // config = بطاقة تعريف الفيديو (avcC أو hvcC) ، bframes: إذا في B-frames
    bool Open(const std::wstring& path, bool hevc, UINT32 width, UINT32 height, UINT32 fps,
              UINT32 videoKbps, const std::vector<uint8_t>& config, bool bframes, UINT32 audioKbps);
    void Write(const EncodedPacket& pkt);        // فيديو (بطول قبل كل وحدة) أو صوت AAC
    HRESULT Close();
    bool IsOpen() const { return m_writer != nullptr; }
    const std::wstring& Path() const { return m_path; }

private:
    Microsoft::WRL::ComPtr<IMFSinkWriter> m_writer;
    DWORD        m_video = 0, m_audio = 0;
    bool         m_started = false, m_bframes = false;
    uint32_t     m_baseMs = 0;
    LONGLONG     m_frameDuration = 10'000'000 / 30;
    std::wstring m_path;
    std::vector<uint8_t> m_tmp;
};
