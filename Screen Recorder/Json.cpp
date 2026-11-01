// =============================================================
//  Json.cpp - قراءة وكتابة JSON (شوف Json.h)
// =============================================================
#include "Json.h"
#include "HttpClient.h"
#include <cstdio>
#include <cstdlib>

namespace
{
    const JsonValue& Empty()
    {
        static const JsonValue v;
        return v;
    }

    struct Parser
    {
        const std::string& s;
        size_t p = 0;
        int    depth = 0;

        void Skip() { while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p; }

        static void AppendUtf8(std::string& out, unsigned cp)
        {
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
            else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        }

        bool Hex4(unsigned& v)
        {
            if (p + 4 > s.size()) return false;
            v = 0;
            for (int i = 0; i < 4; ++i)
            {
                const char c = s[p++];
                v <<= 4;
                if (c >= '0' && c <= '9') v |= c - '0';
                else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
                else return false;
            }
            return true;
        }

        bool String(std::string& out)
        {
            if (p >= s.size() || s[p] != '"') return false;
            ++p;
            while (p < s.size())
            {
                const char c = s[p++];
                if (c == '"') return true;
                if (c != '\\') { out += c; continue; }
                if (p >= s.size()) return false;
                const char e = s[p++];
                switch (e)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    unsigned cp = 0;
                    if (!Hex4(cp)) return false;
                    // زوج (للحروف خارج المستوى الأساسي)
                    if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u')
                    {
                        p += 2;
                        unsigned lo = 0;
                        if (!Hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return false;
                }
            }
            return false;
        }

        bool Value(JsonValue& v)
        {
            if (++depth > 64) return false;
            Skip();
            if (p >= s.size()) return false;
            const char c = s[p];
            bool ok = false;
            if (c == '{')
            {
                v.type = JsonValue::Type::Object;
                ++p;
                Skip();
                if (p < s.size() && s[p] == '}') { ++p; ok = true; }
                else
                    for (;;)
                    {
                        Skip();
                        std::string key;
                        if (!String(key)) break;
                        Skip();
                        if (p >= s.size() || s[p] != ':') break;
                        ++p;
                        JsonValue child;
                        if (!Value(child)) break;
                        v.keys.push_back(std::move(key));
                        v.items.push_back(std::move(child));
                        Skip();
                        if (p < s.size() && s[p] == ',') { ++p; continue; }
                        if (p < s.size() && s[p] == '}') { ++p; ok = true; }
                        break;
                    }
            }
            else if (c == '[')
            {
                v.type = JsonValue::Type::Array;
                ++p;
                Skip();
                if (p < s.size() && s[p] == ']') { ++p; ok = true; }
                else
                    for (;;)
                    {
                        JsonValue child;
                        if (!Value(child)) break;
                        v.items.push_back(std::move(child));
                        Skip();
                        if (p < s.size() && s[p] == ',') { ++p; continue; }
                        if (p < s.size() && s[p] == ']') { ++p; ok = true; }
                        break;
                    }
            }
            else if (c == '"')
            {
                v.type = JsonValue::Type::String;
                ok = String(v.text);
            }
            else if (s.compare(p, 4, "true") == 0)  { v.type = JsonValue::Type::Bool; v.boolean = true;  p += 4; ok = true; }
            else if (s.compare(p, 5, "false") == 0) { v.type = JsonValue::Type::Bool; v.boolean = false; p += 5; ok = true; }
            else if (s.compare(p, 4, "null") == 0)  { v.type = JsonValue::Type::Null; p += 4; ok = true; }
            else
            {
                char* end = nullptr;
                v.number = std::strtod(s.c_str() + p, &end);
                if (end && end != s.c_str() + p)
                {
                    v.type = JsonValue::Type::Number;
                    p = end - s.c_str();
                    ok = true;
                }
            }
            --depth;
            return ok;
        }
    };
}

const JsonValue& JsonValue::operator[](const char* key) const
{
    if (type != Type::Object)
        return Empty();
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key)
            return items[i];
    return Empty();
}

const JsonValue& JsonValue::operator[](size_t index) const
{
    return (type == Type::Array || type == Type::Object) && index < items.size() ? items[index] : Empty();
}

std::wstring JsonValue::Str() const
{
    return type == Type::String ? FromUtf8(text) : std::wstring();
}

bool JsonParse(const std::string& utf8, JsonValue& out)
{
    out = JsonValue();
    Parser parser{ utf8 };
    return parser.Value(out);
}

std::string JsonQuote(const std::wstring& w)
{
    const std::string s = ToUtf8(w);
    std::string out = "\"";
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
                out += (char)c;
        }
    }
    return out + "\"";
}
