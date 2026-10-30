// =============================================================
//  PlayerPanel.cpp - مشغّل الفيديو وعارض الصور
// =============================================================
#include "PlayerPanel.h"
#include "MediaControls.h"
#include "Tooltip.h"
#include "VideoTools.h"
#include <shellapi.h>
#include <algorithm>
#include <climits>
#include <thread>

namespace
{
    enum : int
    {
        ID_VIDEO  = ID_PANEL_USER,        // كبسة على الفيديو = تشغيل/إيقاف
        ID_PLAY   = ID_PANEL_USER + 1,
        ID_BACK5  = ID_PANEL_USER + 2,
        ID_FWD5   = ID_PANEL_USER + 3,
        ID_SEEK   = ID_PANEL_USER + 4,
        ID_MUTE   = ID_PANEL_USER + 5,
        ID_VOLUME = ID_PANEL_USER + 6,
        ID_TRIM   = ID_PANEL_USER + 7,
        ID_FOLDER = ID_PANEL_USER + 8,
        ID_BACK   = ID_PANEL_USER + 9
    };
    constexpr LONGLONG JUMP = 50'000'000;          // 5 ثواني

    // الصوت بيضل محفوظ طول ما البرنامج مفتوح
    float s_volume = 1.0f;
    bool  s_muted  = false;
}

PlayerPanel::PlayerPanel(const std::wstring& path, bool image, PlayerHooks hooks)
    : m_path(path), m_image(image), m_h(std::move(hooks))
{
    const size_t slash = path.find_last_of(L'\\'), dot = path.find_last_of(L'.');
    m_name = path.substr(slash == std::wstring::npos ? 0 : slash + 1);
    if (dot != std::wstring::npos && dot > slash)
        m_name.resize(dot - (slash == std::wstring::npos ? 0 : slash + 1));

    if (image)
    {
        auto img = m_img;
        std::thread([img, path]
        {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            std::vector<BYTE> px;
            int w = 0, h = 0;
            if (ShellThumbnail(path, 1920, px, w, h))
            {
                std::lock_guard<std::mutex> lock(img->mutex);
                img->px = std::move(px);
                img->w = w; img->h = h;
            }
            img->done = true;
            if (SUCCEEDED(hr)) CoUninitialize();
        }).detach();
    }
    else
    {
        m_player = std::make_unique<VideoPlayer>();
        if (!m_player->Open(path))
            m_player.reset();
    }
}

PlayerPanel::~PlayerPanel() = default;

void PlayerPanel::ApplyVolume()
{
    if (!m_player) return;
    m_player->SetVolume(s_volume);
    m_player->SetMuted(s_muted);
}

void PlayerPanel::TogglePlay()
{
    if (!m_player) return;
    if (m_player->Playing()) m_player->Pause();
    else                     m_player->Play();
}

void PlayerPanel::SeekBy(LONGLONG delta)
{
    if (!m_player) return;
    const LONGLONG len = m_player->Length();
    m_player->Seek(std::clamp<LONGLONG>(m_player->Position() + delta, 0, len > 0 ? len : LLONG_MAX));
}

