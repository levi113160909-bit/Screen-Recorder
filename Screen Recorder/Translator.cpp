// =============================================================
//  Translator.cpp - ترجمة الألعاب للعربي (شوف Translator.h)
//
//  الأقسام:
//   1) معلومات الخدمات
//   2) الحفظ: الملفات الشخصية والمفاتيح (مشفّرة) والترجمات المحفوظة
//   3) الاتصال بخدمات الترجمة
//   4) الجلسة: خيط القراءة (OCR) وخيط الترجمة والجمل مع أوقاتها
//   5) رسم الطبقة (المربع والنص العربي)
// =============================================================
#include "Translator.h"
#include "HttpClient.h"
#include "Json.h"
#include "GameOcr.h"

#include <shlobj.h>
#include <wincrypt.h>
#include <wincodec.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

namespace
{
    ULONGLONG Now() { return GetTickCount64(); }
    std::atomic<bool>     g_shutdown{ false };          // البرنامج عم يسكّر: الخيوط بتطلع
    std::atomic<bool>     g_placeholder{ false };
    std::atomic<uint64_t> g_frameVersion{ 0x7000000000000000ull };   // أرقام صور الترجمة (بعيدة عن أرقام Media)
}

// =============================================================
//  1) معلومات الخدمات
// =============================================================
const wchar_t* EngineName(TransEngine e)
{
    switch (e)
    {
    case TransEngine::Gemini: return L"Gemini (Google)";
    case TransEngine::Claude: return L"Claude (Anthropic)";
    case TransEngine::OpenAI: return L"ChatGPT (OpenAI)";
    case TransEngine::DeepL:  return L"DeepL";
    case TransEngine::Azure:  return L"Microsoft Translator";
    case TransEngine::Google: return L"Google Translate";
    case TransEngine::Ollama: return L"Ollama (على جهازك)";
    default:                  return L"";
    }
}

const wchar_t* EngineNote(TransEngine e)
{
    switch (e)
    {
    case TransEngine::Gemini: return L"ذكاء اصطناعي • سريع • له استخدام مجاني";
    case TransEngine::Claude: return L"ذكاء اصطناعي • الأدق في العامية والسياق";
    case TransEngine::OpenAI: return L"ذكاء اصطناعي • دقيق";
    case TransEngine::DeepL:  return L"ترجمة آلية • 500 ألف حرف مجانًا شهريًا";
    case TransEngine::Azure:  return L"ترجمة آلية • مليونا حرف مجانًا شهريًا";
    case TransEngine::Google: return L"ترجمة آلية • سريعة";
    case TransEngine::Ollama: return L"ذكاء اصطناعي على كرت الشاشة • بلا إنترنت ومجاني";
    default:                  return L"";
    }
}

bool EngineIsAi(TransEngine e)
{
    return e == TransEngine::Gemini || e == TransEngine::Claude || e == TransEngine::OpenAI || e == TransEngine::Ollama;
}
bool EngineNeedsKey(TransEngine e)  { return e != TransEngine::Ollama; }
bool EngineHasModel(TransEngine e)  { return EngineIsAi(e); }
bool EngineHasExtra(TransEngine e)  { return e == TransEngine::Azure || e == TransEngine::Ollama; }

const wchar_t* EngineExtraLabel(TransEngine e)
{
    return e == TransEngine::Azure ? L"المنطقة (Region)" : e == TransEngine::Ollama ? L"العنوان" : L"";
}

const wchar_t* EngineKeyHelp(TransEngine e)
{
    switch (e)
    {
    case TransEngine::Gemini: return L"من Google AI Studio ← Get API key";
    case TransEngine::Claude: return L"من console.anthropic.com ← API Keys";
    case TransEngine::OpenAI: return L"من platform.openai.com ← API keys";
    case TransEngine::DeepL:  return L"من حساب DeepL API ← مفتاح المصادقة";
    case TransEngine::Azure:  return L"من بوابة Azure ← مورد Translator ← المفاتيح";
    case TransEngine::Google: return L"من Google Cloud ← Cloud Translation API ← المفاتيح";
    case TransEngine::Ollama: return L"ثبّت Ollama ثم نموذجًا يدعم العربية (مثل aya-expanse)";
    default:                  return L"";
    }
}

std::wstring EngineDefaultModel(TransEngine e)
{
    switch (e)
    {
    case TransEngine::Gemini: return L"gemini-2.5-flash";
    case TransEngine::Claude: return L"claude-haiku-5-5";
    case TransEngine::OpenAI: return L"gpt-4.1-mini";
    case TransEngine::Ollama: return L"aya-expanse:8b";
    default:                  return L"";
    }
}

const wchar_t* DialectName(TransDialect d)
{
    switch (d)
    {
    case TransDialect::Fusha:     return L"فصحى";
    case TransDialect::Levantine: return L"شامية";
    case TransDialect::Gulf:      return L"خليجية";
    case TransDialect::Egyptian:  return L"مصرية";
    case TransDialect::Iraqi:     return L"عراقية";
    default:                      return L"";
    }
}

const wchar_t* TransColorName(int i)
{
    static const wchar_t* names[TRANS_COLOR_COUNT] = { L"أبيض", L"أصفر", L"سماوي" };
    return names[std::clamp(i, 0, TRANS_COLOR_COUNT - 1)];
}

unsigned TransColor(int i)
{
    static const unsigned colors[TRANS_COLOR_COUNT] = { 0xFFFFFF, 0xFFE15A, 0x8FE3FF };
    return colors[std::clamp(i, 0, TRANS_COLOR_COUNT - 1)];
}

int DelayMemoryMb(int width, int height, double fps, int delayMs)
{
    const double bytes = (double)width * height * 4 * std::max(1.0, fps) * delayMs / 1000.0;
    return (int)std::lround(bytes / (1024.0 * 1024.0));
}

// =============================================================
//  2) الحفظ
// =============================================================
namespace
{
    std::mutex                    g_mx;            // بيحمي الملفات الشخصية والمفاتيح
    bool                          g_loaded = false;
    std::vector<TranslateProfile> g_profiles;
    EngineConfig                  g_engines[TRANS_ENGINE_COUNT];

