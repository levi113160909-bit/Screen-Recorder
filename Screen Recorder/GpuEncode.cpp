// =============================================================
//  GpuEncode.cpp - الطريق السريع للتسجيل (شوف GpuEncode.h)
// =============================================================
#include "GpuEncode.h"
#include <d3d11_1.h>     // ID3D11VideoContext1 (الألوان بدقة)
#include <wrl/implements.h>
#include <mferror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr size_t MIN_SLOTS = 6, MAX_SLOTS = 24;

    // لما المشفّر يخلص من الصورة، Media Foundation بينادي هالكائن، فمنعلّم الخانة فاضية
    class SlotReleased : public Microsoft::WRL::RuntimeClass<
        Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFAsyncCallback>
    {
    public:
        explicit SlotReleased(std::shared_ptr<std::atomic<bool>> busy) : m_busy(std::move(busy)) {}
        STDMETHOD(GetParameters)(DWORD*, DWORD*) override { return E_NOTIMPL; }
        STDMETHOD(Invoke)(IMFAsyncResult*) override
        {
            *m_busy = false;
            return S_OK;
        }
    private:
        std::shared_ptr<std::atomic<bool>> m_busy;
    };
}

GpuFramePipe::GpuFramePipe() = default;
GpuFramePipe::~GpuFramePipe() = default;

bool GpuFramePipe::Init(ID3D11Device* device, UINT width, UINT height, UINT fps)
{
    if (!device || width < 16 || height < 16)
        return false;
    m_device = device;
    m_device->GetImmediateContext(&m_context);
    m_width  = width & ~1u;
    m_height = height & ~1u;
    if (FAILED(m_device.As(&m_videoDevice)) || FAILED(m_context.As(&m_videoContext)))
        return false;                      // الجهاز انعمل بلا VIDEO_SUPPORT

    // الـ Video Processor: BGRA بنفس الحجم ← NV12
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
    cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    cd.InputWidth  = m_width;  cd.InputHeight  = m_height;
    cd.OutputWidth = m_width;  cd.OutputHeight = m_height;
    cd.InputFrameRate  = { fps, 1 };
    cd.OutputFrameRate = { fps, 1 };
    cd.Usage = D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;
    if (FAILED(m_videoDevice->CreateVideoProcessorEnumerator(&cd, &m_enum)))
        return false;
    UINT flags = 0;
    if (FAILED(m_enum->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &flags)) ||
        !(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
        return false;
    if (FAILED(m_videoDevice->CreateVideoProcessor(m_enum.Get(), 0, &m_processor)))
        return false;

    // الألوان: الدخل RGB كامل، والخرج فيديو قياسي BT.709 (16-235) متل كل الفيديوهات
    ComPtr<ID3D11VideoContext1> vc1;
    if (SUCCEEDED(m_videoContext.As(&vc1)))
    {
        vc1->VideoProcessorSetStreamColorSpace1(m_processor.Get(), 0, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        vc1->VideoProcessorSetOutputColorSpace1(m_processor.Get(), DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
    }
    else
    {
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE in{}, out{};
        in.RGB_Range = 0;                     // 0-255
        out.YCbCr_Matrix = 1;                 // BT.709
        out.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        m_videoContext->VideoProcessorSetStreamColorSpace(m_processor.Get(), 0, &in);
        m_videoContext->VideoProcessorSetOutputColorSpace(m_processor.Get(), &out);
    }
    m_videoContext->VideoProcessorSetStreamAutoProcessingMode(m_processor.Get(), 0, FALSE);   // بلا "تحسينات" من التعريف

    // مدير الجهاز لـ Media Foundation
    if (FAILED(MFCreateDXGIDeviceManager(&m_resetToken, &m_manager)) ||
        FAILED(m_manager->ResetDevice(m_device.Get(), m_resetToken)))
        return false;

    for (size_t i = 0; i < MIN_SLOTS; ++i)
        if (!AddSlot())
            return false;
    return true;
}

bool GpuFramePipe::AddSlot()
{
    Slot s;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = m_width;  d.Height = m_height;
    d.MipLevels = 1;    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_NV12;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&d, nullptr, &s.tex)))
        return false;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{};
    od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    if (FAILED(m_videoDevice->CreateVideoProcessorOutputView(s.tex.Get(), m_enum.Get(), &od, &s.view)))
        return false;
    m_slots.push_back(std::move(s));
    return true;
}

int GpuFramePipe::Convert(ID3D11Texture2D* bgra)
{
    if (!bgra || !m_processor)
        return -1;

    // خانة فاضية (إذا المشفّر متأخر منزيد خانات، لحد حد معيّن)
    int slot = -1;
    for (int tries = 0; slot < 0 && tries < 20; ++tries)
    {
        for (size_t i = 0; i < m_slots.size(); ++i)
            if (!*m_slots[i].busy) { slot = (int)i; break; }
        if (slot < 0 && m_slots.size() < MAX_SLOTS && AddSlot())
            slot = (int)m_slots.size() - 1;
        if (slot < 0)
            Sleep(1);
    }
    if (slot < 0)
        return -1;                          // المشفّر عالق: منترك هالإطار

    if (bgra != m_inTex)
    {
        m_inView.Reset();
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};
        iv.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        if (FAILED(m_videoDevice->CreateVideoProcessorInputView(bgra, m_enum.Get(), &iv, &m_inView)))
            return -1;
        m_inTex = bgra;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = m_inView.Get();
    if (FAILED(m_videoContext->VideoProcessorBlt(m_processor.Get(), m_slots[slot].view.Get(), 0, 1, &stream)))
        return -1;
    *m_slots[slot].busy = true;
    return slot;
}

HRESULT GpuFramePipe::MakeSample(int slot, IMFSample** out)
{
    if (slot < 0 || slot >= (int)m_slots.size())
        return E_INVALIDARG;
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), m_slots[slot].tex.Get(), 0, FALSE, &buffer);
    ComPtr<IMF2DBuffer> buf2d;
    DWORD len = 0;
    if (SUCCEEDED(hr)) hr = buffer.As(&buf2d);
    if (SUCCEEDED(hr)) hr = buf2d->GetContiguousLength(&len);
    if (SUCCEEDED(hr)) hr = buffer->SetCurrentLength(len);

    // "عيّنة متتبَّعة": بتخبّرنا لما المشفّر يخلص منها، لحتى نرجع نستعمل الخانة
    ComPtr<IMFTrackedSample> tracked;
    ComPtr<IMFSample> sample;
    if (SUCCEEDED(hr)) hr = MFCreateTrackedSample(&tracked);
    if (SUCCEEDED(hr)) hr = tracked.As(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.Get());
    if (SUCCEEDED(hr))
    {
        auto cb = Microsoft::WRL::Make<SlotReleased>(m_slots[slot].busy);
        hr = tracked->SetAllocator(cb.Get(), nullptr);
    }
    if (FAILED(hr))
    {
        *m_slots[slot].busy = false;
        return hr;
    }
    *out = sample.Detach();
    return S_OK;
}

void GpuFramePipe::Release(int slot)
{
    if (slot >= 0 && slot < (int)m_slots.size())
        *m_slots[slot].busy = false;
}
