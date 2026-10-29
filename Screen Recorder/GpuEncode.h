// =============================================================
//  GpuEncode.h - الطريق السريع للتسجيل (متل برنامج NVIDIA)
//
//  بدل: كرت الشاشة ← الذاكرة ← المعالج يحوّل الألوان ← كرت الشاشة يضغط
//  صار: كرت الشاشة يركّب ← كرت الشاشة يحوّل (NV12) ← كرت الشاشة يضغط
//  يعني الصورة ما بتطلع من كرت الشاشة أبداً: أسرع، أنعم، وأخف عالجهاز.
//
//  تحويل الألوان بيصير بالـ "Video Processor" تبع كرت الشاشة
//  بالمعيار الصحيح للفيديو (BT.709)، فالنصوص والحواف بتطلع أوضح.
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <vector>

class GpuFramePipe
{
public:
    GpuFramePipe();
    ~GpuFramePipe();

    // device: جهاز المركّب (لازم يكون فيه VIDEO_SUPPORT)
    bool Init(ID3D11Device* device, UINT width, UINT height, UINT fps);

    // مدير الجهاز لـ Media Foundation (بيعطيه للـ SinkWriter لحتى يقبل textures)
    IMFDXGIDeviceManager* Manager() const { return m_manager.Get(); }

    // بيحوّل صورة المشهد (BGRA) لـ NV12 بخانة فاضية. بيرجّع رقم الخانة أو -1
    int  Convert(ID3D11Texture2D* bgra);
    // بيعمل sample من الخانة (الخانة بترجع فاضية لحالها لما المشفّر يخلص منها)
    HRESULT MakeSample(int slot, IMFSample** sample);
    // خانة ما رح نكتبها (مثلاً وقفنا): منرجّعها فاضية
    void Release(int slot);
    // صورة الخانة نفسها (لمشفّر إنفيديا المباشر)
    ID3D11Texture2D* SlotTexture(int slot) const
    { return slot >= 0 && slot < (int)m_slots.size() ? m_slots[slot].tex.Get() : nullptr; }

private:
    struct Slot
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D>                tex;
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> view;
        std::shared_ptr<std::atomic<bool>>                     busy = std::make_shared<std::atomic<bool>>(false);
    };
    bool AddSlot();

    Microsoft::WRL::ComPtr<ID3D11Device>                    m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>             m_context;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice>               m_videoDevice;
    Microsoft::WRL::ComPtr<ID3D11VideoContext>              m_videoContext;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator>  m_enum;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor>            m_processor;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager>            m_manager;
    UINT                                                    m_resetToken = 0;
    UINT                                                    m_width = 0, m_height = 0;

    // مدخل الـ Video Processor: منعيد نعمله إذا تغيّر texture المشهد
    ID3D11Texture2D*                                        m_inTex = nullptr;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView>   m_inView;

    std::vector<Slot> m_slots;          // صور NV12 جاهزة للمشفّر
};
