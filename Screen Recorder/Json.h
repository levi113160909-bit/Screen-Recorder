// =============================================================
//  Json.h - قراءة وكتابة JSON (الصيغة اللي بتحكي فيها خدمات الترجمة)
//
//  بسيط: منقرا الجواب كله لشجرة، ومنوصل للقيم بالأسماء:
//      v["choices"][0]["message"]["content"].Str()
//  وأي اسم مش موجود بيرجّع قيمة فاضية (ما في أخطاء).
// =============================================================
#pragma once
#include <string>
#include <vector>

struct JsonValue
{
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type                     type = Type::Null;
    bool                     boolean = false;
    double                   number = 0;
    std::string              text;            // نص (UTF-8)
    std::vector<JsonValue>   items;           // مصفوفة ، أو قيم الكائن
    std::vector<std::string> keys;            // أسماء الكائن (بنفس ترتيب items)

    const JsonValue& operator[](const char* key) const;
    const JsonValue& operator[](size_t index) const;
    const JsonValue& operator[](int index) const { return (*this)[(size_t)index]; }
    size_t           Size() const { return items.size(); }
    bool             IsNull() const { return type == Type::Null; }
    std::wstring     Str() const;             // النص كنص عريض (فاضي إذا مش نص)
};

bool JsonParse(const std::string& utf8, JsonValue& out);

// نص جاهز يتحط بـ JSON (مع علامات التنصيص)
std::string JsonQuote(const std::wstring& s);