    std::wstring Folder()
    {
        PWSTR appData = nullptr;
        std::wstring path;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appData)))
        {
            path = std::wstring(appData) + L"\\ScreenRecorder";
            CoTaskMemFree(appData);
            CreateDirectoryW(path.c_str(), nullptr);
        }
        return path;
    }

    std::wstring CleanField(std::wstring s)
    {
        for (wchar_t& c : s)
            if (c == L'\t' || c == L'\r' || c == L'\n') c = L' ';
        return s;
    }

    std::vector<std::wstring> Split(const std::wstring& line)
    {
        std::vector<std::wstring> parts;
        size_t start = 0;
        for (;;)
        {
            const size_t tab = line.find(L'\t', start);
            parts.push_back(line.substr(start, tab == std::wstring::npos ? std::wstring::npos : tab - start));
            if (tab == std::wstring::npos) break;
            start = tab + 1;
        }
        return parts;
    }

    // المفاتيح مشفّرة بحساب ويندوز (DPAPI): ما حدا غيرك بيقدر يفكّها
    std::wstring Protect(const std::wstring& plain)
    {
        if (plain.empty())
            return L"";
        DATA_BLOB in{ (DWORD)(plain.size() * sizeof(wchar_t)), (BYTE*)plain.data() }, out{};
        if (!CryptProtectData(&in, L"translator key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return L"";
        DWORD len = 0;
        CryptBinaryToStringW(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len);
        std::wstring b64(len, L'\0');
        CryptBinaryToStringW(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &len);
        b64.resize(len);
        LocalFree(out.pbData);
        return b64;
    }

    std::wstring Unprotect(const std::wstring& b64)
    {
        if (b64.empty())
            return L"";
        DWORD len = 0;
        if (!CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr))
            return L"";
        std::vector<BYTE> bin(len);
        if (!CryptStringToBinaryW(b64.c_str(), 0, CRYPT_STRING_BASE64, bin.data(), &len, nullptr, nullptr))
            return L"";
        DATA_BLOB in{ len, bin.data() }, out{};
        if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
            return L"";
        std::wstring plain((wchar_t*)out.pbData, out.cbData / sizeof(wchar_t));
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
        return plain;
    }

    std::wstring Num(double v)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%.4f", v);
        return buf;
    }

    // لازم القفل ممسوك
    void SaveLocked()
    {
        const std::wstring folder = Folder();
        if (folder.empty())
            return;
        const std::wstring path = folder + L"\\translation.txt", tmp = path + L".tmp";
        FILE* f = nullptr;
        if (_wfopen_s(&f, tmp.c_str(), L"w, ccs=UTF-8") != 0 || !f)
            return;
        for (int e = 0; e < TRANS_ENGINE_COUNT; ++e)
            fwprintf(f, L"E\t%d\t%ls\t%ls\t%ls\n", e, Protect(g_engines[e].key).c_str(),
                     CleanField(g_engines[e].model).c_str(), CleanField(g_engines[e].extra).c_str());
        for (const TranslateProfile& p : g_profiles)
        {
            fwprintf(f, L"P\t%ls\t%ls\t%d\t%ls\t%ls\t%ld\t%ld\t%ld\t%ld\t%ls\t%ls\t%ls\t%ls\t%d\t%d\t%ls\t%d\t%d\t%d\t%d\t%ls\t%ls\t%d\n",
                     CleanField(p.id).c_str(), CleanField(p.name).c_str(), p.fromScreen ? 1 : 0,
                     CleanField(p.deviceLink).c_str(), CleanField(p.deviceName).c_str(),
                     p.monitor.left, p.monitor.top, p.monitor.right, p.monitor.bottom,
                     Num(p.rx).c_str(), Num(p.ry).c_str(), Num(p.rw).c_str(), Num(p.rh).c_str(),
                     (int)p.engine, (int)p.dialect, CleanField(p.game).c_str(), p.soften ? 1 : 0,
                     p.delayMs, p.delayAll ? 1 : 0, p.threshold,
                     Num(p.fontScale).c_str(), Num(p.boxOpacity).c_str(), p.colorIndex);
            for (const GlossaryItem& g : p.glossary)
                fwprintf(f, L"G\t%ls\t%ls\t%ls\n", CleanField(p.id).c_str(), CleanField(g.en).c_str(), CleanField(g.ar).c_str());
        }
        fclose(f);
        MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);   // ما بيخرب الملف إذا انقطعت الكهرباء
    }

    // لازم القفل ممسوك
    void EnsureLoaded()
    {
        if (g_loaded)
            return;
        g_loaded = true;
        const std::wstring folder = Folder();
        FILE* f = nullptr;
        if (folder.empty() || _wfopen_s(&f, (folder + L"\\translation.txt").c_str(), L"r, ccs=UTF-8") != 0 || !f)
            return;
        wchar_t buf[8192];
        while (fgetws(buf, (int)std::size(buf), f))
        {
            std::wstring line = buf;
            while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r'))
                line.pop_back();
            const std::vector<std::wstring> p = Split(line);
            if (p.empty())
                continue;
            if (p[0] == L"E" && p.size() >= 5)
            {
                const int e = _wtoi(p[1].c_str());
                if (e < 0 || e >= TRANS_ENGINE_COUNT)
                    continue;
                g_engines[e].key   = Unprotect(p[2]);
                g_engines[e].model = p[3];
                g_engines[e].extra = p[4];
            }
            else if (p[0] == L"P" && p.size() >= 24)
            {
                TranslateProfile t;
                t.id         = p[1];
                t.name       = p[2];
                t.fromScreen = p[3] == L"1";
                t.deviceLink = p[4];
                t.deviceName = p[5];
                t.monitor    = { _wtol(p[6].c_str()), _wtol(p[7].c_str()), _wtol(p[8].c_str()), _wtol(p[9].c_str()) };
                t.rx = std::clamp(_wtof(p[10].c_str()), 0.0, 0.98);
                t.ry = std::clamp(_wtof(p[11].c_str()), 0.0, 0.98);
                t.rw = std::clamp(_wtof(p[12].c_str()), 0.02, 1.0 - t.rx);
                t.rh = std::clamp(_wtof(p[13].c_str()), 0.02, 1.0 - t.ry);
                t.engine     = (TransEngine)std::clamp(_wtoi(p[14].c_str()), 0, TRANS_ENGINE_COUNT - 1);
                t.dialect    = (TransDialect)std::clamp(_wtoi(p[15].c_str()), 0, TRANS_DIALECT_COUNT - 1);
                t.game       = p[16];
                t.soften     = p[17] == L"1";
                t.delayMs    = std::clamp(_wtoi(p[18].c_str()), 0, 5000);
                t.delayAll   = p[19] == L"1";
                t.threshold  = std::clamp(_wtoi(p[20].c_str()), 0, 255);
                t.fontScale  = std::clamp((float)_wtof(p[21].c_str()), 0.5f, 2.0f);
                t.boxOpacity = std::clamp((float)_wtof(p[22].c_str()), 0.0f, 1.0f);
                t.colorIndex = std::clamp(_wtoi(p[23].c_str()), 0, TRANS_COLOR_COUNT - 1);
                if (!t.id.empty())
                    g_profiles.push_back(t);
            }
            else if (p[0] == L"G" && p.size() >= 4)
            {
                for (TranslateProfile& t : g_profiles)
                    if (t.id == p[1] && !p[2].empty())
                        t.glossary.push_back({ p[2], p[3] });
            }
        }
        fclose(f);
    }

    std::wstring CachePath(const std::wstring& profileId)
    {
        std::wstring safe;
        for (wchar_t c : profileId)
            if (std::iswalnum(c)) safe += c;
        const std::wstring folder = Folder();
        return folder.empty() || safe.empty() ? L"" : folder + L"\\tcache_" + safe + L".txt";
    }
}

