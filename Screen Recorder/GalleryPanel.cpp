// =============================================================
//  GalleryPanel.cpp - "المعرض"
// =============================================================
#include "GalleryPanel.h"
#include "VideoTools.h"
#include "Tooltip.h"
#include <shellapi.h>
#include <algorithm>

namespace
{
    enum : int
    {
        ID_FILTER   = ID_PANEL_USER,          // +0..4
        ID_OPEN     = ID_PANEL_USER + 10,
        ID_REFRESH  = ID_PANEL_USER + 11,
        // البطاقة i: + i*8 + (0 تشغيل ، 1 قص ، 2 تسمية ، 3 بالمجلد ، 4 حذف)
        // بعيدة عن أرقام قائمة الإعدادات (المعرض بينعرض جوّاها كمان)
        ID_CARD     = ID_PANEL_USER + 2000
    };
    constexpr int MAX_ITEMS = 300;
    // المعرض بينعرض جوّا الإعدادات كمان: ممنوع يلمس أرقام قائمتها (+900..+999)
    static_assert(ID_REFRESH < ID_PANEL_SETTINGS_NAV && ID_CARD >= ID_PANEL_SETTINGS_NAV + 100, "GalleryPanel IDs");
    static_assert(ID_CARD + MAX_ITEMS * 8 <= ID_PANEL_LAST, "GalleryPanel: البطاقات طلعت برّا أرقام النوافذ");

