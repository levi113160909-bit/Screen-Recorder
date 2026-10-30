// =============================================================
//  Version.h - رقم الإصدار (المكان الوحيد)
//
//  لتطلع إصدار جديد: غيّر الأرقام الأربعة هون بس.
//  بيستعملها الكود (APP_VERSION) و app.rc (معلومات الملف) وملف التثبيت
//  (installer: بيقرا الرقم من الـ exe نفسه). وAppState.cpp بيتأكد إنو النص
//  مطابق للأرقام، فإذا نسيت وحدة البناء بيفشل.
//  (بلا #pragma once لأن app.rc كمان بيقرا هالملف)
// =============================================================
#ifndef SCREEN_RECORDER_VERSION_H
#define SCREEN_RECORDER_VERSION_H

#define APP_VER_MAJOR    1
#define APP_VER_MINOR    3
#define APP_VER_PATCH    0
#define APP_VERSION_STR  "1.3.0"     /* لازم يطابق الأرقام فوق */

#endif