std::vector<TranslateProfile> TranslateProfiles()
{
    std::lock_guard<std::mutex> lock(g_mx);
    EnsureLoaded();
    return g_profiles;
}

bool GetTranslateProfile(const std::wstring& id, TranslateProfile& out)
{
    std::lock_guard<std::mutex> lock(g_mx);
    EnsureLoaded();
    for (const TranslateProfile& p : g_profiles)
        if (p.id == id)
        {
            out = p;
            return true;
        }
    return false;
}

void SaveTranslateProfile(const TranslateProfile& p)
{
    SetTranslateProfile(p, true);
}

void SetTranslateProfile(const TranslateProfile& p, bool persist)
{
    std::lock_guard<std::mutex> lock(g_mx);
    EnsureLoaded();
    bool found = false;
    for (TranslateProfile& t : g_profiles)
        if (t.id == p.id)
        {
            t = p;
            found = true;
        }
    if (!found)
        g_profiles.push_back(p);
    if (persist)
        SaveLocked();
}

void DeleteTranslateProfile(const std::wstring& id)
{
    {
        std::lock_guard<std::mutex> lock(g_mx);
        EnsureLoaded();
        g_profiles.erase(std::remove_if(g_profiles.begin(), g_profiles.end(),
                                        [&](const TranslateProfile& p) { return p.id == id; }), g_profiles.end());
        SaveLocked();
    }
    ClearTranslateCache(id);
}

std::wstring NewTranslateProfileId()
{
    wchar_t buf[40];
    swprintf_s(buf, L"t%llx%04x", Now(), (unsigned)(GetCurrentThreadId() & 0xFFFF) ^ (unsigned)(Now() & 0xFFFF));
    return buf;
}

EngineConfig GetEngineConfig(TransEngine e)
{
    std::lock_guard<std::mutex> lock(g_mx);
    EnsureLoaded();
    const int i = std::clamp((int)e, 0, TRANS_ENGINE_COUNT - 1);
    return g_engines[i];
}

void SetEngineConfig(TransEngine e, const EngineConfig& c)
{
    std::lock_guard<std::mutex> lock(g_mx);
    EnsureLoaded();
    g_engines[std::clamp((int)e, 0, TRANS_ENGINE_COUNT - 1)] = c;
    SaveLocked();
}

// =============================================================
//  أدوات النص
// =============================================================
namespace
{
    std::wstring Trim(std::wstring s)
    {
        while (!s.empty() && std::iswspace(s.back())) s.pop_back();
        size_t i = 0;
        while (i < s.size() && std::iswspace(s[i])) ++i;
        return s.substr(i);
    }

    // مفتاح للمقارنة: حروف وأرقام صغيرة بس
    std::wstring Key(const std::wstring& s)
    {
        std::wstring k;
        for (wchar_t c : s)
            if (std::iswalnum(c)) k += (wchar_t)std::towlower(c);
        return k;
    }

    // مفتاح للحفظ: نفس الجملة حتى لو اختلفت المسافات
    std::wstring CacheKey(const TranslateProfile& p, const std::wstring& en)
    {
        std::wstring k;
        bool space = false;
        for (wchar_t c : en)
        {
            if (std::iswalnum(c)) { if (space && !k.empty()) k += L' '; k += (wchar_t)std::towlower(c); space = false; }
            else space = true;
        }
        return std::to_wstring((int)p.engine) + L"|" + std::to_wstring((int)p.dialect) + L"|" + k;
    }

