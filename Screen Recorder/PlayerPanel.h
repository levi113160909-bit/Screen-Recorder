// =============================================================
//  PlayerPanel.h - مشغّل الفيديو وعارض الصور (جوّا البرنامج)
//
//  بينفتح من المعرض. الفيديو: صورة وصوت، شريط تنقّل، تشغيل/إيقاف،
//  ±5 ثواني، صوت وكتم، وزر قص. الصورة: بتنعرض بحجمها المناسب.
//  اختصارات: مسافة = تشغيل/إيقاف ، الأسهم = ±5 ثواني ، M = كتم
// =============================================================
#pragma once
#include "Panel.h"
#include "VideoPlayer.h"
#include <atomic>
#include <memory>
#include <mutex>

struct PlayerHooks
{
    std::function<void(const std::wstring&)> trim;     // فتح نافذة القص
    std::function<void()>                    back;     // رجوع للمعرض
};

class PlayerPanel : public Panel
{
public:
    PlayerPanel(const std::wstring& path, bool image, PlayerHooks hooks);
    ~PlayerPanel() override;

    std::wstring   Title() const override    { return m_name; }
    std::wstring   Subtitle() const override { return m_image ? L"صورة" : L"فيديو"; }
    const wchar_t* IconGlyph() const override { return m_image ? Icon::Photo : Icon::Play; }
    float          Width() const override    { return 980; }
    float          BodyHeight() const override { return m_image ? 560.0f : 600.0f; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    bool           OnDrag(int id, D2D1_POINT_2F p) override;
    bool           OnKeyDown(WPARAM vk) override;

private:
    void TogglePlay();
    void SeekBy(LONGLONG delta);
    void ApplyVolume();

    struct ImageLoad
    {
        std::mutex        mutex;
        std::vector<BYTE> px;
        int               w = 0, h = 0;
        std::atomic<bool> done{ false };
    };

    std::wstring m_path, m_name;
    bool         m_image = false;
    PlayerHooks  m_h;
    std::unique_ptr<VideoPlayer> m_player;
    std::shared_ptr<ImageLoad>   m_img = std::make_shared<ImageLoad>();
    ComPtr<ID2D1Bitmap> m_bmp;
    std::vector<BYTE>   m_px;
    int          m_w = 0, m_h2 = 0;
    ID2D1HwndRenderTarget* m_owner = nullptr;
    D2D1_RECT_F  m_seek{};
    bool         m_started = false;          // تشغيل تلقائي أول ما يجهز
};
