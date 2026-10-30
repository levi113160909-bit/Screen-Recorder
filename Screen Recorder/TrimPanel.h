// =============================================================
//  TrimPanel.h - "قصّ سريع" بدون إعادة ضغط
//
//  فوق: مشغّل فيديو (صورة وصوت) لحتى تعرف من وين لوين بدك تقص.
//  شريط فيه صور من الفيديو، وعليه مقبضين: البداية والنهاية.
//  وانت عم تسحب بتشوف الصورة عند المقبض. "حفظ المقطع" بيعمل ملف جديد
//  (الأصلي ما بيتغيّر) بثواني، وبنفس الجودة بالضبط.
// =============================================================
#pragma once
#include "Panel.h"
#include "VideoPlayer.h"
#include <atomic>
#include <memory>
#include <mutex>

class TrimPanel : public Panel
{
public:
    // done: بعد ما ينحفظ المقطع (المسار الجديد) ، back: رجوع للمعرض
    TrimPanel(const std::wstring& path, std::function<void(const std::wstring&)> done, std::function<void()> back);
    ~TrimPanel() override;

    std::wstring   Title() const override    { return L"قصّ سريع"; }
    std::wstring   Subtitle() const override { return L"بدون إعادة ضغط: فوري وبنفس الجودة"; }
    const wchar_t* IconGlyph() const override { return L""; }
    float          Width() const override    { return 820; }
    float          BodyHeight() const override { return 640; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    bool           OnDrag(int id, D2D1_POINT_2F p) override;
    bool           OnKeyDown(WPARAM vk) override;

private:
    struct Shared
    {
        std::mutex         mutex;
        LONGLONG           duration = 0;
        std::vector<std::vector<BYTE>> strip;          // صور الشريط
        int                stripW = 0, stripH = 0;
        std::vector<BYTE>  preview;                    // الصورة عند المقبض
        int                previewW = 0, previewH = 0;
        unsigned           previewVersion = 0;
        LONGLONG           wantTime = -1;              // آخر وقت طلبناه للمعاينة
        std::atomic<bool>  stop{ false };
        std::atomic<int>   progress{ -1 };             // -1 = ما عم نقص ، 0..100 ، 101 = خلص ، -2 = فشل
        std::atomic<bool>  cancel{ false };
        std::wstring       output;
    };

    std::wstring m_path;
    std::function<void(const std::wstring&)> m_done;
    std::function<void()> m_back;
    std::shared_ptr<Shared> m_s = std::make_shared<Shared>();
    LONGLONG     m_start = 0, m_end = 0;              // 100 نانوثانية
    int          m_active = 1;                         // المقبض الأخير (0 بداية ، 1 نهاية)
    D2D1_RECT_F  m_track{};
    ComPtr<ID2D1Bitmap> m_previewBmp;
    unsigned     m_previewVer = 0;
    std::vector<ComPtr<ID2D1Bitmap>> m_stripBmp;
    ID2D1HwndRenderTarget* m_owner = nullptr;
    bool         m_notified = false;

    // ---- المشغّل ----
    LONGLONG Duration();
    void     TogglePlay();
    void     SetStartHere();
    void     SetEndHere();
    void     SeekBy(LONGLONG delta);
    std::unique_ptr<VideoPlayer> m_player;           // فاضي = ما اشتغل (منرجع للمعاينة القديمة)
    ComPtr<ID2D1Bitmap> m_playBmp;
    std::vector<BYTE>   m_playPx;
    int          m_playW = 0, m_playH = 0;
    bool         m_playSelection = false;            // عم نشغّل المقطع المحدد بس (منوقف عند النهاية)
    D2D1_RECT_F  m_seek{};
};
