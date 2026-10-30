// =============================================================
//  TrimPanel.cpp - "قصّ سريع"
// =============================================================
#include "TrimPanel.h"
#include "VideoTools.h"
#include <algorithm>
#include <thread>

namespace
{
    enum : int
    {
        ID_TRACK  = ID_PANEL_USER,
        ID_SAVE   = ID_PANEL_USER + 1,
        ID_BACK   = ID_PANEL_USER + 2,
        ID_CANCEL = ID_PANEL_USER + 3
    };
    constexpr int STRIP = 10;

    std::wstring Clock(LONGLONG t)
    {
        const LONGLONG ms = t / 10'000, s = ms / 1000;
        wchar_t buf[40];
        swprintf_s(buf, L"%lld:%02lld.%lld", s / 60, s % 60, (ms % 1000) / 100);
        return buf;
    }
}

TrimPanel::TrimPanel(const std::wstring& path, std::function<void(const std::wstring&)> done, std::function<void()> back)
    : m_path(path), m_done(std::move(done)), m_back(std::move(back))
{
    auto s = m_s;
    const std::wstring p = path;
    std::thread([s, p]
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        LONGLONG dur = 0;
        VideoDuration(p, dur);
        {
            std::lock_guard<std::mutex> lock(s->mutex);
            s->duration = dur;
        }
        // الشريط: 10 صور على طول الفيديو
        for (int i = 0; i < STRIP && !s->stop && dur > 0; ++i)
        {
            std::vector<BYTE> px;
            int w = 0, h = 0;
            if (!VideoFrameAt(p, dur * i / STRIP + dur / (STRIP * 2), 160, px, w, h))
                break;
            std::lock_guard<std::mutex> lock(s->mutex);
            s->strip.push_back(std::move(px));
            s->stripW = w; s->stripH = h;
        }
        // بعدين: المعاينة عند المقبض (آخر وقت انطلب بس)
        LONGLONG shown = -2;
        while (!s->stop)
        {
            LONGLONG want;
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                want = s->wantTime;
            }
            if (want < 0 || want == shown)
            {
                Sleep(40);
                continue;
            }
            std::vector<BYTE> px;
            int w = 0, h = 0;
            if (VideoFrameAt(p, want, 640, px, w, h))
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                s->preview = std::move(px);
                s->previewW = w; s->previewH = h;
                ++s->previewVersion;
            }
            shown = want;
        }
        if (SUCCEEDED(hr)) CoUninitialize();
    }).detach();
}

TrimPanel::~TrimPanel()
{
    m_s->stop = true;
    m_s->cancel = true;
}

