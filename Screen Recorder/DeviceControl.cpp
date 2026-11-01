// =============================================================
//  DeviceControl.cpp - أجهزة الالتقاط بالمشهد: الإضافة ، الصوت ، ونافذة الإعدادات
// =============================================================
#include "App.h"

// طبقة جهاز التقاط: مالية الإطار ، وصوتها بيتوصل لحالو
void AddDeviceLayer(const CameraInfo& c)
{
    SceneLayer l;
    l.kind        = LayerKind::Device;
    l.source      = c.link;
    l.target.name = c.name;
    l.mode        = 0;
    std::wstring audioName;
    l.audioId     = FindDeviceAudio(c.link, c.name, &audioName);
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const double cw = g_scene.canvasW, ch = g_scene.canvasH;
        l.id = g_scene.nextId++;
        // 16:9 بنص الإطار (عادة الإطار نفسه 16:9 فبيعبّيه كله)
        const double k = std::min(cw / 1920.0, ch / 1080.0);
        l.w = 1920 * k;
        l.h = 1080 * k;
        l.x = (cw - l.w) / 2;
        l.y = (ch - l.h) / 2;
        g_scene.layers.insert(g_scene.layers.begin(), l);   // تحت كل شي (الكاميرا والنص فوقه)
        g_selectedLayer = l.id;
    }
    if (!l.audioId.empty())
    {
        g_appMixer.AddDevice(l.audioId, L"صوت " + c.name, 0);
        ShowMessage(L"أُضيف " + c.name + L" مع صوته", false);
    }
    else
        ShowMessage(L"أُضيف " + c.name + L" • لم يُعثر على صوته تلقائيًا: اختره من إعدادات الجهاز", true);
    SaveScenes();
}

namespace
{
    bool GetLayer(int id, SceneLayer& out)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i < 0) return false;
        out = g_scene.layers[i];
        return true;
    }

    void ModifyLayer(int id, const std::function<void(SceneLayer&)>& fn)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        const int i = FindLayer(id);
        if (i >= 0) fn(g_scene.layers[i]);
    }

    // صوت الجهاز مستعمل بطبقة تانية بالمشهد؟
    bool AudioUsedElsewhere(const std::wstring& audioId, int exceptLayer)
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (const SceneLayer& l : g_scene.layers)
            if (l.id != exceptLayer && l.kind == LayerKind::Device && l.audioId == audioId)
                return true;
        return false;
    }

    void RemoveDeviceAudio(const std::wstring& audioId)
    {
        const int idx = audioId.empty() ? -1 : g_appMixer.FindDevice(audioId);
        if (idx >= 0)
        {
            g_appMixer.Remove(idx);
            g_appLevels.clear();
        }
    }
}

// الطبقة انحذفت: منشيل صوتها (إذا ما في طبقة تانية بتستعمله)
void OnDeviceLayerRemoved(const SceneLayer& l)
{
    if (l.kind == LayerKind::Device && !l.audioId.empty() && !AudioUsedElsewhere(l.audioId, l.id))
        RemoveDeviceAudio(l.audioId);
}

// تشغيل/إطفاء صوت الجهاز
void SetDeviceAudio(int layerId, bool on)
{
    SceneLayer l;
    if (!GetLayer(layerId, l))
        return;
    if (!on)
    {
        if (!AudioUsedElsewhere(l.audioId, l.id))
            RemoveDeviceAudio(l.audioId);
        SaveScenes();
        return;
    }
    std::wstring id = l.audioId, name;
    if (id.empty())
        id = FindDeviceAudio(l.source, l.target.name, &name);
    if (id.empty())
    {
        PickDeviceAudio(layerId);
        return;
    }
    ModifyLayer(layerId, [&](SceneLayer& x) { x.audioId = id; });
    g_appMixer.AddDevice(id, L"صوت " + l.target.name, 0);
    SaveScenes();
}

