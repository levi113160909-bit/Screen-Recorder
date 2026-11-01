// =============================================================
//  TranslationPanel.cpp - نافذة "ترجمة اللعبة إلى العربية" (شوف TranslationPanel.h)
// =============================================================
#include "TranslationPanel.h"
#include "GameOcr.h"
#include "PlayerOverlay.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <mutex>
#include <thread>

namespace
{
    enum : int
    {
        ID_PROFILE_FIRST = ID_PANEL_USER,          // +0 .. +39
        ID_PROFILE_NEW   = ID_PANEL_USER + 40,
        ID_PROFILE_RENAME,
        ID_PROFILE_DELETE,
        ID_SOURCE_FIRST  = ID_PANEL_USER + 50,     // +50 .. +79
        ID_SOURCE_SCREEN = ID_PANEL_USER + 80,
        ID_PREVIEW       = ID_PANEL_USER + 90,
        ID_ENGINE_FIRST  = ID_PANEL_USER + 100,    // TRANS_ENGINE_COUNT
        ID_KEY           = ID_PANEL_USER + 110,
        ID_MODEL,
        ID_EXTRA,
        ID_TEST,
        ID_DIALECT_FIRST = ID_PANEL_USER + 120,    // TRANS_DIALECT_COUNT
        ID_GAME          = ID_PANEL_USER + 126,
        ID_SOFTEN,
        ID_GLOSS_ADD     = ID_PANEL_USER + 130,
        ID_GLOSS_DEL     = ID_PANEL_USER + 131,    // +131 .. +229
        ID_DELAY         = ID_PANEL_USER + 240,
        ID_DELAY_ALL,
        ID_FONT          = ID_PANEL_USER + 250,
        ID_BOX,
        ID_COLOR_FIRST   = ID_PANEL_USER + 252,    // TRANS_COLOR_COUNT
        ID_THRESHOLD     = ID_PANEL_USER + 260,
        ID_THRESHOLD_ON,
        ID_ADD_SCENE     = ID_PANEL_USER + 270,
        ID_FIT,
        ID_CLEAR_CACHE,
        ID_PLAYER_OVERLAY = ID_PANEL_USER + 280,
        ID_OVERLAY_MON_FIRST,                      // +281 .. +296
    };
    constexpr int MAX_PROFILES = 40, MAX_SOURCES = 30, MAX_GLOSSARY = 99, MAX_OVERLAY_SCREENS = 16;
    static_assert(ID_OVERLAY_MON_FIRST + MAX_OVERLAY_SCREENS < ID_PANEL_SETTINGS_NAV, "TranslationPanel: الأرقام وصلت لأرقام الإعدادات");

    std::wstring Seconds(int ms)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f", ms / 1000.0);
        return LtrText(buf) + L" ثانية";
    }
}

struct TranslationPanel::TestState
{
    std::mutex   mx;
    bool         running = false, done = false, ok = false;
    std::wstring result;
    int          ms = 0;
};

TranslationPanel::TranslationPanel(TranslationPanelHooks hooks, const std::wstring& profileId, bool addMode)
    : m_h(std::move(hooks)), m_id(profileId), m_addMode(addMode), m_test(std::make_shared<TestState>())
{
    if (m_h.devices)
        m_devices = m_h.devices();
    if (m_h.monitors)
        m_monitors = m_h.monitors();
    m_all = TranslateProfiles();
    if (m_id.empty() && !m_all.empty())
        m_id = m_all.front().id;
    if (m_id.empty() || !Load())
        NewProfile();
}

TranslationPanel::~TranslationPanel()
{
    SetTranslatePlaceholder(false);
    if (!m_p.id.empty())
        SaveTranslateProfile(m_p);
}

bool TranslationPanel::Load()
{
    return GetTranslateProfile(m_id, m_p);
}

void TranslationPanel::Apply(bool persist)
{
    SetTranslateProfile(m_p, persist);
}

void TranslationPanel::OnCommit()
{
    Apply(true);
}

// ملف جديد: منختار أول جهاز التقاط بالمشهد ، ومكان الترجمة المعتاد (تحت بالنص)
void TranslationPanel::NewProfile()
{
    TranslateProfile p;
    p.id   = NewTranslateProfileId();
    p.name = m_all.empty() ? L"GTA" : L"لعبة " + std::to_wstring(m_all.size() + 1);
    if (!m_devices.empty())
    {
        p.deviceLink = m_devices.front().link;
        p.deviceName = m_devices.front().name;
    }
    else if (!m_monitors.empty())
    {
        p.fromScreen = true;
        p.monitor    = m_monitors.front().rect;
    }
    // سياق جاهز للعبة اللي بدك تبثها
    if (m_all.empty())
    {
        p.game = L"Grand Theft Auto";
        p.glossary = { { L"Lucia", L"لوسيا" }, { L"Jason", L"جيسون" }, { L"Vice City", L"فايس سيتي" }, { L"Leonida", L"ليونيدا" } };
    }
    SaveTranslateProfile(p);
    m_p  = p;
    m_id = p.id;
    m_all = TranslateProfiles();
    if (!m_addMode && m_h.useProfile)
        m_h.useProfile(m_id);
}

