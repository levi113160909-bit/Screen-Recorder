// =============================================================
//  MediaControls.cpp - أزرار المشغّل المشتركة
// =============================================================
#include "MediaControls.h"
#include <algorithm>
#include <cmath>

namespace
{
    // نقطة على الدائرة: الزاوية بالدرجات من فوق ومع عقارب الساعة
    D2D1_POINT_2F OnCircle(float cx, float cy, float r, float deg)
    {
        const float a = deg * 3.14159265f / 180.0f;
        return D2D1::Point2F(cx + r * std::sin(a), cy - r * std::cos(a));
    }
}

void DrawJumpButton(float cx, float cy, float radius, int seconds, bool hover, bool enabled)
{
    Circle(cx, cy, radius, hover && enabled ? Theme::Hover : Theme::Card);
    const D2D1_COLOR_F color = enabled ? Theme::Text : Theme::Off;
    const bool  forward = seconds > 0;
    const float r = radius * 0.62f;

    // القوس: بيبلّش جنب السهم وبيلف 300 درجة ، وبيخلص فوق الرقم
    // تقديم: مع عقارب الساعة (الفتحة فوق يمين) ، رجوع: عكسها (الفتحة فوق يسار)
    ComPtr<ID2D1Factory> factory;
    g_rt->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> path;
    if (factory && SUCCEEDED(factory->CreatePathGeometry(&path)))
    {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(path->Open(&sink)))
        {
            const D2D1_POINT_2F start = OnCircle(cx, cy, r, forward ? 60.0f : -60.0f);
            const D2D1_POINT_2F end   = OnCircle(cx, cy, r, 0.0f);
            sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(end, D2D1::SizeF(r, r), 0.0f,
                                          forward ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                          D2D1_ARC_SIZE_LARGE));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            g_brush->SetColor(color);
            ComPtr<ID2D1StrokeStyle> round;
            factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                   D2D1_CAP_STYLE_ROUND), nullptr, 0, &round);
            g_rt->DrawGeometry(path.Get(), g_brush.Get(), std::max(1.6f, radius * 0.1f), round.Get());
        }
        // رأس السهم فوق الرقم ، لجهة الحركة
        ComPtr<ID2D1PathGeometry> head;
        if (SUCCEEDED(factory->CreatePathGeometry(&head)))
        {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(head->Open(&sink)))
            {
                const float s = std::max(3.5f, radius * 0.24f), dir = forward ? 1.0f : -1.0f;
                const float tx = cx, ty = cy - r;
                sink->BeginFigure(D2D1::Point2F(tx + dir * s, ty), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(tx - dir * s * 0.45f, ty - s * 0.9f));
                sink->AddLine(D2D1::Point2F(tx - dir * s * 0.45f, ty + s * 0.9f));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                g_brush->SetColor(color);
                g_rt->FillGeometry(head.Get(), g_brush.Get());
            }
        }
    }
    // الرقم بالنص
    Text(LtrText(std::to_wstring(std::abs(seconds))), g_fSmallLtr.Get(),
         Rect(cx - radius, cy - 8, cx + radius, cy + 9), color);
}

void DrawPlayButton(float cx, float cy, float radius, bool playing, bool hover, bool enabled)
{
    Circle(cx, cy, radius, !enabled ? Theme::Off : (hover ? D2D1::ColorF(0x6A9DFF) : Theme::Accent));
    Text(playing ? Icon::Pause : Icon::Play, radius >= 24 ? g_fIconBig.Get() : g_fIcon.Get(),
         Rect(cx - radius, cy - radius, cx + radius, cy + radius), Theme::White);
}

bool UpdatePlayerBitmap(VideoPlayer& player, int maxWidth, std::vector<BYTE>& px, int& w, int& h,
                        ComPtr<ID2D1Bitmap>& bmp)
{
    const bool fresh = player.Poll(maxWidth, px, w, h);
    if (px.empty())
        return false;
    if (fresh || !bmp)
    {
        if (bmp && bmp->GetPixelSize().width == (UINT32)w && bmp->GetPixelSize().height == (UINT32)h)
            bmp->CopyFromMemory(nullptr, px.data(), w * 4);
        else
        {
            bmp.Reset();
            g_rt->CreateBitmap(D2D1::SizeU(w, h), px.data(), w * 4,
                               D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
                               &bmp);
        }
    }
    return bmp != nullptr;
}

std::wstring MediaClock(LONGLONG t, bool tenths)
{
    const LONGLONG ms = std::max<LONGLONG>(0, t) / 10'000, s = ms / 1000;
    wchar_t buf[40];
    if (s >= 3600)  swprintf_s(buf, L"%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else if (tenths) swprintf_s(buf, L"%lld:%02lld.%lld", s / 60, s % 60, (ms % 1000) / 100);
    else            swprintf_s(buf, L"%lld:%02lld", s / 60, s % 60);
    return buf;
}
