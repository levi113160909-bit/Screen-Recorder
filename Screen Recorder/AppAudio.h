// =============================================================
//  AppAudio.h - أصوات البرامج (كل مشهد إله أصواته)
//
//  بدل ما نسجّل كل صوت الكمبيوتر، منختار برامج معيّنة:
//  مثلاً صوت اللعبة + صوت ديسكورد، بدون إشعارات ويندوز ولا موسيقى تانية.
//
//  كيف بيشتغل:
//   - كل برنامج إله AudioSource من النوع Application (بيلقط صوته لحالو)
//   - "الخلّاط" بيجمعهم كلهم بمصدر واحد (Output) من النوع Generated
//   - التسجيل والبث بيشوفوا Output متل أي مصدر صوت عادي
//   - لما تبدّل المشهد منبدّل لائحة البرامج، والـ Output بيضل نفسه
//     (فالتسجيل بيكمّل بدون ما ينقطع)
//
//  بيحتاج ويندوز 10 نسخة 2004 أو أحدث (أو ويندوز 11)
// =============================================================
#pragma once
#include "Audio.h"
#include <memory>

// برنامج فيه صوت (للقائمة)
struct AudioApp
{
    DWORD        pid = 0;
    std::wstring exePath;     // المسار الكامل (منستعمله لنلاقيه مرة تانية)
    std::wstring name;        // الاسم اللي بيظهر (مثلاً "Google Chrome")
    std::wstring title;       // عنوان نافذته (إذا إله نافذة)
    bool         playing = false;   // عم يطلّع صوت هلق؟
};

// البرامج المفتوحة: اللي إلها نوافذ + اللي عم تطلّع صوت (حتى لو بلا نافذة)
// اللي عم يطلّعوا صوت بيجوا أول
std::vector<AudioApp> ListAudioApps(HWND exclude);

// مسارات البرامج اللي عم تطلّع صوت هلق (بحروف صغيرة)
std::vector<std::wstring> ExePathsPlayingAudio();
std::wstring LowerPath(std::wstring s);
// اسم البرنامج الحلو (مثلاً "Google Chrome") من ملف الـ exe
std::wstring AppFriendlyName(const std::wstring& exePath);

// برنامج محفوظ بالمشهد (أو صوت جهاز التقاط: deviceId مش فاضي)
struct AppAudioEntry
{
    std::wstring exePath;
    std::wstring name;
    float        volume = 1.0f;
    bool         muted  = false;
    std::wstring deviceId;            // صوت جهاز التقاط (كرت البلايستيشن): رقم جهاز الصوت بويندوز
    int          delayMs = 0;         // تأخير الصوت لحتى يطابق الصورة (جهاز الالتقاط)
    bool IsDevice() const { return !deviceId.empty(); }
};

class AppAudioMixer
{
public:
    AppAudioMixer();
    ~AppAudioMixer() { Stop(); }

    void Start();                              // بلّش الخلّاط (مرة وحدة بأول البرنامج)
    void Stop();
    AudioSource& Output() { return m_output; } // المصدر اللي بيروح للتسجيل والبث

    // لائحة البرامج للمشهد الحالي
    void                       SetEntries(const std::vector<AppAudioEntry>& entries);
    std::vector<AppAudioEntry> Entries();
    void   Add(const AudioApp& app);
    // صوت جهاز التقاط (بيرجّع false إذا موجود من قبل)
    bool   AddDevice(const std::wstring& deviceId, const std::wstring& name, int delayMs);
    int    FindDevice(const std::wstring& deviceId);    // -1 = مش موجود
    void   SetDelay(int index, int ms);
    // تأخير إضافي للكل (لمزامنة الترجمة): لأجهزة الالتقاط ، وللبرامج
    void   SetExtraDelays(int deviceMs, int appMs) { m_extraDevice = deviceMs; m_extraApp = appMs; }
    void   Remove(int index);
    size_t Count();

    // لكل برنامج: معلوماته، هل عم يلتقط هلق، ومستوى صوته
    AppAudioEntry Entry(int index);
    bool          IsConnected(int index);
    float         TakePeak(int index);
    void          SetVolume(int index, float v);
    void          SetMuted(int index, bool m);

    // كل كم ثانية: البرامج اللي انسكّرت منفصلها، واللي انفتحت منوصلها
    void Reconnect();

private:
    struct Child
    {
        AppAudioEntry                e;
        std::unique_ptr<AudioSource> src;     // فاضي = البرنامج مش مفتوح
        int                          tap = 0;
        HANDLE                       process = nullptr;   // لنعرف إذا انسكّر
        bool                         primed = false;      // تجمّع صوت كفاية لنبلّش ناخد منه؟
        size_t                       primedFor = 0;       // التأخير (بالعينات) اللي تجمّعنا عليه
        ULONGLONG                    lastTry = 0;
    };

    void Generate(float* out, size_t frames);         // بيشتغل بخيط الصوت
    static void Connect(Child& c, const AudioApp& app);
    static void ConnectDevice(Child& c);
    static void Disconnect(Child& c);

    AudioSource        m_output{ AudioSource::Kind::Generated };
    std::mutex         m_mutex;                   // بيحمي m_children
    std::vector<Child> m_children;
    std::vector<float> m_tmp;
    std::atomic<int>   m_extraDevice{ 0 }, m_extraApp{ 0 };
};
