// =============================================================
//  FirstRunPanel.cpp - نافذة "أهلاً بك"
// =============================================================
#include "FirstRunPanel.h"

namespace
{
    enum : int
    {
        ID_CARD   = ID_PANEL_USER,        // +0..2 (لقطات ، تسجيل ، بث)
        ID_SECS   = ID_PANEL_USER + 10,   // +0..3
        ID_KEY    = ID_PANEL_USER + 20,
        ID_NEXT   = ID_PANEL_USER + 21,
        ID_BACK   = ID_PANEL_USER + 22,
        ID_SKIP   = ID_PANEL_USER + 23
    };
    const int SECONDS[] = { 15, 30, 60, 120 };
}

void FirstRunPanel::DrawBody(float L, float top, float R)
{
    float y = top;
    if (m_step == 0)
    {
        Text(L"ماذا تريد أن تفعل بالبرنامج؟", g_fSection.Get(), Rect(L, y, R, y + 26), Theme::Text);
        Text(L"اختر واحدًا أو أكثر، وسنضبط كل شيء لك تلقائيًا", g_fSmall.Get(), Rect(L, y + 28, R, y + 46), Theme::SubText);
        y += 62;
        struct Card { const wchar_t* glyph; const wchar_t* title; const wchar_t* sub; bool on; };
        const Card cards[] = {
            { L"", L"حفظ اللقطات", L"اضغط زرًا لحفظ آخر ثوانٍ من اللعب، مثل برنامج NVIDIA", m_c.clips },
            { L"", L"تسجيل مقاطع", L"سجّل الشاشة أو اللعبة بالطول الذي تريده", m_c.record },
            { L"", L"البث المباشر", L"على يوتيوب وتويتش وتيك توك وغيرها", m_c.stream },
        };
        const float gap = 14, cw = (R - L - gap * 2) / 3, ch = 210;
        for (int i = 0; i < 3; ++i)
        {
            const float right = R - i * (cw + gap);
            const D2D1_RECT_F c = Rect(right - cw, y, right, y + ch);
            const bool hover = AddHit(c, ID_CARD + i);
            const bool on = cards[i].on;
            Fill(c, on ? WithAlpha(Theme::Accent, 0.16f) : (hover ? Theme::Hover : Theme::Card), 14);
            Stroke(c, on ? Theme::Accent : Theme::Border, 14, on ? 2.0f : 1.0f);
            const D2D1_RECT_F ib = Rect((c.left + c.right) / 2 - 32, y + 26, (c.left + c.right) / 2 + 32, y + 90);
            Fill(ib, WithAlpha(Theme::Accent, on ? 0.35f : 0.14f), 16);
            Text(cards[i].glyph, g_fIconBig.Get(), ib, Theme::Accent);
            Text(cards[i].title, g_fSection.Get(), Rect(c.left + 12, y + 104, c.right - 12, y + 130), Theme::Text);
            Text(cards[i].sub, g_fSmall.Get(), Rect(c.left + 14, y + 134, c.right - 14, y + 190), Theme::SubText);
            // علامة الاختيار
            const D2D1_RECT_F box = Rect(c.right - 34, c.top + 12, c.right - 12, c.top + 34);
            Fill(box, on ? Theme::Accent : Theme::Background, 6);
            if (on) Text(L"", g_fIcon.Get(), box, Theme::White);
        }
        y += ch + 30;
        const bool any = m_c.clips || m_c.record || m_c.stream;
        DrawButton(Rect(R - 150, y, R, y + 40), L"التالي", ID_NEXT, true, any);
        DrawButton(Rect(L, y, L + 140, y + 40), L"تخطَّ", ID_SKIP, false, true);
        return;
    }

    if (m_step == 1)
    {
        Text(L"كم ثانية تريد أن تحفظ؟", g_fSection.Get(), Rect(L, y, R, y + 26), Theme::Text);
        y += 40;
        DrawSegmented(Rect(L, y, R, y + 42), { L"15 ثانية", L"30 ثانية", L"دقيقة", L"دقيقتان" }, m_c.clipSeconds, ID_SECS, true);
        y += 62;

        // الشرح الحي: الرقم بيتغيّر مع اختيارك
        const D2D1_RECT_F info = Rect(L, y, R, y + 120);
        Fill(info, WithAlpha(Theme::Accent, 0.10f), 14);
        Text(L"عندما يحدث شيء رائع، اضغط", g_fBody.Get(), Rect(L + 20, y + 16, R - 20, y + 40), Theme::Text);
        const std::wstring key = m_capturing ? std::wstring(L"اضغط الزر الجديد الآن…") : LtrText(m_h.saveKey());
        const D2D1_RECT_F kb = Rect(R - 300, y + 46, R - 20, y + 86);
        Fill(kb, m_capturing ? WithAlpha(Theme::Yellow, 0.2f) : Theme::Background, 10);
        Stroke(kb, m_capturing ? Theme::Yellow : Theme::Accent, 10, 1.5f);
        Text(key, g_fSection.Get(), Rect(kb.left + 10, kb.top, kb.right - 10, kb.bottom), m_capturing ? Theme::Yellow : Theme::Text);
        Text(L"لحفظ آخر " + std::to_wstring(SECONDS[m_c.clipSeconds]) + L" ثانية فورًا", g_fBody.Get(),
             Rect(L + 20, y + 46, R - 310, y + 86), Theme::Text);
        DrawButton(Rect(L + 20, y + 90 - 4, L + 160, y + 114), L"تغيير الزر", ID_KEY, false, !m_capturing);
        y += 136;
        Text(L"تعمل في الخلفية بجودة عالية جدًا ولا تُبطئ اللعبة • تبدأ تلقائيًا عند فتح أي لعبة", g_fSmall.Get(),
             Rect(L, y, R, y + 20), Theme::SubText);
        y += 50;
        DrawButton(Rect(R - 150, y, R, y + 40), L"التالي", ID_NEXT, true, !m_capturing);
        DrawButton(Rect(L, y, L + 140, y + 40), L"رجوع", ID_BACK, false, !m_capturing);
        return;
    }

    // ---- الملخّص ----
    Text(L"جاهز! هذا ما ضبطناه لك:", g_fSection.Get(), Rect(L, y, R, y + 26), Theme::Text);
    y += 42;
    for (const std::wstring& line : m_h.summary(m_c))
    {
        Circle(R - 8, y + 11, 4, Theme::Accent);
        Text(line, g_fBody.Get(), Rect(L, y, R - 22, y + 24), Theme::Text);
        y += 34;
    }
    y += 10;
    Text(L"يمكنك تغيير أي شيء لاحقًا من «الإعدادات»", g_fSmall.Get(), Rect(L, y, R, y + 18), Theme::SubText);
    y += 40;
    DrawButton(Rect(R - 150, y, R, y + 40), L"ابدأ", ID_NEXT, true, true);
    DrawButton(Rect(L, y, L + 140, y + 40), L"رجوع", ID_BACK, false, true);
}