void TrimPanel::DrawBody(float L, float top, float R)
{
    LONGLONG dur;
    {
        std::lock_guard<std::mutex> lock(m_s->mutex);
        dur = m_s->duration;
        if (g_rt.Get() != m_owner)
        {
            m_owner = g_rt.Get();
            m_previewBmp.Reset();
            m_stripBmp.clear();
            m_previewVer = 0;
        }
        const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (m_s->previewVersion != m_previewVer && !m_s->preview.empty())
        {
            m_previewBmp.Reset();
            g_rt->CreateBitmap(D2D1::SizeU(m_s->previewW, m_s->previewH), m_s->preview.data(), m_s->previewW * 4, props, &m_previewBmp);
            m_previewVer = m_s->previewVersion;
        }
        while (m_stripBmp.size() < m_s->strip.size())
        {
            ComPtr<ID2D1Bitmap> b;
            g_rt->CreateBitmap(D2D1::SizeU(m_s->stripW, m_s->stripH), m_s->strip[m_stripBmp.size()].data(), m_s->stripW * 4, props, &b);
            m_stripBmp.push_back(b);
        }
    }
    if (dur > 0 && m_end == 0)
    {
        m_end = dur;
        std::lock_guard<std::mutex> lock(m_s->mutex);
        m_s->wantTime = 0;
    }

    float y = top;
    // ---- المعاينة ----
    const float ph = 300, pw = ph * 16 / 9;
    const float cx = (L + R) / 2;
    const D2D1_RECT_F pv = Rect(cx - pw / 2, y, cx + pw / 2, y + ph);
    Fill(pv, D2D1::ColorF(0x000000), 10);
    if (m_previewBmp)
    {
        const D2D1_SIZE_F sz = m_previewBmp->GetSize();
        const float s = std::min(pw / sz.width, ph / sz.height);
        g_rt->DrawBitmap(m_previewBmp.Get(), Rect(cx - sz.width * s / 2, y + ph / 2 - sz.height * s / 2,
                                                   cx + sz.width * s / 2, y + ph / 2 + sz.height * s / 2), 1.0f,
                         D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    else
        Text(dur > 0 ? L"…" : L"جارٍ قراءة الفيديو…", g_fCenter.Get(), pv, Theme::SubText);
    y += ph + 16;

    // ---- الشريط (الزمن من اليسار لليمين متل كل مشغّلات الفيديو) ----
    const float tl = L + 8, tr = R - 8, th = 56;
    m_track = Rect(tl, y, tr, y + th);
    Fill(m_track, Theme::Background, 8);
    const float cellW = (tr - tl) / STRIP;
    for (size_t i = 0; i < m_stripBmp.size(); ++i)
        if (m_stripBmp[i])
            g_rt->DrawBitmap(m_stripBmp[i].Get(), Rect(tl + i * cellW, y, tl + (i + 1) * cellW, y + th), 0.85f,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    AddHit(Rect(tl - 10, y - 6, tr + 10, y + th + 6), ID_TRACK);
    if (dur > 0)
    {
        const float xs = tl + (tr - tl) * (float)m_start / dur, xe = tl + (tr - tl) * (float)m_end / dur;
        // اللي برّا التحديد: غامق
        Fill(Rect(tl, y, xs, y + th), D2D1::ColorF(0x000000, 0.65f));
        Fill(Rect(xe, y, tr, y + th), D2D1::ColorF(0x000000, 0.65f));
        Stroke(Rect(xs, y, xe, y + th), Theme::Accent, 4, 3);
        for (int k = 0; k < 2; ++k)
        {
            const float hx = k == 0 ? xs : xe;
            Fill(Rect(hx - 6, y - 6, hx + 6, y + th + 6), m_active == k ? Theme::Accent : Theme::White, 4);
        }
        y += th + 12;
        Text(L"البداية " + LtrText(Clock(m_start)), g_fBody.Get(), Rect(tl, y, tl + 220, y + 22), Theme::Text);
        Text(L"النهاية " + LtrText(Clock(m_end)), g_fBody.Get(), Rect(tr - 220, y, tr, y + 22), Theme::Text);
        Text(L"المدة " + LtrText(Clock(m_end - m_start)), g_fCenter.Get(), Rect(cx - 120, y, cx + 120, y + 22), Theme::Accent);
        y += 30;
        Text(L"اسحب المقبضين • يبدأ المقطع من أقرب إطار كامل قبل البداية (بفارق ثانيتين كحد أقصى)", g_fSmall.Get(),
             Rect(tl, y, tr, y + 18), Theme::SubText);
        y += 30;
    }
    else
        y += th + 72;

    // ---- الأزرار / التقدّم ----
    const int prog = m_s->progress;
    if (prog >= 0 && prog <= 100)
    {
        Fill(Rect(tl, y + 8, tr, y + 16), Theme::Border, 4);
        Fill(Rect(tl, y + 8, tl + (tr - tl) * prog / 100.0f, y + 16), Theme::Accent, 4);
        Text(L"جارٍ الحفظ… " + std::to_wstring(prog) + L"%", g_fSmall.Get(), Rect(tl, y + 20, tr, y + 38), Theme::SubText);
        DrawButton(Rect(L, y + 40, L + 110, y + 74), L"إلغاء", ID_CANCEL, false, true);
    }
    else
    {
        if (prog == -2)
            Text(L"تعذّر حفظ المقطع", g_fSmall.Get(), Rect(tl, y, tr, y + 18), Theme::Red);
        DrawButton(Rect(R - 160, y + 24, R, y + 58), L"حفظ المقطع", ID_SAVE, true, dur > 0 && m_end > m_start);
        DrawButton(Rect(R - 290, y + 24, R - 170, y + 58), L"رجوع", ID_BACK, false, true);
    }
    if (prog == 101 && !m_notified)
    {
        m_notified = true;
        std::wstring out;
        {
            std::lock_guard<std::mutex> lock(m_s->mutex);
            out = m_s->output;
        }
        auto done = m_done;
        CloseThen([done, out] { if (done) done(out); });
    }
}

bool TrimPanel::OnDrag(int id, D2D1_POINT_2F p)
{
    LONGLONG dur;
    {
        std::lock_guard<std::mutex> lock(m_s->mutex);
        dur = m_s->duration;
    }
    if (id != ID_TRACK || dur <= 0 || m_s->progress >= 0)
        return id == ID_TRACK;
    const float f = std::clamp((p.x - m_track.left) / (m_track.right - m_track.left), 0.0f, 1.0f);
    const LONGLONG t = (LONGLONG)(f * dur);
    const LONGLONG minLen = 10'000'000;                // ثانية على الأقل
    // أقرب مقبض للماوس هو اللي بيتحرك
    if (std::llabs(t - m_start) < std::llabs(t - m_end) || (t < m_start))
    {
        m_start = std::clamp<LONGLONG>(t, 0, std::max<LONGLONG>(0, m_end - minLen));
        m_active = 0;
    }
    else
    {
        m_end = std::clamp<LONGLONG>(t, std::min(dur, m_start + minLen), dur);
        m_active = 1;
    }
    std::lock_guard<std::mutex> lock(m_s->mutex);
    m_s->wantTime = m_active == 0 ? m_start : std::max<LONGLONG>(0, m_end - 1'000'000);
    return true;
}

void TrimPanel::OnClick(int id)
{
    if (id == ID_BACK)
    {
        auto back = m_back;
        CloseThen([back] { if (back) back(); });
    }
    else if (id == ID_CANCEL)
        m_s->cancel = true;
    else if (id == ID_SAVE && m_s->progress < 0)
    {
        // اسم جديد جنب الأصلي: "..._مقطع.mp4" (وإذا موجود: رقم)
        const size_t dot = m_path.find_last_of(L'.');
        std::wstring out;
        for (int n = 1; n < 1000; ++n)
        {
            out = m_path.substr(0, dot) + (n == 1 ? L"_مقطع" : L"_مقطع" + std::to_wstring(n)) + L".mp4";
            if (GetFileAttributesW(out.c_str()) == INVALID_FILE_ATTRIBUTES)
                break;
        }
        {
            std::lock_guard<std::mutex> lock(m_s->mutex);
            m_s->output = out;
        }
        m_s->cancel = false;
        m_s->progress = 0;
        auto s = m_s;
        const std::wstring in = m_path;
        const LONGLONG a = m_start, b = m_end;
        std::thread([s, in, out, a, b]
        {
            const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const HRESULT hr = TrimVideo(in, out, a, b, &s->progress, &s->cancel);
            if (FAILED(hr))
            {
                DeleteFileW(out.c_str());
                s->progress = hr == E_ABORT ? -1 : -2;      // إلغاء = رجوع عادي
            }
            else
                s->progress = 101;
            if (SUCCEEDED(hrCo)) CoUninitialize();
        }).detach();
    }
}
