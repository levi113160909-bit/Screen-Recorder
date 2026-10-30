// =============================================================
//  PopupMenu.cpp - القائمة المنبثقة ونافذة الكتابة الصغيرة (بنفس ستايل البرنامج بدل قوائم ويندوز)
// =============================================================
#include "App.h"

struct MenuState
{
    std::vector<std::wstring> items;
    std::vector<int>          icons;     // أيقونة منصة لكل خيار (-1 = بلا)
    std::vector<MenuGlyph>    glyphs;    // أيقونة (Segoe MDL2) لكل خيار (فاضي = بلا)
    int   checked = -1;
    int   hover   = -1;
    int   result  = -1;
    bool  done    = false;
    UINT  dpi     = 96;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush>  brush;
};

struct InputState
{
    std::wstring title;
    HWND         edit = nullptr;
    HFONT        font = nullptr;
    HBRUSH       editBrush = nullptr;
    bool         done = false, ok = false;
    UINT         dpi = 96;
    ComPtr<ID2D1HwndRenderTarget> rt;
    ComPtr<ID2D1SolidColorBrush>  brush;
};

constexpr float MENU_ITEM_H = 34, MENU_SEP_H = 9, MENU_PAD = 6;

// وين كل خيار (بنقاط DIP)
float MenuItemTop(const MenuState& m, int index)
{
    float y = MENU_PAD;
    for (int i = 0; i < index; ++i)
        y += m.items[i].empty() ? MENU_SEP_H : MENU_ITEM_H;
    return y;
}

int MenuItemAt(const MenuState& m, float y)
{
    float top = MENU_PAD;
    for (int i = 0; i < (int)m.items.size(); ++i)
    {
        const float h = m.items[i].empty() ? MENU_SEP_H : MENU_ITEM_H;
        if (y >= top && y < top + h)
            return m.items[i].empty() ? -1 : i;
        top += h;
    }
    return -1;
}

void PaintMenu(HWND hwnd, MenuState& m)
{
    if (!m.rt)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        props.dpiX = props.dpiY = (float)m.dpi;
        g_d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)), &m.rt);
        if (!m.rt)
            return;
        m.rt->CreateSolidColorBrush(Theme::Text, &m.brush);
    }

    // منستعمل نفس أدوات الرسم تبع النافذة الرئيسية (منبدّلهم مؤقتاً)
    ComPtr<ID2D1HwndRenderTarget> mainRt = g_rt;
    ComPtr<ID2D1SolidColorBrush>  mainBrush = g_brush;
    g_rt    = m.rt;
    g_brush = m.brush;

    g_rt->BeginDraw();
    const D2D1_SIZE_F size = g_rt->GetSize();
    g_rt->Clear(Theme::Panel);
    Stroke(Rect(0.5f, 0.5f, size.width - 0.5f, size.height - 0.5f), Theme::Border, 8, 1);

    for (int i = 0; i < (int)m.items.size(); ++i)
    {
        const float top = MenuItemTop(m, i);
        if (m.items[i].empty())     // خط فاصل
        {
            Fill(Rect(12, top + 4, size.width - 12, top + 5), Theme::Border);
            continue;
        }
        const D2D1_RECT_F row = Rect(MENU_PAD, top, size.width - MENU_PAD, top + MENU_ITEM_H);
        if (i == m.hover)
            Fill(row, Theme::Hover, 6);
        if (i == m.checked)
        {
            Fill(Rect(row.right - 3, row.top + 8, row.right, row.bottom - 8), Theme::Accent, 1.5f);   // خط أزرق
            Text(L"\uE73E", g_fIcon.Get(), Rect(row.right - 32, row.top, row.right - 6, row.bottom), Theme::Accent);
        }
        float textRight = row.right - 36;
        if (i < (int)m.glyphs.size() && m.glyphs[i].glyph)
        {
            // أيقونة بمربع صغير مدوّر (متل أزرار المعرض)
            const float cy = (row.top + row.bottom) / 2;
            const D2D1_RECT_F box = Rect(row.right - 38, cy - 13, row.right - 12, cy + 13);
            const D2D1_COLOR_F tone = m.glyphs[i].danger ? Theme::Red : Theme::Accent;
            Fill(box, WithAlpha(tone, i == m.hover ? 0.28f : 0.16f), 7);
            Text(m.glyphs[i].glyph, g_fIcon.Get(), box, tone);
            textRight = row.right - 48;
        }
        else if (i < (int)m.icons.size() && m.icons[i] >= 0)
        {
            const float cy = (row.top + row.bottom) / 2;
            DrawPlatformIcon(m.icons[i], Rect(row.right - 34, cy - 12, row.right - 10, cy + 12), true);
            textRight = row.right - 44;
        }
        Text(m.items[i], g_fBody.Get(), Rect(row.left + 12, row.top, textRight, row.bottom),
             i == m.checked ? Theme::Accent : Theme::Text);
    }

    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET)
    {
        m.brush.Reset();
        m.rt.Reset();
    }
    g_rt    = mainRt;
    g_brush = mainBrush;
}

LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* m = (MenuState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!m)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    const float s = 96.0f / m->dpi;
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        PaintMenu(hwnd, *m);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
    {
        const int hover = MenuItemAt(*m, GET_Y_LPARAM(lParam) * s);
        if (hover != m->hover)
        {
            m->hover = hover;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        m->hover = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP:
    {
        const int i = MenuItemAt(*m, GET_Y_LPARAM(lParam) * s);
        if (i >= 0)
        {
            m->result = i;
            m->done   = true;
        }
        return 0;
    }

    case WM_KEYDOWN:      // الكيبورد: أسهم + Enter + Esc
    {
        const int n = (int)m->items.size();
        if (wParam == VK_ESCAPE) { m->done = true; return 0; }
        if (wParam == VK_RETURN && m->hover >= 0) { m->result = m->hover; m->done = true; return 0; }
        if (wParam == VK_DOWN || wParam == VK_UP)
        {
            const int dir = (wParam == VK_DOWN) ? 1 : -1;
            int i = m->hover;
            for (int k = 0; k < n; ++k)
            {
                i = (i + dir + n) % n;
                if (!m->items[i].empty()) break;
            }
            m->hover = i;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_ACTIVATE:     // كبست برّا القائمة: منسكّرها
        if (LOWORD(wParam) == WA_INACTIVE)
            m->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ShowMenu(const std::vector<std::wstring>& items, int checked, POINT screenPt, const std::vector<int>& icons, const std::vector<MenuGlyph>& glyphs)
{
    MenuState m;
    m.items   = items;
    m.icons   = icons;
    m.glyphs  = glyphs;
    m.checked = checked;

    // دقة الشاشة اللي عليها النقطة
    HMONITOR monitor = MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, MENU_CLASS, L"", WS_POPUP,
                                screenPt.x, screenPt.y, 10, 10, g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
        return -1;
    m.dpi = GetDpiForWindow(hwnd);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&m);

    // العرض = أطول نص + مسافات
    float textW = 0;
    for (const std::wstring& item : items)
    {
        ComPtr<IDWriteTextLayout> layout;
        if (!item.empty() && SUCCEEDED(g_dwrite->CreateTextLayout(item.c_str(), (UINT32)item.size(), g_fBody.Get(), 2000, 50, &layout)))
        {
            DWRITE_TEXT_METRICS tm{};
            layout->GetMetrics(&tm);
            textW = std::max(textW, tm.widthIncludingTrailingWhitespace);
        }
    }
    const float wDip = std::clamp(textW + (glyphs.empty() ? 60.0f : 72.0f) + MENU_PAD * 2, 200.0f, 560.0f);
    const float hDip = MenuItemTop(m, (int)items.size()) + MENU_PAD;
    const int   w = (int)(wDip * m.dpi / 96.0f);
    const int   h = (int)(hDip * m.dpi / 96.0f);

    // القائمة بتطلع لليسار من النقطة (لأن الواجهة عربية)، ومنضل جوّا الشاشة
    int x = screenPt.x - w, y = screenPt.y;
    if (x < mi.rcWork.left)       x = mi.rcWork.left;
    if (x + w > mi.rcWork.right)  x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom) y = std::max((int)mi.rcWork.top, (int)screenPt.y - h);

    // زوايا مدوّرة بويندوز 11
    const int corner = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    // حلقة رسائل خاصة لحتى يختار المستخدم
    MSG msg;
    while (!m.done && GetMessageW(&msg, nullptr, 0, 0))
    {
        // كبسة على أي نافذة تانية = إلغاء
        if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
            && msg.hwnd != hwnd)
        {
            m.done = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
        {
            PostQuitMessage((int)msg.wParam);
            break;
        }
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    m.brush.Reset();
    m.rt.Reset();
    DestroyWindow(hwnd);
    return m.result;
}

LRESULT CALLBACK InputProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* st = (InputState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (!st->rt)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
            props.dpiX = props.dpiY = (float)st->dpi;
            g_d2d->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)), &st->rt);
            if (st->rt) st->rt->CreateSolidColorBrush(Theme::Text, &st->brush);
        }
        if (st->rt)
        {
            // منستعمل أدوات الرسم تبع البرنامج (منبدّلها مؤقتاً)
            ComPtr<ID2D1HwndRenderTarget> mainRt = g_rt;
            ComPtr<ID2D1SolidColorBrush>  mainBrush = g_brush;
            g_rt = st->rt;
            g_brush = st->brush;
            g_rt->BeginDraw();
            const D2D1_SIZE_F s = g_rt->GetSize();
            g_rt->Clear(Theme::Panel);
            Stroke(Rect(0.5f, 0.5f, s.width - 0.5f, s.height - 0.5f), Theme::Border, 10, 1);
            Text(st->title, g_fSection.Get(), Rect(16, 10, s.width - 16, 36), Theme::Text);
            Fill(Rect(16, 42, s.width - 16, 78), Theme::Card, 8);
            Stroke(Rect(16, 42, s.width - 16, 78), Theme::Accent, 8, 1);
            Text(L"Enter للحفظ  •  Esc للإلغاء", g_fSmall.Get(), Rect(16, 84, s.width - 16, 104), Theme::SubText);
            if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) { st->brush.Reset(); st->rt.Reset(); }
            g_rt = mainRt;
            g_brush = mainBrush;
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:      // مربع الكتابة غامق متل البرنامج
    {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, RGB(0xE8, 0xEA, 0xED));
        SetBkColor(dc, RGB(0x1D, 0x21, 0x29));
        return (LRESULT)st->editBrush;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
            st->done = true;
        else if (st->edit)
            SetFocus(st->edit);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// بترجّع true إذا المستخدم ضغط Enter
