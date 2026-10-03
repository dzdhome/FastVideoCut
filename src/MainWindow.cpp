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
// MainWindow.cpp - application window, toolbar, worker jobs and export flow
// ---------------------------------------------------------------------------
#include "MainWindow.h"
#include "SettingsDialog.h"

#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#endif

#define FVC_CLASS_NAME  L"FastVideoCutMainWnd"
#define FVC_WND_TITLE   L"FastVideoCut - 黑屏自动剪辑工具 (ffmpeg 无损剪切)"

namespace
{
    struct BtnDef
    {
        int            id;
        const wchar_t* text;
        const wchar_t* tip;
    };

    const BtnDef kButtons[] =
    {
        { IDB_ADD,        L"添加视频",   L"添加视频文件 (Ctrl+O)，也可以直接把文件拖到窗口" },
        { IDB_REMOVE,     L"移除",       L"移除列表中选中的视频" },
        { IDB_UP,         L"上移",       L"把选中的视频在列表里上移" },
        { IDB_DOWN,       L"下移",       L"把选中的视频在列表里下移" },
        { IDB_DETECT,     L"检测黑屏",   L"用 ffmpeg blackdetect 检测所有视频的黑屏位置 (F6)" },
        { IDB_EXPORT,     L"导出剪辑",   L"按选择导出：单文件裁剪或按顺序合并 (F7)" },
        { IDB_CLEARCACHE, L"清理缩略图", L"清除磁盘上的缩略图缓存（下次重新生成）" },
        { IDB_SETTINGS,   L"设置",       L"配置 ffmpeg 路径、黑屏检测参数与导出参数" },
        { IDB_INFO,       L"说明",       L"查看使用说明" },
        { IDB_LISTVIEW,   L"列表",       L"左侧切换：文件列表 / 视频列表（帧流）\n文件列表框默认隐藏，点击切换 (Ctrl+L)" }
    };

    const int kButtonCount = (int)(sizeof(kButtons) / sizeof(kButtons[0]));

    std::wstring SanitizeFileName(const std::wstring& name)
    {
        std::wstring out;
        out.reserve(name.size());
        for (size_t i = 0; i < name.size(); ++i)
        {
            wchar_t c = name[i];
            if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
                c == L'"' || c == L'<' || c == L'>' || c == L'|' || c < 32)
                out.push_back(L'_');
            else
                out.push_back(c);
        }
        if (out.empty()) out = L"merged";
        return out;
    }

    std::wstring UniquePath(const std::wstring& path)
    {
        if (!FileExists(path)) return path;
        std::wstring dir = PathGetDirectory(path);
        std::wstring base = PathGetFileNameNoExt(path);
        std::wstring ext = PathGetExtension(path);
        for (int i = 1; i < 10000; ++i)
        {
            std::wstring cand = PathCombine(dir, FormatString(L"%s_%d%s", base.c_str(), i, ext.c_str()));
            if (!FileExists(cand)) return cand;
        }
        return path;
    }

    std::wstring FormatSize(long long bytes)
    {
        double v = (double)bytes;
        if (v >= 1024.0 * 1024.0 * 1024.0)
            return FormatString(L"%.2f GB", v / (1024.0 * 1024.0 * 1024.0));
        if (v >= 1024.0 * 1024.0)
            return FormatString(L"%.1f MB", v / (1024.0 * 1024.0));
        if (v >= 1024.0)
            return FormatString(L"%.0f KB", v / 1024.0);
        return FormatString(L"%d B", (int)v);
    }

    std::wstring BlackRangesText(const std::vector<BlackRange>& ranges, size_t maxShow)
    {
        std::wstring out;
        for (size_t i = 0; i < ranges.size(); ++i)
        {
            if (i >= maxShow) break;
            if (i) out += L", ";
            out += FormatString(L"%s-%s",
                                FormatTimecode(ranges[i].start).c_str(),
                                FormatTimecode(ranges[i].end).c_str());
        }
        if (ranges.size() > maxShow)
            out += FormatString(L" …等共 %d 段", (int)ranges.size());
        return out;
    }
}   // namespace

// ---------------------------------------------------------------------------
// window creation
// ---------------------------------------------------------------------------
bool MainWindow::Create(HINSTANCE hInst, const AppArgs& args)
{
    hInst_ = hInst;
    args_ = args;
    jobRunning_ = false;
    cancel_.ev = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);

    LoadSettings(settings_);
    if (settings_.outputDir.empty()) settings_.outputDir = DefaultOutputDir();

    if (!args_.ffmpegDir.empty()) settings_.ffmpegDir = args_.ffmpegDir;
    if (args_.thumbHeight > 0)    settings_.thumbHeight = args_.thumbHeight;
    if (args_.blackMin > 0.0)     settings_.blackMinDuration = args_.blackMin;
    if (args_.blackPix > 0.0)     settings_.blackPixTh = args_.blackPix;
    if (args_.blackPic > 0.0)     settings_.blackPicTh = args_.blackPic;
    if (args_.scanWindow >= 0.0)  settings_.blackEdgeScan = args_.scanWindow;
    if (args_.reencode)           settings_.reencodeExport = true;
    if (!args_.output.empty())    settings_.outputDir = PathGetDirectory(PathGetFull(args_.output));

    logFilePath_ = args_.logFile;
    if (!logFilePath_.empty())
    {
        ::DeleteFileW(logFilePath_.c_str());   // fresh log per run
        WriteLogFile(L"FastVideoCut started " + [] {
            SYSTEMTIME st; ::GetLocalTime(&st);
            wchar_t b[64];
            ::swprintf(b, L"%04d-%02d-%02d %02d:%02d:%02d",
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            return std::wstring(b);
        }());
        ffmpeg_.setLog([this](const std::wstring& s)
        {
            PostUiMessage(hwnd_, UiLog, L"[ffmpeg] " + s);
        });
    }

    ffmpeg_.Locate(settings_.ffmpegDir);
    if (ffmpeg_.available() && settings_.ffmpegDir != ffmpeg_.paths().binDir)
    {
        settings_.ffmpegDir = ffmpeg_.paths().binDir;
        SaveSettings(settings_);
    }

    WNDCLASSEXW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProcStatic;
    wc.hInstance = hInst;
    wc.hIcon = (HICON)::LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                   0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)::LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                     16, 16, 0);
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = FVC_CLASS_NAME;
    ::RegisterClassExW(&wc);

    hwnd_ = ::CreateWindowExW(0, FVC_CLASS_NAME, FVC_WND_TITLE,
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1400, 880,
                              nullptr, nullptr, hInst, this);
    if (!hwnd_) return false;

    if (args_.noGui)
        ::ShowWindow(hwnd_, SW_HIDE);
    else
    {
        ::ShowWindow(hwnd_, SW_SHOW);
        ::UpdateWindow(hwnd_);
    }

    if (!args_.files.empty())
    {
        std::vector<std::wstring>* files = new std::vector<std::wstring>(args_.files);
        ::PostMessageW(hwnd_, WM_FVC_ADD, 0, (LPARAM)files);
    }
    if (args_.autoDetect || args_.autoExport)
    {
        // queued AFTER WM_FVC_ADD so the job starts with the files already loaded
        ::PostMessageW(hwnd_, WM_FVC_AUTOSTART, 0, 0);
    }
    return true;
}

void MainWindow::Shutdown()
{
    preview_.Shutdown();           // stops preview workers before anything else
    if (cancel_.ev) ::SetEvent(cancel_.ev);
    if (worker_.joinable()) worker_.join();

    timeline_.Shutdown();

    if (fontUi_) { ::DeleteObject(fontUi_); fontUi_ = nullptr; }
    if (bgBrush_) { ::DeleteObject(bgBrush_); bgBrush_ = nullptr; }
    if (cancel_.ev) { ::CloseHandle(cancel_.ev); cancel_.ev = nullptr; }

    // Only write settings the user actually changed. Command line overrides
    // (--output, --ffmpeg, ...) are runtime values and must not be persisted -
    // otherwise one scripted run silently rewrites the user's configuration.
    if (settingsDirty_) SaveSettings(settings_);
}

