# =============================================================
#  package.ps1 - تغليف البرنامج للنشر (بعد بناء Release)
#
#  بيشتغل لحالو بآخر كل بناء Release (مشروع Installer بالحل).
#  أو شغّله بإيدك:  powershell -ExecutionPolicy Bypass -File installer\package.ps1
#
#  الناتج بمجلد dist:
#    ScreenRecorder-v<الإصدار>-win64.zip    نسخة محمولة (بلا تثبيت)
#    ScreenRecorder-Setup-<الإصدار>.exe     المثبّت (إذا Inno Setup متثبّت)
#    symbols\v<الإصدار>\                    ملفات الرموز لتقارير الأعطال (لك ، مش للنشر)
#  الإصدار بينقرا من الـ exe نفسه (Version.h) ، فما في رقم بينكتب بإيد هون.
# =============================================================
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$rel  = Join-Path $root 'x64\Release'
$exe  = Join-Path $rel 'Screen Recorder.exe'
$dll  = Join-Path $rel 'VirtualCamera.dll'
foreach ($f in @($exe, $dll)) {
    if (-not (Test-Path $f)) { Write-Host "package: error: missing $f (build Release first)"; exit 1 }
}
$ver  = (Get-Item $exe).VersionInfo.ProductVersion.Trim()
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null

# ---- 0) ملفات الرموز (pdb) لهالإصدار: لازمين لقراءة تقارير الأعطال (.dmp) من المستخدمين ----
#      ما بتنوزّع. احتفظ فيها (dist\symbols مش بـ git). بدونها التقرير بيطلع أرقام بس.
$sym = Join-Path $dist "symbols\v$ver"
New-Item -ItemType Directory -Force $sym | Out-Null
foreach ($f in @($exe, $dll, (Join-Path $rel 'Screen Recorder.pdb'), (Join-Path $rel 'VirtualCamera.pdb'))) {
    if (Test-Path $f) { Copy-Item $f $sym -Force }
}
Write-Host "package: symbols -> $sym"

# ---- 1) النسخة المحمولة (zip) ----
$tmp   = Join-Path $env:TEMP 'ScreenRecorder-package'
$stage = Join-Path $tmp 'ScreenRecorder'
if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
New-Item -ItemType Directory -Force (Join-Path $stage 'licenses') | Out-Null
Copy-Item $exe (Join-Path $stage 'ScreenRecorder.exe')
Copy-Item $dll $stage
Copy-Item (Join-Path $PSScriptRoot 'اقرأني.txt') $stage
Copy-Item (Join-Path $root 'LICENSE-PROGRAM.txt') (Join-Path $stage 'شروط الاستخدام.txt')
Copy-Item (Join-Path $root 'VirtualCamera\LICENSE-VCamSample.txt') (Join-Path $stage 'licenses')
Copy-Item (Join-Path $root 'VirtualCamera\wil\LICENSE.txt') (Join-Path $stage 'licenses\LICENSE-WIL.txt')
Copy-Item (Join-Path $root 'Screen Recorder\third_party\webview2\LICENSE.txt') (Join-Path $stage 'licenses\LICENSE-WebView2.txt')
Copy-Item (Join-Path $PSScriptRoot 'licenses\LICENSE-NVENC.txt') (Join-Path $stage 'licenses')
$zip = Join-Path $dist "ScreenRecorder-v$ver-win64.zip"
# ملاحظة: Compress-Archive بـ PowerShell 5 بيكتب المسارات بـ \ (غلط حسب معيار zip)،
# فمنكتب كل ملف بإيدنا باسم فيه / (وكلها جوّا مجلد ScreenRecorder/)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
if (Test-Path $zip) { Remove-Item -Force $zip }
$fs = [System.IO.File]::Open($zip, [System.IO.FileMode]::CreateNew)
$archive = New-Object System.IO.Compression.ZipArchive($fs, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem $stage -Recurse -File | ForEach-Object {
        $name = 'ScreenRecorder/' + $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $name,
            [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $archive.Dispose()
    $fs.Dispose()
}
Remove-Item -Recurse -Force $tmp
Write-Host "package: $zip"

# ---- 2) المثبّت (Inno Setup) ----
$candidates = @(
    "$env:ProgramFiles\Inno Setup 7\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")
$iscc = $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $iscc) {
    $cmd = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($cmd) { $iscc = $cmd.Source }
}
if (-not $iscc) {
    Write-Host "package: warning: Inno Setup not found, installer skipped. Get it from https://jrsoftware.org/isdl.php"
    exit 0
}
& $iscc /Qp "/DAppVersion=$ver" (Join-Path $PSScriptRoot 'ScreenRecorder.iss')
if ($LASTEXITCODE -ne 0) { Write-Host "package: error: Inno Setup failed ($LASTEXITCODE)"; exit $LASTEXITCODE }
Write-Host ("package: " + (Join-Path $dist "ScreenRecorder-Setup-$ver.exe"))
