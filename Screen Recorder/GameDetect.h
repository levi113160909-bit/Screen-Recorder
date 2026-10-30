// =============================================================
//  GameDetect.h - "شو قاعد تلعب؟"
//
//  كل كم ثانية منشوف النافذة اللي قدامك: إذا لعبة، منخبّر البرنامج
//  لحتى يشغّل الإعادة ويلتقط نافذة اللعبة نفسها.
//
//  كيف منعرف إنها لعبة؟
//   1) البرنامج مثبّت بمجلد ألعاب معروف (Steam ، Epic ، Xbox ، GOG ، EA ، Ubisoft ، Riot ، Battle.net)
//   2) أو النافذة مالية الشاشة كلها (ملء الشاشة / بلا حواف) وهي مش متصفح أو مشغّل فيديو أو سطح المكتب
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

struct GameInfo
{
    HWND         hwnd = nullptr;
    std::wstring exePath;
    std::wstring title;          // اسم اللعبة (عنوان النافذة أو اسم الملف)
};

// self = نافذة البرنامج (ما منعتبرها لعبة)
bool DetectForegroundGame(HWND self, GameInfo& out);