    // قديش الجملتين متشابهين (القارئ أحياناً بيغلط بحرف)
    double Similarity(const std::wstring& a, const std::wstring& b)
    {
        std::wstring x = Key(a), y = Key(b);
        if (x.empty() || y.empty())
            return 0;
        if (x == y)
            return 1;
        x.resize(std::min<size_t>(x.size(), 160));
        y.resize(std::min<size_t>(y.size(), 160));
        std::vector<int> prev(y.size() + 1), cur(y.size() + 1);
        for (size_t j = 0; j <= y.size(); ++j) prev[j] = (int)j;
        for (size_t i = 1; i <= x.size(); ++i)
        {
            cur[0] = (int)i;
            for (size_t j = 1; j <= y.size(); ++j)
                cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (x[i - 1] == y[j - 1] ? 0 : 1) });
            std::swap(prev, cur);
        }
        return 1.0 - (double)prev[y.size()] / (double)std::max(x.size(), y.size());
    }

    int Letters(const std::wstring& s)
    {
        int n = 0;
        for (wchar_t c : s)
            if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) ++n;
        return n;
    }

    // تنظيف جواب الخدمة: بلا علامات تنصيص ولا تفكير ولا أسطر
    std::wstring CleanAnswer(std::wstring s)
    {
        for (;;)   // نماذج "بتفكّر" (<think>...</think>)
        {
            const size_t a = s.find(L"<think>");
            if (a == std::wstring::npos) break;
            const size_t b = s.find(L"</think>", a);
            s.erase(a, b == std::wstring::npos ? std::wstring::npos : b + 8 - a);
        }
        for (wchar_t& c : s)
            if (c == L'\n' || c == L'\r' || c == L'\t') c = L' ';
        s = Trim(s);
        const wchar_t* quotes[][2] = { { L"\"", L"\"" }, { L"«", L"»" }, { L"“", L"”" }, { L"'", L"'" } };
        for (auto& q : quotes)
            if (s.size() >= 2 && s.compare(0, 1, q[0]) == 0 && s.compare(s.size() - 1, 1, q[1]) == 0)
                s = Trim(s.substr(1, s.size() - 2));
        std::wstring out;
        for (wchar_t c : s)
            if (!(c == L' ' && !out.empty() && out.back() == L' ')) out += c;
        return out;
    }

    const wchar_t* DialectPrompt(TransDialect d)
    {
        switch (d)
        {
        case TransDialect::Levantine: return L"Syrian / Levantine colloquial Arabic (شامي)";
        case TransDialect::Gulf:      return L"Gulf colloquial Arabic (خليجي)";
        case TransDialect::Egyptian:  return L"Egyptian colloquial Arabic (مصري)";
        case TransDialect::Iraqi:     return L"Iraqi colloquial Arabic (عراقي)";
        default:                      return L"Modern Standard Arabic (فصحى), simple and natural";
        }
    }

    std::wstring SystemPrompt(const TranslateProfile& p, const std::vector<std::pair<std::wstring, std::wstring>>& context)
    {
        std::wstring s =
            L"You translate English video game subtitles into Arabic for a live stream.\n"
            L"Rules:\n"
            L"- Reply with ONLY the Arabic translation. No quotes, notes, or explanations.\n"
            L"- Use " + std::wstring(DialectPrompt(p.dialect)) + L".\n"
            L"- Keep it short and natural like real subtitles, and keep the tone (jokes, slang, anger).\n"
            L"- The text was read by OCR from the screen and may contain small mistakes; fix them from context.\n"
            L"- If the text is not a sentence (a button, a number, random letters), reply with it unchanged.\n";
        s += p.soften ? L"- Tone down strong profanity to mild Arabic expressions suitable for YouTube.\n"
                      : L"- Translate profanity faithfully but naturally.\n";
        if (!p.game.empty())
            s += L"- The game is: " + p.game + L".\n";
        if (!p.glossary.empty())
        {
            s += L"- Names and terms (always use exactly):\n";
            for (const GlossaryItem& g : p.glossary)
                s += L"  " + g.en + L" -> " + (g.ar.empty() ? g.en + L" (keep in English letters)" : g.ar) + L"\n";
        }
        if (!context.empty())
        {
            s += L"Previous lines (for context only, do not translate again):\n";
            for (const auto& c : context)
                s += L"  " + c.first + L"  =>  " + c.second + L"\n";
        }
        return s;
    }

    // للترجمة الآلية: منبدّل الأسماء قبل ما نبعت (بتضل متل ما بدك)
    std::wstring ApplyGlossary(const TranslateProfile& p, std::wstring text)
    {
        for (const GlossaryItem& g : p.glossary)
        {
            if (g.en.empty() || g.ar.empty())
                continue;
            std::wstring low = text, key = g.en;
            for (wchar_t& c : low) c = (wchar_t)std::towlower(c);
            for (wchar_t& c : key) c = (wchar_t)std::towlower(c);
            size_t pos = 0;
            while ((pos = low.find(key, pos)) != std::wstring::npos)
            {
                const bool startOk = pos == 0 || !std::iswalnum(low[pos - 1]);
                const bool endOk   = pos + key.size() >= low.size() || !std::iswalnum(low[pos + key.size()]);
                if (startOk && endOk)
                {
                    text.replace(pos, key.size(), g.ar);
                    low.replace(pos, key.size(), g.ar);
                    pos += g.ar.size();
                }
                else
                    pos += key.size();
            }
        }
        return text;
    }

    std::wstring ServiceError(const HttpResponse& r)
    {
        if (!r.ok)
            return r.error;
        JsonValue v;
        std::wstring msg;
        if (JsonParse(r.body, v))
        {
            msg = v["error"]["message"].Str();
            if (msg.empty()) msg = v["message"].Str();
            if (msg.empty()) msg = v["error"].Str();
        }
        std::wstring head;
        switch (r.status)
        {
        case 400: head = L"طلب غير مقبول"; break;
        case 401: case 403: head = L"المفتاح غير صحيح أو بلا صلاحية"; break;
        case 404: head = L"اسم النموذج غير صحيح"; break;
        case 429: head = L"تجاوزت حدّ الطلبات، انتظر قليلًا"; break;
        case 456: head = L"انتهى الرصيد الشهري"; break;
        default:  head = L"خطأ من الخدمة (" + std::to_wstring(r.status) + L")"; break;
        }
        if (msg.size() > 140) msg = msg.substr(0, 140) + L"…";
        return msg.empty() ? head : head + L": " + msg;
    }

    // =========================================================
    //  3) الاتصال بخدمة الترجمة
    // =========================================================
    bool CallEngine(const TranslateProfile& p, EngineConfig cfg, const std::wstring& english,
                    const std::vector<std::pair<std::wstring, std::wstring>>& context,
                    std::wstring& arabic, std::wstring& error, bool* rateLimited)
    {
        if (rateLimited) *rateLimited = false;
        const TransEngine e = p.engine;
        if (cfg.model.empty()) cfg.model = EngineDefaultModel(e);
        if (EngineNeedsKey(e) && cfg.key.empty())
        {
            error = L"أدخل مفتاح الخدمة أولًا";
            return false;
        }
        const std::wstring sys = SystemPrompt(p, context);
        const std::string  jsys = JsonQuote(sys), juser = JsonQuote(english);
        std::wstring url;
        std::vector<std::wstring> headers = { L"Content-Type: application/json; charset=utf-8" };
        std::string body;

        switch (e)
        {
        case TransEngine::Gemini:
        {
            url = L"https://generativelanguage.googleapis.com/v1beta/models/" + UrlEncode(cfg.model) + L":generateContent";
            headers.push_back(L"x-goog-api-key: " + cfg.key);
            const bool flash = cfg.model.find(L"flash") != std::wstring::npos;
            body = "{\"systemInstruction\":{\"parts\":[{\"text\":" + jsys + "}]},"
                   "\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":" + juser + "}]}],"
                   "\"generationConfig\":{\"temperature\":0.3,\"maxOutputTokens\":400" +
                   std::string(flash ? ",\"thinkingConfig\":{\"thinkingBudget\":0}" : "") + "}}";
            break;
        }
        case TransEngine::Claude:
            url = L"https://api.anthropic.com/v1/messages";
            headers.push_back(L"x-api-key: " + cfg.key);
            headers.push_back(L"anthropic-version: 2023-06-01");
            body = "{\"model\":" + JsonQuote(cfg.model) + ",\"max_tokens\":400,\"temperature\":0.3,\"system\":" + jsys +
                   ",\"messages\":[{\"role\":\"user\",\"content\":" + juser + "}]}";
            break;
        case TransEngine::OpenAI:
            url = L"https://api.openai.com/v1/chat/completions";
            headers.push_back(L"Authorization: Bearer " + cfg.key);
            body = "{\"model\":" + JsonQuote(cfg.model) + ",\"messages\":[{\"role\":\"system\",\"content\":" + jsys +
                   "},{\"role\":\"user\",\"content\":" + juser + "}]}";
            break;
        case TransEngine::Ollama:
        {
            std::wstring base = cfg.extra.empty() ? L"http://localhost:11434" : cfg.extra;
            while (!base.empty() && base.back() == L'/') base.pop_back();
            url = base + L"/api/chat";
            body = "{\"model\":" + JsonQuote(cfg.model) + ",\"stream\":false,\"options\":{\"temperature\":0.3},"
                   "\"messages\":[{\"role\":\"system\",\"content\":" + jsys + "},{\"role\":\"user\",\"content\":" + juser + "}]}";
            break;
        }
        case TransEngine::DeepL:
        {
            const bool free = cfg.key.size() > 3 && cfg.key.compare(cfg.key.size() - 3, 3, L":fx") == 0;
            url = free ? L"https://api-free.deepl.com/v2/translate" : L"https://api.deepl.com/v2/translate";
            headers.push_back(L"Authorization: DeepL-Auth-Key " + cfg.key);
            body = "{\"text\":[" + JsonQuote(ApplyGlossary(p, english)) + "],\"source_lang\":\"EN\",\"target_lang\":\"AR\"}";
            break;
        }
        case TransEngine::Azure:
            url = L"https://api.cognitive.microsofttranslator.com/translate?api-version=3.0&from=en&to=ar";
            headers.push_back(L"Ocp-Apim-Subscription-Key: " + cfg.key);
            if (!cfg.extra.empty())
                headers.push_back(L"Ocp-Apim-Subscription-Region: " + cfg.extra);
            body = "[{\"Text\":" + JsonQuote(ApplyGlossary(p, english)) + "}]";
            break;
        case TransEngine::Google:
            url = L"https://translation.googleapis.com/language/translate/v2?key=" + UrlEncode(cfg.key);
            body = "{\"q\":" + JsonQuote(ApplyGlossary(p, english)) + ",\"source\":\"en\",\"target\":\"ar\",\"format\":\"text\"}";
            break;
        default:
            error = L"خدمة غير معروفة";
            return false;
        }

        const HttpResponse r = HttpRequest(L"POST", url, headers, body, e == TransEngine::Ollama ? 60000 : 15000);
        if (!r.ok || r.status < 200 || r.status >= 300)
        {
            if (rateLimited) *rateLimited = r.status == 429;
            error = (e == TransEngine::Ollama && !r.ok) ? L"Ollama لا يعمل على هذا العنوان (شغّله أولًا)" : ServiceError(r);
            return false;
        }
        JsonValue v;
        if (!JsonParse(r.body, v))
        {
            error = L"جواب غير مفهوم من الخدمة";
            return false;
        }
        std::wstring text;
        switch (e)
        {
        case TransEngine::Gemini:
            for (size_t i = 0; i < v["candidates"][0]["content"]["parts"].Size(); ++i)
                text += v["candidates"][0]["content"]["parts"][i]["text"].Str();
            break;
        case TransEngine::Claude:
            for (size_t i = 0; i < v["content"].Size(); ++i)
                if (v["content"][i]["type"].Str() == L"text")
                    text += v["content"][i]["text"].Str();
            break;
        case TransEngine::OpenAI: text = v["choices"][0]["message"]["content"].Str(); break;
        case TransEngine::Ollama: text = v["message"]["content"].Str(); break;
        case TransEngine::DeepL:  text = v["translations"][0]["text"].Str(); break;
        case TransEngine::Azure:  text = v[0]["translations"][0]["text"].Str(); break;
        case TransEngine::Google: text = v["data"]["translations"][0]["translatedText"].Str(); break;
        default: break;
        }
        arabic = CleanAnswer(text);
        if (arabic.empty())
        {
            error = L"الخدمة أرجعت ترجمة فارغة";
            return false;
        }
        return true;
    }
}