void PlayerPanel::DrawBody(float L, float top, float R)
{
    if (g_rt.Get() != m_owner)              // الرسّام تغيّر: منعيد بناء الصورة
    {
        m_owner = g_rt.Get();
        m_bmp.Reset();
    }
    if (m_player && m_player->Failed())
        m_player.reset();

    // ---- تشغيل تلقائي أول ما يجهز ----
    if (m_player && !m_started && m_player->Ready())
    {
        m_started = true;
        ApplyVolume();
        m_player->Play();
    }

    // ---- الصورة ----
    if (m_player)
        UpdatePlayerBitmap(*m_player, 1600, m_px, m_w, m_h2, m_bmp);
    else if (m_image && !m_bmp && m_img->done)
    {
        std::lock_guard<std::mutex> lock(m_img->mutex);
        if (!m_img->px.empty())
            g_rt->CreateBitmap(D2D1::SizeU(m_img->w, m_img->h), m_img->px.data(), m_img->w * 4,
                               D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
                               &m_bmp);
    }

    float y = top;
    const float cx = (L + R) / 2;
    const float vh = m_image ? 470.0f : 460.0f;
    const float vw = std::min(R - L, vh * 16 / 9);
    const D2D1_RECT_F pv = Rect(cx - vw / 2, y, cx + vw / 2, y + vh);
    Fill(pv, D2D1::ColorF(0x000000), 10);
    if (m_bmp)
    {
        const D2D1_SIZE_F sz = m_bmp->GetSize();
        const float s = std::min(vw / sz.width, vh / sz.height);
        g_rt->DrawBitmap(m_bmp.Get(), Rect(cx - sz.width * s / 2, y + vh / 2 - sz.height * s / 2,
                                           cx + sz.width * s / 2, y + vh / 2 + sz.height * s / 2), 1.0f,
                         D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    else if (m_image ? (bool)m_img->done : (m_player == nullptr))
        Text(m_image ? L"تعذّر فتح الصورة" : L"تعذّر تشغيل الفيديو", g_fCenter.Get(), pv, Theme::Red);
    else
        Text(L"جارٍ التحميل…", g_fCenter.Get(), pv, Theme::SubText);

    const bool playing = m_player && m_player->Playing();
    if (m_player)
    {
        const bool vHover = AddHit(pv, ID_VIDEO);
        Tip(ID_VIDEO, playing ? L"إيقاف مؤقت (مسافة)" : L"تشغيل (مسافة)");
        // متوقف: زر تشغيل كبير شفاف بالنص
        if (!playing && m_player->Ready())
        {
            const float pcy = y + vh / 2;
            Circle(cx, pcy, 34, D2D1::ColorF(0x000000, vHover ? 0.7f : 0.5f));
            Text(m_player->Ended() ? Icon::Refresh : Icon::Play, g_fIconBig.Get(),
                 Rect(cx - 34, pcy - 34, cx + 34, pcy + 34), Theme::White);
        }
    }
    y += vh + 14;

    if (m_player)
    {
        // ---- شريط التنقّل: الوقت يسار ، المدة يمين (الزمن من اليسار لليمين) ----
        const LONGLONG pos = m_player->Position(), len = m_player->Length();
        Text(LtrText(MediaClock(pos)), g_fSmallLtr.Get(), Rect(L, y, L + 56, y + 16), Theme::Text);
        Text(LtrText(MediaClock(len)), g_fSmallLtr.Get(), Rect(R - 56, y, R, y + 16), Theme::SubText);
        m_seek = Rect(L + 64, y, R - 64, y + 16);
        const float my = y + 8;
        const bool seekHover = AddHit(Rect(m_seek.left - 6, y - 5, m_seek.right + 6, y + 21), ID_SEEK);
        const float f = len > 0 ? std::clamp((float)pos / len, 0.0f, 1.0f) : 0.0f;
        const float xp = m_seek.left + (m_seek.right - m_seek.left) * f;
        const float th = seekHover ? 3.0f : 2.0f;
        Fill(Rect(m_seek.left, my - th, m_seek.right, my + th), Theme::Border, th);
        Fill(Rect(m_seek.left, my - th, xp, my + th), Theme::Accent, th);
        Circle(xp, my, seekHover ? 8.0f : 6.5f, Theme::White);
        if (seekHover && len > 0)            // الوقت تحت الماوس
        {
            const float hf = std::clamp((g_mouse.x - m_seek.left) / (m_seek.right - m_seek.left), 0.0f, 1.0f);
            const float hx = std::clamp(g_mouse.x, m_seek.left + 26, m_seek.right - 26);
            const D2D1_RECT_F bubble = Rect(hx - 26, y - 30, hx + 26, y - 8);
            Fill(bubble, Theme::Card, 6);
            Stroke(bubble, Theme::Border, 6);
            Text(LtrText(MediaClock((LONGLONG)(hf * len))), g_fSmallLtr.Get(), Rect(bubble.left, bubble.top + 3, bubble.right, bubble.bottom), Theme::Text);
        }
        y += 30;

        // ---- الأزرار ----
        const float rowH = 48, cy = y + rowH / 2;
        // بالنص: [5 رجوع] [تشغيل] [5 تقديم]
        const D2D1_RECT_F play = Rect(cx - 24, cy - 24, cx + 24, cy + 24);
        DrawPlayButton(cx, cy, 24, playing, AddHit(play, ID_PLAY), true);
        Tip(ID_PLAY, playing ? L"إيقاف مؤقت (مسافة)" : L"تشغيل (مسافة)");
        const D2D1_RECT_F back5 = Rect(cx - 82, cy - 19, cx - 44, cy + 19);
        const D2D1_RECT_F fwd5  = Rect(cx + 44, cy - 19, cx + 82, cy + 19);
        DrawJumpButton(cx - 63, cy, 19, -5, AddHit(back5, ID_BACK5), true);
        DrawJumpButton(cx + 63, cy, 19, +5, AddHit(fwd5, ID_FWD5), true);
        Tip(ID_BACK5, L"رجوع 5 ثوانٍ (←)");
        Tip(ID_FWD5, L"تقديم 5 ثوانٍ (→)");

        // يسار: الصوت (المنزلق متل باقي البرنامج: الأعلى لليسار) + زر الكتم
        const D2D1_RECT_F mute = Rect(L + 130, cy - 17, L + 164, cy + 17);
        if (AddHit(mute, ID_MUTE)) Fill(mute, Theme::Hover, 8);
        Text(s_muted || s_volume <= 0.001f ? Icon::Muted : Icon::Speaker, g_fIcon.Get(), mute,
             s_muted ? Theme::Red : Theme::Text);
        Tip(ID_MUTE, s_muted ? L"إلغاء الكتم (M)" : L"كتم (M)");
        const D2D1_RECT_F track = Rect(L + 4, cy - 2.5f, L + 122, cy + 2.5f);
        AddSlider(Rect(track.left - 6, cy - 12, track.right + 6, cy + 12), track, s_volume, 0.0f, 1.0f, 0.01f, ID_VOLUME,
                  [this](float v) { s_volume = v; s_muted = false; ApplyVolume(); });
        Tip(ID_VOLUME, L"مستوى الصوت " + std::to_wstring((int)std::lround(s_volume * 100)) + L"%");
        const float kx = track.right - (track.right - track.left) * (s_muted ? 0.0f : s_volume);
        Fill(track, Theme::Border, 2.5f);
        Fill(Rect(kx, track.top, track.right, track.bottom), s_muted ? Theme::Off : Theme::Accent, 2.5f);
        Circle(kx, cy, 6.5f, Theme::White);

        // يمين: رجوع ، قصّ ، المجلد
        DrawButton(Rect(R - 90, cy - 17, R, cy + 17), L"رجوع", ID_BACK, false, true);
        Tip(ID_BACK, L"رجوع إلى المعرض");
        DrawButton(Rect(R - 188, cy - 17, R - 98, cy + 17), L"قصّ", ID_TRIM, false, true, L"\uE8C6");
        Tip(ID_TRIM, L"قصّ مقطع من هذا الفيديو");
        DrawButton(Rect(R - 236, cy - 17, R - 196, cy + 17), L"", ID_FOLDER, false, true);
        Text(Icon::Folder, g_fIcon.Get(), Rect(R - 236, cy - 17, R - 196, cy + 17), Theme::Text);
        Tip(ID_FOLDER, L"إظهار في المجلد");
    }
    else if (m_image)
    {
        const float cy = y + 20;
        DrawButton(Rect(R - 90, cy - 17, R, cy + 17), L"رجوع", ID_BACK, false, true);
        Tip(ID_BACK, L"رجوع إلى المعرض");
        DrawButton(Rect(R - 250, cy - 17, R - 98, cy + 17), L"في المجلد", ID_FOLDER, false, true, Icon::Folder);
        Tip(ID_FOLDER, L"إظهار في المجلد");
    }
}

void PlayerPanel::OnClick(int id)
{
    switch (id)
    {
    case ID_VIDEO:
    case ID_PLAY:  TogglePlay(); break;
    case ID_BACK5: SeekBy(-JUMP); break;
    case ID_FWD5:  SeekBy(JUMP);  break;
    case ID_MUTE:  s_muted = !s_muted; ApplyVolume(); break;
    case ID_TRIM:
        if (m_h.trim)
        {
            auto trim = m_h.trim;
            const std::wstring p = m_path;
            CloseThen([trim, p] { trim(p); });
        }
        break;
    case ID_FOLDER:
    {
        if (m_player) m_player->Pause();
        const std::wstring args = L"/select,\"" + m_path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        break;
    }
    case ID_BACK:
        if (m_h.back) { auto back = m_h.back; CloseThen([back] { back(); }); }
        else          RequestClose();
        break;
    }
}

bool PlayerPanel::OnDrag(int id, D2D1_POINT_2F p)
{
    if (id != ID_SEEK)
        return false;
    if (m_player)
    {
        const LONGLONG len = m_player->Length();
        const float f = std::clamp((p.x - m_seek.left) / (m_seek.right - m_seek.left), 0.0f, 1.0f);
        if (len > 0)
            m_player->Seek((LONGLONG)(f * len));
    }
    return true;
}

bool PlayerPanel::OnKeyDown(WPARAM vk)
{
    if (!m_player)
        return false;
    switch (vk)
    {
    case VK_SPACE: TogglePlay();   return true;
    case VK_LEFT:  SeekBy(-JUMP);  return true;      // الزمن من اليسار لليمين
    case VK_RIGHT: SeekBy(JUMP);   return true;
    case 'M':      s_muted = !s_muted; ApplyVolume(); return true;
    }
    return false;
}
