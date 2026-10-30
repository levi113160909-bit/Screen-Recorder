// =============================================================
//  VideoPlayer.h - مشغّل فيديو صغير (صورة + صوت) جوّا الواجهة
//
//  مبني على Media Engine تبع ويندوز: هو بيفك الفيديو وبيشغّل الصوت،
//  ونحنا بس منسحب الصورة الجديدة كل رسمة (Poll) ومنرسمها بـ Direct2D.
//  هيك المشغّل بيضل بنفس ثيم البرنامج (مش نافذة منفصلة).
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <string>
#include <vector>

struct IMFMediaEngine;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct IMFDXGIDeviceManager;
class  PlayerNotify;

class VideoPlayer
{
public:
    VideoPlayer();                            // بالـ .cpp (لأن أنواع D3D كاملة هناك بس)
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    bool Open(const std::wstring& path);      // false = ما قدرنا (منرجع للمعاينة القديمة)

    bool     Ready() const;                   // الفيديو انفتح وجاهز
    bool     Failed() const;
    bool     Playing() const;
    void     Play();
    void     Pause();
    void     Seek(LONGLONG t100ns);
    LONGLONG Position() const;                // 100 نانوثانية
    LONGLONG Length() const;                  // مدة الفيديو (0 = لسا ما منعرف)
    bool     Ended() const;
    void     SetVolume(float v);              // 0..1
    void     SetMuted(bool m);

    // صورة جديدة؟ (BGRA من فوق لتحت ، عرضها maxWidth كحد أقصى). false = نفس الصورة القديمة
    bool Poll(int maxWidth, std::vector<BYTE>& pixels, int& width, int& height);

private:
    Microsoft::WRL::ComPtr<ID3D11Device>         m_dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>  m_ctx;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> m_manager;
    Microsoft::WRL::ComPtr<IMFMediaEngine>       m_engine;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>      m_target, m_staging;
    PlayerNotify* m_notify = nullptr;
    int           m_w = 0, m_h = 0;
    bool          m_started = false;          // MFStartup
    bool          m_firstSeek = false;        // أول صورة (قبل التشغيل)
};