bool TranslateOnce(const TranslateProfile& p, const std::wstring& english, std::wstring& arabic, std::wstring& error, int* ms)
{
    const ULONGLONG t0 = Now();
    const bool ok = CallEngine(p, GetEngineConfig(p.engine), english, {}, arabic, error, nullptr);
    if (ms) *ms = (int)(Now() - t0);
    return ok;
}

std::vector<std::wstring> OllamaModels(const std::wstring& address, std::wstring* error)
{
    std::vector<std::wstring> list;
    std::wstring base = address.empty() ? L"http://localhost:11434" : address;
    while (!base.empty() && base.back() == L'/') base.pop_back();
    const HttpResponse r = HttpRequest(L"GET", base + L"/api/tags", {}, "", 4000);
    if (!r.ok || r.status != 200)
    {
        if (error) *error = L"Ollama لا يعمل على هذا العنوان";
        return list;
    }
    JsonValue v;
    if (JsonParse(r.body, v))
        for (size_t i = 0; i < v["models"].Size(); ++i)
        {
            const std::wstring name = v["models"][i]["name"].Str();
            if (!name.empty()) list.push_back(name);
        }
    if (list.empty() && error)
        *error = L"لا توجد نماذج مثبّتة (ollama pull aya-expanse:8b)";
    return list;
}

// =============================================================
//  4) الجلسة الحيّة
// =============================================================
namespace
{
    struct Event
    {
        uint64_t     id = 0;
        ULONGLONG    start = 0, end = 0;        // end = 0: لسا ظاهرة
        std::wstring en, ar;
        int          seen = 1;                  // كم مرة قريناها (مرتين = أكيد مش غلط بالقراءة)
        bool         requested = false, done = false, failed = false, inflight = false;
    };

    // صورة جزء من الشاشة (لألعاب الكمبيوتر)
    bool GrabScreen(const RECT& r, std::vector<uint8_t>& out, int& w, int& h)
    {
        w = r.right - r.left;
        h = r.bottom - r.top;
        if (w < 8 || h < 8)
            return false;
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        bool ok = false;
        if (dib)
        {
            HGDIOBJ old = SelectObject(mem, dib);
            ok = BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != FALSE;
            GdiFlush();
            if (ok)
            {
                out.assign((uint8_t*)bits, (uint8_t*)bits + (size_t)w * h * 4);
                for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
            }
            SelectObject(mem, old);
            DeleteObject(dib);
        }
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return ok;
    }

    class Session : public std::enable_shared_from_this<Session>
    {
    public:
        explicit Session(const std::wstring& id) : m_id(id) {}

        void Start()
        {
            m_running = true;
            m_lastUsed = Now();
            std::shared_ptr<Session> self = shared_from_this();
            // الخيوط بتمسك الجلسة لحد ما تخلص (فالإيقاف ما بيستنى الإنترنت)
            std::thread([self] { self->OcrLoop(); }).detach();
            std::thread([self] { self->TranslateLoop(); }).detach();
        }

        void Stop()
        {
            m_running = false;
            m_cv.notify_all();
        }

        std::atomic<ULONGLONG> m_lastUsed{ 0 };

        TranslateStatus Status()
        {
            std::lock_guard<std::mutex> lock(m_mx);
            TranslateStatus s = m_status;
            s.running = m_running;
            return s;
        }

        bool EventAt(ULONGLONG t, Event& out)
        {
            std::lock_guard<std::mutex> lock(m_mx);
            for (auto it = m_events.rbegin(); it != m_events.rend(); ++it)
                if (it->start <= t && (it->end == 0 || t < it->end))
                {
                    out = *it;
                    return true;
                }
            return false;
        }

        void ClearCache()
        {
            std::lock_guard<std::mutex> lock(m_mx);
            m_cache.clear();
            m_status.fromCache = 0;
        }

        bool SourceLost()
        {
            std::lock_guard<std::mutex> lock(m_mx);
            return Now() - m_created > 3000 && !m_status.sourceOk;
        }

    private:
        // ---- الحفظ: الجمل المترجمة من قبل ----
        void LoadCache()
        {
            const std::wstring path = CachePath(m_id);
            FILE* f = nullptr;
            if (path.empty() || _wfopen_s(&f, path.c_str(), L"r, ccs=UTF-8") != 0 || !f)
                return;
            wchar_t buf[4096];
            while (fgetws(buf, (int)std::size(buf), f))
            {
                std::wstring line = buf;
                while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r'))
                    line.pop_back();
                const size_t tab = line.find(L'\t');
                if (tab != std::wstring::npos && tab > 0 && tab + 1 < line.size())
                    m_cache[line.substr(0, tab)] = line.substr(tab + 1);
                if (m_cache.size() > 50000)
                    break;
            }
            fclose(f);
        }