    std::wstring Duration(LONGLONG d)
    {
        const LONGLONG s = d / 10'000'000;
        wchar_t buf[32];
        if (s >= 3600) swprintf_s(buf, L"%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
        else           swprintf_s(buf, L"%lld:%02lld", s / 60, s % 60);
        return buf;
    }
    std::wstring SizeText(ULONGLONG b)
    {
        wchar_t buf[32];
        if (b >= 1024ull * 1024 * 1024) swprintf_s(buf, L"%.1f غيغابايت", b / (1024.0 * 1024 * 1024));
        else                            swprintf_s(buf, L"%.0f ميغابايت", std::max(1.0, b / (1024.0 * 1024)));
        return buf;
    }
    std::wstring WhenText(const FILETIME& ft)
    {
        SYSTEMTIME utc, st, now;
        FileTimeToSystemTime(&ft, &utc);
        SystemTimeToTzSpecificLocalTime(nullptr, &utc, &st);
        GetLocalTime(&now);
        wchar_t buf[64];
        if (st.wYear == now.wYear && st.wMonth == now.wMonth && st.wDay == now.wDay)
            swprintf_s(buf, L"اليوم %02d:%02d", st.wHour, st.wMinute);
        else
            swprintf_s(buf, L"%d/%d/%d  %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
        return buf;
    }
}

GalleryPanel::GalleryPanel(GalleryHooks hooks) : m_h(std::move(hooks))
{
    Refresh();
    // محمّل الصور المصغّرة (بالخلفية)
    auto mutex = m_mutex; auto thumbs = m_thumbs; auto queue = m_queue; auto stop = m_stop;
    m_thread = std::thread([mutex, thumbs, queue, stop]
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        while (!*stop)
        {
            std::wstring path;
            {
                std::lock_guard<std::mutex> lock(*mutex);
                if (!queue->empty())
                {
                    path = queue->front();
                    queue->erase(queue->begin());
                }
            }
            if (path.empty())
            {
                Sleep(60);
                continue;
            }
            Thumb t;
            ShellThumbnail(path, 320, t.px, t.w, t.h);
            const size_t dot = path.find_last_of(L'.');
            if (dot != std::wstring::npos && _wcsicmp(path.c_str() + dot, L".mp4") == 0)
                VideoDuration(path, t.duration);
            t.ready = true;
            std::lock_guard<std::mutex> lock(*mutex);
            (*thumbs)[path] = std::move(t);
        }
        if (SUCCEEDED(hr)) CoUninitialize();
    });
    m_thread.detach();          // ما منستناه (الحالة مشتركة بـ shared_ptr)
}

GalleryPanel::~GalleryPanel()
{
    *m_stop = true;
}

void GalleryPanel::Refresh()
{
    m_items.clear();
    for (const auto& [folder, kind] : m_h.folders())
    {
        const wchar_t* pattern = kind == 3 ? L"\\*.png" : L"\\*.mp4";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((folder + pattern).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            Item it;
            it.path = folder + L"\\" + fd.cFileName;
            it.name = fd.cFileName;
            const size_t dot = it.name.find_last_of(L'.');
            if (dot != std::wstring::npos) it.name.resize(dot);
            it.kind = kind;
            it.size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            it.time = fd.ftLastWriteTime;
            m_items.push_back(it);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(m_items.begin(), m_items.end(), [](const Item& a, const Item& b)
              { return CompareFileTime(&a.time, &b.time) > 0; });
    if (m_items.size() > MAX_ITEMS)
        m_items.resize(MAX_ITEMS);
    std::lock_guard<std::mutex> lock(*m_mutex);
    m_queue->clear();
    for (const Item& it : m_items)
        if (!m_thumbs->count(it.path))
            m_queue->push_back(it.path);
}

void GalleryPanel::DrawBody(float L, float top, float R)
{
    float y = top;
    const bool narrow = (R - L) < 780;          // جوّا الإعدادات المكان أضيق
    // ---- الفلتر + أزرار ----
    const std::vector<std::wstring> filters = { L"الكل", L"التسجيلات", L"الإعادات", L"البث", L"الصور" };
    if (narrow)
    {
        DrawSegmented(Rect(L, y, R, y + 34), filters, m_filter, ID_FILTER, true);
        y += 44;
        DrawButton(Rect(R - 130, y, R, y + 34), L"فتح المجلد", ID_OPEN, false, true, Icon::Folder);
        DrawButton(Rect(R - 230, y, R - 138, y + 34), L"تحديث", ID_REFRESH, false, true, Icon::Refresh);
    }
    else
    {
        DrawSegmented(Rect(R - 520, y, R, y + 34), filters, m_filter, ID_FILTER, true);
        DrawButton(Rect(L, y, L + 130, y + 34), L"فتح المجلد", ID_OPEN, false, true, Icon::Folder);
        DrawButton(Rect(L + 138, y, L + 230, y + 34), L"تحديث", ID_REFRESH, false, true, Icon::Refresh);
    }
    Tip(ID_OPEN, L"فتح مجلد التسجيلات في مستكشف الملفات");
    Tip(ID_REFRESH, L"إعادة قراءة الملفات من القرص");
    y += 48;

    std::vector<int> shown;
    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        const int k = m_items[i].kind;
        if (m_filter == 0 || (m_filter == 1 && k == 0) || (m_filter == 2 && k == 1) || (m_filter == 3 && k == 2) || (m_filter == 4 && k == 3))
            shown.push_back(i);
    }
    if (shown.empty())
    {
        Text(L"لا توجد ملفات بعد", g_fCenter.Get(), Rect(L, y + 40, R, y + 70), Theme::SubText);
        Text(L"سجّل أو احفظ إعادة أو خذ لقطة شاشة وستظهر هنا", g_fSmall.Get(), Rect(L, y + 72, R, y + 92), Theme::Off);
        m_height = y - top + 120;
        return;
    }

    const float gap = 14;
    const int   cols = narrow ? 2 : 3;
    const float cw = (R - L - gap * (cols - 1)) / cols, th = cw * 9 / 16, ch = th + 54;
    std::lock_guard<std::mutex> lock(*m_mutex);
    for (size_t n = 0; n < shown.size(); ++n)
    {
        const int i = shown[n];
        const Item& it = m_items[i];
        const float right = R - (n % cols) * (cw + gap);             // من اليمين
        const float cy = y + (n / cols) * (ch + gap);
        const D2D1_RECT_F card = Rect(right - cw, cy, right, cy + ch);
        const D2D1_RECT_F thumb = Rect(card.left, card.top, card.right, card.top + th);
        const int base = ID_CARD + i * 8;
        // البطاقة أول، وبعدها الأزرار الصغيرة: آخر شي بينسجّل هو اللي بياخد الكبسة (شوف HitAt)
        const bool hover = AddHit(card, base);
        Tip(base, it.kind == 3 ? L"فتح الصورة" : L"تشغيل الفيديو");
        const bool over = g_mouse.x >= card.left && g_mouse.x <= card.right && g_mouse.y >= card.top && g_mouse.y <= card.bottom;
        struct Btn { int a; const wchar_t* glyph; const wchar_t* tip; };
        std::vector<std::pair<Btn, D2D1_RECT_F>> btns;
        bool btnHover[5] = {};
        if (over)
        {
            const Btn all[] = { { 1, L"\uE8C6", L"قصّ المقطع" }, { 2, L"\uE8AC", L"إعادة التسمية" },
                                { 3, Icon::Folder, L"إظهار في المجلد" }, { 4, Icon::Delete, L"حذف (إلى سلة المحذوفات)" } };
            float bx = thumb.left + 8;
            for (const Btn& b : all)
            {
                if (b.a == 1 && it.kind == 3) continue;          // الصور ما بتنقص
                const D2D1_RECT_F r = Rect(bx, thumb.top + 8, bx + 32, thumb.top + 40);
                btnHover[b.a] = AddHit(r, base + b.a);
                Tip(base + b.a, b.tip);
                btns.push_back({ b, r });
                bx += 38;
            }
        }
        Fill(card, hover ? Theme::Hover : Theme::Card, 10);

        // الصورة المصغّرة
        Fill(thumb, D2D1::ColorF(0x000000), 10);
        auto t = m_thumbs->find(it.path);
        if (t != m_thumbs->end() && t->second.ready && !t->second.px.empty())
        {
            Thumb& tb = t->second;
            if (!tb.bitmap || tb.owner != g_rt.Get())
            {
                tb.bitmap.Reset();
                tb.owner = g_rt.Get();
                const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
                g_rt->CreateBitmap(D2D1::SizeU(tb.w, tb.h), tb.px.data(), tb.w * 4, props, &tb.bitmap);
            }
            if (tb.bitmap)
            {
                const float s = std::min((thumb.right - thumb.left) / tb.w, th / tb.h);
                const float dw = tb.w * s, dh = tb.h * s;
                const float cx = (thumb.left + thumb.right) / 2, cyy = (thumb.top + thumb.bottom) / 2;
                g_rt->DrawBitmap(tb.bitmap.Get(), Rect(cx - dw / 2, cyy - dh / 2, cx + dw / 2, cyy + dh / 2), 1.0f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            if (tb.duration > 0)
            {
                const std::wstring d = Duration(tb.duration);
                const D2D1_RECT_F db = Rect(thumb.left + 8, thumb.bottom - 28, thumb.left + 70, thumb.bottom - 8);
                Fill(db, D2D1::ColorF(0x000000, 0.7f), 5);
                Text(LtrText(d), g_fSmallLtr.Get(), Rect(db.left + 6, db.top, db.right - 4, db.bottom), Theme::White);
            }
        }
        else
            Text(L"…", g_fCenter.Get(), thumb, Theme::SubText);

        // نوع الملف (شارة صغيرة)
        static const wchar_t* kinds[] = { L"تسجيل", L"إعادة", L"بث", L"صورة" };
        const D2D1_RECT_F kb = Rect(thumb.right - 64, thumb.top + 8, thumb.right - 8, thumb.top + 28);
        Fill(kb, WithAlpha(Theme::Accent, 0.85f), 5);
        Text(kinds[it.kind], g_fSmall.Get(), Rect(kb.left + 4, kb.top, kb.right - 4, kb.bottom), Theme::White);

        // الاسم + التاريخ والحجم
        Text(it.name, g_fBody.Get(), Rect(card.left + 10, thumb.bottom + 6, card.right - 10, thumb.bottom + 28), Theme::Text);
        Text(WhenText(it.time) + L"  •  " + SizeText(it.size), g_fSmall.Get(),
             Rect(card.left + 10, thumb.bottom + 28, card.right - 10, thumb.bottom + 48), Theme::SubText);

        // أزرار (بتبيّن وقت الماوس فوق البطاقة)
        if (over)
        {
            const bool video = it.kind != 3;
            for (const auto& [b, r] : btns)
            {
                const bool h = btnHover[b.a];
                Fill(r, h ? (b.a == 4 ? D2D1::ColorF(0xE5484D) : Theme::Accent) : D2D1::ColorF(0x000000, 0.65f), 8);
                Text(b.glyph, g_fIcon.Get(), r, Theme::White);
            }
            // زر تشغيل بالنص
            const float pcx = (thumb.left + thumb.right) / 2, pcy = (thumb.top + thumb.bottom) / 2;
            Circle(pcx, pcy, 22, D2D1::ColorF(0x000000, 0.55f));
            Text(video ? Icon::Play : L"", g_fIcon.Get(), Rect(pcx - 22, pcy - 22, pcx + 22, pcy + 22), Theme::White);
        }
    }
    const size_t rows = (shown.size() + cols - 1) / cols;
    m_height = (y - top) + rows * (ch + gap) + 4;
}

void GalleryPanel::OnClick(int id)
{
    if (id >= ID_FILTER && id < ID_FILTER + 5) { m_filter = id - ID_FILTER; return; }
    if (id == ID_OPEN)    { if (m_h.openFolder) m_h.openFolder(); return; }
    if (id == ID_REFRESH) { Refresh(); return; }
    if (id < ID_CARD)
        return;
    const int i = (id - ID_CARD) / 8, a = (id - ID_CARD) % 8;
    if (i < 0 || i >= (int)m_items.size())
        return;
    const Item it = m_items[i];
    switch (a)
    {
    case 0:     // تشغيل / عرض جوّا البرنامج
        if (m_h.play)
        {
            auto play = m_h.play;
            const std::wstring p = it.path;
            const bool image = it.kind == 3;
            CloseThen([play, p, image] { play(p, image); });
        }
        break;
    case 1:     // قص
        if (m_h.trim) { auto trim = m_h.trim; const std::wstring p = it.path; CloseThen([trim, p] { trim(p); }); }
        break;
    case 2:     // إعادة تسمية
    {
        std::wstring name = it.name;
        if (!m_h.rename || !m_h.rename(name) || name.empty() || name == it.name)
            break;
        for (wchar_t& c : name)
            if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
        const size_t slash = it.path.find_last_of(L'\\'), dot = it.path.find_last_of(L'.');
        const std::wstring to = it.path.substr(0, slash + 1) + name + it.path.substr(dot);
        if (MoveFileW(it.path.c_str(), to.c_str()))
        {
            std::lock_guard<std::mutex> lock(*m_mutex);
            auto t = m_thumbs->find(it.path);
            if (t != m_thumbs->end()) { (*m_thumbs)[to] = std::move(t->second); m_thumbs->erase(it.path); }
        }
        Refresh();
        break;
    }
    case 3:     // إظهار بالمجلد
    {
        const std::wstring args = L"/select,\"" + it.path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        break;
    }
    case 4:     // حذف لسلة المحذوفات (فيك ترجعه)
    {
        std::wstring from = it.path;
        from.push_back(L'\0');                   // SHFileOperation بدها صفرين بالآخر
        SHFILEOPSTRUCTW op{};
        op.wFunc  = FO_DELETE;
        op.pFrom  = from.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        SHFileOperationW(&op);
        Refresh();
        break;
    }
    }
}