void MainWindow::CreateChildren()
{
    HDC dc = ::GetDC(hwnd_);
    dpi_ = dc ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ::ReleaseDC(hwnd_, dc);
    if (dpi_ <= 0) dpi_ = 96;

    LOGFONTW lf;
    ::ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    ::lstrcpynW(lf.lfFaceName, L"Microsoft YaHei UI", LF_FACESIZE);
    lf.lfHeight = -MulDiv(13, dpi_, 96);
    fontUi_ = ::CreateFontIndirectW(&lf);
    bgBrush_ = ::CreateSolidBrush(RGB(24, 26, 31));

    // ---- toolbar buttons --------------------------------------------------
    buttonCount_ = kButtonCount;
    for (int i = 0; i < kButtonCount; ++i)
    {
        buttons_[i] = ::CreateWindowExW(0, L"BUTTON", kButtons[i].text,
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        0, 0, 10, 10, hwnd_,
                                        (HMENU)(INT_PTR)kButtons[i].id, hInst_, nullptr);
        ::SendMessageW(buttons_[i], WM_SETFONT, (WPARAM)fontUi_, TRUE);
    }

    // ---- list view --------------------------------------------------------
    list_ = ::CreateWindowExW(WS_EX_ACCEPTFILES, WC_LISTVIEWW, L"",
                              WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS |
                              LVS_SINGLESEL,
                              0, 0, 10, 10, hwnd_, (HMENU)IDC_LIST, hInst_, nullptr);
    ::SendMessageW(list_, WM_SETFONT, (WPARAM)fontUi_, TRUE);
    ::SendMessageW(list_, LVM_SETEXTENDEDLISTVIEWSTYLE,
                   LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES,
                   LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

    struct ColDef { const wchar_t* text; int width; int fmt; };
    const ColDef cols[] =
    {
        { L"#",        36,  LVCFMT_RIGHT  },
        { L"文件",     250, LVCFMT_LEFT   },
        { L"状态",     96,  LVCFMT_LEFT   },
        { L"时长",     80,  LVCFMT_RIGHT  },
        { L"分辨率",   86,  LVCFMT_LEFT   },
        { L"黑屏段",   60,  LVCFMT_RIGHT  },
        { L"黑屏时长", 80,  LVCFMT_RIGHT  },
        { L"起始时间", 104, LVCFMT_RIGHT  },
        { L"结束时间", 104, LVCFMT_RIGHT  },
        { L"保留时长", 80,  LVCFMT_RIGHT  },
        { L"已选段",   64,  LVCFMT_RIGHT  },
        { L"大小",     82,  LVCFMT_RIGHT  }
    };
    for (int i = 0; i < (int)(sizeof(cols) / sizeof(cols[0])); ++i)
    {
        LVCOLUMNW col;
        ::ZeroMemory(&col, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        col.pszText = (LPWSTR)cols[i].text;
        col.cx = MulDiv(cols[i].width, dpi_, 96);
        col.fmt = cols[i].fmt;
        col.iSubItem = i;
        ::SendMessageW(list_, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }

    // ---- timeline ---------------------------------------------------------
    timeline_.Create(hwnd_, IDC_TIMELINE, hInst_);
    timeline_.Attach(&project_, &ffmpeg_, &settings_);

    // ---- right hand preview pane ------------------------------------------
    if (!args_.noGui && preview_.Create(hwnd_, IDC_PREVIEW, hInst_))
    {
        preview_.SetFont(fontUi_);
        preview_.SetFfmpeg(ffmpeg_.paths().ffmpeg);
    }

    // ---- log --------------------------------------------------------------
    log_ = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                             ES_AUTOVSCROLL | ES_READONLY | ES_LEFT,
                             0, 0, 10, 10, hwnd_, (HMENU)IDC_LOG, hInst_, nullptr);
    ::SendMessageW(log_, WM_SETFONT, (WPARAM)fontUi_, TRUE);
    ::SendMessageW(log_, EM_SETLIMITTEXT, (WPARAM)(1 << 20), 0);

    progress_ = ::CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                  WS_CHILD | PBS_SMOOTH,
                                  0, 0, 10, 10, hwnd_, (HMENU)IDC_PROGRESS, hInst_, nullptr);
    ::SendMessageW(progress_, PBM_SETRANGE32, 0, 100);

    help_ = ::CreateWindowExW(0, L"STATIC", L"",
                              WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                              0, 0, 10, 10, hwnd_, (HMENU)IDC_HELPBOX, hInst_, nullptr);
    ::SendMessageW(help_, WM_SETFONT, (WPARAM)fontUi_, TRUE);

    status_ = ::CreateWindowExW(0, L"STATIC", L"",
                                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                0, 0, 10, 10, hwnd_, (HMENU)IDC_STATUS, hInst_, nullptr);
    ::SendMessageW(status_, WM_SETFONT, (WPARAM)fontUi_, TRUE);

    BuildMenu();
    UpdateTitles();

    showList_ = settings_.showFileList;
    ApplyListMode(false);       // 按设置决定文件列表框默认隐藏还是显示
}

// 左侧显示方式：showList_ = true 显示文件列表框；false 时文件列表框隐藏，
// 整块左边都留给视频列表（帧流）。工具栏“列表/视频”按钮和 Ctrl+L 共用。
void MainWindow::ApplyListMode(bool save)
{
    if (save)
    {
        settings_.showFileList = showList_;
        SaveSettings(settings_);
    }

    // 按钮文字显示“点一下会切到哪个视图”
    if (buttonCount_ >= 10 && buttons_[9])
        ::SetWindowTextW(buttons_[9], showList_ ? L"视频" : L"列表");

    if (viewMenu_)
        ::CheckMenuItem(viewMenu_, IDM_VIEW_LIST,
                        MF_BYCOMMAND | (showList_ ? MF_CHECKED : MF_UNCHECKED));

    if (list_ && !showList_ && ::IsWindowVisible(list_))
        ::ShowWindow(list_, SW_HIDE);

    LayoutChildren();
}

void MainWindow::ToggleFileList()
{
    showList_ = !showList_;
    ApplyListMode(true);
    AppendLog(showList_ ? L"左侧切换为文件列表（隐藏视频列表）"
                        : L"左侧切换为视频列表（隐藏文件列表框）");
}

void MainWindow::BuildMenu()
{
    HMENU bar = ::CreateMenu();

    HMENU file = ::CreatePopupMenu();
    ::AppendMenuW(file, MF_STRING, IDM_FILE_ADD,      L"添加视频文件(&A)...\tCtrl+O");
    ::AppendMenuW(file, MF_STRING, IDM_FILE_ADDDIR,   L"添加整个文件夹(&D)...");
    ::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(file, MF_STRING, IDM_FILE_REMOVE,   L"移除选中(&R)");
    ::AppendMenuW(file, MF_STRING, IDM_FILE_CLEAR,    L"清空列表(&C)");
    ::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(file, MF_STRING, IDM_FILE_EXIT,     L"退出(&X)");
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"文件(&F)");

    HMENU vid = ::CreatePopupMenu();
    ::AppendMenuW(vid, MF_STRING, IDM_VID_DETECT,     L"检测黑屏（跳过已检测）(&B)\tF6");
    ::AppendMenuW(vid, MF_STRING, IDM_VID_REDETECT,   L"重新检测全部黑屏(&R)\tShift+F6");
    ::AppendMenuW(vid, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(vid, MF_STRING, IDM_VID_UP,         L"上移(&U)");
    ::AppendMenuW(vid, MF_STRING, IDM_VID_DOWN,       L"下移(&W)");
    ::AppendMenuW(vid, MF_STRING, IDM_VID_SORTNAME,   L"按文件名排序(&S)");
    ::AppendMenuW(vid, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(vid, MF_STRING, IDM_VID_RESET,      L"重置选择（整段保留）(&T)");
    ::AppendMenuW(vid, MF_STRING, IDM_VID_CLEARCACHE, L"清理缩略图缓存(&L)");
    ::AppendMenuW(vid, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(vid, MF_STRING, IDM_VID_SETTINGS,   L"设置(&G)...");
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)vid, L"视频(&V)");

    HMENU sel = ::CreatePopupMenu();
    ::AppendMenuW(sel, MF_STRING, IDM_SEL_BODY,  L"保留主体（首末非黑屏段之间）(&B)\tCtrl+B");
    ::AppendMenuW(sel, MF_STRING, IDM_SEL_ALL,   L"整段保留（含黑屏）(&K)\tCtrl+A");
    ::AppendMenuW(sel, MF_STRING, IDM_SEL_CLEAR, L"清除选择(&C)\tCtrl+R");
    ::AppendMenuW(sel, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(sel, MF_STRING, 0, L"左键=保留起点，右键=保留终点，Ctrl+左键=单段保留");
    ::EnableMenuItem(sel, GetMenuItemCount(sel) - 1, MF_BYPOSITION | MF_GRAYED);
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)sel, L"选择(&S)");

    HMENU exp = ::CreatePopupMenu();
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_EACH,  L"每个视频单独导出（切掉未选段，无损）(&E)\tF7");
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_MERGE, L"按列表顺序合并为一个视频（无损）(&M)\tF8");
    ::AppendMenuW(exp, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_ALL,   L"两个都导出(&B)");
    ::AppendMenuW(exp, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_OPEN,  L"打开输出文件夹(&D)");
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_CANCEL,L"取消当前任务(&C)\tEsc");
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)exp, L"导出(&E)");

    HMENU view = ::CreatePopupMenu();
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_LIST,       L"显示文件列表框(&L)\tCtrl+L");
    ::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_FIT,        L"适应窗口(&F)\tF5");
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_ZIN,        L"放大(&I)");
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_ZOUT,       L"缩小(&O)");
    ::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_THUMB_BIG,  L"缩略图更大(&B)");
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_THUMB_SMALL,L"缩略图更小(&S)");
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"视图(&W)");
    viewMenu_ = view;

    HMENU help = ::CreatePopupMenu();
    ::AppendMenuW(help, MF_STRING, IDM_HELP_INFO,  L"使用说明(&H)");
    ::AppendMenuW(help, MF_STRING, IDM_HELP_ABOUT, L"关于(&A)");
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"帮助(&H)");

    ::SetMenu(hwnd_, bar);
}

