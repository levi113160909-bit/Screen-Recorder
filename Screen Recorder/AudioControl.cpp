// =============================================================
//  AudioControl.cpp - الصوت: تشغيل/إطفاء المصادر، اختيار الجهاز، ومنزلقات العلو
// =============================================================
#include "App.h"

void SetAudioEnabled(AudioSource& src, bool& flag, bool on)
{
    if (Busy())
        return;                     // ما منغيّر الصوت بنص التسجيل أو البث
    flag = on;
    if (on) src.Start();
    else    src.Stop();
    SaveSettings();
    RestartReplayIfActive();
}

// قائمة اختيار الجهاز (سماعات أو ميكروفون)
void ShowDeviceMenu(AudioSource& src, bool on, AudioSource::Kind kind, POINT pt)
{
    if (Busy())
        return;
    const std::vector<AudioDevice> devices = AudioSource::ListDevices(kind);
    std::vector<std::wstring> items = { L"الجهاز الافتراضي في ويندوز" };
    int checked = src.DeviceId().empty() ? 0 : -1;
    for (size_t i = 0; i < devices.size(); ++i)
    {
        items.push_back(devices[i].name);
        if (devices[i].id == src.DeviceId())
            checked = (int)i + 1;
    }
    const int pick = ShowMenu(items, checked, pt);
    if (pick < 0)
        return;
    src.SetDeviceId(pick == 0 ? L"" : devices[pick - 1].id);
    if (on)                     // منعيد تشغيله على الجهاز الجديد
    {
        src.Stop();
        src.Start();
    }
    SaveSettings();
    RestartReplayIfActive();
}

// من مستوى الصوت (0..1) لطول الشريط، بالـ dB لأن الأذن هيك بتسمع
float LevelToMeter(float peak)
{
    if (peak <= 0.00001f)
        return 0.0f;
    const float db = 20.0f * std::log10(peak);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

// من مكان الماوس لعلو الصوت (مع مغناطيس على 100%)
void SetVolumeFromX(AudioSource& src, const D2D1_RECT_F& track, float x)
{
    float f = (track.right - x) / (track.right - track.left);
    f = std::clamp(f, 0.0f, 1.0f);
    float vol = f * 2.0f;
    if (std::fabs(vol - 1.0f) < 0.05f)
        vol = 1.0f;
    src.SetVolume(vol);
}

// من مكان الماوس لعلو (0..2، مع مغناطيس على 100%)
float VolumeAtX(const D2D1_RECT_F& track, float x)
{
    const float f = std::clamp((track.right - x) / (track.right - track.left), 0.0f, 1.0f);
    const float vol = f * 2.0f;
    return std::fabs(vol - 1.0f) < 0.05f ? 1.0f : vol;
}
