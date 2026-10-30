// =============================================================
//  VideoPlayer.cpp - مشغّل الفيديو (Media Engine بوضع "سحب الصور")
// =============================================================
#include "VideoPlayer.h"
#undef GetCurrentTime            // ماكرو قديم من ويندوز بيخرّب IMFMediaEngine::GetCurrentTime
#include <d3d11.h>
#include <d3d10.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <algorithm>
#include <atomic>
#include <cmath>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

// ---- أحداث المشغّل (انفتح الملف ، صار خطأ) ----
class PlayerNotify : public IMFMediaEngineNotify
{
public:
    std::atomic<bool> ready{ false }, error{ false };

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify))
        {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR, DWORD) override
    {
        if (event == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA) ready = true;
        else if (event == MF_MEDIA_ENGINE_EVENT_ERROR)     error = true;
        return S_OK;
    }

private:
    std::atomic<ULONG> m_ref{ 1 };
};

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer()
{
    if (m_engine)
        m_engine->Shutdown();            // بيوقّف الصوت والفك
    m_engine.Reset();
    if (m_notify)
        m_notify->Release();
    m_staging.Reset();
    m_target.Reset();
    m_manager.Reset();
    m_ctx.Reset();
    m_dev.Reset();
    if (m_started)
        MFShutdown();
}

bool VideoPlayer::Open(const std::wstring& path)
{
    if (FAILED(MFStartup(MF_VERSION)))
        return false;
    m_started = true;

    // كرت شاشة خاص بالمشغّل (بيفك الفيديو بالـ GPU)
    const UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                 &m_dev, nullptr, &m_ctx)))
        return false;
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(m_dev.As(&mt)))
        mt->SetMultithreadProtected(TRUE);   // المشغّل بيستعمله من خيوط تانية
    UINT token = 0;
    if (FAILED(MFCreateDXGIDeviceManager(&token, &m_manager)) || FAILED(m_manager->ResetDevice(m_dev.Get(), token)))
        return false;

    ComPtr<IMFMediaEngineClassFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;
    m_notify = new PlayerNotify();
    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 3);
    attr->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, m_manager.Get());
    attr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, m_notify);
    attr->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
    if (FAILED(factory->CreateInstance(0, attr.Get(), &m_engine)))
        return false;

    BSTR url = SysAllocString(path.c_str());
    const HRESULT hr = m_engine->SetSource(url);
    SysFreeString(url);
    return SUCCEEDED(hr);
}

bool VideoPlayer::Ready() const  { return m_engine && m_notify && m_notify->ready && !m_notify->error; }
bool VideoPlayer::Failed() const { return !m_engine || (m_notify && m_notify->error); }
bool VideoPlayer::Playing() const { return Ready() && !m_engine->IsPaused() && !m_engine->IsEnded(); }

void VideoPlayer::Play()
{
    if (!Ready()) return;
    if (m_engine->IsEnded())
        m_engine->SetCurrentTime(0.0);     // خلص: منرجع من الأول
    m_engine->Play();
}

void VideoPlayer::Pause()
{
    if (Ready()) m_engine->Pause();
}

void VideoPlayer::Seek(LONGLONG t)
{
    if (Ready()) m_engine->SetCurrentTime(std::max<LONGLONG>(0, t) / 1e7);
}

LONGLONG VideoPlayer::Position() const
{
    return Ready() ? (LONGLONG)(m_engine->GetCurrentTime() * 1e7) : 0;
}

LONGLONG VideoPlayer::Length() const
{
    if (!Ready()) return 0;
    const double d = m_engine->GetDuration();
    return (d > 0 && std::isfinite(d)) ? (LONGLONG)(d * 1e7) : 0;
}

bool VideoPlayer::Ended() const { return Ready() && m_engine->IsEnded(); }
void VideoPlayer::SetVolume(float v) { if (m_engine) m_engine->SetVolume(std::clamp(v, 0.0f, 1.0f)); }
void VideoPlayer::SetMuted(bool m)   { if (m_engine) m_engine->SetMuted(m ? TRUE : FALSE); }

bool VideoPlayer::Poll(int maxWidth, std::vector<BYTE>& px, int& w, int& h)
{
    if (!Ready() || !m_engine->HasVideo())
        return false;
    if (!m_firstSeek)                       // لحتى تطلع أول صورة بلا ما نشغّل
    {
        m_firstSeek = true;
        m_engine->SetCurrentTime(0.0);
    }
    LONGLONG pts = 0;
    if (m_engine->OnVideoStreamTick(&pts) != S_OK)
        return false;                       // ما في صورة جديدة

    DWORD vw = 0, vh = 0;
    if (FAILED(m_engine->GetNativeVideoSize(&vw, &vh)) || vw == 0 || vh == 0)
        return false;
    int tw = (int)std::min<DWORD>(vw, (DWORD)maxWidth);
    int th = (int)((ULONGLONG)vh * tw / vw);
    tw &= ~1; th &= ~1;
    if (tw <= 0 || th <= 0)
        return false;

    if (!m_target || tw != m_w || th != m_h)
    {
        m_target.Reset();
        m_staging.Reset();
        D3D11_TEXTURE2D_DESC d{};
        d.Width = tw; d.Height = th; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(m_dev->CreateTexture2D(&d, nullptr, &m_target)))
            return false;
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_dev->CreateTexture2D(&d, nullptr, &m_staging)))
        {
            m_target.Reset();
            return false;
        }
        m_w = tw; m_h = th;
    }

    const MFVideoNormalizedRect src{ 0, 0, 1, 1 };
    const RECT dst{ 0, 0, tw, th };
    const MFARGB border{ 0, 0, 0, 255 };
    if (FAILED(m_engine->TransferVideoFrame(m_target.Get(), &src, &dst, &border)))
        return false;
    m_ctx->CopyResource(m_staging.Get(), m_target.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(m_ctx->Map(m_staging.Get(), 0, D3D11_MAP_READ, 0, &map)))
        return false;
    px.resize((size_t)tw * th * 4);
    for (int y = 0; y < th; ++y)
        memcpy(px.data() + (size_t)y * tw * 4, (const BYTE*)map.pData + (size_t)y * map.RowPitch, (size_t)tw * 4);
    m_ctx->Unmap(m_staging.Get(), 0);
    w = tw; h = th;
    return true;
}