void FirstRunPanel::OnClick(int id)
{
    if (m_capturing)
        return;
    if (id >= ID_CARD && id < ID_CARD + 3)
    {
        bool* v[] = { &m_c.clips, &m_c.record, &m_c.stream };
        *v[id - ID_CARD] = !*v[id - ID_CARD];
    }
    else if (id >= ID_SECS && id < ID_SECS + 4)
        m_c.clipSeconds = id - ID_SECS;
    else if (id == ID_KEY && m_h.changeKey)
    {
        m_capturing = true;
        std::weak_ptr<bool> alive = m_alive;
        m_h.changeKey([this, alive] { if (auto a = alive.lock(); a && *a) m_capturing = false; });
    }
    else if (id == ID_BACK)
        m_step = (m_step == 2 && !m_c.clips) ? 0 : m_step - 1;
    else if (id == ID_SKIP)
    {
        auto skip = m_h.skip;
        CloseThen([skip] { if (skip) skip(); });
    }
    else if (id == ID_NEXT)
    {
        if (m_step == 0)
        {
            if (!(m_c.clips || m_c.record || m_c.stream)) return;
            m_step = m_c.clips ? 1 : 2;
        }
        else if (m_step == 1)
            m_step = 2;
        else
        {
            const SetupChoice c = m_c;
            auto apply = m_h.apply;
            CloseThen([apply, c] { if (apply) apply(c); });
        }
    }
}