// -------------------------------------------------------------
//  أجزاء صغيرة
// -------------------------------------------------------------
float TranslationPanel::ToggleRow(float y, float L, float R, const std::wstring& label, const std::wstring& sub, bool on, int id)
{
    const D2D1_RECT_F row = Rect(L, y, R, y + 48);
    AddHit(row, id);
    DrawToggle(L + 4, y + 12, on, true);
    Text(label, g_fBody.Get(), Rect(L + 60, y + 4, R, y + 26), on ? Theme::Text : Theme::SubText);
    if (!sub.empty())
        Text(sub, g_fSmall.Get(), Rect(L + 60, y + 26, R, y + 44), Theme::SubText);
    return y + 52;
}

float TranslationPanel::DrawProfiles(float y, float L, float R)
{
    Text(L"الملف الشخصي (لكل لعبة ملف)", g_fSection.Get(), Rect(L, y, R, y + 24), Theme::SubText);
    y += 28;
    float x = R;
    for (int i = 0; i < (int)m_all.size() && i < MAX_PROFILES; ++i)
    {
        const std::wstring& name = m_all[i].name;
        const float w = std::min(200.0f, TextWidth(name, g_fSegment.Get()) + 30);
        if (x - w < L + 330)                 // ما في مكان: سطر جديد
        {
            x = R;
            y += 40;
        }
        const D2D1_RECT_F chip = Rect(x - w, y, x, y + 34);
        const bool sel = m_all[i].id == m_id;
        const bool hover = AddHit(chip, ID_PROFILE_FIRST + i);
        Fill(chip, sel ? Theme::Accent : (hover ? Theme::Hover : Theme::Card), 9);
        Text(name, g_fSegment.Get(), Rect(chip.left + 8, chip.top, chip.right - 8, chip.bottom), sel ? Theme::White : Theme::Text);
        x -= w + 6;
    }
    DrawButton(Rect(L + 220, y, L + 320, y + 34), L"جديد", ID_PROFILE_NEW, false, true, Icon::Add);
    DrawButton(Rect(L + 110, y, L + 212, y + 34), L"تسمية", ID_PROFILE_RENAME, false, true);
    DrawButton(Rect(L, y, L + 102, y + 34), L"حذف", ID_PROFILE_DELETE, false, m_all.size() > 1, Icon::Delete);
    return y + 48;
}

