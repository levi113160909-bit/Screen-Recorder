; =============================================================
;  ScreenRecorder.iss - ملف التثبيت (Inno Setup 6.4.1 أو أحدث ، أو 7)
;
;  بيتبنى لحالو مع كل بناء Release (مشروع Installer بالحل ، عبر package.ps1).
;  أو افتحه بـ Inno Setup Compiler واكبس Compile (بعد بناء Release).
;  الناتج: dist\ScreenRecorder-Setup-<الإصدار>.exe
;
;  - تثبيت لكل مستخدم لحالو (بلا صلاحية المسؤول) ، بمجلد:
;      %LOCALAPPDATA%\Programs\Screen Recorder
;    (وبيقدر يختار "لكل المستخدمين" إذا بدو)
;  - إذا البرنامج مفتوح ، المثبّت بيطلب يتسكّر (AppMutex)
;  - إزالة التثبيت: بتشيل التشغيل مع ويندوز ، والكاميرا الافتراضية (بموافقة المسؤول)،
;    وبتسأل إذا بدك تحذف الإعدادات. التسجيلات ما بتنلمس أبداً.
;  - AppId ثابت لازم ما يتغيّر أبداً (هو اللي بيخلّي الإصدار الجديد يتثبّت فوق القديم)
; =============================================================

#define AppName      "مسجّل الشاشة"
#define AppNameEn    "Screen Recorder"
#define AppExeName   "ScreenRecorder.exe"
#define BuildExe     "..\x64\Release\Screen Recorder.exe"
#define BuildDll     "..\x64\Release\VirtualCamera.dll"
#define AppPublisher "levi113160909-bit"
#define AppUrl       "https://github.com/levi113160909-bit/Screen-Recorder"
; الإصدار: package.ps1 بيبعته (/DAppVersion=...) ، وإلا منقراه من الـ exe نفسه (Version.h)
#ifndef AppVersion
  #define AppVersion GetStringFileInfo(BuildExe, "ProductVersion")
#endif

[Setup]
AppId={{34FF91E3-4127-4C82-ABD6-C44B9AEA8F35}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
DefaultDirName={autopf}\{#AppNameEn}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; ويندوز 10 الإصدار 2004 أو أحدث
MinVersion=10.0.19041
AppMutex=ScreenRecorder_SingleInstance
CloseApplications=yes
RestartApplications=no
OutputDir=..\dist
OutputBaseFilename=ScreenRecorder-Setup-{#AppVersion}
SetupIconFile=..\Screen Recorder\app.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppNameEn}
VersionInfoDescription={#AppNameEn} Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no

[Languages]
Name: "arabic"; MessagesFile: "compiler:Languages\Arabic.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BuildExe}"; DestDir: "{app}"; DestName: "{#AppExeName}"; Flags: ignoreversion
Source: "{#BuildDll}"; DestDir: "{app}"; Flags: ignoreversion
Source: "اقرأني.txt"; DestDir: "{app}"; Flags: ignoreversion
; رخص المكتبات المفتوحة المصدر (رخصة MIT بتطلب نوزّعها مع البرنامج)
Source: "..\VirtualCamera\LICENSE-VCamSample.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "..\VirtualCamera\wil\LICENSE.txt"; DestDir: "{app}\licenses"; DestName: "LICENSE-WIL.txt"; Flags: ignoreversion
Source: "..\Screen Recorder\third_party\webview2\LICENSE.txt"; DestDir: "{app}\licenses"; DestName: "LICENSE-WebView2.txt"; Flags: ignoreversion
Source: "licenses\LICENSE-NVENC.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Registry]
; "التشغيل مع ويندوز" (البرنامج بيكتبها من الإعدادات): منشيلها عند إزالة التثبيت
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "ScreenRecorder"; Flags: uninsdeletevalue dontcreatekey

[Code]
const
  // نفس VCAM_CLSID_STRING بـ VirtualCamera\VirtualCameraShared.h
  VCamKey = 'SOFTWARE\Classes\CLSID\{a319638e-7579-42dd-b59c-1430eabc002d}\InprocServer32';

// الكاميرا الافتراضية: البرنامج نسخها لـ ProgramData وسجّلها بـ HKLM (بصلاحية المسؤول)،
// فإزالتها كمان بدها صلاحية المسؤول (طلب موافقة واحد)
procedure RemoveVirtualCamera();
var
  Path, Dir, Params: String;
  Code: Integer;
begin
  if not RegQueryStringValue(HKLM64, VCamKey, '', Path) then
    Exit;
  Dir := ExpandConstant('{commonappdata}\ScreenRecorder\VirtualCamera');
  if CompareText(Copy(Path, 1, Length(Dir)), Dir) <> 0 then
    Exit;                                   // مش تبعنا: ما منلمسه
  if UninstallSilent then
    Exit;                                   // ما فينا نطلب موافقة بالوضع الصامت
  if MsgBox('لإزالة الكاميرا الافتراضية من ويندوز يلزم إذن المسؤول مرة واحدة.' + #13#10 + 'هل تريد إزالتها الآن؟',
            mbConfirmation, MB_YESNO) <> IDYES then
    Exit;
  Params := '/s /c "regsvr32 /u /s "' + Path + '" & rmdir /s /q "' + Dir + '" & rmdir "' +
            ExpandConstant('{commonappdata}\ScreenRecorder') + '" 2>nul"';
  ShellExec('runas', ExpandConstant('{cmd}'), Params, '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveVirtualCamera();
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent) then
    if MsgBox('هل تريد حذف الإعدادات والمشاهد وتقارير الأعطال أيضًا؟' + #13#10 +
              '(التسجيلات نفسها لن تُحذف)', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{localappdata}\ScreenRecorder'), True, True, True);
end;
