// =============================================================
//  CaptureDevice.cpp - أجهزة الالتقاط (شوف CaptureDevice.h)
// =============================================================
#include <initguid.h>          // لازم أول شي: بيعرّف أرقام الخصائص (DEVPKEY) بهالملف
#include "CaptureDevice.h"
#include <cfgmgr32.h>
#include <devpkey.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <vector>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "propsys.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    std::wstring Lower(std::wstring s)
    {
        for (wchar_t& c : s) c = (wchar_t)std::towlower(c);
        return s;
    }

    // "رقم الحاوية" لجهاز فيديو (من رابطه)
    bool VideoContainerId(const std::wstring& link, GUID& out)
    {
        DEVPROPTYPE type = 0;
        ULONG size = 0;
        if (CM_Get_Device_Interface_PropertyW(link.c_str(), &DEVPKEY_Device_InstanceId, &type, nullptr, &size, 0) != CR_BUFFER_SMALL || size == 0)
            return false;
        std::vector<BYTE> buf(size);
        if (CM_Get_Device_Interface_PropertyW(link.c_str(), &DEVPKEY_Device_InstanceId, &type, buf.data(), &size, 0) != CR_SUCCESS)
            return false;
        DEVINST inst = 0;
        if (CM_Locate_DevNodeW(&inst, reinterpret_cast<DEVINSTID_W>(buf.data()), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            return false;
        GUID id{};
        size = sizeof(id);
        if (CM_Get_DevNode_PropertyW(inst, &DEVPKEY_Device_ContainerId, &type, reinterpret_cast<PBYTE>(&id), &size, 0) != CR_SUCCESS)
            return false;
        out = id;
        return true;
    }

    // كلمات مميّزة من الاسم (منشيل الكلمات العامة)
    std::vector<std::wstring> Words(const std::wstring& name)
    {
        static const wchar_t* common[] = { L"video", L"audio", L"usb", L"capture", L"game", L"device", L"camera",
                                           L"input", L"digital", L"interface", L"microphone", L"line", L"in" };
        std::vector<std::wstring> out;
        std::wstring w;
        auto flush = [&]
        {
            if (w.size() >= 2 && std::none_of(std::begin(common), std::end(common), [&](const wchar_t* c) { return w == c; }))
                out.push_back(w);
            w.clear();
        };
        for (wchar_t c : Lower(name))
        {
            if (std::iswalnum(c)) w += c;
            else flush();
        }
        flush();
        return out;
    }
}

bool LooksLikeCaptureCard(const std::wstring& name)
{
    static const wchar_t* keys[] = {
        L"elgato", L"game capture", L"avermedia", L"live gamer", L"cam link", L"hd60", L"4k60", L"4k s", L"4k x",
        L"ripsaw", L"nzxt", L"ugreen", L"genki", L"shadowcast", L"magewell", L"capture", L"hdmi", L"usb video",
        L"usb3 video", L"ms2109", L"ms2130", L"ezcap", L"mirabox", L"live streamer", L"streamer x" };
    const std::wstring n = Lower(name);
    for (const wchar_t* k : keys)
        if (n.find(k) != std::wstring::npos)
            return true;
    return false;
}

std::wstring FindDeviceAudio(const std::wstring& videoLink, const std::wstring& videoName, std::wstring* audioName)
{
    GUID videoContainer{};
    const bool haveContainer = VideoContainerId(videoLink, videoContainer);

    ComPtr<IMMDeviceEnumerator> en;
    ComPtr<IMMDeviceCollection> list;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))) ||
        FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &list)))
        return L"";

    const std::vector<std::wstring> videoWords = Words(videoName);
    std::wstring bestId, bestName;
    int bestScore = 0;
    UINT count = 0;
    list->GetCount(&count);
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> dev;
        ComPtr<IPropertyStore> props;
        if (FAILED(list->Item(i, &dev)) || FAILED(dev->OpenPropertyStore(STGM_READ, &props)))
            continue;
        LPWSTR idRaw = nullptr;
        if (FAILED(dev->GetId(&idRaw)))
            continue;
        const std::wstring id = idRaw;
        CoTaskMemFree(idRaw);

        std::wstring name;
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal)
            name = v.pwszVal;
        PropVariantClear(&v);

        int score = 0;
        // 1) نفس "رقم الحاوية" = نفس الجهاز بالضبط
        if (haveContainer)
        {
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(PKEY_Device_ContainerId, &v)) && v.vt == VT_CLSID && v.puuid &&
                IsEqualGUID(*v.puuid, videoContainer))
                score += 100;
            PropVariantClear(&v);
        }
        // 2) احتياط: كلمات مشتركة بالاسم (مثلاً "4K S" أو "HD60 X")
        const std::wstring lowName = Lower(name);
        for (const std::wstring& w : videoWords)
            if (lowName.find(w) != std::wstring::npos)
                score += 10;
        if (score > bestScore)
        {
            bestScore = score;
            bestId    = id;
            bestName  = name;
        }
    }
    if (bestScore < 10)
        return L"";
    if (audioName)
        *audioName = bestName;
    return bestId;
}

const wchar_t* DeviceModeName(int mode)
{
    static const wchar_t* names[DEVICE_MODE_COUNT] = { L"تلقائي", L"1080p • 60", L"1440p • 60", L"4K • 30", L"4K • 60" };
    return names[std::clamp(mode, 0, DEVICE_MODE_COUNT - 1)];
}