void MainWindow::LayoutChildren()
{
    if (!hwnd_) return;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);

    const int s = dpi_;
    int pad     = MulDiv(6,  s, 96);
    int btnH    = MulDiv(30, s, 96);
    int listH   = MulDiv(150, s, 96);
    int statusH = MulDiv(24, s, 96);
    int helpH   = MulDiv(38, s, 96);
    int logH    = MulDiv(120, s, 96);
    int progW   = MulDiv(190, s, 96);

    // ---- toolbar ---------------------------------------------------------
    int widths[] = { 96, 62, 62, 62, 92, 92, 104, 62, 62, 74 };
    int x = pad;
    for (int i = 0; i < buttonCount_; ++i)
    {
        int w = MulDiv(widths[i], s, 96);
        ::MoveWindow(buttons_[i], x, pad, w, btnH, TRUE);
        x += w + MulDiv(6, s, 96);
    }

    int top = pad + btnH + MulDiv(4, s, 96);

    // ---- bottom stack ----------------------------------------------------
    int statusTop = rc.bottom - statusH;
    int helpTop   = statusTop - helpH;
    int logTop    = helpTop - logH;
    int timelineBottom = logTop - MulDiv(4, s, 96);

    // 文件列表框默认隐藏(showList_ = false)：整块左边都留给视频列表（帧流）；
    // 切到“列表”模式时，文件列表独占左边区域，方便一次看完所有行。
    int listTop    = top;
    int listBottom = listTop;
    if (showList_)
    {
        listBottom = listTop + listH;
        if (listBottom > timelineBottom - MulDiv(60, s, 96))
        {
            listBottom = timelineBottom - MulDiv(60, s, 96);
            if (listBottom < listTop + MulDiv(60, s, 96))
                listBottom = listTop + MulDiv(60, s, 96);
        }
    }

    // ---- right hand preview column ----------------------------------------
    int pvW = MulDiv(340, s, 96);
    int contentW = rc.right - pvW - pad;          // left column: list + timeline
    if (contentW < MulDiv(480, s, 96))
    {
        contentW = MulDiv(480, s, 96);
        pvW = rc.right - contentW - pad;
    }
    int pvX = contentW + pad;
    bool showPv = (pvW >= MulDiv(240, s, 96));

    if (showList_)
    {
        ::ShowWindow(list_, SW_SHOWNOACTIVATE);
        ::MoveWindow(list_, 0, listTop, contentW, listBottom - listTop, TRUE);
        ::MoveWindow(timeline_.hwnd(), 0, listBottom + MulDiv(4, s, 96),
                     contentW, timelineBottom - listBottom - MulDiv(4, s, 96), TRUE);
    }
    else
    {
        if (::IsWindowVisible(list_)) ::ShowWindow(list_, SW_HIDE);
        ::MoveWindow(timeline_.hwnd(), 0, listTop, contentW, timelineBottom - listTop, TRUE);
    }
    timeline_.NotifyResized();      // keep the strip fitted to the real width
    if (preview_.hwnd())
    {
        if (showPv)
        {
            ::MoveWindow(preview_.hwnd(), pvX, top, rc.right - pvX,
                         timelineBottom - top, TRUE);
            if (!::IsWindowVisible(preview_.hwnd()))
                ::ShowWindow(preview_.hwnd(), SW_SHOWNOACTIVATE);
        }
        else if (::IsWindowVisible(preview_.hwnd()))
        {
            ::ShowWindow(preview_.hwnd(), SW_HIDE);
        }
    }
    ::MoveWindow(log_, 0, logTop, rc.right, logH, TRUE);
    ::MoveWindow(help_, 0, helpTop, rc.right, helpH, TRUE);
    ::MoveWindow(status_, 0, statusTop, rc.right - progW - pad, statusH, TRUE);
    ::MoveWindow(progress_, rc.right - progW - pad, statusTop + MulDiv(4, s, 96),
                 progW, statusH - MulDiv(8, s, 96), TRUE);
}

LRESULT CALLBACK MainWindow::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MainWindow* self = (MainWindow*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (MainWindow*)cs->lpCreateParams;
        if (self)
        {
            self->hwnd_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        }
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(msg, wp, lp);
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateChildren();
        return 0;

    case WM_FVC_AUTOSTART:
        // runs after the queued WM_FVC_ADD, so the project already has its files
        if (args_.autoDetect || args_.autoExport)
        {
            StartDetect(false);     // 跳过已检测过的（命令行批处理时全部都是新文件）
            if (!jobRunning_)
            {
                exitCode_ = 1;
                if (args_.quitOnEnd) ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
        }
        return 0;

    case WM_SIZE:
        LayoutChildren();
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = MulDiv(1100, dpi_, 96);   // room for the preview column
        mm->ptMinTrackSize.y = MulDiv(620, dpi_, 96);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        // WM_MOUSEWHEEL 只发给键盘焦点窗口。焦点停在工具栏/列表上、鼠标却在这块
        // 区域滚动时没人处理，这里兜底转发给视频列表，否则表现为“滚不动”。
        HWND tl = timeline_.hwnd();
        if (tl && ::IsWindowVisible(tl))
        {
            POINT pt;
            pt.x = GET_X_LPARAM(lp);
            pt.y = GET_Y_LPARAM(lp);
            RECT tr;
            ::GetWindowRect(tl, &tr);
            if (::PtInRect(&tr, pt))
                return ::SendMessageW(tl, WM_MOUSEWHEEL, wp, lp);
        }
        return 0;
    }

    case WM_CLOSE:
        if (jobRunning_ && !args_.noGui)
        {
            int r = ::MessageBoxW(hwnd_,
                                  L"当前还有任务在运行，确定要退出吗？",
                                  L"FastVideoCut", MB_ICONQUESTION | MB_YESNO);
            if (r != IDYES) return 0;
        }
        ::DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        Shutdown();
        ::PostQuitMessage(exitCode_);
        return 0;

    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if (id == IDC_LIST && (UINT)code == LVN_ITEMCHANGED)
        {
            int sel = SelectedItem();
            timeline_.SetCurrentItem(sel);
            UpdateTitles();
            UpdateButtonStates();
            return 0;
        }
        if (code == BN_CLICKED || code == 0 || code == 1)
        {
            OnCommand(id);
            return 0;
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR* nh = (NMHDR*)lp;
        if (nh && nh->idFrom == IDC_LIST && nh->code == NM_DBLCLK)
        {
            if (ffmpeg_.available())
            {
                int sel = SelectedItem();
                if (sel >= 0 && project_.items[sel].isAnalysed() == false)
                    StartDetect();
            }
            return 0;
        }
        if (nh && nh->idFrom == IDC_LIST && nh->code == LVN_KEYDOWN)
        {
            NMLVKEYDOWN* kd = (NMLVKEYDOWN*)lp;
            if (kd->wVKey == VK_DELETE) { OnCommand(IDM_FILE_REMOVE); return 0; }
        }
        break;
    }

    case WM_FVC_UI:
        OnUiMessage((UiMessage*)lp);
        return 0;

    case WM_FVC_ADD:
    {
        std::vector<std::wstring>* files = (std::vector<std::wstring>*)lp;
        OnAddFiles(files);
        delete files;
        return 0;
    }

    case WM_FVC_SELCHG:
    {
        int idx = (int)lp;
        if (idx >= 0)
        {
            int current = SelectedItem();
            if (current != idx)
            {
                ::SendMessageW(list_, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)nullptr);
                LVITEMW item;
                ::ZeroMemory(&item, sizeof(item));
                item.mask = LVIF_STATE;
                item.state = LVIS_SELECTED | LVIS_FOCUSED;
                item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
                ::SendMessageW(list_, LVM_SETITEMSTATE, (WPARAM)idx, (LPARAM)&item);
                EnsureVisibleItem(idx);
            }
        }
        UpdateTitles();
        UpdateButtonStates();
        return 0;
    }

    case WM_FVC_KEEPCHG:
    {
        int idx = (int)wp;
        if (idx < 0) RebuildList();
        else if (idx < (int)project_.items.size()) UpdateListRow(idx);
        UpdateTitles();
        UpdateButtonStates();
        LogKeepChange(idx);
        return 0;
    }

    case WM_FVC_PREVIEW:
        StartSegmentPreview((int)wp, (int)lp);
        return 0;

    case WM_DROPFILES:
        OnDropAdd((HDROP)wp);
        return 0;

    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wp;
        HWND ctl = (HWND)lp;
        ::SetBkMode(dc, TRANSPARENT);
        if (ctl == status_ || ctl == help_)
        {
            ::SetTextColor(dc, RGB(198, 204, 214));
            ::SetBkColor(dc, RGB(24, 26, 31));
            return (LRESULT)bgBrush_;
        }
        break;
    }

    case WM_ERASEBKGND:
    {
        RECT rc;
        ::GetClientRect(hwnd_, &rc);
        ::FillRect((HDC)wp, &rc, bgBrush_);
        return 1;
    }
    }
    return ::DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// list view helpers
