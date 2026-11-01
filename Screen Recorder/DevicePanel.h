// =============================================================
//  DevicePanel.h - نافذة "إعدادات جهاز الالتقاط" (بلايستيشن ، إكس بوكس...)
//
//  فيها:
//   - حالة الجهاز: الدقة والإطارات اللي عم توصل فعلياً ، وتنبيه إذا الصورة سودا (HDCP)
//   - اختيار الدقة: تلقائي ، 1080p60 ، 1440p60 ، 4K30 ، 4K60
//   - صوت الجهاز: تشغيل/إطفاء ، العلو ، الكتم ، اختيار جهاز الصوت ،
//     وتأخير الصوت لحتى يطابق الصورة تماماً
// =============================================================
#pragma once
#include "Panel.h"
#include "Capture.h"
#include "AppAudio.h"

struct DevicePanelHooks
{
    std::function<bool(SceneLayer&)>                              get;         // نسخة من الطبقة (false = انحذفت)
    std::function<void(const std::function<void(SceneLayer&)>&)>  modify;      // تعديل الطبقة الحقيقية
    std::function<void(bool on)>                                  setAudio;    // تشغيل/إطفاء صوت الجهاز
    std::function<void()>                                         pickAudio;   // اختيار جهاز الصوت بإيدك
    std::function<void()>                                         save;
};

class DevicePanel : public Panel
{
public:
    DevicePanel(AppAudioMixer& mixer, DevicePanelHooks hooks) : m_mixer(mixer), m_h(std::move(hooks)) {}

    std::wstring   Title() const override    { return L"جهاز الالتقاط"; }
    std::wstring   Subtitle() const override { return L"الصورة والصوت من البلايستيشن أو الإكس بوكس"; }
    const wchar_t* IconGlyph() const override { return L"\xE7FC"; }
    float          Width() const override    { return 580; }
    float          BodyHeight() const override { return m_height; }
    void           DrawBody(float left, float top, float right) override;
    void           OnClick(int id) override;
    void           OnCommit() override { if (m_h.save) m_h.save(); }

private:
    AppAudioMixer&   m_mixer;
    DevicePanelHooks m_h;
    float            m_height = 600;
    float            m_level = 0;
};
