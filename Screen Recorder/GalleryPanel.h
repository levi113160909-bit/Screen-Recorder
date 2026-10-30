// =============================================================
//  GalleryPanel.h - "المعرض": كل تسجيلاتك ولقطاتك بمكان واحد
//
//  صور مصغّرة بتنحمّل بالخلفية (ما بتعلّق البرنامج)، مع المدة والحجم.
//  فوق: فلتر (الكل / التسجيلات / الإعادات / البث / الصور).
//  على كل بطاقة: تشغيل، قصّ، إعادة تسمية، إظهار بالمجلد، حذف (لسلة المحذوفات).
// =============================================================
#pragma once
#include "Panel.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

struct GalleryHooks
{
    // المجلدات: {المسار، النوع} النوع: 0 تسجيل ، 1 إعادة ، 2 بث ، 3 صورة
    std::function<std::vector<std::pair<std::wstring, int>>()> folders;
    std::function<void(const std::wstring&)> trim;         // فتح نافذة القص
    std::function<bool(std::wstring&)>       rename;       // اسم جديد (بلا الامتداد) ، false = إلغاء
    std::function<void()>                    openFolder;
};

class GalleryPanel : public Panel
{
public:
    explicit GalleryPanel(GalleryHooks hooks);
    ~GalleryPanel() override;

    std::wstring   Title() const override    { return L"المعرض"; }
    std::wstring   Subtitle() const override { return L"تسجيلاتك ولقطاتك وصورك"; }
    const wchar_t* IconGlyph() const override { return L""; }
    float          Width() const override    { return 900; }
    float          BodyHeight() const override { return m_height; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;

    void Refresh();

private:
    struct Item
    {
        std::wstring path, name;
        int          kind = 0;
        ULONGLONG    size = 0;
        FILETIME     time{};
    };
    struct Thumb
    {
        bool              ready = false;
        std::vector<BYTE> px;
        int               w = 0, h = 0;
        LONGLONG          duration = 0;
        ComPtr<ID2D1Bitmap> bitmap;
        ID2D1HwndRenderTarget* owner = nullptr;
    };
    void Loader();

    GalleryHooks             m_h;
    std::vector<Item>        m_items;
    int                      m_filter = 0;
    float                    m_height = 500;
    std::shared_ptr<std::mutex>                    m_mutex = std::make_shared<std::mutex>();
    std::shared_ptr<std::map<std::wstring, Thumb>> m_thumbs = std::make_shared<std::map<std::wstring, Thumb>>();
    std::shared_ptr<std::vector<std::wstring>>     m_queue = std::make_shared<std::vector<std::wstring>>();
    std::shared_ptr<std::atomic<bool>>             m_stop = std::make_shared<std::atomic<bool>>(false);
    std::thread              m_thread;
};