        void AppendCache(const std::wstring& key, const std::wstring& ar)
        {
            const std::wstring path = CachePath(m_id);
            FILE* f = nullptr;
            if (path.empty() || _wfopen_s(&f, path.c_str(), L"a, ccs=UTF-8") != 0 || !f)
                return;
            fwprintf(f, L"%ls\t%ls\n", CleanField(key).c_str(), CleanField(ar).c_str());
            fclose(f);
        }

        // ---- خيط القراءة: كذا مرة بالثانية منقرا مكان الترجمة ----
        void OcrLoop()
        {
            const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            {
                OcrReader ocr;
                {
                    std::lock_guard<std::mutex> lock(m_mx);
                    m_status.ocrReady    = ocr.Ready();
                    m_status.ocrEnglish  = ocr.English();
                    m_status.ocrLanguage = ocr.LanguageName();
                }
                LoadCacheOnce();

                TranslateProfile prof;
                ULONGLONG profTick = 0, lastSample = 0, prevTick = 0;
                uint64_t lastVersion = 0;
                std::vector<uint8_t> crop, work, screen;
                std::vector<std::wstring> lines;

                while (m_running && !g_shutdown)
                {
                    const ULONGLONG now = Now();
                    if (now - profTick > 500 || prof.id.empty())
                    {
                        if (!GetTranslateProfile(m_id, prof))
                        {
                            Sleep(200);
                            continue;
                        }
                        profTick = now;
                    }
                    if (now - lastSample < 110)                 // حوالي 8 مرات بالثانية
                    {
                        Sleep(10);
                        continue;
                    }

                    // ---- الصورة ----
                    int cw = 0, ch = 0;
                    ULONGLONG tick = now;
                    if (!prof.fromScreen)
                    {
                        std::shared_ptr<const MediaFrame> f = PeekDeviceFrame(prof.deviceLink);
                        if (!f || f->width < 16 || f->height < 16)
                        {
                            SetSource(false);
                            Sleep(150);
                            continue;
                        }
                        if (f->version == lastVersion)
                        {
                            Sleep(5);
                            continue;
                        }
                        lastVersion = f->version;
                        tick = f->tick ? f->tick : now;
                        const int x0 = std::clamp((int)(prof.rx * f->width), 0, f->width - 8);
                        const int y0 = std::clamp((int)(prof.ry * f->height), 0, f->height - 8);
                        cw = std::clamp((int)(prof.rw * f->width), 8, f->width - x0);
                        ch = std::clamp((int)(prof.rh * f->height), 8, f->height - y0);
                        crop.resize((size_t)cw * ch * 4);
                        for (int y = 0; y < ch; ++y)
                            memcpy(crop.data() + (size_t)y * cw * 4, f->pixels.data() + ((size_t)(y0 + y) * f->width + x0) * 4, (size_t)cw * 4);
                    }
                    else
                    {
                        const RECT& m = prof.monitor;
                        const int mw = m.right - m.left, mh = m.bottom - m.top;
                        if (mw < 16 || mh < 16)
                        {
                            SetSource(false);
                            Sleep(300);
                            continue;
                        }
                        RECT r{ m.left + (LONG)(prof.rx * mw), m.top + (LONG)(prof.ry * mh), 0, 0 };
                        r.right  = r.left + std::max<LONG>(8, (LONG)(prof.rw * mw));
                        r.bottom = r.top + std::max<LONG>(8, (LONG)(prof.rh * mh));
                        if (!GrabScreen(r, crop, cw, ch))
                        {
                            SetSource(false);
                            Sleep(300);
                            continue;
                        }
                    }
                    lastSample = now;
                    SetSource(true);

                    // ---- تجهيز الصورة للقارئ ----
                    int ow = cw, oh = ch;
                    const uint8_t* src = crop.data();
                    // صغيرة؟ منكبّرها ضعفين (القارئ بيحب الحروف الكبيرة) ، كبيرة كتير؟ منصغّرها
                    int scale = ch < 90 ? 2 : 1;
                    if (cw * scale > 2400) scale = 1;
                    const bool shrink = cw > 2400;
                    if (scale == 2 || shrink || prof.threshold > 0)
                    {
                        ow = shrink ? cw / 2 : cw * scale;
                        oh = shrink ? ch / 2 : ch * scale;
                        work.resize((size_t)ow * oh * 4);
                        for (int y = 0; y < oh; ++y)
                            for (int x = 0; x < ow; ++x)
                            {
                                const int sx = shrink ? x * 2 : x / scale, sy = shrink ? y * 2 : y / scale;
                                const uint8_t* s = crop.data() + ((size_t)sy * cw + sx) * 4;
                                uint8_t* d = work.data() + ((size_t)y * ow + x) * 4;
                                if (prof.threshold > 0)
                                {
                                    // النص الفاتح = أسود ، الباقي = أبيض (أوضح للقارئ)
                                    const int lum = (s[2] * 2 + s[1] * 5 + s[0]) / 8;
                                    const uint8_t v = lum >= prof.threshold ? 0 : 255;
                                    d[0] = d[1] = d[2] = v;
                                }
                                else
                                {
                                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                                }
                                d[3] = 255;
                            }
                        src = work.data();
                    }

                    std::wstring text;
                    if (ocr.Ready() && ocr.Read(src, ow, oh, ow * 4, lines))
                    {
                        for (std::wstring& l : lines)
                        {
                            l = Trim(l);
                            while (!l.empty() && (l[0] == L'-' || l[0] == L'|' || l[0] == L'—')) l = Trim(l.substr(1));
                            if (!l.empty())
                                text += (text.empty() ? L"" : L" ") + l;
                        }
                    }
                    OnReading(Letters(text) >= 3 ? text : std::wstring(), tick, prevTick);
                    prevTick = tick;
                }
            }
            if (SUCCEEDED(hrCom))
                CoUninitialize();
        }

        void SetSource(bool ok)
        {
            std::lock_guard<std::mutex> lock(m_mx);
            m_status.sourceOk = ok;
        }

        void LoadCacheOnce()
        {
            std::lock_guard<std::mutex> lock(m_mx);
            if (!m_cacheLoaded)
            {
                m_cacheLoaded = true;
                LoadCache();
            }
        }

