// =============================================================
//  AppAudioPanel.cpp - نافذة "إضافة صوت برنامج"
// =============================================================
#include "AppAudioPanel.h"
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "windowscodecs.lib")

namespace
{
    enum : int
    {
        ID_REFRESH = ID_PANEL_USER,
        ID_APP_FIRST = ID_PANEL_USER + 100     // البرنامج رقم i
    };

    constexpr float TOOLBAR_H = 46;
    constexpr float SECTION_H = 30;
    constexpr float ROW_H     = 58;
    constexpr float ROW_GAP   = 6;
}

AppAudioPanel::AppAudioPanel(std::function<bool(const std::wstring&)> isAdded, std::function<void(const AudioApp&)> add)
    : m_isAdded(std::move(isAdded)), m_add(std::move(add))
{
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wic));
    Refresh();
}

void AppAudioPanel::Refresh()
{
    m_apps = ListAudioApps(GetActiveWindow());
    m_playingCount = 0;
    for (const AudioApp& a : m_apps)
        if (a.playing) ++m_playingCount;
}

void AppAudioPanel::UpdatePlaying()
{
    const std::vector<std::wstring> playing = ExePathsPlayingAudio();
    for (AudioApp& a : m_apps)
        a.playing = std::find(playing.begin(), playing.end(), LowerPath(a.exePath)) != playing.end();
    // ما منعيد الترتيب وانت عم تطلّع (مشان ما تتنطط الأسطر)، بس منعدّ
    m_playingCount = 0;
    for (const AudioApp& a : m_apps)
        if (a.playing) ++m_playingCount;
}

float AppAudioPanel::BodyHeight() const
{
    const float rows = (float)m_apps.size() * (ROW_H + ROW_GAP);
    if (m_apps.empty())
        return TOOLBAR_H + 120;
    // قسمين (إذا في برامج عم تطلّع صوت وبرامج لأ)
    int sections = 0;
    bool anyPlaying = false, anyQuiet = false;
    for (const AudioApp& a : m_apps) (a.playing ? anyPlaying : anyQuiet) = true;
    sections = (anyPlaying ? 1 : 0) + (anyQuiet ? 1 : 0);
    return TOOLBAR_H + sections * SECTION_H + rows + 8;
}

// -------------------------------------------------------------
//  أيقونة البرنامج الحقيقية (من ملف الـ exe)
// -------------------------------------------------------------
ID2D1Bitmap* AppAudioPanel::AppIcon(const std::wstring& exePath)
{
    if (m_iconRt.Get() != (ID2D1RenderTarget*)g_rt.Get())    // الرسّام تغيّر: منعيد الصور
    {
        m_iconRt = g_rt;
        m_icons.clear();
        m_iconTried.clear();
    }
    const std::wstring key = LowerPath(exePath);
    if (m_iconTried[key])
        return m_icons[key].Get();
    m_iconTried[key] = true;
    if (!m_wic || !g_rt)
        return nullptr;

    // أيقونة كبيرة (48) لحتى تبيّن حلوة على الشاشات العالية الدقة
    HICON icon = nullptr;
    if (FAILED(SHDefExtractIconW(exePath.c_str(), 0, 0, &icon, nullptr, MAKELONG(48, 16))) || !icon)
    {
        SHFILEINFOW info{};
        if (SHGetFileInfoW(exePath.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON))
            icon = info.hIcon;
    }
    if (!icon)
        return nullptr;

    ComPtr<IWICBitmap>          wicBmp;
    ComPtr<IWICFormatConverter> conv;
    ComPtr<ID2D1Bitmap>         bmp;
    if (SUCCEEDED(m_wic->CreateBitmapFromHICON(icon, &wicBmp)) &&
        SUCCEEDED(m_wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(wicBmp.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                   nullptr, 0, WICBitmapPaletteTypeMedianCut)))
        g_rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &bmp);
    DestroyIcon(icon);
    m_icons[key] = bmp;
    return bmp.Get();
}

