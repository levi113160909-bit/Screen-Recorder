// =============================================================
//  CaptureDevice.h - أجهزة الالتقاط (كرت التقاط للبلايستيشن والإكس بوكس...)
//
//  ويندوز بيشوف كرت الالتقاط متل الكاميرا، فالصورة بتنقرا بنفس طريقة
//  الكاميرا (Media.cpp). هون الأشيا الخاصة فيه:
//   - منعرف إذا الجهاز "كرت التقاط" من اسمه (Elgato ، AVerMedia...)
//   - منلاقي صوته: الكرت إله صوت لحالو (متل مايك) ، ومنربطه بالصورة
//     عن طريق "رقم الحاوية" (ContainerId): نفس الرقم = نفس الجهاز
//   - أسماء الدقات اللي بتنختار
// =============================================================
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

// الاسم بيدل على كرت التقاط؟ (Elgato ، AVerMedia ، HDMI ...)
bool LooksLikeCaptureCard(const std::wstring& name);

// صوت الجهاز: رقم جهاز الصوت بويندوز (WASAPI) لنفس الكرت ، أو فاضي إذا ما لقينا
// audioName (اختياري): اسم جهاز الصوت
std::wstring FindDeviceAudio(const std::wstring& videoLink, const std::wstring& videoName, std::wstring* audioName = nullptr);

// الدقة والإطارات (SceneLayer::mode)
constexpr int DEVICE_MODE_COUNT = 5;
const wchar_t* DeviceModeName(int mode);      // "تلقائي" ، "1080p • 60" ...