// password = نخفي الحروف (مفتاح البث) ، ltr = نص إنكليزي (روابط ومفاتيح)
bool InputBox(const std::wstring& title, std::wstring& text, bool password, bool ltr, int limit)
{
    InputState st;
    st.title = title;

    RECT owner;
    GetWindowRect(g_hwnd, &owner);
    st.dpi = GetDpiForWindow(g_hwnd);
    auto S = [&](float v) { return (int)(v * st.dpi / 96.0f); };
    const int w = S(ltr ? 460.0f : 380.0f), h = S(112.0f);
    const int x = (owner.left + owner.right - w) / 2;
    const int y = (owner.top + owner.bottom - h) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, INPUT_CLASS, L"", WS_POPUP,
                                x, y, w, h, g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
        return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);
    const int corner = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    st.font      = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    st.editBrush = CreateSolidBrush(RGB(0x1D, 0x21, 0x29));
    DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | (ltr ? ES_LEFT : ES_RIGHT);
    if (password) style |= ES_PASSWORD;
    st.edit = CreateWindowExW(ltr ? 0 : WS_EX_RTLREADING, L"EDIT", text.c_str(), style,
                              S(26), S(50), w - S(52), S(22), hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(st.edit, WM_SETFONT, (WPARAM)st.font, TRUE);
    if (password)
        SendMessageW(st.edit, EM_SETPASSWORDCHAR, (WPARAM)L'•', 0);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);              // النص كلّه محدّد
    SendMessageW(st.edit, EM_LIMITTEXT, limit, 0);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(st.edit);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_KEYDOWN && (msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE))
        {
            st.ok   = (msg.wParam == VK_RETURN);
            st.done = true;
            break;
        }
        if ((msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
            && msg.hwnd != hwnd && msg.hwnd != st.edit)
        {
            st.done = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
        {
            PostQuitMessage((int)msg.wParam);
            break;
        }
    }

    if (st.ok)
    {
        std::vector<wchar_t> buf(limit + 2, L'\0');
        GetWindowTextW(st.edit, buf.data(), limit + 1);
        text = buf.data();
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    st.brush.Reset();
    st.rt.Reset();
    DestroyWindow(hwnd);
    DeleteObject(st.font);
    DeleteObject(st.editBrush);
    SetForegroundWindow(g_hwnd);
    return st.ok;
}
