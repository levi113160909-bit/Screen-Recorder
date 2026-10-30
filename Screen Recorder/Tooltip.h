// =============================================================
//  Tooltip.h - التلميحات (نص صغير بيطلع لما توقّف الماوس فوق زر)
//
//  الاستعمال: بعد AddHit(r, id) نادِ Tip(id, L"شو بيعمل هالزر")
//  التلميح بيطلع بس إذا هالزر هو الأعلى تحت الماوس، وبعد وقفة قصيرة.
//  main.cpp بينادي BeginTooltips() أول كل رسمة و DrawTooltip() بآخرها.
// =============================================================
#pragma once
#include "Ui.h"
#include <string>

void Tip(int id, const std::wstring& text);
void BeginTooltips();
void DrawTooltip(float W, float H);
