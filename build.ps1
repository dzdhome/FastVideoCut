# ---------------------------------------------------------------------------
# build.ps1 - build FastVideoCut (GUI) and SelfTest with MinGW-w64
#
#   .\build.ps1            build both binaries (debug)
#   .\build.ps1 -Test      build and run the self test
#   .\build.ps1 -Release   optimised build into dist\ (for GitHub releases)
#
# Override the toolchain location with  -Mingw <dir>\mingw64\bin
# ---------------------------------------------------------------------------
param(
    [string]$Mingw = 'C:\D\02_Tool\WinLibs\mingw64\bin',
    [string]$Ffmpeg = 'C:\D\02_Tool\ffmpeg-9.0.2-full_build\bin',
    [switch]$Test,
    [switch]$Release
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$outDir = if ($Release) { Join-Path $root 'dist' } else { Join-Path $root '_test' }
$obj  = if ($Release) { Join-Path $root 'build\obj' } else { Join-Path $root '_test\obj' }
$gxx  = Join-Path $Mingw 'g++.exe'
$wres = Join-Path $Mingw 'windres.exe'

if (!(Test-Path $gxx))  { throw "g++ not found: $gxx" }
if (!(Test-Path $wres)) { throw "windres not found: $wres" }
New-Item -ItemType Directory -Force $obj    | Out-Null
New-Item -ItemType Directory -Force $outDir | Out-Null

# Release: -O3 + LTO + strip symbols + no exception/debug cruft. SelfTest is
# always built -O2 so it still produces readable assertion output.
$cxxflags = @('-std=c++17', '-Wall', '-Wextra', '-DUNICODE', '-D_UNICODE')
if ($Release) {
    $cxxflags += @('-O3', '-flto', '-fno-exceptions', '-DNDEBUG', '-DVERSION="1.1.0"')
    # GCC's LTO pass reports -Wstringop-overread on std::wstring(L"") passed to
    # PostUiMessage (reading the NUL of an empty literal). Harmless, only noise.
    $cxxflags += '-Wno-stringop-overread'
} else {
    $cxxflags += @('-O2', '-g')
}
$ldflags  = @()
if ($Release) {
    $ldflags += '-s'        # strip symbols
}

# gcc writes informational notes (e.g. lto-wrapper's "serial compilation of N
# LTRANS jobs") to stderr. With $ErrorActionPreference='Stop' PowerShell turns any
# stderr line into a terminating NativeCommandError, which would abort the build
# on a purely cosmetic warning. Compile/link still verify $LASTEXITCODE below.
function Invoke-Tool {
    param([string]$Exe, [string[]]$ToolArgs, [string]$What)
    $ErrorActionPreference = 'Continue'
    & $Exe @ToolArgs 2>&1 | ForEach-Object {
        $line = "$_"
        if ($line -match '\b(warning|note|reminder)\b') {
            Write-Host ("  {0}: {1}" -f $What, $line.Trim()) -ForegroundColor DarkYellow
        } elseif ($line.Trim()) {
            Write-Host ("  {0}: {1}" -f $What, $line.Trim())
        }
    }
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { throw "$What failed (exit $code)" }
    return $code
}
$static   = @('-static', '-static-libgcc', '-static-libstdc++')
$libs     = @('-lcomctl32', '-lcomdlg32', '-lshell32', '-lole32', '-loleaut32',
              '-lmsimg32', '-lgdi32', '-luser32', '-lwinmm')

$sources = Get-ChildItem (Join-Path $root 'src\*.cpp') | Sort-Object Name

# ---- compile ---------------------------------------------------------------
$objs = @()
foreach ($s in $sources) {
    $o = Join-Path $obj ($s.BaseName + '.o')
    Write-Host ("  compile {0}" -f $s.Name)
    Invoke-Tool $gxx ($cxxflags + @('-c', $s.FullName, '-o', $o)) "compile $($s.Name)" | Out-Null
    $objs += $o
}

# ---- resources (icon + version info + settings dialog) --------------------
Write-Host "  windres  app.rc settings.rc"
Invoke-Tool $wres (@('-I', (Join-Path $root 'src'), '-O', 'coff', '-o', (Join-Path $obj 'appres.o'), (Join-Path $root 'src\app.rc'))) 'windres app.rc' | Out-Null
Invoke-Tool $wres (@('-I', (Join-Path $root 'src'), '-O', 'coff', '-o', (Join-Path $obj 'settingsres.o'), (Join-Path $root 'src\settings.rc'))) 'windres settings.rc' | Out-Null
$objs += (Join-Path $obj 'appres.o')
$objs += (Join-Path $obj 'settingsres.o')

# ---- link GUI ---------------------------------------------------------------
$gui = Join-Path $outDir 'FastVideoCut.exe'
Write-Host ("  link     {0}" -f $gui)
Invoke-Tool $gxx ($cxxflags + $static + $ldflags + @('-municode', '-o', $gui) + $objs + @('-mwindows') + $libs) "link $gui" | Out-Null

# ---- build + link the self test -------------------------------------------
$st = Join-Path $outDir 'SelfTest.exe'
Write-Host ("  build    {0}" -f $st)
$stSrc = @((Join-Path $root 'tests\SelfTest.cpp'))
foreach ($n in 'Utf', 'Process', 'Ffmpeg', 'Project') {
    $stSrc += (Join-Path $root ("src\$n.cpp"))
}
# SelfTest stays at -O2 with exceptions: no GUI, so strip/LTO buys nothing
# and readable assertion output is worth more than a few KB.
Invoke-Tool $gxx (@('-std=c++17', '-Wall', '-Wextra', '-municode', '-O2') + $static + @('-o', $st) + $stSrc) 'SelfTest build' | Out-Null

$sz = [math]::Round((Get-Item $gui).Length / 1KB)
Write-Host ("OK  FastVideoCut.exe {0} KB   SelfTest.exe {1} KB" -f $sz, [math]::Round((Get-Item $st).Length / 1KB))

# ---- optional test run ------------------------------------------------------
if ($Test) {
    if ($Ffmpeg) { $env:FASTVIDEOCUT_FFMPEG = $Ffmpeg }

    # Two clips, generated on demand (they are build output, never committed):
        #   v1.mp4 - 20.52 s, short GOP, black 6-8 s and 14-end, with audio.
        #           The self test asserts on this exact layout (3 segments over
        #           0-14 s, a dropped middle segment splitting the run at 6/8).
        #   v2.mp4 - 10.5 s, short GOP, with audio, used for the merge test.
        # GOP is deliberately 25 (1 keyframe/s): the merge test trims at 6 s and a
        # longer GOP would snap that cut back to the keyframe at 0.
    $mediaDir = Join-Path $root '_test\media'
    $ffmpegExe = Join-Path $Ffmpeg 'ffmpeg.exe'
    if (!(Test-Path $mediaDir) -and (Test-Path $ffmpegExe)) {
        New-Item -ItemType Directory -Force $mediaDir | Out-Null
        Write-Host "  gen      _test\media\v1.mp4 (short GOP, black 6-8s + tail, audio)"
        & $ffmpegExe -hide_banner -loglevel error -y `
            -f lavfi -i "testsrc2=size=320x180:rate=25:duration=20.52" `
            -f lavfi -i "sine=frequency=440:duration=20.52" `
            -vf "drawbox=enable='between(t,6,8)':color=black:t=fill,drawbox=enable='gte(t,14)':color=black:t=fill" `
            -c:v libx264 -g 25 -keyint_min 25 -pix_fmt yuv420p `
            -c:a aac -b:a 96k -shortest `
            (Join-Path $mediaDir 'v1.mp4')
        if ($LASTEXITCODE -ne 0) { throw "failed to generate v1.mp4" }
        Write-Host "  gen      _test\media\v2.mp4 (short GOP, audio)"
        & $ffmpegExe -hide_banner -loglevel error -y `
            -f lavfi -i "testsrc2=size=320x180:rate=25:duration=10.5" `
            -f lavfi -i "sine=frequency=330:duration=10.5" `
            -c:v libx264 -g 25 -keyint_min 25 -pix_fmt yuv420p `
            -c:a aac -b:a 96k -shortest `
            (Join-Path $mediaDir 'v2.mp4')
        if ($LASTEXITCODE -ne 0) { throw "failed to generate v2.mp4" }
    }

    Write-Host "`n== SelfTest =="
    & $st (Join-Path $root '_test\media\v1.mp4') (Join-Path $root '_test\media\v2.mp4')
    exit $LASTEXITCODE
}