        // قراءة جديدة: نفس الجملة؟ جملة جديدة؟ ولا اختفت؟
        void OnReading(const std::wstring& text, ULONGLONG tick, ULONGLONG prevTick)
        {
            TranslateProfile prof;
            const bool haveProf = GetTranslateProfile(m_id, prof);
            std::lock_guard<std::mutex> lock(m_mx);
            Event* cur = (!m_events.empty() && m_events.back().end == 0) ? &m_events.back() : nullptr;
            if (text.empty())
            {
                if (cur) cur->end = tick;                        // الجملة اختفت
            }
            else if (cur && Similarity(cur->en, text) >= 0.72)   // نفس الجملة (يمكن بغلطة قراءة)
            {
                ++cur->seen;
                if (!cur->requested)
                {
                    if (text.size() > cur->en.size()) cur->en = text;   // القراءة الأكمل
                    if (cur->seen >= 2)
                        Request(*cur, prof, haveProf);
                }
            }
            else                                                  // جملة جديدة
            {
                Event e;
                e.id    = m_nextId++;
                // بلّشت بين القراءة اللي قبل وهي: منبلّش المربع من القراءة اللي قبل (أبكر شوي أحسن من إنو يبيّن الإنكليزي)
                e.start = prevTick && prevTick < tick ? prevTick + 1 : tick;
                e.en    = text;
                if (cur) cur->end = e.start;
                m_events.push_back(e);
            }
            if (!text.empty())
                m_status.lastEnglish = text;
            // منرمي الجمل القديمة (أكتر من دقيقتين)
            const ULONGLONG now = Now();
            while (m_events.size() > 1 && m_events.front().end != 0 && now - m_events.front().end > 120000)
                m_events.pop_front();
            while (m_events.size() > 400)
                m_events.pop_front();
        }

        // لازم القفل ممسوك
        void Request(Event& e, const TranslateProfile& prof, bool haveProf)
        {
            e.requested = true;
            if (haveProf)
            {
                auto it = m_cache.find(CacheKey(prof, e.en));
                if (it != m_cache.end())               // مترجمة من قبل: فوراً وبلا إنترنت
                {
                    e.ar   = it->second;
                    e.done = true;
                    ++m_status.fromCache;
                    m_status.lastArabic = e.ar;
                    return;
                }
            }
            m_cv.notify_all();
        }

        // ---- خيط الترجمة: الجمل وحدة ورا التانية ----
        void TranslateLoop()
        {
            while (m_running && !g_shutdown)
            {
                uint64_t id = 0;
                std::wstring en;
                std::vector<std::pair<std::wstring, std::wstring>> context;
                {
                    std::unique_lock<std::mutex> lock(m_mx);
                    m_cv.wait_for(lock, std::chrono::milliseconds(250));
                    if (!m_running || g_shutdown)
                        break;
                    for (Event& e : m_events)
                        if (e.requested && !e.done && !e.failed && !e.inflight)
                        {
                            e.inflight = true;
                            id = e.id;
                            en = e.en;
                            break;
                        }
                    if (!id)
                        continue;
                    // آخر 3 جمل مترجمة (للسياق)
                    for (auto it = m_events.rbegin(); it != m_events.rend() && context.size() < 3; ++it)
                        if (it->done && it->id != id && !it->ar.empty())
                            context.insert(context.begin(), { it->en, it->ar });
                }

                TranslateProfile prof;
                std::wstring ar, err;
                bool limited = false, ok = false;
                const ULONGLONG t0 = Now();
                if (GetTranslateProfile(m_id, prof))
                    ok = CallEngine(prof, GetEngineConfig(prof.engine), en, context, ar, err, &limited);
                else
                    err = L"الملف الشخصي غير موجود";
                if (g_shutdown)
                    break;

                const std::wstring key = CacheKey(prof, en);
                {
                    std::lock_guard<std::mutex> lock(m_mx);
                    for (Event& e : m_events)
                        if (e.id == id)
                        {
                            e.inflight = false;
                            if (ok) { e.ar = ar; e.done = true; }
                            else    e.failed = true;
                        }
                    m_status.lastMs = (int)(Now() - t0);
                    if (ok)
                    {
                        ++m_status.translated;
                        m_status.lastArabic = ar;
                        m_status.error.clear();
                        m_cache[key] = ar;
                    }
                    else
                        m_status.error = err;
                }
                if (ok)
                    AppendCache(key, ar);
                if (limited)
                    Sleep(2000);                    // كتير طلبات: منرتاح شوي
            }
        }

        std::wstring             m_id;
        std::atomic<bool>        m_running{ false };
        std::mutex               m_mx;
        std::condition_variable  m_cv;
        std::deque<Event>        m_events;
        uint64_t                 m_nextId = 1;
        TranslateStatus          m_status;
        std::map<std::wstring, std::wstring> m_cache;
        bool                     m_cacheLoaded = false;
        ULONGLONG                m_created = Now();
    };

    std::mutex g_sessMx;
    std::map<std::wstring, std::shared_ptr<Session>> g_sessions;

    std::shared_ptr<Session> GetSession(const std::wstring& id, bool create)
    {
        std::lock_guard<std::mutex> lock(g_sessMx);
        const ULONGLONG now = Now();
        // الجلسات اللي ما حدا استعملها من 5 ثواني منوقّفها
        for (auto it = g_sessions.begin(); it != g_sessions.end();)
        {
            if (now - it->second->m_lastUsed > 5000)
            {
                it->second->Stop();
                it = g_sessions.erase(it);
            }
            else
                ++it;
        }
        auto it = g_sessions.find(id);
        if (it != g_sessions.end())
        {
            it->second->m_lastUsed = now;
            return it->second;
        }
        if (!create || g_shutdown)
            return nullptr;
        auto s = std::make_shared<Session>(id);
        g_sessions[id] = s;
        s->Start();
        return s;
    }
}

TranslateStatus GetTranslateStatus(const std::wstring& profileId)
{
    std::shared_ptr<Session> s;
    {
        std::lock_guard<std::mutex> lock(g_sessMx);
        auto it = g_sessions.find(profileId);
        if (it != g_sessions.end())
            s = it->second;
    }
    return s ? s->Status() : TranslateStatus{};
}

void ClearTranslateCache(const std::wstring& profileId)
{
    const std::wstring path = CachePath(profileId);
    if (!path.empty())
        DeleteFileW(path.c_str());
    std::shared_ptr<Session> s;
    {
        std::lock_guard<std::mutex> lock(g_sessMx);
        auto it = g_sessions.find(profileId);
        if (it != g_sessions.end())
            s = it->second;
    }
    if (s)
        s->ClearCache();
}

void KeepTranslateSessionAlive(const std::wstring& profileId)
{
    GetSession(profileId, true);
}

void SetTranslatePlaceholder(bool on)
{
    g_placeholder = on;
}

void ShutdownTranslator()
{
    g_shutdown = true;
    std::lock_guard<std::mutex> lock(g_sessMx);
    for (auto& [id, s] : g_sessions)
        s->Stop();
    g_sessions.clear();
}