// اختيار جهاز الصوت بإيدك (من أجهزة الإدخال: كرت الالتقاط بيبيّن متل مايك)
void PickDeviceAudio(int layerId)
{
    SceneLayer l;
    if (!GetLayer(layerId, l))
        return;
    const std::vector<AudioDevice> devices = AudioSource::ListDevices(AudioSource::Kind::Microphone);
    if (devices.empty())
    {
        ShowMessage(L"لا توجد أجهزة صوت إدخال", true);
        OpenDevicePanel(layerId);
        return;
    }
    std::vector<std::wstring> items;
    int checked = -1;
    for (int i = 0; i < (int)devices.size(); ++i)
    {
        items.push_back(devices[i].name);
        if (devices[i].id == l.audioId) checked = i;
    }
    POINT pt;
    GetCursorPos(&pt);
    const int pick = ShowMenu(items, checked, pt);
    if (pick >= 0 && devices[pick].id != l.audioId)
    {
        // الصوت القديم منشيله ، والجديد منضيفه (منحافظ على العلو والتأخير)
        AppAudioEntry old;
        const int oldIdx = l.audioId.empty() ? -1 : g_appMixer.FindDevice(l.audioId);
        if (oldIdx >= 0)
            old = g_appMixer.Entry(oldIdx);
        if (!AudioUsedElsewhere(l.audioId, l.id))
            RemoveDeviceAudio(l.audioId);
        const std::wstring id = devices[pick].id;
        ModifyLayer(layerId, [&](SceneLayer& x) { x.audioId = id; });
        g_appMixer.AddDevice(id, L"صوت " + l.target.name, old.delayMs);
        const int idx = g_appMixer.FindDevice(id);
        if (idx >= 0 && oldIdx >= 0)
        {
            g_appMixer.SetVolume(idx, old.volume);
            g_appMixer.SetMuted(idx, old.muted);
        }
        SaveScenes();
    }
    OpenDevicePanel(layerId);
}

void OpenDevicePanel(int layerId)
{
    DevicePanelHooks h;
    h.get       = [layerId](SceneLayer& out) { return GetLayer(layerId, out); };
    h.modify    = [layerId](const std::function<void(SceneLayer&)>& fn) { ModifyLayer(layerId, fn); };
    h.setAudio  = [layerId](bool on) { SetDeviceAudio(layerId, on); };
    h.pickAudio = [layerId] { PickDeviceAudio(layerId); };
    h.save      = [] { SaveScenes(); };
    g_panels.Open(std::make_unique<DevicePanel>(g_appMixer, std::move(h)));
}

// كبسة على سطر صوت جهاز: منفتح إعدادات الجهاز اللي إلو
bool OpenDevicePanelForAudio(const std::wstring& audioId)
{
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(g_scene.mutex);
        for (const SceneLayer& l : g_scene.layers)
            if (l.kind == LayerKind::Device && l.audioId == audioId) { id = l.id; break; }
    }
    if (!id)
        return false;
    OpenDevicePanel(id);
    return true;
}

// تحويل كاميرا <-> جهاز التقاط (إذا ما انعرف لحالو من الاسم)
void ConvertCameraLayer(int layerId, bool toDevice)
{
    SceneLayer l;
    if (!GetLayer(layerId, l))
        return;
    if (toDevice)
    {
        std::wstring id = FindDeviceAudio(l.source, l.target.name);
        ModifyLayer(layerId, [&](SceneLayer& x)
        {
            x.kind = LayerKind::Device;
            x.shape = SHAPE_RECT;
            x.mirror = false;
            x.chroma = false;
            x.mode = 0;
            x.audioId = id;
        });
        if (!id.empty())
            g_appMixer.AddDevice(id, L"صوت " + l.target.name, 0);
    }
    else
    {
        OnDeviceLayerRemoved(l);
        ModifyLayer(layerId, [](SceneLayer& x) { x.kind = LayerKind::Camera; x.audioId.clear(); });
    }
    SaveScenes();
}