// ---- المصدر + الصورة الحيّة + مكان الترجمة ----
float TranslationPanel::DrawSource(float y, float L, float R)
{
    Text(L"١. المصدر ومكان الترجمة", g_fBody.Get(), Rect(L, y, R, y + 26), Theme::Text);
    y += 30;

    // شرائح المصادر: أجهزة الالتقاط بالمشهد + الشاشة
    float x = R;
    auto chip = [&](const std::wstring& label, bool sel, int id)
    {
        const float w = std::min(260.0f, TextWidth(label, g_fSegment.Get()) + 34);
        if (x - w < L)
        {
            x = R;
            y += 40;
        }
        const D2D1_RECT_F c = Rect(x - w, y, x, y + 34);
        const bool hover = AddHit(c, id);
        Fill(c, sel ? WithAlpha(Theme::Accent, 0.22f) : (hover ? Theme::Hover : Theme::Card), 9);
        Stroke(c, sel ? Theme::Accent : Theme::Border, 9);
        Text(label, g_fSegment.Get(), Rect(c.left + 8, c.top, c.right - 8, c.bottom), sel ? Theme::Text : Theme::SubText);
        x -= w + 6;
    };
    bool known = m_p.fromScreen;
    for (int i = 0; i < (int)m_devices.size() && i < MAX_SOURCES; ++i)
    {
        const bool sel = !m_p.fromScreen && m_devices[i].link == m_p.deviceLink;
        known = known || sel;
        chip(m_devices[i].name, sel, ID_SOURCE_FIRST + i);
    }
    chip(L"جزء من الشاشة (ألعاب الكمبيوتر)", m_p.fromScreen, ID_SOURCE_SCREEN);
    y += 42;
    if (!known)
    {
        Text(m_p.deviceLink.empty() ? L"أضف «جهاز التقاط» إلى المشهد أولًا ثم اختره هنا"
                                    : L"«" + m_p.deviceName + L"» ليس في المشهد الحالي",
             g_fSmall.Get(), Rect(L, y, R, y + 20), Theme::Yellow);
        y += 26;
    }

    // ---- الصورة الحيّة: اسحب مستطيلًا فوق مكان الترجمة ----
    const float pw = R - L, ph = pw * 9 / 16;
    const D2D1_RECT_F box = Rect(L, y, R, y + ph);
    Fill(box, Theme::Background, 8);
    std::shared_ptr<const MediaFrame> f = TranslateSourceFrame(m_p);
    if (f && f->width > 0 && g_rt)
    {
        if (!m_bitmap || m_owner != g_rt.Get() || m_bmpW != f->width || m_bmpH != f->height)
        {
            m_bitmap.Reset();
            m_owner = g_rt.Get();
            const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
            g_rt->CreateBitmap(D2D1::SizeU(f->width, f->height), f->pixels.data(), f->width * 4, props, &m_bitmap);
            m_bmpW = f->width;
            m_bmpH = f->height;
            m_version = f->version;
        }
        else if (m_version != f->version && (m_frame % 3 == 0 || m_dragging))   // 10 مرات بالثانية بتكفي
        {
            m_bitmap->CopyFromMemory(nullptr, f->pixels.data(), f->width * 4);
            m_version = f->version;
        }
        const float s = std::min(pw / f->width, ph / f->height);
        const float dw = f->width * s, dh = f->height * s;
        m_imgDst = Rect(box.left + (pw - dw) / 2, box.top + (ph - dh) / 2, box.left + (pw + dw) / 2, box.top + (ph + dh) / 2);
        if (m_bitmap)
            g_rt->DrawBitmap(m_bitmap.Get(), m_imgDst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    else
    {
        m_imgDst = box;
        Text(m_p.fromScreen ? L"تعذّر التقاط الشاشة" : L"بانتظار صورة جهاز الالتقاط…", g_fCenter.Get(), box, Theme::SubText);
    }
    AddHit(box, ID_PREVIEW);

    // المستطيل: اللي برّاته أغمق
    const float iw = m_imgDst.right - m_imgDst.left, ih = m_imgDst.bottom - m_imgDst.top;
    const D2D1_RECT_F reg = Rect(m_imgDst.left + (float)m_p.rx * iw, m_imgDst.top + (float)m_p.ry * ih,
                                 m_imgDst.left + (float)(m_p.rx + m_p.rw) * iw, m_imgDst.top + (float)(m_p.ry + m_p.rh) * ih);
    const D2D1_COLOR_F shade = D2D1::ColorF(0x000000, 0.45f);
    Fill(Rect(m_imgDst.left, m_imgDst.top, m_imgDst.right, reg.top), shade);
    Fill(Rect(m_imgDst.left, reg.bottom, m_imgDst.right, m_imgDst.bottom), shade);
    Fill(Rect(m_imgDst.left, reg.top, reg.left, reg.bottom), shade);
    Fill(Rect(reg.right, reg.top, m_imgDst.right, reg.bottom), shade);
    Stroke(reg, Theme::Accent, 4, 2);
    Stroke(box, Theme::Border, 8);
    y += ph + 6;
    Text(L"اسحب بالفأرة على الصورة لتحديد المكان الذي تظهر فيه الترجمة الإنكليزية", g_fSmall.Get(),
         Rect(L, y, R, y + 20), Theme::SubText);
    y += 28;

    // ---- شو عم يقرا هلق ----
    KeepTranslateSessionAlive(m_id);
    const TranslateStatus st = GetTranslateStatus(m_id);
    const D2D1_RECT_F live = Rect(L, y, R, y + 96);
    Fill(live, Theme::Card, 10);
    Text(L"يقرأ الآن:", g_fSmall.Get(), Rect(R - 90, y + 8, R - 12, y + 28), Theme::SubText);
    Text(st.lastEnglish.empty() ? std::wstring(L"—") : LtrText(st.lastEnglish), g_fSmall.Get(),
         Rect(L + 12, y + 8, R - 96, y + 28), Theme::Text);
    Text(L"الترجمة:", g_fSmall.Get(), Rect(R - 90, y + 34, R - 12, y + 54), Theme::SubText);
    Text(st.lastArabic.empty() ? std::wstring(L"—") : st.lastArabic, g_fBody.Get(), Rect(L + 12, y + 32, R - 96, y + 56), Theme::Green);
    std::wstring info;
    D2D1_COLOR_F infoColor = Theme::SubText;
    if (!st.error.empty())          { info = st.error; infoColor = Theme::Red; }
    else if (st.running && !st.sourceOk) { info = L"لا تصل صورة من المصدر"; infoColor = Theme::Yellow; }
    else
        info = L"تُرجمت " + LtrText(std::to_wstring(st.translated)) + L" جملة  •  من الذاكرة " + LtrText(std::to_wstring(st.fromCache))
             + (st.lastMs > 0 ? L"  •  آخر ترجمة " + LtrText(std::to_wstring(st.lastMs)) + L" ملّي ثانية" : std::wstring());
    Text(info, g_fSmall.Get(), Rect(L + 12, y + 64, R - 12, y + 86), infoColor);
    y += 104;
    if (st.running && !st.ocrReady)
    {
        Text(L"قارئ النصوص غير متاح في ويندوز", g_fSmall.Get(), Rect(L, y, R, y + 20), Theme::Red);
        y += 24;
    }
    else if (st.running && !st.ocrEnglish)
    {
        Text(L"للقراءة الأدق أضف لغة English (United States) من إعدادات ويندوز ← الوقت واللغة ← اللغة", g_fSmall.Get(),
             Rect(L, y, R, y + 20), Theme::Yellow);
        y += 24;
    }
    return y + 8;
}

// ---- خدمة الترجمة ----
float TranslationPanel::DrawEngine(float y, float L, float R)
{
    Text(L"٢. خدمة الترجمة", g_fBody.Get(), Rect(L, y, R, y + 26), Theme::Text);
    y += 32;
    const float gap = 8, cw = (R - L - gap) / 2, chh = 60;
    for (int i = 0; i < TRANS_ENGINE_COUNT; ++i)
    {
        const float right = R - (i % 2) * (cw + gap);
        const float top = y + (i / 2) * (chh + gap);
        const D2D1_RECT_F card = Rect(right - cw, top, right, top + chh);
        const bool sel = (int)m_p.engine == i;
        const bool hover = AddHit(card, ID_ENGINE_FIRST + i);
        Fill(card, sel ? WithAlpha(Theme::Accent, 0.16f) : (hover ? Theme::Hover : Theme::Card), 10);
        Stroke(card, sel ? Theme::Accent : Theme::Border, 10, sel ? 1.5f : 1.0f);
        const TransEngine e = (TransEngine)i;
        const bool hasKey = !EngineNeedsKey(e) || !GetEngineConfig(e).key.empty();
        Text(EngineName(e), g_fBody.Get(), Rect(card.left + 30, top + 8, card.right - 12, top + 30), Theme::Text);
        Text(EngineNote(e), g_fSmall.Get(), Rect(card.left + 12, top + 32, card.right - 12, top + 52), Theme::SubText);
        if (hasKey && EngineNeedsKey(e))
            Circle(card.left + 16, top + 19, 4.5f, Theme::Green);     // مفتاحها محفوظ
    }
    y += ((TRANS_ENGINE_COUNT + 1) / 2) * (chh + gap) + 4;

    // ---- إعدادات الخدمة المختارة ----
    const TransEngine e = m_p.engine;
    const EngineConfig cfg = GetEngineConfig(e);
    const float rowH = 40;
    if (EngineNeedsKey(e))
    {
        Text(L"المفتاح:", g_fBody.Get(), Rect(R - 120, y, R, y + rowH), Theme::Text);
        Text(cfg.key.empty() ? L"غير مُدخل" : L"محفوظ ✓ (مشفّر على جهازك)", g_fSmall.Get(), Rect(L + 180, y, R - 126, y + rowH),
             cfg.key.empty() ? Theme::Yellow : Theme::Green);
        DrawButton(Rect(L, y + 3, L + 170, y + rowH - 3), cfg.key.empty() ? L"إدخال المفتاح" : L"تغيير المفتاح", ID_KEY, cfg.key.empty());
        y += rowH;
    }
    Text(EngineKeyHelp(e), g_fSmall.Get(), Rect(L, y, R, y + 20), Theme::SubText);
    y += 26;
    if (EngineHasModel(e))
    {
        const std::wstring model = cfg.model.empty() ? EngineDefaultModel(e) : cfg.model;
        Text(L"النموذج:", g_fBody.Get(), Rect(R - 120, y, R, y + rowH), Theme::Text);
        Text(LtrText(model), g_fSmall.Get(), Rect(L + 180, y, R - 126, y + rowH), Theme::Text);
        DrawButton(Rect(L, y + 3, L + 170, y + rowH - 3), L"تغيير النموذج", ID_MODEL);
        y += rowH;
    }
    if (EngineHasExtra(e))
    {
        const std::wstring value = cfg.extra.empty() ? (e == TransEngine::Ollama ? std::wstring(L"http://localhost:11434") : std::wstring(L"—")) : cfg.extra;
        Text(std::wstring(EngineExtraLabel(e)) + L":", g_fBody.Get(), Rect(R - 160, y, R, y + rowH), Theme::Text);
        Text(LtrText(value), g_fSmall.Get(), Rect(L + 180, y, R - 166, y + rowH), Theme::Text);
        DrawButton(Rect(L, y + 3, L + 170, y + rowH - 3), L"تغيير", ID_EXTRA);
        y += rowH;
    }

    // ---- تجربة ----
    {
        std::lock_guard<std::mutex> lock(m_test->mx);
        DrawButton(Rect(L, y + 4, L + 170, y + 38), m_test->running ? L"جارٍ التجربة…" : L"تجربة الترجمة", ID_TEST, true, !m_test->running);
        if (m_test->done)
            Text(m_test->ok ? m_test->result + L"  (" + LtrText(std::to_wstring(m_test->ms)) + L" ملّي ثانية)" : m_test->result,
                 g_fSmall.Get(), Rect(L + 180, y + 4, R, y + 38), m_test->ok ? Theme::Green : Theme::Red);
        else
            Text(L"«Get in the car, we don't have much time!»", g_fSmall.Get(), Rect(L + 180, y + 4, R, y + 38), Theme::SubText);
    }
    return y + 52;
}

// ---- الأسلوب ----
float TranslationPanel::DrawStyle(float y, float L, float R)
{
    const bool ai = EngineIsAi(m_p.engine);
    Text(L"٣. أسلوب الترجمة", g_fBody.Get(), Rect(L, y, R, y + 26), Theme::Text);
    y += 32;
    std::vector<std::wstring> dialects;
    for (int i = 0; i < TRANS_DIALECT_COUNT; ++i)
        dialects.push_back(DialectName((TransDialect)i));
    DrawSegmented(Rect(L, y, R, y + 36), dialects, ai ? (int)m_p.dialect : 0, ID_DIALECT_FIRST, ai);
    y += 40;
    if (!ai)
    {
        Text(L"اللهجات وفهم السياق متاحة مع خدمات الذكاء الاصطناعي فقط", g_fSmall.Get(), Rect(L, y, R, y + 20), Theme::SubText);
        y += 24;
    }
    Text(L"اسم اللعبة:", g_fBody.Get(), Rect(R - 120, y, R, y + 40), Theme::Text);
    Text(m_p.game.empty() ? std::wstring(L"—") : LtrText(m_p.game), g_fSmall.Get(), Rect(L + 180, y, R - 126, y + 40), Theme::Text);
    DrawButton(Rect(L, y + 3, L + 170, y + 37), L"تغيير", ID_GAME);
    y += 44;
    y = ToggleRow(y, L, R, L"تلطيف الألفاظ القوية", L"مناسب ليوتيوب (يحوّل الشتائم إلى ألفاظ خفيفة)", m_p.soften, ID_SOFTEN);
    return y + 6;
}

// ---- الأسماء ----
float TranslationPanel::DrawGlossary(float y, float L, float R)
{
    Text(L"٤. الأسماء والمصطلحات", g_fBody.Get(), Rect(L, y, R, y + 26), Theme::Text);
    DrawButton(Rect(L, y - 2, L + 150, y + 30), L"إضافة اسم", ID_GLOSS_ADD, false, (int)m_p.glossary.size() < MAX_GLOSSARY, Icon::Add);
    y += 34;
    Text(L"أسماء الشخصيات والأماكن كما تريد أن تظهر دائمًا", g_fSmall.Get(), Rect(L + 160, y - 4, R, y + 16), Theme::SubText);
    y += 22;
    for (int i = 0; i < (int)m_p.glossary.size() && i < MAX_GLOSSARY; ++i)
    {
        const GlossaryItem& g = m_p.glossary[i];
        const D2D1_RECT_F row = Rect(L, y, R, y + 36);
        Fill(row, Theme::Card, 8);
        Text(LtrText(g.en), g_fBody.Get(), Rect(R - 260, y, R - 12, y + 36), Theme::Text);
        Text(L"←", g_fBody.Get(), Rect(R - 300, y, R - 264, y + 36), Theme::SubText);
        Text(g.ar.empty() ? std::wstring(L"يبقى كما هو") : g.ar, g_fBody.Get(), Rect(L + 50, y, R - 304, y + 36),
             g.ar.empty() ? Theme::SubText : Theme::Accent);
        const D2D1_RECT_F del = Rect(L + 6, y + 4, L + 34, y + 32);
        if (AddHit(del, ID_GLOSS_DEL + i))
            Fill(del, D2D1::ColorF(0xE5484D, 0.2f), 6);
        Text(Icon::Delete, g_fIcon.Get(), del, Theme::SubText);
        y += 40;
    }
    return y + 8;
}

// ---- المزامنة ----
float TranslationPanel::DrawSync(float y, float L, float R)
{
    Text(L"٥. المزامنة", g_fBody.Get(), Rect(L, y, R, y + 26), Theme::Text);
    y += 28;
    Text(L"نؤخّر الصورة والصوت قليلًا ليرى المشاهد العربية مع ظهور الجملة مباشرة (دون أن تتأخر أنت في اللعب)",
         g_fSmall.Get(), Rect(L, y, R, y + 20), Theme::SubText);
    y += 22;
    y = SliderRow(y, L, R, L"التأخير", m_p.delayMs == 0 ? std::wstring(L"بلا تأخير") : Seconds(m_p.delayMs),
                  (float)m_p.delayMs, 0.0f, 3000.0f, 100.0f, ID_DELAY,
                  [this](float v) { m_p.delayMs = (int)std::lround(v); Apply(false); });
    int w = 1920, h = 1080;
    double fps = 60;
    if (!m_p.fromScreen)
        DeviceActualFormat(m_p.deviceLink, w, h, fps);
    if (m_p.fromScreen)
        Text(L"التأخير لا يشمل التقاط الشاشة (يعمل مع أجهزة الالتقاط والكاميرا)", g_fSmall.Get(), Rect(L, y - 4, R, y + 16), Theme::Yellow);
    else if (m_p.delayMs > 0)
        Text(L"يستخدم قرابة " + LtrText(std::to_wstring(DelayMemoryMb(w, h, fps, m_p.delayMs))) + L" ميغابايت من الذاكرة  •  "
             + L"1.5 ثانية تكفي عادةً", g_fSmall.Get(), Rect(L, y - 4, R, y + 16), Theme::SubText);
    else
        Text(L"بلا تأخير: يظهر مربع فوق الجملة الإنكليزية حتى تصل الترجمة (أقل من ثانية)", g_fSmall.Get(), Rect(L, y - 4, R, y + 16), Theme::SubText);
    y += 22;
    y = ToggleRow(y, L, R, L"تأخير المايك والكاميرا والأصوات الأخرى بالمقدار نفسه",
                  L"حتى لا يسبق كلامُك وردّةُ فعلك ما يراه المشاهد", m_p.delayAll, ID_DELAY_ALL);
    return y + 6;
}

// ---- الشكل والقراءة ----
float TranslationPanel::DrawLook(float y, float L, float R)
{
    Text(L"٦. الشكل", g_fBody.Get(), Rect(L, y, R, y + 26), Theme::Text);
    y += 30;
    y = SliderRow(y, L, R, L"حجم الخط", LtrText(std::to_wstring((int)std::lround(m_p.fontScale * 100)) + L"%"),
                  m_p.fontScale, 0.6f, 1.6f, 0.05f, ID_FONT, [this](float v) { m_p.fontScale = v; Apply(false); });
    y = SliderRow(y, L, R, L"تعتيم الخلفية", LtrText(std::to_wstring((int)std::lround(m_p.boxOpacity * 100)) + L"%"),
                  m_p.boxOpacity, 0.0f, 1.0f, 0.05f, ID_BOX, [this](float v) { m_p.boxOpacity = v; Apply(false); });
    if (m_p.boxOpacity < 0.75f)
    {
        Text(L"تعتيم قليل: قد تظهر الجملة الإنكليزية من تحت العربية", g_fSmall.Get(), Rect(L, y - 4, R, y + 16), Theme::Yellow);
        y += 20;
    }
    std::vector<std::wstring> colors;
    for (int i = 0; i < TRANS_COLOR_COUNT; ++i)
        colors.push_back(TransColorName(i));
    Text(L"لون النص", g_fBody.Get(), Rect(R - 130, y, R, y + 36), Theme::Text);
    DrawSegmented(Rect(L, y, R - 140, y + 36), colors, m_p.colorIndex, ID_COLOR_FIRST, true);
    return y + 48;
}

// ---- الترجمة على شاشتك ----
float TranslationPanel::DrawPlayer(float y, float L, float R)
{
    Text(L"٧. الترجمة على شاشتك", g_fBody.Get(), Rect(L, y, R, y + 26), Theme::Text);
    y += 30;
    y = ToggleRow(y, L, R, L"إظهار الترجمة العربية فوق اللعبة على شاشتي",
                  L"لا تظهر في البث ولا في التسجيل  •  تبقى تعمل ما دام الخيار مفعّلًا ولو أغلقت هذه النافذة",
                  m_p.playerOverlay, ID_PLAYER_OVERLAY);
    if (!m_p.playerOverlay)
        return y + 6;

    if (PlayerOverlayBlocked())
    {
        Text(L"نسخة ويندوز لديك لا تسمح بإخفاء الترجمة عن التصوير، حدّث ويندوز 10 إلى الإصدار 2004 أو أحدث",
             g_fSmall.Get(), Rect(L, y, R, y + 20), Theme::Red);
        y += 26;
    }
    if (!m_p.fromScreen && !m_monitors.empty())
    {
        // جهاز الالتقاط: صورة اللعبة على أي شاشة؟ (مثلاً برنامج الكرت بملء الشاشة)
        Text(L"الشاشة التي تعرض صورة اللعبة:", g_fBody.Get(), Rect(R - 260, y, R, y + 36), Theme::Text);
        float x = R - 270;
        bool any = false;
        for (const CaptureTarget& t : m_monitors)
            any = any || EqualRect(&t.rect, &m_p.overlayMonitor);
        for (int i = 0; i < (int)m_monitors.size() && i < MAX_OVERLAY_SCREENS; ++i)
        {
            const CaptureTarget& t = m_monitors[i];
            const std::wstring label = LtrText(std::to_wstring(i + 1)) + (t.isPrimary ? L" (الرئيسية)" : L"");
            const float w = TextWidth(label, g_fSegment.Get()) + 30;
            if (x - w < L)
                break;
            const D2D1_RECT_F c = Rect(x - w, y + 1, x, y + 35);
            const bool sel = any ? EqualRect(&t.rect, &m_p.overlayMonitor) != FALSE : t.isPrimary;
            const bool hover = AddHit(c, ID_OVERLAY_MON_FIRST + i);
            Fill(c, sel ? WithAlpha(Theme::Accent, 0.22f) : (hover ? Theme::Hover : Theme::Card), 9);
            Stroke(c, sel ? Theme::Accent : Theme::Border, 9);
            Text(label, g_fSegment.Get(), Rect(c.left + 6, c.top, c.right - 6, c.bottom), sel ? Theme::Text : Theme::SubText);
            x -= w + 6;
        }
        y += 42;
        Text(L"تظهر في المكان نفسه الذي حدّدته على الصورة، فاعرض صورة اللعبة على هذه الشاشة بملء الشاشة",
             g_fSmall.Get(), Rect(L, y - 4, R, y + 16), Theme::SubText);
        y += 22;
    }
    Text(L"شغّل اللعبة بوضع «ملء الشاشة بلا حدود» (Borderless) لتظهر الترجمة فوقها",
         g_fSmall.Get(), Rect(L, y - 4, R, y + 16), Theme::Yellow);
    y += 20;
    Text(L"يمكنك إظهارها وإخفاؤها باختصار من نافذة «اختصارات لوحة المفاتيح»",
         g_fSmall.Get(), Rect(L, y - 4, R, y + 16), Theme::SubText);
    return y + 26;
}

// ---- قراءة النص ----
float TranslationPanel::DrawReading(float y, float L, float R)
{
    Text(L"٨. قراءة النص", g_fBody.Get(), Rect(L, y, R, y + 26), Theme::Text);
    y += 30;
    y = ToggleRow(y, L, R, L"فصل النص الفاتح عن الخلفية", L"جرّبه إذا كانت القراءة غير دقيقة فوق خلفيات مزدحمة",
                  m_p.threshold > 0, ID_THRESHOLD_ON);
    if (m_p.threshold > 0)
        y = SliderRow(y, L, R, L"حدّ السطوع", LtrText(std::to_wstring(m_p.threshold)), (float)m_p.threshold, 100.0f, 250.0f, 5.0f,
                      ID_THRESHOLD, [this](float v) { m_p.threshold = (int)std::lround(v); Apply(false); });
    return y + 6;
}

// -------------------------------------------------------------
//  الرسم
// -------------------------------------------------------------
void TranslationPanel::DrawBody(float L, float top, float R)
{
    ++m_frame;
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        m_dragging = false;
    if (!Load())                         // انحذف من مكان تاني؟
    {
        m_all = TranslateProfiles();
        if (m_all.empty()) NewProfile();
        else { m_id = m_all.front().id; Load(); }
    }
    m_all = TranslateProfiles();
    SetTranslatePlaceholder(!(m_h.busy && m_h.busy()));

    float y = top;
    y = DrawProfiles(y, L, R);
    Fill(Rect(L, y - 6, R, y - 5), Theme::Border);
    y = DrawSource(y + 6, L, R);
    y = DrawEngine(y + 6, L, R);
    y = DrawStyle(y + 6, L, R);
    y = DrawGlossary(y + 6, L, R);
    y = DrawSync(y + 6, L, R);
    y = DrawLook(y + 6, L, R);
    y = DrawPlayer(y + 6, L, R);
    y = DrawReading(y + 6, L, R);

    // ---- الأزرار ----
    Fill(Rect(L, y, R, y + 1), Theme::Border);
    y += 12;
    if (m_addMode)
        DrawButton(Rect(R - 260, y, R, y + 42), L"إضافة الترجمة إلى المشهد", ID_ADD_SCENE, true, true, Icon::Add);
    else
        DrawButton(Rect(R - 260, y, R, y + 42), L"إعادة الطبقة فوق مكان الترجمة", ID_FIT, false, true);
    DrawButton(Rect(L, y, L + 220, y + 42), L"مسح الترجمات المحفوظة", ID_CLEAR_CACHE, false, true, Icon::Delete);
    y += 56;
    m_height = y - top;
}

// -------------------------------------------------------------
//  تحديد المكان بالسحب
// -------------------------------------------------------------
bool TranslationPanel::OnDrag(int id, D2D1_POINT_2F p)
{
    if (id != ID_PREVIEW)
        return false;
    const float iw = m_imgDst.right - m_imgDst.left, ih = m_imgDst.bottom - m_imgDst.top;
    if (iw < 10 || ih < 10)
        return true;
    p.x = std::clamp(p.x, m_imgDst.left, m_imgDst.right);
    p.y = std::clamp(p.y, m_imgDst.top, m_imgDst.bottom);
    if (!m_dragging)
    {
        m_dragging = true;
        m_anchor = p;
        return true;
    }
    const float x0 = std::min(m_anchor.x, p.x), x1 = std::max(m_anchor.x, p.x);
    const float y0 = std::min(m_anchor.y, p.y), y1 = std::max(m_anchor.y, p.y);
    if (x1 - x0 < 8 || y1 - y0 < 6)
        return true;
    m_p.rx = (x0 - m_imgDst.left) / iw;
    m_p.ry = (y0 - m_imgDst.top) / ih;
    m_p.rw = std::max(0.02, (double)(x1 - x0) / iw);
    m_p.rh = std::max(0.02, (double)(y1 - y0) / ih);
    Apply(false);
    return true;
}

// -------------------------------------------------------------
//  تجربة (بخيط بالخلفية لحتى ما تعلق الواجهة)
// -------------------------------------------------------------
void TranslationPanel::RunTest()
{
    {
        std::lock_guard<std::mutex> lock(m_test->mx);
        if (m_test->running)
            return;
        m_test->running = true;
        m_test->done = false;
    }
    std::shared_ptr<TestState> t = m_test;
    const TranslateProfile p = m_p;
    std::thread([t, p]
    {
        std::wstring ar, err;
        int ms = 0;
        const bool ok = TranslateOnce(p, L"Get in the car, we don't have much time!", ar, err, &ms);
        std::lock_guard<std::mutex> lock(t->mx);
        t->running = false;
        t->done    = true;
        t->ok      = ok;
        t->result  = ok ? ar : err;
        t->ms      = ms;
    }).detach();
}

// -------------------------------------------------------------
//  الكبسات
// -------------------------------------------------------------
void TranslationPanel::OnClick(int id)
{
    auto input = [this](const std::wstring& title, std::wstring& text, bool password, bool ltr, int limit)
    {
        return m_h.input && m_h.input(title, text, password, ltr, limit);
    };

    if (id >= ID_PROFILE_FIRST && id < ID_PROFILE_FIRST + MAX_PROFILES)
    {
        const int i = id - ID_PROFILE_FIRST;
        if (i < (int)m_all.size())
        {
            Apply(true);
            m_id = m_all[i].id;
            Load();
            m_bitmap.Reset();
            if (!m_addMode && m_h.useProfile)
                m_h.useProfile(m_id);
        }
        return;
    }
    if (id >= ID_SOURCE_FIRST && id < ID_SOURCE_FIRST + MAX_SOURCES)
    {
        const int i = id - ID_SOURCE_FIRST;
        if (i < (int)m_devices.size())
        {
            m_p.fromScreen = false;
            m_p.deviceLink = m_devices[i].link;
            m_p.deviceName = m_devices[i].name;
            m_bitmap.Reset();
            Apply(true);
        }
        return;
    }
    if (id >= ID_ENGINE_FIRST && id < ID_ENGINE_FIRST + TRANS_ENGINE_COUNT)
    {
        m_p.engine = (TransEngine)(id - ID_ENGINE_FIRST);
        std::lock_guard<std::mutex> lock(m_test->mx);
        m_test->done = false;
        Apply(true);
        return;
    }
    if (id >= ID_DIALECT_FIRST && id < ID_DIALECT_FIRST + TRANS_DIALECT_COUNT)
    {
        if (EngineIsAi(m_p.engine)) { m_p.dialect = (TransDialect)(id - ID_DIALECT_FIRST); Apply(true); }
        return;
    }
    if (id >= ID_COLOR_FIRST && id < ID_COLOR_FIRST + TRANS_COLOR_COUNT)
    {
        m_p.colorIndex = id - ID_COLOR_FIRST;
        Apply(true);
        return;
    }
    if (id >= ID_OVERLAY_MON_FIRST && id < ID_OVERLAY_MON_FIRST + MAX_OVERLAY_SCREENS)
    {
        const int i = id - ID_OVERLAY_MON_FIRST;
        if (i < (int)m_monitors.size())
        {
            m_p.overlayMonitor = m_monitors[i].rect;
            Apply(true);
        }
        return;
    }
    if (id >= ID_GLOSS_DEL && id < ID_GLOSS_DEL + MAX_GLOSSARY)
    {
        const int i = id - ID_GLOSS_DEL;
        if (i < (int)m_p.glossary.size())
        {
            m_p.glossary.erase(m_p.glossary.begin() + i);
            Apply(true);
        }
        return;
    }

    switch (id)
    {
    case ID_PROFILE_NEW:
        Apply(true);
        NewProfile();
        m_bitmap.Reset();
        return;
    case ID_PROFILE_RENAME:
    {
        std::wstring name = m_p.name;
        if (input(L"اسم الملف (مثلاً اسم اللعبة)", name, false, false, 40) && !name.empty())
        {
            m_p.name = name;
            Apply(true);
        }
        return;
    }
    case ID_PROFILE_DELETE:
        if (m_all.size() > 1)
        {
            const std::wstring old = m_id;
            DeleteTranslateProfile(old);
            m_all = TranslateProfiles();
            m_id = m_all.front().id;
            Load();
            if (!m_addMode && m_h.useProfile)
                m_h.useProfile(m_id);
        }
        return;
    case ID_SOURCE_SCREEN:
    {
        if (m_h.monitors)
            m_monitors = m_h.monitors();           // يمكن وصّلت شاشة جديدة
        const std::vector<CaptureTarget>& mons = m_monitors;
        if (mons.empty())
            return;
        int pick = 0;
        if (mons.size() > 1 && m_h.menu)
        {
            std::vector<std::wstring> names;
            for (const CaptureTarget& t : mons) names.push_back(t.name);
            pick = m_h.menu(names, -1);
            if (pick < 0) return;
        }
        m_p.fromScreen = true;
        m_p.monitor    = mons[pick].rect;
        m_bitmap.Reset();
        Apply(true);
        return;
    }
    case ID_KEY:
    {
        EngineConfig c = GetEngineConfig(m_p.engine);
        std::wstring key = c.key;
        if (input(std::wstring(L"مفتاح ") + EngineName(m_p.engine), key, true, true, 400))
        {
            c.key = key;
            for (wchar_t& ch : c.key) if (ch == L' ') ch = 0;
            c.key.erase(std::remove(c.key.begin(), c.key.end(), L'\0'), c.key.end());
            SetEngineConfig(m_p.engine, c);
        }
        return;
    }
    case ID_MODEL:
    {
        EngineConfig c = GetEngineConfig(m_p.engine);
        std::wstring model = c.model.empty() ? EngineDefaultModel(m_p.engine) : c.model;
        if (m_p.engine == TransEngine::Ollama && m_h.menu)
        {
            std::wstring err;
            const std::vector<std::wstring> models = OllamaModels(c.extra, &err);
            if (!models.empty())
            {
                int checked = -1;
                for (int i = 0; i < (int)models.size(); ++i) if (models[i] == model) checked = i;
                const int pick = m_h.menu(models, checked);
                if (pick >= 0) { c.model = models[pick]; SetEngineConfig(m_p.engine, c); }
                return;
            }
        }
        if (input(L"اسم النموذج (اتركه فارغًا للافتراضي)", model, false, true, 120))
        {
            c.model = model == EngineDefaultModel(m_p.engine) ? std::wstring() : model;
            SetEngineConfig(m_p.engine, c);
        }
        return;
    }
    case ID_EXTRA:
    {
        EngineConfig c = GetEngineConfig(m_p.engine);
        std::wstring v = c.extra;
        if (input(EngineExtraLabel(m_p.engine), v, false, true, 200))
        {
            c.extra = v;
            SetEngineConfig(m_p.engine, c);
        }
        return;
    }
    case ID_TEST:
        Apply(true);
        RunTest();
        return;
    case ID_GAME:
    {
        std::wstring game = m_p.game;
        if (input(L"اسم اللعبة (بالإنكليزي أفضل)", game, false, true, 80))
        {
            m_p.game = game;
            Apply(true);
        }
        return;
    }
    case ID_SOFTEN:    m_p.soften = !m_p.soften; Apply(true); return;
    case ID_DELAY_ALL: m_p.delayAll = !m_p.delayAll; Apply(true); return;
    case ID_PLAYER_OVERLAY: m_p.playerOverlay = !m_p.playerOverlay; Apply(true); return;
    case ID_THRESHOLD_ON:
        m_p.threshold = m_p.threshold > 0 ? 0 : 180;
        Apply(true);
        return;
    case ID_GLOSS_ADD:
    {
        std::wstring text;
        if (!input(L"الاسم = كما يُكتب بالعربية  (مثال: Jason = جيسون)", text, false, false, 120))
            return;
        const size_t eq = text.find(L'=');
        GlossaryItem g;
        g.en = eq == std::wstring::npos ? text : text.substr(0, eq);
        g.ar = eq == std::wstring::npos ? L"" : text.substr(eq + 1);
        auto trim = [](std::wstring s)
        {
            while (!s.empty() && iswspace(s.back())) s.pop_back();
            size_t i = 0;
            while (i < s.size() && iswspace(s[i])) ++i;
            return s.substr(i);
        };
        g.en = trim(g.en);
        g.ar = trim(g.ar);
        if (!g.en.empty())
        {
            m_p.glossary.push_back(g);
            Apply(true);
        }
        return;
    }
    case ID_ADD_SCENE:
        Apply(true);
        if (m_h.addToScene)
        {
            const std::wstring pid = m_id;
            CloseThen([fn = m_h.addToScene, pid] { fn(pid); });
        }
        return;
    case ID_FIT:
        Apply(true);
        if (m_h.fitLayer)
            m_h.fitLayer(m_id);
        return;
    case ID_CLEAR_CACHE:
        ClearTranslateCache(m_id);
        return;
    }
}