// ---------------------------------------------------------------------------
int MainWindow::SelectedItem() const
{
    if (!list_) return -1;
    return (int)::SendMessageW(list_, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

void MainWindow::EnsureVisibleItem(int index)
{
    if (!list_ || index < 0) return;
    ::SendMessageW(list_, LVM_ENSUREVISIBLE, (WPARAM)index, FALSE);
}

void MainWindow::RebuildList()
{
    if (!list_) return;
    ::SendMessageW(list_, LVM_DELETEALLITEMS, 0, 0);
    for (size_t i = 0; i < project_.items.size(); ++i)
    {
        std::wstring num = FormatString(L"%d", (int)i + 1);
        LVITEMW item;
        ::ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        item.pszText = (LPWSTR)num.c_str();
        ::SendMessageW(list_, LVM_INSERTITEMW, 0, (LPARAM)&item);
        UpdateListRow((int)i);
    }
    if (!project_.items.empty())
    {
        LVITEMW item;
        ::ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_STATE;
        item.state = LVIS_SELECTED | LVIS_FOCUSED;
        item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        ::SendMessageW(list_, LVM_SETITEMSTATE, 0, (LPARAM)&item);
    }
}

void MainWindow::UpdateListRow(int index)
{
    if (!list_ || index < 0 || index >= (int)project_.items.size()) return;
    const VideoItem& it = project_.items[index];

    std::wstring texts[11];
    texts[0] = it.name;
    texts[1] = it.statusText();
    texts[2] = it.info.duration > 0.0 ? FormatClock(it.info.duration) : L"-";
    texts[3] = it.info.width > 0 ? FormatString(L"%dx%d", it.info.width, it.info.height) : L"-";
    texts[4] = FormatString(L"%d", it.blackCount());
    texts[5] = FormatClock(it.blackDuration());
    // 选择起始时间 / 结束时间（还没有点选过时显示 “-”）
    double keepT0 = it.keepStartTime();
    double keepT1 = it.keepEndTime();
    texts[6] = keepT0 >= 0.0 ? FormatTimecode(keepT0) : L"-";
    texts[7] = keepT1 >= 0.0 ? FormatTimecode(keepT1) : L"-";
    texts[8] = FormatClock(it.selectedDuration());
    texts[9] = FormatString(L"%d/%d", it.selectedSegmentCount(), (int)it.segments.size());
    texts[10] = it.info.sizeBytes > 0 ? FormatSize(it.info.sizeBytes) : L"-";

    for (int i = 0; i < 11; ++i)
    {
        LVITEMW item;
        ::ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.iSubItem = i + 1;
        item.pszText = (LPWSTR)texts[i].c_str();
        ::SendMessageW(list_, LVM_SETITEMW, 0, (LPARAM)&item);
    }
}

void MainWindow::UpdateTitles()
{
    int n = (int)project_.items.size();
    double total = project_.TotalDuration();
    double keep = project_.SelectedDuration();
    int black = 0;
    for (size_t i = 0; i < project_.items.size(); ++i) black += project_.items[i].blackCount();

    std::wstring title = FormatString(
        L"FastVideoCut - %d 个视频 | 总时长 %s | 保留 %s | 黑屏 %d 段",
        n, FormatClock(total).c_str(), FormatClock(keep).c_str(), black);
    ::SetWindowTextW(hwnd_, title.c_str());

    std::wstring ffName = ffmpeg_.available()
                              ? PathGetFileName(ffmpeg_.paths().ffmpeg)
                              : std::wstring(L"未找到 (请在设置里指定)");
    double pct = (total > 0.0) ? (keep * 100.0 / total) : 0.0;
    SetStatus(FormatString(L"已选 %d 段 · 保留 %s (%.1f%%) · ffmpeg: %s",
                           project_.SelectedSegmentCount(), FormatClock(keep).c_str(),
                           pct, ffName.c_str()));

    ::SetWindowTextW(help_,
        L"左键分段 = 保留起点，右键分段 = 保留终点（两者之间全部保留）| Ctrl+左键 = 单段保留/取消 | "
        L"双击 = 只保留该段 | 点击任一分段都会在右侧预览播放 | 滚轮 = 上下滚动视频 | "
        L"Ctrl+滚轮 = 缩放，Shift+滚轮 = 横向平移，中键拖动 = 平移 | 工具栏“列表” = 文件列表/视频列表切换");
}

void MainWindow::UpdateButtonStates()
{
    bool busy     = jobRunning_;
    bool hasItems = !project_.items.empty();
    bool hasSel   = project_.SelectedSegmentCount() > 0;
    bool hasFf    = ffmpeg_.available();
    int  sel      = SelectedItem();

    ::EnableWindow(buttons_[0], !busy);
    ::EnableWindow(buttons_[1], !busy && hasItems);
    ::EnableWindow(buttons_[2], !busy && sel > 0);
    ::EnableWindow(buttons_[3], !busy && sel >= 0 && sel + 1 < (int)project_.items.size());
    ::EnableWindow(buttons_[4], !busy && hasItems && hasFf);
    ::EnableWindow(buttons_[5], !busy && hasItems && hasFf && hasSel);
    ::EnableWindow(buttons_[6], !busy);
    ::EnableWindow(buttons_[7], !busy);
    ::EnableWindow(buttons_[8], TRUE);
    ::EnableWindow(buttons_[9], TRUE);      // 列表/视频 视图切换随时可用
}

// ---------------------------------------------------------------------------
// adding files
// ---------------------------------------------------------------------------
void MainWindow::OnAddFiles(std::vector<std::wstring>* files)
{
    if (!files) return;

    int added = 0, dup = 0, bad = 0;
    for (size_t i = 0; i < files->size(); ++i)
    {
        std::wstring p = (*files)[i];
        if (DirectoryExists(p))
        {
            std::vector<std::wstring> found = ListFilesByExt(p, SupportedMediaExtensions());
            std::sort(found.begin(), found.end());
            for (size_t k = 0; k < found.size(); ++k)
            {
                if (project_.AddFile(found[k]) >= 0) ++added; else ++dup;
            }
            continue;
        }
        if (!FileExists(p) || !IsSupportedMediaFile(p)) { ++bad; continue; }
        if (project_.AddFile(p) >= 0) ++added; else ++dup;
    }

    if (added > 0)
    {
        settings_.lastAddDir = PathGetDirectory(project_.items[project_.items.size() - 1].path);
        RebuildList();
        timeline_.Refresh();
        UpdateTitles();
        UpdateButtonStates();
        AppendLog(FormatString(L"添加 %d 个视频（重复 %d，不支持 %d）", added, dup, bad));
    }
    else
    {
        AppendLog(FormatString(L"没有添加视频（重复 %d，不支持/不存在 %d）", dup, bad));
    }
}

void MainWindow::OnDropAdd(HDROP drop)
{
    std::vector<std::wstring>* files = new std::vector<std::wstring>();
    UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i)
    {
        wchar_t buf[MAX_PATH * 2];
        UINT n = ::DragQueryFileW(drop, i, buf, (UINT)_countof(buf));
        if (n > 0) files->push_back(std::wstring(buf, n));
    }
    ::DragFinish(drop);
    OnAddFiles(files);
    delete files;
}

// Logs the new keep range after a left / right / Ctrl+click on the timeline.
void MainWindow::LogKeepChange(int itemIndex)
{
    if (itemIndex >= 0 && itemIndex < (int)project_.items.size())
    {
        const VideoItem& it = project_.items[itemIndex];
        std::wstring range = it.keepRangeText();
        if (range.empty())
        {
            AppendLog(FormatString(L"保留：%s → 未选择任何分段", it.name.c_str()));
        }
        else
        {
            AppendLog(FormatString(L"保留：%s → %s  （%d/%d 段，共 %s）",
                                   it.name.c_str(), range.c_str(),
                                   it.selectedSegmentCount(), (int)it.segments.size(),
                                   FormatClock(it.selectedDuration()).c_str()));
        }
        return;
    }

    if (itemIndex < 0)
    {
        int n = (int)project_.items.size();
        AppendLog(FormatString(L"保留：已更新 %d 个视频，合计保留 %s",
                               n, FormatClock(project_.SelectedDuration()).c_str()));
    }
}

// Starts previewing one segment (called from WM_FVC_PREVIEW on the UI thread).
void MainWindow::StartSegmentPreview(int itemIndex, int segIndex)
{
    if (args_.noGui || !preview_.hwnd()) return;
    if (itemIndex < 0 || itemIndex >= (int)project_.items.size()) return;

    VideoItem& it = project_.items[itemIndex];
    if (segIndex < 0 || segIndex >= (int)it.segments.size()) return;

    const Segment& s = it.segments[segIndex];
    preview_.SetFfmpeg(ffmpeg_.paths().ffmpeg);
    preview_.Play(it.path, s.t0, s.t1,
                  it.info.width, it.info.height, it.info.fps, it.info.hasAudio,
                  FormatString(L"%s · 第 %d 段%s",
                               it.name.c_str(), segIndex + 1,
                               s.kind == SegKind::Black ? L"（黑屏）" : L""));
}

// ---------------------------------------------------------------------------
// ui helpers
// ---------------------------------------------------------------------------
void MainWindow::AppendLog(const std::wstring& text)
{
    std::wstring line = text;
    if (line.size() > 4000) line = line.substr(0, 4000) + L" ...";

    WriteLogFile(line);        // --log works even before the log control exists

    if (!log_) return;
    std::wstring line2 = line + L"\r\n";
    int len = ::GetWindowTextLengthW(log_);
    ::SendMessageW(log_, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    ::SendMessageW(log_, EM_REPLACESEL, FALSE, (LPARAM)line2.c_str());
    ::SendMessageW(log_, EM_SCROLLCARET, 0, 0);
}

void MainWindow::WriteLogFile(const std::wstring& text)
{
    if (logFilePath_.empty()) return;
    FILE* fp = _wfopen(logFilePath_.c_str(), L"ab");
    if (!fp) return;
    if (_ftelli64(fp) == 0)
    {
        static const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
        fwrite(bom, 1, sizeof(bom), fp);
    }
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    char stamp[32];
    snprintf(stamp, sizeof(stamp), "[%02d:%02d:%02d.%03d] ",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    fwrite(stamp, 1, strlen(stamp), fp);
    std::string line = WideToUtf8(text);
    fwrite(line.data(), 1, line.size(), fp);
    if (line.empty() || line[line.size() - 1] != '\n') fwrite("\r\n", 1, 2, fp);
    fclose(fp);
}

void MainWindow::SetStatus(const std::wstring& text)
{
    if (status_) ::SetWindowTextW(status_, text.c_str());
}

void MainWindow::Notify(const std::wstring& text, UINT flags)
{
    if (args_.noGui)
    {
        // headless runs must never block on a dialog
        AppendLog(text);
        return;
    }
    ::MessageBoxW(hwnd_, text.c_str(), L"FastVideoCut", flags);
}

void MainWindow::SetBusy(bool busy, const std::wstring& text)
{
    jobRunning_ = busy;
    if (!busy && cancel_.ev) ::ResetEvent(cancel_.ev);
    if (busy)
    {
        ::SendMessageW(progress_, PBM_SETPOS, 0, 0);
        if (!text.empty()) SetStatus(text);
    }
    else
    {
        ::SendMessageW(progress_, PBM_SETPOS, 0, 0);
    }
    UpdateButtonStates();
}

void MainWindow::OnUiMessage(UiMessage* m)
{
    if (!m) return;
    switch (m->kind)
    {
    case UiLog:
        AppendLog(m->text);
        break;

    case UiStatus:
        SetStatus(m->text);
        break;

    case UiProgress:
        if (m->a >= 0) ::SendMessageW(progress_, PBM_SETPOS, (WPARAM)m->a, 0);
        if (!m->text.empty()) SetStatus(m->text);
        break;

    case UiItemUpdated:
        UpdateListRow(m->a);
        timeline_.Refresh();
        UpdateTitles();
        break;

    case UiSelectItem:
        EnsureVisibleItem(m->a);
        break;

    case UiJobDone:
        OnJobFinished(currentJob_, m->a != 0, m->text);
        break;
    }
    delete m;
}

// ---------------------------------------------------------------------------
// commands
// ---------------------------------------------------------------------------
namespace
{
    std::vector<std::wstring> ParseMultiSelect(const wchar_t* buf)
    {
        std::vector<std::wstring> out;
        if (!buf || !buf[0]) return out;
        std::wstring first = buf;
        const wchar_t* p = buf + first.size() + 1;
        if (*p == L'\0')
        {
            out.push_back(first);
            return out;
        }
        while (*p)
        {
            out.push_back(PathCombine(first, p));
            p += wcslen(p) + 1;
        }
        return out;
    }
}

void MainWindow::OnCommand(int id)
{
    switch (id)
    {
    case IDB_ADD:
    case IDM_FILE_ADD:
    {
        std::vector<wchar_t> buf(64 * 1024, 0);
        OPENFILENAMEW ofn;
        ::ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter =
            L"视频文件\0*.mp4;*.mkv;*.mov;*.avi;*.flv;*.wmv;*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.m4v;*.webm;*.rmvb;*.3gp;*.vob;*.mxf\0"
            L"所有文件\0*.*\0\0";
        ofn.lpstrFile = &buf[0];
        ofn.nMaxFile = (DWORD)buf.size();
        ofn.lpstrTitle = L"选择视频文件（可多选）";
        std::wstring initDir = settings_.lastAddDir;
        if (!initDir.empty() && DirectoryExists(initDir)) ofn.lpstrInitialDir = initDir.c_str();
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY;
        if (::GetOpenFileNameW(&ofn))
        {
            std::vector<std::wstring> picked = ParseMultiSelect(&buf[0]);
            OnAddFiles(&picked);
        }
        return;
    }

    case IDM_FILE_ADDDIR:
    {
        BROWSEINFOW bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.hwndOwner = hwnd_;
        bi.lpszTitle = L"选择包含视频文件的文件夹";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
        LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
        if (pidl)
        {
            wchar_t path[MAX_PATH * 2];
            if (::SHGetPathFromIDListW(pidl, path))
            {
                std::vector<std::wstring> one;
                one.push_back(path);
                OnAddFiles(&one);
            }
            ::CoTaskMemFree(pidl);
        }
        return;
    }

    case IDB_REMOVE:
    case IDM_FILE_REMOVE:
    {
        int sel = SelectedItem();
        if (sel >= 0)
        {
            project_.RemoveAt(sel);
            RebuildList();
            int newSel = (sel < (int)project_.items.size()) ? sel : (int)project_.items.size() - 1;
            timeline_.SetCurrentItem(newSel);
            timeline_.Refresh();
        }
        break;
    }

    case IDM_FILE_CLEAR:
        if (!project_.items.empty())
        {
            if (::MessageBoxW(hwnd_, L"确定清空视频列表吗？", L"FastVideoCut",
                              MB_ICONQUESTION | MB_YESNO) != IDYES)
                return;
            project_.Clear();
            RebuildList();
            timeline_.Refresh();
        }
        break;

    case IDB_UP:
    case IDM_VID_UP:
    {
        int sel = SelectedItem();
        if (project_.MoveUp(sel))
        {
            RebuildList();
            LVITEMW item;
            ::ZeroMemory(&item, sizeof(item));
            item.mask = LVIF_STATE;
            item.state = LVIS_SELECTED | LVIS_FOCUSED;
            item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
            ::SendMessageW(list_, LVM_SETITEMSTATE, sel - 1, (LPARAM)&item);
            timeline_.Refresh();
        }
        break;
    }

    case IDB_DOWN:
    case IDM_VID_DOWN:
    {
        int sel = SelectedItem();
        if (project_.MoveDown(sel))
        {
            RebuildList();
            LVITEMW item;
            ::ZeroMemory(&item, sizeof(item));
            item.mask = LVIF_STATE;
            item.state = LVIS_SELECTED | LVIS_FOCUSED;
            item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
            ::SendMessageW(list_, LVM_SETITEMSTATE, sel + 1, (LPARAM)&item);
            timeline_.Refresh();
        }
        break;
    }

    case IDM_VID_SORTNAME:
        project_.SortByName();
        RebuildList();
        timeline_.Refresh();
        break;

    case IDB_DETECT:
    case IDM_VID_DETECT:
        StartDetect(false);
        return;

    case IDM_VID_REDETECT:
        StartDetect(true);
        return;

    case IDB_LISTVIEW:
    case IDM_VIEW_LIST:
        ToggleFileList();
        return;

    case IDB_EXPORT:
    {
        HMENU m = ::CreatePopupMenu();
        ::AppendMenuW(m, MF_STRING, IDM_EXP_EACH, L"每个视频单独导出（切掉未选段，无损）");
        ::AppendMenuW(m, MF_STRING, IDM_EXP_MERGE, L"按列表顺序合并为一个视频（无损）");
        ::AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(m, MF_STRING, IDM_EXP_ALL, L"两个都导出");
        RECT rc;
        ::GetWindowRect(buttons_[5], &rc);
        int cmd = ::TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN,
                                   rc.left, rc.bottom, 0, hwnd_, nullptr);
        ::DestroyMenu(m);
        if (cmd) OnCommand(cmd);
        return;
    }

    case IDM_EXP_EACH:
        StartExport(JobCutEach);
        return;

    case IDM_EXP_MERGE:
        StartExport(JobMergeAll);
        return;

    case IDM_EXP_ALL:
        nextJob_ = JobMergeAll;
        StartExport(JobCutEach);
        return;

    case IDM_EXP_OPEN:
    {
        std::wstring dir = lastOutputDir_.empty() ? settings_.outputDir : lastOutputDir_;
        if (dir.empty() || !DirectoryExists(dir)) dir = PreferredOutputDir();
        ::ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    // ---- selection -------------------------------------------------------
    case IDM_SEL_BODY:
        timeline_.SelectBodyAll();
        break;
    case IDM_SEL_ALL:
    case IDM_VID_RESET:
        timeline_.SelectAllAll();
        break;
    case IDM_SEL_CLEAR:
        timeline_.ClearKeepAll();
        break;

    case IDM_VID_CLEARCACHE:
    case IDB_CLEARCACHE:
        ClearThumbCache();
        timeline_.Refresh();
        AppendLog(L"缩略图缓存已清理");
        break;

    case IDB_SETTINGS:
    case IDM_VID_SETTINGS:
        ShowSettingsDialog();
        break;

    case IDB_INFO:
    case IDM_HELP_INFO:
        ShowInfoDialog();
        break;

    case IDM_HELP_ABOUT:
        ::MessageBoxW(hwnd_,
                      L"FastVideoCut 1.0\n\n"
                      L"用 ffmpeg 做后端的黑屏自动剪辑工具：\n"
                      L"  · 黑屏检测 blackdetect\n"
                      L"  · 帧流缩略图 tile 快速展开\n"
                      L"  · 无损剪切 -c copy + concat 合并\n\n"
                      L"界面: Win32 / C++ (VC++)    后端: ffmpeg.exe",
                      L"关于 FastVideoCut", MB_ICONINFORMATION);
        break;

    // ---- view ------------------------------------------------------------
    case IDM_VIEW_FIT:
        timeline_.FitToWidth();
        break;
    case IDM_VIEW_ZIN:
        timeline_.ZoomBy(1.3, 600);
        break;
    case IDM_VIEW_ZOUT:
        timeline_.ZoomBy(1.0 / 1.3, 600);
        break;
    case IDM_VIEW_THUMB_BIG:
        settings_.thumbHeight = (settings_.thumbHeight + 16 > 240) ? 240 : settings_.thumbHeight + 16;
        timeline_.SetThumbHeight(settings_.thumbHeight);
        SaveSettings(settings_);
        break;
    case IDM_VIEW_THUMB_SMALL:
        settings_.thumbHeight = (settings_.thumbHeight - 16 < 28) ? 28 : settings_.thumbHeight - 16;
        timeline_.SetThumbHeight(settings_.thumbHeight);
        SaveSettings(settings_);
        break;

    case IDM_FILE_EXIT:
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;

    case IDM_EXP_CANCEL:
        CancelJob();
        return;
    default:
        break;
    }

    UpdateTitles();
    UpdateButtonStates();
}

// ---------------------------------------------------------------------------
// jobs
// ---------------------------------------------------------------------------
void MainWindow::StartJobInternal(int job, bool detectAll)
{
    if (jobRunning_) return;
    preview_.Stop();               // decoding is expensive - stop previewing first
    if (worker_.joinable()) worker_.join();

    currentJob_ = job;
    if (cancel_.ev) ::ResetEvent(cancel_.ev);

    if (job == JobDetect)
    {
        std::vector<int> todo = Project::PendingDetect(project_, detectAll);
        int skipped = (int)project_.items.size() - (int)todo.size();

        SetBusy(true, L"正在检测黑屏…");
        if (detectAll)
            AppendLog(FormatString(L"开始检测黑屏：全部 %d 个视频 (d=%.2fs pix_th=%.2f pic_th=%.2f)",
                                   (int)project_.items.size(),
                                   settings_.blackMinDuration, settings_.blackPixTh, settings_.blackPicTh));
        else
            AppendLog(FormatString(L"开始检测黑屏：%d 个待检测，跳过 %d 个已检测 (d=%.2fs pix_th=%.2f pic_th=%.2f)",
                                   (int)todo.size(), skipped,
                                   settings_.blackMinDuration, settings_.blackPixTh, settings_.blackPicTh));

        // 已经检测过的视频保持“已就绪”，不要再退回“读取信息”
        for (int i : todo)
        {
            project_.items[i].status = ItemStatus::Probing;
            project_.items[i].detectProgress = 0.0;
            UpdateListRow(i);
        }
    }
    else
    {
        SetBusy(true, L"正在导出…");
        AppendLog(job == JobCutEach
                      ? L"开始导出：每个视频单独裁剪（切掉未选段，无损流复制）"
                      : L"开始导出：按列表顺序合并为一个视频（无损流复制）");
    }

    timeline_.Refresh();
    UpdateButtonStates();
    worker_ = std::thread(&MainWindow::JobThreadMain, this, job, detectAll);
}

void MainWindow::StartDetect(bool forceAll)
{
    if (jobRunning_) return;
    if (project_.items.empty())
    {
        Notify(L"请先添加视频文件（“添加视频”按钮或直接把文件拖进窗口）。", MB_ICONINFORMATION);
        return;
    }
    if (!ffmpeg_.available())
    {
        Notify(L"没有找到 ffmpeg.exe / ffprobe.exe。\n\n"
               L"请把 ffmpeg 的 bin 目录放到程序目录下，或在“设置”里指定路径。", MB_ICONWARNING);
        return;
    }

    // 默认跳过已经检测过的视频（新增/移除文件后再点检测不会全部重算）
    if (!forceAll && Project::PendingDetect(project_, false).empty())
    {
        if (args_.noGui)
        {
            AppendLog(L"所有视频都已经检测过，无需重复检测（如需强制重检请用 Shift+F6）。");
            return;
        }
        int r = ::MessageBoxW(hwnd_,
                              L"所有视频都已经检测过黑屏了。\n\n"
                              L"是：重新检测全部视频\n否：什么都不做",
                              L"检测黑屏", MB_ICONQUESTION | MB_YESNO);
        if (r != IDYES) return;
        forceAll = true;
    }

    if (args_.autoExport) nextJob_ = args_.mergeAll ? JobMergeAll : JobCutEach;
    StartJobInternal(JobDetect, forceAll);
}

void MainWindow::StartExport(int job)
{
    if (jobRunning_) return;
    if (project_.items.empty())
    {
        Notify(L"请先添加视频文件。", MB_ICONINFORMATION);
        return;
    }
    if (!ffmpeg_.available())
    {
        Notify(L"没有找到 ffmpeg.exe / ffprobe.exe，无法导出。", MB_ICONWARNING);
        return;
    }

    int pending = 0;
    for (size_t i = 0; i < project_.items.size(); ++i)
        if (!project_.items[i].isAnalysed()) ++pending;

    if (pending > 0)
    {
        int r = IDYES;      // headless: detect first, then export
        if (!args_.noGui)
        {
            r = ::MessageBoxW(hwnd_,
                              FormatString(L"还有 %d 个视频没有检测黑屏。\n\n"
                                           L"是：先检测黑屏，然后继续导出\n"
                                           L"否：直接导出（这些视频整段保留）\n"
                                           L"取消：什么都不做", pending).c_str(),
                              L"FastVideoCut", MB_ICONQUESTION | MB_YESNOCANCEL);
        }
        if (r == IDCANCEL) return;
        if (r == IDYES)
        {
            nextJob_ = job;
            StartJobInternal(JobDetect);
            return;
        }
    }

    if (project_.SelectedDuration() <= 0.0)
    {
        Notify(L"当前没有任何选中（高亮）的分段，导出结果会是空的。\n\n"
               L"提示：默认会保留所有非黑屏分段，黑屏分段默认不选中。", MB_ICONINFORMATION);
        return;
    }

    StartJobInternal(job);
}

void MainWindow::CancelJob()
{
    if (!jobRunning_) return;
    if (cancel_.ev) ::SetEvent(cancel_.ev);
    AppendLog(L"已请求取消当前任务…");
    SetStatus(L"正在取消…");
}

void MainWindow::OnJobFinished(int job, bool ok, const std::wstring& summary)
{
    if (worker_.joinable()) worker_.join();

    jobRunning_ = false;
    UpdateButtonStates();
    ::SendMessageW(progress_, PBM_SETPOS, ok ? 100 : 0, 0);

    if (!summary.empty()) AppendLog(summary);
    AppendLog(ok ? L"任务完成。" : L"任务结束（失败或已取消）。");
    if (!ok) exitCode_ = 1;

    int chain = nextJob_;
    nextJob_ = JobNone;

    RebuildList();
    timeline_.Refresh();
    UpdateTitles();

    if (chain != JobNone)
    {
        if (ok)
        {
            StartJobInternal(chain);
            return;
        }
        AppendLog(L"上一个任务失败，跳过后续任务。");
    }

    if (args_.quitOnEnd)
    {
        // headless / --quit: always leave, even when the job failed
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;
    }

    if (ok && (job == JobCutEach || job == JobMergeAll) && !lastOutput_.empty())
    {
        if (settings_.confirmBeforeExport && !args_.noGui)
        {
            int r = ::MessageBoxW(hwnd_,
                                  FormatString(L"导出完成：\n%s\n\n是否打开输出文件夹？",
                                               lastOutput_.c_str()).c_str(),
                                  L"FastVideoCut", MB_ICONINFORMATION | MB_YESNO);
            if (r == IDYES)
            {
                std::wstring dir = lastOutputDir_.empty() ? PathGetDirectory(lastOutput_) : lastOutputDir_;
                ::ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
    }
}

void MainWindow::JobThreadMain(int job, bool detectAll)
{
    bool ok = true;
    std::wstring summary;

    if (job == JobDetect)
    {
        BlackParams bp;
        bp.minDuration  = settings_.blackMinDuration;
        bp.pixThreshold = settings_.blackPixTh;
        bp.picThreshold = settings_.blackPicTh;
        bp.edgeScanSec  = settings_.blackEdgeScan;

        // 默认只检测还没分析过的视频（已经检测过的直接跳过，保留原结果）
        std::vector<int> todo = Project::PendingDetect(project_, detectAll);
        int all = (int)project_.items.size();
        int skipped = all - (int)todo.size();
        int total = (int)todo.size();
        int blackTotal = 0;
        int failed = 0;

        for (int idx = 0; idx < total; ++idx)
        {
            if (cancel_.IsCancelled()) { ok = false; summary = L"检测已取消"; break; }

            int i = todo[(size_t)idx];
            VideoItem& it = project_.items[i];
            it.status = ItemStatus::Probing;
            it.detectProgress = 0.0;
            it.message.clear();
            PostUiMessage(hwnd_, UiItemUpdated, L"", i);
            PostUiMessage(hwnd_, UiStatus,
                          FormatString(L"[%d/%d] 读取信息：%s", idx + 1, total, it.name.c_str()));
            PostUiMessage(hwnd_, UiProgress, L"", (int)((long long)idx * 100 / (total > 0 ? total : 1)), i);

            VideoInfo info;
            std::string err;
            if (!ffmpeg_.Probe(it.path, info, err, cancel_))
            {
                it.status = ItemStatus::Error;
                it.message = L"读取失败: " + Utf8ToWide(err);
                ++failed;
                PostUiMessage(hwnd_, UiItemUpdated, L"", i);
                PostUiMessage(hwnd_, UiLog,
                              FormatString(L"%s 读取失败：%s", it.name.c_str(), it.message.c_str()));
                continue;
            }

            it.info = info;
            it.status = ItemStatus::Detecting;
            PostUiMessage(hwnd_, UiItemUpdated, L"", i);
            PostUiMessage(hwnd_, UiStatus,
                          FormatString(L"[%d/%d] 检测黑屏：%s", idx + 1, total, it.name.c_str()));

            std::vector<BlackRange> blacks;
            std::string derr;
            bool detOk = ffmpeg_.DetectBlack(it.path, info.duration, bp, blacks,
                [&](double p)
                {
                    it.detectProgress = p;
                    PostUiMessage(hwnd_, UiProgress, L"", (int)(p * 100.0 + 0.5), i);
                }, cancel_, derr);

            if (cancel_.IsCancelled()) { ok = false; summary = L"检测已取消"; break; }

            if (!detOk)
                PostUiMessage(hwnd_, UiLog,
                              FormatString(L"%s 黑屏检测失败（按无黑屏处理）：%s",
                                           it.name.c_str(), Utf8ToWide(derr).c_str()));

            it.blacks = blacks;
            Project::RebuildSegments(it, true);       // 保留用户已有的选择
            it.status = ItemStatus::Ready;
            it.detectProgress = 1.0;
            blackTotal += (int)blacks.size();

            PostUiMessage(hwnd_, UiItemUpdated, L"", i);
            PostUiMessage(hwnd_, UiLog,
                          FormatString(L"%s：%s，黑屏 %d 段（合计 %s）",
                                       it.name.c_str(), it.summaryText().c_str(),
                                       (int)blacks.size(),
                                       FormatClock(it.blackDuration()).c_str()));
            if (!blacks.empty())
            {
                PostUiMessage(hwnd_, UiLog,
                              FormatString(L"  · 黑屏位置：%s",
                                           BlackRangesText(blacks, 12).c_str()));
            }
            PostUiMessage(hwnd_, UiSelectItem, L"", i);
        }

        if (cancel_.IsCancelled())
            ok = false;
        else
            ok = (failed == 0);

        if (summary.empty())
            summary = FormatString(L"检测结束：%d 个视频（跳过 %d 个已检测），黑屏共 %d 段（失败 %d 个）",
                                   total, skipped, blackTotal, failed);
    }
    else if (job == JobOpenOnly)
    {
        summary = L"输出目录：\n" + lastOutputDir_;
    }
    else
    {
        std::vector<std::wstring> segments;
        std::vector<std::wstring> outputs;
        std::wstring err;
        PostUiMessage(hwnd_, UiStatus, job == JobCutEach ? L"正在导出（单独裁剪）…" : L"正在导出（合并）…");
        ok = BuildOutputs(job, segments, outputs, err);
        if (!ok)
        {
            summary = err;
        }
        else
        {
            if (job == JobCutEach)
            {
                summary = FormatString(L"导出完成，共 %d 个文件：", (int)outputs.size());
                for (size_t i = 0; i < outputs.size(); ++i)
                    summary += L"\n  " + outputs[i];
            }
            else
            {
                summary = FormatString(L"合并完成：%s", lastOutput_.c_str());
            }
        }
    }

    PostUiMessage(hwnd_, UiProgress, L"", ok ? 100 : 0, -1);
    PostUiMessage(hwnd_, UiJobDone, summary, ok ? 1 : 0, 0);
}

// ---------------------------------------------------------------------------
// export
// ---------------------------------------------------------------------------
bool MainWindow::BuildOutputs(int job,
                              std::vector<std::wstring>& segments,
                              std::vector<std::wstring>& outputs,
                              std::wstring& err)
{
    err.clear();
    segments.clear();
    outputs.clear();

    EncodeOptions enc;
    enc.reencode  = settings_.reencodeExport;
    enc.crf       = settings_.crf;
    enc.preset    = settings_.preset;
    enc.faststart = settings_.faststart;

    std::wstring outDir = PreferredOutputDir();
    if (!EnsureDirectory(outDir))
    {
        err = L"无法创建输出目录：" + outDir;
        return false;
    }
    lastOutputDir_ = outDir;

    std::wstring workDir = PathCombine(GetLocalAppDataDir(), L"FastVideoCut\\tmp");
    DeleteDirectoryRecursive(workDir);
    if (!EnsureDirectory(workDir))
    {
        err = L"无法创建临时目录：" + workDir;
        return false;
    }

    int total = (int)project_.items.size();
    int seq = 0;
    int produced = 0;
    std::vector<std::wstring> mergeParts;

    for (int i = 0; i < total; ++i)
    {
        if (cancel_.IsCancelled()) { err = L"导出已取消"; return false; }

        VideoItem& it = project_.items[i];
        PostUiMessage(hwnd_, UiProgress, L"", (int)((long long)i * 100 / (total > 0 ? total : 1)), i);
        PostUiMessage(hwnd_, UiStatus, FormatString(L"[%d/%d] 处理：%s", i + 1, total, it.name.c_str()));

        if (it.info.duration <= 0.0)
        {
            PostUiMessage(hwnd_, UiLog, it.name + L"：没有读取到视频信息，跳过");
            continue;
        }
        if (it.selectedSegmentCount() == 0)
        {
            PostUiMessage(hwnd_, UiLog, it.name + L"：没有选中任何分段，跳过");
            continue;
        }

        std::vector<std::wstring> parts;

        if (it.hasContiguousFullSelection())
        {
            parts.push_back(it.path);       // 整段保留：直接使用源文件
            PostUiMessage(hwnd_, UiLog, it.name + L"：整段保留（无需裁剪）");
        }
        else
        {
            // 连续的选中片段合并成一次裁切；起点回退到前一个黑屏段并对齐关键帧，
            // 否则切出来的文件开头会"只有声音没有画面"。
            std::string notes;
            std::vector<KeepRun> runs;
            if (enc.reencode) runs = it.selectedRuns();      // 重编码可以精确切
            else              runs = Project::BuildCopyRuns(ffmpeg_, it, &notes, cancel_);

            if (!notes.empty())
            {
                std::vector<std::string> noteLines = SplitOutputLines(notes);
                for (size_t n = 0; n < noteLines.size(); ++n)
                {
                    if (noteLines[n].empty()) continue;
                    PostUiMessage(hwnd_, UiLog,
                                  FormatString(L"  · %s", Utf8ToWide(noteLines[n]).c_str()));
                }
            }

            if ((int)runs.size() != it.selectedSegmentCount())
            {
                PostUiMessage(hwnd_, UiLog,
                              FormatString(L"  · %d 个连续片段合并为 %d 次裁切",
                                           it.selectedSegmentCount(), (int)runs.size()));
            }
            else
            {
                PostUiMessage(hwnd_, UiLog,
                              FormatString(L"  · %d 次裁切", (int)runs.size()));
            }

            int idx = 0;
            for (size_t k = 0; k < runs.size(); ++k)
            {
                if (cancel_.IsCancelled()) { err = L"导出已取消"; return false; }
                const KeepRun& s = runs[k];
                ++idx;
                ++seq;

                std::wstring seg = PathCombine(workDir, FormatString(L"seg_%04d.mp4", seq));
                PostUiMessage(hwnd_, UiStatus,
                              FormatString(L"[%d/%d] %s 第 %d/%d 段 %s → %s",
                                           i + 1, total, it.name.c_str(), idx, (int)runs.size(),
                                           FormatClock(s.t0).c_str(), FormatClock(s.t1).c_str()));

                std::string e;
                if (!ffmpeg_.Trim(it.path, s.t0, s.t1, seg, enc, e))
                {
                    err = FormatString(L"裁剪失败：%s（%s - %s）\n%s",
                                       it.name.c_str(), FormatClock(s.t0).c_str(),
                                       FormatClock(s.t1).c_str(), Utf8ToWide(e).c_str());
                    DeleteDirectoryRecursive(workDir);
                    return false;
                }
                segments.push_back(seg);
                parts.push_back(seg);
            }
        }

        if (parts.empty()) continue;

        if (job == JobCutEach)
        {
            std::wstring out = UniquePath(PathCombine(outDir,
                                       PathGetFileNameNoExt(it.name) + L"_cut.mp4"));
            bool okOne = false;
            std::string e;

            if (parts.size() == 1)
            {
                if (parts[0] == it.path)
                {
                    okOne = ::CopyFileW(it.path.c_str(), out.c_str(), FALSE) != 0;
                    if (!okOne) e = "copy failed";
                }
                else
                {
                    if (::MoveFileExW(parts[0].c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING))
                        okOne = true;
                    else
                    {
                        okOne = ::CopyFileW(parts[0].c_str(), out.c_str(), FALSE) != 0;
                        if (!okOne) e = "move/copy failed";
                    }
                }
            }
            else
            {
                std::wstring listFile = PathCombine(workDir, FormatString(L"list_%04d.txt", i));
                okOne = ffmpeg_.Concat(parts, listFile, out, enc, e);
            }

            if (!okOne)
            {
                err = L"输出失败：" + out + L"\n" + Utf8ToWide(e);
                DeleteDirectoryRecursive(workDir);
                return false;
            }
            outputs.push_back(out);
            lastOutput_ = out;
            PostUiMessage(hwnd_, UiLog, L"  → " + out);
        }
        else
        {
            for (size_t k = 0; k < parts.size(); ++k) mergeParts.push_back(parts[k]);
        }
        ++produced;
    }

    if (job == JobMergeAll)
    {
        if (mergeParts.empty())
        {
            err = L"没有可合并的内容（没有选中任何分段）。";
            DeleteDirectoryRecursive(workDir);
            return false;
        }

        std::vector<std::wstring> concatInputs = mergeParts;
        EncodeOptions concatEnc = enc;

        if (enc.reencode)
        {
            // 参数不一致时，先统一分辨率/帧率/音频，再用流复制拼接
            int w = 0, h = 0;
            double fps = 0.0;
            for (size_t i = 0; i < project_.items.size(); ++i)
            {
                if (project_.items[i].info.width > w)  w = project_.items[i].info.width;
                if (project_.items[i].info.height > h) h = project_.items[i].info.height;
                if (fps <= 0.0 && project_.items[i].info.fps > 0.0) fps = project_.items[i].info.fps;
            }
            if (w < 2) w = 1920;
            if (h < 2) h = 1080;
            if (fps <= 0.0) fps = 25.0;
            w &= ~1;
            h &= ~1;

            std::vector<std::wstring> norm;
            for (size_t i = 0; i < concatInputs.size(); ++i)
            {
                if (cancel_.IsCancelled()) { err = L"导出已取消"; DeleteDirectoryRecursive(workDir); return false; }
                PostUiMessage(hwnd_, UiStatus,
                              FormatString(L"统一格式 %d/%d …", (int)i + 1, (int)concatInputs.size()));
                std::wstring n = PathCombine(workDir, FormatString(L"norm_%04d.mp4", (int)i));
                std::string e;
                if (!ffmpeg_.Normalize(concatInputs[i], n, w, h, fps, true, enc, e))
                {
                    err = L"统一格式失败：\n" + Utf8ToWide(e);
                    DeleteDirectoryRecursive(workDir);
                    return false;
                }
                segments.push_back(n);
                norm.push_back(n);
            }
            concatInputs = norm;
            concatEnc.reencode = false;     // 归一化之后可以无损拼接
        }

        std::wstring name = SanitizeFileName(settings_.mergeFileName);
        if (name.empty()) name = L"merged.mp4";
        if (PathGetExtension(name).empty()) name += L".mp4";

        std::wstring out = UniquePath(PathCombine(outDir, name));
        std::wstring listFile = PathCombine(workDir, L"concat_list.txt");

        PostUiMessage(hwnd_, UiStatus, FormatString(L"拼接 %d 段 …", (int)concatInputs.size()));
        std::string e;
        if (!ffmpeg_.Concat(concatInputs, listFile, out, concatEnc, e))
        {
            err = L"合并失败：\n" + Utf8ToWide(e);
            DeleteDirectoryRecursive(workDir);
            return false;
        }
        outputs.push_back(out);
        lastOutput_ = out;
        PostUiMessage(hwnd_, UiLog, L"  → " + out);
    }

    PostUiMessage(hwnd_, UiProgress, L"", 100, -1);
    DeleteDirectoryRecursive(workDir);
    if (produced == 0)
    {
        err = L"没有任何视频被导出（请检查选中情况）。";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// output paths / misc dialogs
// ---------------------------------------------------------------------------
std::wstring MainWindow::PreferredOutputDir() const
{
    if (!args_.output.empty())
    {
        std::wstring p = PathGetFull(args_.output);
        if (DirectoryExists(p)) return p;
        if (PathGetExtension(p).empty())
        {
            EnsureDirectory(p);
            return p;
        }
        return PathGetDirectory(p);
    }
    if (!settings_.outputDir.empty()) return settings_.outputDir;
    return DefaultOutputDir();
}

std::wstring MainWindow::MakeOutputPath(int index) const
{
    std::wstring dir = PreferredOutputDir();
    std::wstring base = L"clip";
    if (index >= 0 && index < (int)project_.items.size())
        base = PathGetFileNameNoExt(project_.items[index].name);
    return UniquePath(PathCombine(dir, base + L"_cut.mp4"));
}

bool MainWindow::AskForOutputDir()
{
    BROWSEINFOW bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd_;
    bi.lpszTitle = L"选择导出目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH * 2];
    bool ok = ::SHGetPathFromIDListW(pidl, path) != FALSE;
    ::CoTaskMemFree(pidl);
    if (ok)
    {
        settings_.outputDir = path;
        SaveSettings(settings_);
    }
    return ok;
}

void MainWindow::ShowSettingsDialog()
{
    if (ShowSettingsDialogModal(hwnd_, settings_, ffmpeg_))
    {
        settingsDirty_ = true;
        SaveSettings(settings_);
        timeline_.SetThumbHeight(settings_.thumbHeight);
        UpdateTitles();
        UpdateButtonStates();
        AppendLog(L"设置已保存");
    }
}

void MainWindow::ShowInfoDialog()
{
    ::MessageBoxW(hwnd_,
        L"FastVideoCut 使用说明\n"
        L"─────────────────────────────\n"
        L"1) 添加视频：点“添加视频”或把文件/文件夹直接拖进窗口。每个视频占一行。\n\n"
        L"2) 检测黑屏：点“检测黑屏”（F6）。程序用 ffmpeg 的 blackdetect 滤镜找出每一段黑屏的\n"
        L"   起止时间与长度，显示在“时长/黑屏段/黑屏时长”列，并直接画在帧流上\n"
        L"   （红色斜纹 = 黑屏段）。黑屏只是帮你快速找到“片头片尾”与“主体”的\n"
        L"   分界线，它本身并不是要删掉的内容。\n"
        L"   · 默认只检测还没检测过的视频，新增/移除文件后再点不会全部重算\n"
        L"   · 需要重算全部时用 Shift+F6 或菜单“视频 → 重新检测全部黑屏”\n\n"
        L"3) 决定保留哪些片段（帧流上点击即可，左键 = 起点，右键 = 终点）：\n"
        L"      · 左键点击分段 = 指定保留起点（标“起”）\n"
        L"      · 右键点击分段 = 指定保留终点（标“止”）\n"
        L"      · 两端之间（含）的所有分段都保留，其余默认删除\n"
        L"      · Ctrl+左键 = 单独保留某一段；再点一次取消\n"
        L"      · 双击分段 = 只保留这一段\n"
        L"      · 左键或右键点击都会在右侧预览窗播放该分段\n"
        L"      · 菜单“选择”里可用 Ctrl+B 直接选中首末非黑屏段之间的主体\n"
        L"      · 灰色 = 会被切掉的部分\n\n"
        L"4) 导出：\n"
        L"      · “每个视频单独导出”：把没选中的段落全部切掉，每个视频输出一个文件。\n"
        L"      · “按列表顺序合并”：把所有视频按列表顺序拼成一个大视频。\n"
        L"   默认使用 ffmpeg 的流复制（-c copy），无损、快速；\n"
        L"   如遇编码参数不一致导致失败，可在“设置”里勾选“导出时重新编码”。\n\n"
        L"5) 视图操作：\n"
        L"      · 工具栏“列表”按钮（Ctrl+L）在“文件列表”和“视频列表（帧流）”之间切换，\n"
        L"        文件列表框默认隐藏；文件列表里能看到“起始时间 / 结束时间”两列\n"
        L"      · 滚轮 = 上下滚动视频（视频多时用），Ctrl+滚轮 = 缩放\n"
        L"      · Shift+滚轮 / 中键拖动 = 横向平移，F5 = 适应窗口\n\n"
        L"6) ffmpeg 位置：优先使用程序目录或上级目录里带 ffmpeg 的 bin 目录，\n"
        L"   其次 PATH；也可以在“设置”里手动指定。\n\n"
        L"开源授权：FastVideoCut 基于 GNU GPL v3.0 或更高版本发布，\n"
        L"Copyright (C) 2026 dzdhome。可自由使用与修改；对外分发时\n"
        L"必须附带许可证全文并提供完整源码（https://github.com/dzdhome/FastVideoCut）。\n"
        L"本程序按“不附带任何担保”提供。ffmpeg 为独立进程调用，未静态链接、不随本程序分发。",
        L"FastVideoCut 使用说明", MB_ICONINFORMATION);
}

// __FVC_MAINWND_CHUNK_END__
