// =============================================================
//  DevicePanel.cpp - نافذة "إعدادات جهاز الالتقاط" (شوف DevicePanel.h)
// =============================================================
#include "DevicePanel.h"
#include "CaptureDevice.h"
#include "Media.h"
#include <algorithm>
#include <cmath>

namespace
{
    enum : int
    {
        ID_MODE_FIRST = ID_PANEL_USER,                 // DEVICE_MODE_COUNT خيارات
        ID_AUDIO_TOGGLE = ID_PANEL_USER + 20,
        ID_AUDIO_RESET,
        ID_AUDIO_VOLUME,
        ID_AUDIO_DELAY,
        ID_AUDIO_MUTE,
        ID_AUDIO_PICK
    };

    std::wstring Ms(float v) { return LtrText(std::to_wstring((int)std::lround(v))) + L" ملّي ثانية"; }
}

void DevicePanel::DrawBody(float L, float top, float R)
{
    SceneLayer l;
    if (!m_h.get || !m_h.get(l))
    {
        Text(L"طبقة جهاز الالتقاط لم تعد موجودة", g_fCenter.Get(), Rect(L, top, R, top + 60), Theme::SubText);
        m_height = 80;
        return;
    }
    float y = top;
    const float lft = L + 16, rgt = R - 16;

    // ---- الحالة ----
    {
        const D2D1_RECT_F card = Rect(L, y, R, y + 84);
        Fill(card, Theme::Card, 12);
        Text(l.target.name, g_fBody.Get(), Rect(lft, y + 12, rgt, y + 36), Theme::Text);
        int w = 0, h = 0;
        double fps = 0;
        std::wstring state;
        D2D1_COLOR_F color = Theme::SubText;
        const bool black = DeviceLooksBlack(l.source);
        if (l.lost)                                    { state = L"الجهاز غير متصل"; color = Theme::Red; }
        else if (DeviceActualFormat(l.source, w, h, fps))
        {
            state = L"يعمل  •  " + LtrText(std::to_wstring(w) + L"×" + std::to_wstring(h)) + L"  •  "
                  + LtrText(std::to_wstring((int)std::lround(fps))) + L" إطار/ث";
            color = black ? Theme::Yellow : Theme::Green;
        }
        else                                           state = L"جارٍ التشغيل…";
        Circle(rgt - 5, y + 57, 4.5f, color);
        Text(state, g_fSmall.Get(), Rect(lft, y + 46, rgt - 16, y + 68), color);
        y += 96;

        if (black && !l.lost)          // HDCP: البلايستيشن بيمنع التصوير
        {
            const D2D1_RECT_F warn = Rect(L, y, R, y + 76);
            Fill(warn, WithAlpha(Theme::Yellow, 0.12f), 12);
            Stroke(warn, WithAlpha(Theme::Yellow, 0.5f), 12);
            Text(L"الصورة سوداء؟ أطفئ HDCP في الجهاز", g_fBody.Get(), Rect(lft, y + 10, rgt, y + 34), Theme::Yellow);
            Text(L"PS5: الإعدادات ← النظام ← HDMI ← إلغاء «تمكين HDCP»", g_fSmall.Get(), Rect(lft, y + 38, rgt, y + 60), Theme::Text);
            y += 88;
        }
    }

    // ---- الدقة ----
    Text(L"الدقة والإطارات", g_fSection.Get(), Rect(lft, y, rgt, y + 24), Theme::SubText);
    y += 28;
    {
        std::vector<std::wstring> labels;
        for (int i = 0; i < DEVICE_MODE_COUNT; ++i)
            labels.push_back(i == 0 ? std::wstring(DeviceModeName(i)) : LtrText(DeviceModeName(i)));
        DrawSegmented(Rect(L, y, R, y + 36), labels, std::clamp(l.mode, 0, DEVICE_MODE_COUNT - 1), ID_MODE_FIRST, true);
        y += 42;
        Text(l.mode == 0 ? L"تلقائي: أعلى دقة حتى 1080p بـ 60 إطارًا (الأنسب للبث)"
                         : L"إذا لم يدعم الجهاز الخيار يُستخدم أقرب خيار له. 4K تحتاج USB 3 ومعالجًا أقوى",
             g_fSmall.Get(), Rect(lft, y, rgt, y + 20), Theme::SubText);
        y += 32;
    }

    // ---- الصوت ----
    {
        const int idx = l.audioId.empty() ? -1 : m_mixer.FindDevice(l.audioId);
        const bool on = idx >= 0;
        const AppAudioEntry e = on ? m_mixer.Entry(idx) : AppAudioEntry{};
        const float cardH = on ? 64.0f + 44.0f * 2 + 104.0f : 64.0f + 52.0f;
        const D2D1_RECT_F card = Rect(L, y, R, y + cardH);
        float cy = Section(card, L"صوت الجهاز", on ? e.name : std::wstring(L"صوت اللعبة من كرت الالتقاط"), on, ID_AUDIO_TOGGLE, ID_AUDIO_RESET);
        if (on)
        {
            const float l2 = L + 16, r2 = R - 16;
            cy = SliderRow(cy - 4, l2, r2, L"العلو", LtrText(std::to_wstring((int)std::lround(e.volume * 100)) + L"%"),
                           e.volume, 0.0f, 2.0f, 0.05f, ID_AUDIO_VOLUME, [this, idx](float v) { m_mixer.SetVolume(idx, v); });
            cy = SliderRow(cy, l2, r2, L"تأخير الصوت", Ms((float)e.delayMs), (float)e.delayMs, 0.0f, 500.0f, 10.0f,
                           ID_AUDIO_DELAY, [this, idx](float v) { m_mixer.SetDelay(idx, (int)std::lround(v)); });
            // مستوى الصوت
            m_level = std::max(PeakToMeter(m_mixer.TakePeak(idx)), m_level * 0.88f);
            DrawMeter(Rect(l2, cy + 6, r2, cy + 12), m_level, m_mixer.IsConnected(idx) && !e.muted);
            cy += 22;
            DrawButton(Rect(r2 - 190, cy, r2, cy + 34), L"اختيار جهاز الصوت…", ID_AUDIO_PICK, false, true, Icon::Speaker);
            DrawButton(Rect(r2 - 310, cy, r2 - 200, cy + 34), e.muted ? L"إلغاء الكتم" : L"كتم", ID_AUDIO_MUTE, false, true,
                       e.muted ? Icon::Muted : nullptr);
            if (!m_mixer.IsConnected(idx))
                Text(L"جهاز الصوت غير متصل", g_fSmallEnd.Get(), Rect(l2, cy, r2 - 320, cy + 34), Theme::Red);
            cy += 46;
            Text(L"إذا سبق الصوتُ الصورة زِد التأخير قليلًا (عادة بين 30 و 120)", g_fSmall.Get(), Rect(l2, cy - 2, r2, cy + 18), Theme::SubText);
        }
        else
        {
            DrawButton(Rect(R - 16 - 190, cy + 2, R - 16, cy + 36), L"اختيار جهاز الصوت…", ID_AUDIO_PICK, false, true, Icon::Speaker);
            Text(l.audioId.empty() ? L"لم يُعثر على صوت هذا الجهاز تلقائيًا" : L"الصوت متوقف",
                 g_fSmallEnd.Get(), Rect(L + 16, cy + 2, R - 220, cy + 36), Theme::SubText);
        }
        y += cardH + 12;
    }

    // ---- نصائح ----
    {
        const D2D1_RECT_F card = Rect(L, y, R, y + 100);
        Fill(card, Theme::Card, 12);
        Text(L"نصائح", g_fBody.Get(), Rect(lft, y + 10, rgt, y + 32), Theme::Text);
        Text(L"• للعب بلا تأخير: وصّل التلفزيون بمخرج الكرت (HDMI Out) والعب عليه", g_fSmall.Get(), Rect(lft, y + 36, rgt, y + 56), Theme::SubText);
        Text(L"• صِل الكرت بمنفذ USB 3 (الأزرق) وليس بمنفذ USB 2", g_fSmall.Get(), Rect(lft, y + 56, rgt, y + 76), Theme::SubText);
        Text(L"• أطفئ «صوت النظام» إذا كنت تسمع صوت اللعبة مرتين", g_fSmall.Get(), Rect(lft, y + 76, rgt, y + 96), Theme::SubText);
        y += 112;
    }
    m_height = y - top;
}

void DevicePanel::OnClick(int id)
{
    SceneLayer l;
    if (!m_h.get || !m_h.get(l))
        return;
    if (id >= ID_MODE_FIRST && id < ID_MODE_FIRST + DEVICE_MODE_COUNT)
    {
        const int mode = id - ID_MODE_FIRST;
        m_h.modify([mode](SceneLayer& x) { x.mode = mode; });
        m_h.save();
        return;
    }
    const int idx = l.audioId.empty() ? -1 : m_mixer.FindDevice(l.audioId);
    switch (id)
    {
    case ID_AUDIO_TOGGLE: if (m_h.setAudio) m_h.setAudio(idx < 0); break;
    case ID_AUDIO_RESET:  if (idx >= 0) { m_mixer.SetDelay(idx, 0); m_mixer.SetVolume(idx, 1.0f); m_mixer.SetMuted(idx, false); } break;
    case ID_AUDIO_MUTE:   if (idx >= 0) m_mixer.SetMuted(idx, !m_mixer.Entry(idx).muted); break;
    case ID_AUDIO_PICK:   if (m_h.pickAudio) CloseThen(m_h.pickAudio); return;
    default: return;
    }
    m_h.save();
}