// -------------------------------------------------------------
//  سطر برنامج
// -------------------------------------------------------------
float AppAudioPanel::DrawRow(float y, float L, float R, int i)
{
    const AudioApp& a = m_apps[i];
    const bool added = m_isAdded(a.exePath);
    const D2D1_RECT_F row = Rect(L, y, R, y + ROW_H);
    const bool hover = AddHit(row, ID_APP_FIRST + i) && !added;

    Fill(row, hover ? Theme::Hover : Theme::Card, 10);
    if (hover)
        Stroke(row, WithAlpha(Theme::Accent, 0.6f), 10);

    // الأيقونة على اليمين
    const D2D1_RECT_F ib = Rect(R - 50, y + 13, R - 18, y + 45);
    if (ID2D1Bitmap* bmp = AppIcon(a.exePath))
        g_rt->DrawBitmap(bmp, ib, added ? 0.45f : 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else
    {
        Fill(ib, Theme::Hover, 8);
        Text(Icon::Window, g_fIcon.Get(), ib, Theme::SubText);
    }

    // الاسم والعنوان
    const float textL = L + 130;
    Text(a.name, g_fBody.Get(), Rect(textL, y + 9, R - 62, y + 31), added ? Theme::SubText : Theme::Text);
    std::wstring sub = a.title;
    if (sub.empty() || sub == a.name)
        sub = a.playing ? L"يُصدر صوتًا الآن" : L"يعمل في الخلفية";
    Text(sub, g_fSmall.Get(), Rect(textL, y + 31, R - 62, y + 49), Theme::SubText);

    // على اليسار: مضاف، أو زر الإضافة
    const D2D1_RECT_F action = Rect(L + 12, y + 14, L + 100, y + 44);
    if (added)
    {
        Text(L"", g_fIcon.Get(), Rect(action.right - 22, action.top, action.right, action.bottom), Theme::Green);
        Text(L"مُضاف", g_fSegment.Get(), Rect(action.left, action.top, action.right - 24, action.bottom), Theme::Green);
    }
    else
    {
        Fill(action, hover ? Theme::Accent : WithAlpha(Theme::Accent, 0.14f), 8);
        Text(L"+  إضافة", g_fSegment.Get(), action, hover ? Theme::White : Theme::Accent);
    }

    // مؤشر صوت متحرك (3 أعمدة) للبرامج اللي عم تطلّع صوت
    if (a.playing)
    {
        const float bx = L + 112, by = y + ROW_H / 2;
        for (int k = 0; k < 3; ++k)
        {
            const float t = m_frame * 0.25f + k * 1.7f + i;
            const float h = 5 + 9 * (0.5f + 0.5f * std::sin(t));
            Fill(Rect(bx + k * 5, by - h / 2, bx + k * 5 + 3, by + h / 2), Theme::Green, 1.5f);
        }
    }
    return y + ROW_H + ROW_GAP;
}

void AppAudioPanel::DrawBody(float L, float top, float R)
{
    ++m_frame;
    if (m_frame % 30 == 0)          // كل ثانية تقريباً: مين عم يطلّع صوت
        UpdatePlaying();

    // شريط علوي: عدد البرامج + زر التحديث
    float y = top;
    Text(std::to_wstring(m_apps.size()) + L" برنامج مفتوح", g_fSmall.Get(), Rect(L + 130, y, R, y + 34), Theme::SubText);
    DrawButton(Rect(L, y, L + 120, y + 34), L"تحديث", ID_REFRESH, false, true, Icon::Refresh);
    y += TOOLBAR_H;

    if (m_apps.empty())
    {
        Text(L"لا توجد برامج مفتوحة", g_fCenter.Get(), Rect(L, y + 20, R, y + 50), Theme::SubText);
        Text(L"افتح البرنامج ثم اضغط «تحديث»", g_fSmall.Get(), Rect(L, y + 54, R, y + 74), Theme::SubText);
        return;
    }

    // قسمين: اللي عم يطلّعوا صوت، وبعدين الباقي (بنفس ترتيب اللائحة)
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool wantPlaying = (pass == 0);
        bool header = false;
        for (int i = 0; i < (int)m_apps.size(); ++i)
        {
            if (m_apps[i].playing != wantPlaying)
                continue;
            if (!header)
            {
                if (wantPlaying)
                    Circle(R - 6, y + 15, 4, Theme::Green);
                Text(wantPlaying ? L"يُصدر صوتًا الآن" : L"برامج مفتوحة أخرى", g_fSection.Get(),
                     Rect(L, y, wantPlaying ? R - 16 : R, y + SECTION_H), Theme::SubText);
                y += SECTION_H;
                header = true;
            }
            y = DrawRow(y, L, R, i);
        }
    }
}

void AppAudioPanel::OnClick(int id)
{
    if (id == ID_REFRESH)
    {
        Refresh();
        return;
    }
    const int i = id - ID_APP_FIRST;
    if (i < 0 || i >= (int)m_apps.size() || m_isAdded(m_apps[i].exePath))
        return;
    m_add(m_apps[i]);
    RequestClose();
}
