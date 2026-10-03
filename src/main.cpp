/*
 * FastVideoCut - ffmpeg based black frame detector and lossless video trimmer
 * Copyright (C) 2026 dzdhome
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// ---------------------------------------------------------------------------
// main.cpp - entry point, command line handling, message loop
// ---------------------------------------------------------------------------
#include "MainWindow.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "msimg32.lib")
#endif

namespace
{
    // GetProcAddress returns FARPROC; route it through void* so the conversion to a
    // real function pointer type does not trigger -Wcast-function-type.
    void* ModuleProc(HMODULE mod, const char* name)
    {
        FARPROC p = ::GetProcAddress(mod, name);
        void* r = nullptr;
        size_t n = sizeof(p) < sizeof(r) ? sizeof(p) : sizeof(r);
        memcpy(&r, &p, n);
        return r;
    }

    void EnableDpiAwareness()
    {
        typedef BOOL (WINAPI *SetCtxFn)(void*);
        HMODULE user32 = ::LoadLibraryW(L"user32.dll");
        if (user32)
        {
            SetCtxFn fn = (SetCtxFn)ModuleProc(user32, "SetProcessDpiAwarenessContext");
            if (fn)
            {
                // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
                if (fn((void*)-4)) { ::FreeLibrary(user32); return; }
            }
            typedef BOOL (WINAPI *SetAwareFn)(void);
            SetAwareFn old = (SetAwareFn)ModuleProc(user32, "SetProcessDPIAware");
            if (old) old();
            ::FreeLibrary(user32);
        }
    }

    void AttachConsoleOutput()
    {
        if (!::AttachConsole(ATTACH_PARENT_PROCESS))
            ::AllocConsole();
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    void PrintUsage()
    {
        ::wprintf(L"FastVideoCut - ffmpeg based black-frame cutter\n"
                  L"\n"
                  L"  FastVideoCut.exe [options] <video files...>\n"
                  L"\n"
                  L"  --ffmpeg <dir>      ffmpeg bin folder (contains ffmpeg.exe/ffprobe.exe)\n"
                  L"  --output <dir>      output folder\n"
                  L"  --thumb <px>        timeline thumbnail height (24-240, default 64)\n"
                  L"  --black-min <sec>   blackdetect d=      (default 0.10)\n"
                  L"  --black-pix <val>   blackdetect pix_th= (default 0.10)\n"
                  L"  --black-pic <val>   blackdetect pic_th= (default 0.98)\n"
        L"  --scan-window <sec> scan only the first/last N seconds (default 180, 0 = whole file)\n"
                  L"  --reencode          re-encode instead of lossless stream copy\n"
                  L"  --merge-all         export as one merged video (default: one per input)\n"
                  L"  --auto-detect       start black detection immediately\n"
                  L"  --auto-export       detect then export automatically\n"
                  L"  --nogui             run without showing the window (console log)\n"
                  L"  --log <file>        append every log line + ffmpeg command to <file>\n"
                  L"  --quit              exit when the job is done\n");
    }

    std::wstring NextArg(int argc, wchar_t** argv, int& i)
    {
        if (i + 1 < argc) return std::wstring(argv[++i]);
        return std::wstring();
    }
}

static int RunApp(HINSTANCE hInst)
{
    EnableDpiAwareness();

    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return 1;

    AppArgs args;
    std::vector<std::wstring> files;

    for (int i = 1; i < argc; ++i)
    {
        std::wstring a = argv[i];
        std::wstring low = ToLowerW(a);

        if (low == L"--ffmpeg")          args.ffmpegDir = NextArg(argc, argv, i);
        else if (low == L"--output")     args.output = NextArg(argc, argv, i);
        else if (low == L"--thumb")      args.thumbHeight = _wtoi(NextArg(argc, argv, i).c_str());
        else if (low == L"--black-min")  args.blackMin = wcstod(NextArg(argc, argv, i).c_str(), nullptr);
        else if (low == L"--black-pix")  args.blackPix = wcstod(NextArg(argc, argv, i).c_str(), nullptr);
        else if (low == L"--black-pic")  args.blackPic = wcstod(NextArg(argc, argv, i).c_str(), nullptr);
        else if (low == L"--scan-window") args.scanWindow = wcstod(NextArg(argc, argv, i).c_str(), nullptr);
        else if (low == L"--reencode")   args.reencode = true;
        else if (low == L"--merge-all")  args.mergeAll = true;
        else if (low == L"--auto-detect") args.autoDetect = true;
        else if (low == L"--auto-export")
        {
            args.autoDetect = true;
            args.autoExport = true;
            args.confirm = false;
        }
        else if (low == L"--nogui" || low == L"--no-gui") args.noGui = true;
        else if (low == L"--log")      args.logFile = NextArg(argc, argv, i);
        else if (low == L"--quit" || low == L"--quit-on-finish") args.quitOnEnd = true;
        else if (low == L"--help" || low == L"-h" || low == L"/?")
        {
            AttachConsoleOutput();
            PrintUsage();
            ::LocalFree(argv);
            return 0;
        }
        else
        {
            files.push_back(a);
        }
    }
    ::LocalFree(argv);

    if (args.noGui) AttachConsoleOutput();
    args.files = files;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES |
                ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    ::InitCommonControlsEx(&icc);

    ::OleInitialize(nullptr);

    MainWindow wnd;
    if (!wnd.Create(hInst, args))
    {
        ::MessageBoxW(nullptr, L"无法创建主窗口。", L"FastVideoCut", MB_ICONERROR);
        ::OleUninitialize();
        return 1;
    }

    ACCEL accels[] =
    {
        { FVIRTKEY | FCONTROL, 'O',       IDM_FILE_ADD },
        { FVIRTKEY | FCONTROL, 'B',       IDM_SEL_BODY },
        { FVIRTKEY | FCONTROL, 'A',       IDM_SEL_ALL },
        { FVIRTKEY | FCONTROL, 'R',       IDM_SEL_CLEAR },
        { FVIRTKEY | FCONTROL, 'L',       IDM_VIEW_LIST},
        { FVIRTKEY,            VK_F5,     IDM_VIEW_FIT },
        { FVIRTKEY,            VK_F6,     IDM_VID_DETECT },
        { FVIRTKEY | FSHIFT,   VK_F6,     IDM_VID_REDETECT },
        { FVIRTKEY,            VK_F7,     IDM_EXP_EACH },
        { FVIRTKEY,            VK_F8,     IDM_EXP_MERGE },
        { FVIRTKEY,            VK_ESCAPE, IDM_EXP_CANCEL }
    };
    HACCEL hAccel = ::CreateAcceleratorTableW(accels, (int)(sizeof(accels) / sizeof(accels[0])));

    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (hAccel && ::TranslateAcceleratorW(wnd.hwnd(), hAccel, &msg))
            continue;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    if (hAccel) ::DestroyAcceleratorTable(hAccel);
    ::OleUninitialize();
    return (int)msg.wParam;
}

// Unicode entry point (used by MinGW with -municode and by MSVC /ENTRY:wWinMainCRTStartup)
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    return RunApp(hInst);
}

// ANSI entry point (used by MSVC's default WinMainCRTStartup)
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    return RunApp(hInst);
}