std::shared_ptr<const MediaFrame> TranslateSourceFrame(const TranslateProfile& p)
{
    if (!p.fromScreen)
        return PeekDeviceFrame(p.deviceLink);
    // الشاشة كلها (مصغّرة) لحتى تحدّد عليها
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!GrabScreen(p.monitor, px, w, h))
        return nullptr;
    const int scale = std::max(1, (w + 1279) / 1280);
    auto f = std::make_shared<MediaFrame>();
    f->width  = w / scale;
    f->height = h / scale;
    f->tick   = Now();
    f->pixels.resize((size_t)f->width * f->height * 4);
    for (int y = 0; y < f->height; ++y)
        for (int x = 0; x < f->width; ++x)
            memcpy(f->pixels.data() + ((size_t)y * f->width + x) * 4, px.data() + ((size_t)y * scale * w + x * scale) * 4, 4);
    f->version = ++g_frameVersion;
    return f;
}

// =============================================================
//  5) رسم الطبقة
// =============================================================
namespace
{
    std::mutex             g_renderMx;
    ComPtr<ID2D1Factory>   g_d2d;
    ComPtr<IDWriteFactory> g_dw;
    std::map<std::wstring, std::shared_ptr<const MediaFrame>> g_rendered;   // آخر الصور (حسب النص والحجم)

    std::shared_ptr<const MediaFrame> Transparent()
    {
        static std::shared_ptr<const MediaFrame> f = []
        {
            auto t = std::make_shared<MediaFrame>();
            t->width = t->height = 2;
            t->pixels.assign(16, 0);
            t->version = 0x6FFFFFFFFFFFFFFFull;
            return t;
        }();
        return f;
    }

    std::shared_ptr<const MediaFrame> RenderOverlay(const std::wstring& text, int W, int H, const TranslateProfile& p, bool dim)
    {
        if (!g_d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, g_d2d.GetAddressOf())))
            return nullptr;
        if (!g_dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                                reinterpret_cast<IUnknown**>(g_dw.GetAddressOf()))))
            return nullptr;
        ComPtr<IWICImagingFactory> wic;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))))
            return nullptr;
        ComPtr<IWICBitmap> bitmap;
        if (FAILED(wic->CreateBitmap((UINT)W, (UINT)H, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)))
            return nullptr;
        ComPtr<ID2D1RenderTarget> rt;
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (FAILED(g_d2d->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt)))
            return nullptr;
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        // الخط: بلّش كبير وصغّر لحد ما يوسع (سطرين بالكتير)
        const float padX = W * 0.03f;
        const float maxW = W - padX * 2, maxH = H * 0.92f;
        float size = H * 0.34f * p.fontScale;
        ComPtr<IDWriteTextLayout> layout;
        for (int i = 0; i < 14; ++i)
        {
            ComPtr<IDWriteTextFormat> format;
            if (FAILED(g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, std::max(8.0f, size), L"ar", &format)))
                return nullptr;
            format->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            layout.Reset();
            if (FAILED(g_dw->CreateTextLayout(text.c_str(), (UINT32)text.size(), format.Get(), maxW, (float)H, &layout)))
                return nullptr;
            DWRITE_TEXT_METRICS m{};
            layout->GetMetrics(&m);
            if ((m.height <= maxH && m.widthIncludingTrailingWhitespace <= maxW + 1) || size <= 8)
                break;
            size *= 0.9f;
        }

        const unsigned c = TransColor(p.colorIndex);
        const D2D1_COLOR_F textColor = D2D1::ColorF(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f,
                                                    dim ? 0.55f : 1.0f);
        ComPtr<ID2D1SolidColorBrush> brush, outline, box;
        rt->CreateSolidColorBrush(textColor, &brush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.85f), &outline);
        rt->CreateSolidColorBrush(D2D1::ColorF(0x0B0D12, p.boxOpacity), &box);

        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        if (p.boxOpacity > 0.01f)
        {
            const float r = H * 0.14f;
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0, 0, (float)W, (float)H), r, r), box.Get());
        }
        // إطار أسود حوالين الحروف (بيبيّن فوق أي خلفية)
        const float o = std::max(1.0f, size * 0.06f);
        const float offs[8][2] = { { -o, 0 }, { o, 0 }, { 0, -o }, { 0, o }, { -o, -o }, { o, -o }, { -o, o }, { o, o } };
        for (const auto& d : offs)
            rt->DrawTextLayout(D2D1::Point2F(padX + d[0], d[1]), layout.Get(), outline.Get());
        rt->DrawTextLayout(D2D1::Point2F(padX, 0), layout.Get(), brush.Get());
        if (FAILED(rt->EndDraw()))
            return nullptr;

        auto frame = std::make_shared<MediaFrame>();
        frame->width  = W;
        frame->height = H;
        frame->pixels.resize((size_t)W * H * 4);
        if (FAILED(bitmap->CopyPixels(nullptr, (UINT)W * 4, (UINT)frame->pixels.size(), frame->pixels.data())))
            return nullptr;
        frame->version = ++g_frameVersion;
        return frame;
    }
}

std::shared_ptr<const MediaFrame> TranslationLayerFrame(const SceneLayer& layer, bool* missing)
{
    if (missing) *missing = false;
    TranslateProfile prof;
    if (!GetTranslateProfile(layer.source, prof))
    {
        if (missing) *missing = true;
        return nullptr;
    }
    std::shared_ptr<Session> s = GetSession(prof.id, true);
    if (!s)
        return Transparent();
    if (missing && s->SourceLost())
        *missing = true;

    // اللي بينعرض هلق = اللي صار قبل التأخير (متل صورة اللعبة المتأخرة)
    // (التقاط الشاشة ما بيتأخّر ، فالترجمة كمان بلا تأخير)
    const ULONGLONG t = Now() - (ULONGLONG)(prof.fromScreen ? 0 : std::max(0, prof.delayMs));
    Event e;
    std::wstring text;
    bool dim = false;
    if (s->EventAt(t, e) && e.requested && !e.failed)
    {
        if (e.done && !e.ar.empty()) text = e.ar;
        else                         { text = L"…"; dim = true; }   // الترجمة لسا ما وصلت: منغطّي الإنكليزي
    }
    else if (g_placeholder)
    {
        text = L"هنا تظهر الترجمة العربية";
        dim = true;
    }
    if (text.empty())
        return Transparent();

    const int W = std::clamp((int)std::lround(layer.w), 16, 3840);
    const int H = std::clamp((int)std::lround(layer.h), 16, 2160);
    wchar_t style[96];
    swprintf_s(style, L"|%d|%d|%.2f|%.2f|%d|%d", W, H, prof.fontScale, prof.boxOpacity, prof.colorIndex, dim ? 1 : 0);
    const std::wstring key = text + style;

    std::lock_guard<std::mutex> lock(g_renderMx);
    auto it = g_rendered.find(key);
    if (it != g_rendered.end())
        return it->second;
    if (g_rendered.size() > 24)
        g_rendered.clear();
    std::shared_ptr<const MediaFrame> f = RenderOverlay(text, W, H, prof, dim);
    g_rendered[key] = f;
    return f ? f : Transparent();
}
