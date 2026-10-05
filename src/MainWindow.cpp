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
#include "Loc.h"
#include "Sound.h"

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

namespace
{
    // 顺序必须和 ToolBtn 枚举一致（LayoutChildren 里的 widths 数组也按它排）。
    // 这里既不能做成全局数组（TR() 不是常量表达式，不能用于静态初始化），
    // 也不能缓存成 static —— 界面语言会在运行中改，缓存会把旧语言留住。
    // 只有建窗口和切换语言时各调一次，构造开销可以忽略。
    const BtnDef* ToolbarButtons(BtnDef* storage)
    {
        BtnDef* b = storage;
        b[TB_Add] = { IDB_ADD,
                      TR(L"添加视频", L"Add"),
                      TR(L"添加视频文件 (Ctrl+O)，也可以直接把文件拖到窗口",
                         L"Add video files (Ctrl+O), or drag them onto the window") };
        b[TB_Remove] = { IDB_REMOVE,
                         TR(L"移除", L"Remove"),
                         TR(L"移除列表中选中的视频", L"Remove the selected video") };
        b[TB_Clear] = { IDB_CLEARLIST,
                        TR(L"清空列表", L"Clear"),
                        TR(L"清空整个视频列表（不删除磁盘上的文件）",
                           L"Clear the whole list (files on disk are kept)") };
        b[TB_Up] = { IDB_UP,
                     TR(L"上移", L"Up"),
                     TR(L"把选中的视频在列表里上移", L"Move the selected video up") };
        b[TB_Down] = { IDB_DOWN,
                       TR(L"下移", L"Down"),
                       TR(L"把选中的视频在列表里下移", L"Move the selected video down") };
        b[TB_Detect] = { IDB_DETECT,
                         TR(L"自动分析", L"Analyse"),
                         TR(L"用 ffmpeg blackdetect 自动分析所有视频的黑屏位置 (F6)\n分析进行中点击可停止",
                            L"Find black frames with ffmpeg blackdetect (F6)\nClick again while running to stop") };
        b[TB_Export] = { IDB_EXPORT,
                         TR(L"导出剪辑", L"Export"),
                         TR(L"按选择导出：单文件裁剪或按顺序合并 (F7)",
                            L"Export the selection: cut each file or merge (F7)") };
        b[TB_ClearCache] = { IDB_CLEARCACHE,
                             TR(L"清理缩略图", L"Clear thumbs"),
                             TR(L"清除磁盘上的缩略图缓存（下次重新生成）",
                                L"Delete the thumbnail cache on disk") };
        b[TB_Settings] = { IDB_SETTINGS,
                           TR(L"设置", L"Settings"),
                           TR(L"配置界面语言、ffmpeg 路径、黑屏检测参数与导出参数",
                              L"Language, ffmpeg path, black detect and export options") };
        b[TB_Info] = { IDB_INFO,
                       TR(L"说明", L"Help"),
                       TR(L"查看使用说明", L"Show the usage guide") };
        b[TB_List] = { IDB_LISTVIEW,
                       TR(L"列表", L"List"),
                       TR(L"左侧切换：文件列表 / 视频列表（帧流）\n文件列表框默认隐藏，点击切换 (Ctrl+L)",
                          L"Left pane: file list / video strip\nThe file list is hidden by default (Ctrl+L)") };
        return b;
    }

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
            out += FormatString(TR(L" …等共 %d 段", L" ... %d in total"), (int)ranges.size());
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

    // 语言必须在建任何窗口之前定下来：控件文字、菜单、工具提示都在创建时取一次
    // TR()。Auto = 首次启动按系统区域自动选中文或英文。
    Loc::Apply(settings_.lang);

    if (!args_.ffmpegDir.empty()) settings_.ffmpegDir = args_.ffmpegDir;
    if (args_.thumbHeight > 0)    settings_.thumbHeight = args_.thumbHeight;
    if (args_.blackMin > 0.0)     settings_.blackMinDuration = args_.blackMin;
    if (args_.blackPix > 0.0)     settings_.blackPixTh = args_.blackPix;
    if (args_.blackPic > 0.0)     settings_.blackPicTh = args_.blackPic;
    // --scan-window 一次设置两侧，--scan-head/--scan-tail 可再单独覆盖。
    // 0 = 该侧完全不扫，负数 = 该侧不限制（整段）。兼容旧版：--scan-window 0
    // 过去表示“整段扫描”，这里仍按整段处理，老脚本不会突然什么都不扫。
    if (args_.scanWindow != kScanUnset)
    {
        double v = (args_.scanWindow == 0.0) ? -1.0 : args_.scanWindow;
        settings_.blackHeadScan = v;
        settings_.blackTailScan = v;
    }
    if (args_.scanHead != kScanUnset) settings_.blackHeadScan = args_.scanHead;
    if (args_.scanTail != kScanUnset) settings_.blackTailScan = args_.scanTail;
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

    hwnd_ = ::CreateWindowExW(0, FVC_CLASS_NAME,
                              TR(L"FastVideoCut - 黑屏自动剪辑工具 (ffmpeg 无损剪切)",
                                 L"FastVideoCut - black frame cutter (ffmpeg lossless trim)"),
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
    Sound::Shutdown();            // let a ringing chime finish instead of cutting it off
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
    const BtnDef* btns = ToolbarButtons(btnDefs_);
    buttonCount_ = TB_Count;
    for (int i = 0; i < buttonCount_; ++i)
    {
        buttons_[i] = ::CreateWindowExW(0, L"BUTTON", btns[i].text,
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                        0, 0, 10, 10, hwnd_,
                                        (HMENU)(INT_PTR)btns[i].id, hInst_, nullptr);
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
        { L"#",            36,  LVCFMT_RIGHT  },
        { TR(L"文件", L"File"),           250, LVCFMT_LEFT   },
        { TR(L"状态", L"Status"),          96,  LVCFMT_LEFT   },
        { TR(L"时长", L"Length"),          80,  LVCFMT_RIGHT  },
        { TR(L"分辨率", L"Resolution"),    86,  LVCFMT_LEFT   },
        { TR(L"规格", L"Format"),          96,  LVCFMT_LEFT   },   // HDR/SDR + 位深
        { TR(L"黑屏段", L"Black"),         60,  LVCFMT_RIGHT  },
        { TR(L"黑屏时长", L"Black len"),   80,  LVCFMT_RIGHT  },
        { TR(L"起始时间", L"Start"),      104, LVCFMT_RIGHT  },
        { TR(L"结束时间", L"End"),        104, LVCFMT_RIGHT  },
        { TR(L"保留时长", L"Kept len"),    80,  LVCFMT_RIGHT  },
        { TR(L"已选段", L"Segments"),      64,  LVCFMT_RIGHT  },
        { TR(L"大小", L"Size"),            82,  LVCFMT_RIGHT  }
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
    // 缩略图开关必须在 Create 之前设置好（默认关闭：只做黑屏检测）
    timeline_.SetThumbsEnabled(settings_.makeThumbs);
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
    if (buttons_[TB_List])
        ::SetWindowTextW(buttons_[TB_List], showList_ ? TR(L"视频", L"Videos")
                                                      : TR(L"列表", L"List"));

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
    AppendLog(showList_ ? TR(L"左侧切换为文件列表（隐藏视频列表）",
                                    L"Left pane: file list (video strip hidden)")
                      : TR(L"左侧切换为视频列表（隐藏文件列表框）",
                           L"Left pane: video strip (file list hidden)"));
}

void MainWindow::BuildMenu()
{
    HMENU bar = ::CreateMenu();

    HMENU file = ::CreatePopupMenu();
    ::AppendMenuW(file, MF_STRING, IDM_FILE_ADD,      TR(L"添加视频文件(&A)...\tCtrl+O", L"Add video file(s)(&A)...\tCtrl+O"));
    ::AppendMenuW(file, MF_STRING, IDM_FILE_ADDDIR,   TR(L"添加整个文件夹(&D)...", L"Add a folder(&D)..."));
    ::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(file, MF_STRING, IDM_FILE_REMOVE,   TR(L"移除选中(&R)", L"Remove selected(&R)"));
    ::AppendMenuW(file, MF_STRING, IDM_FILE_CLEAR,    TR(L"清空列表(&C)", L"Clear the list(&C)"));
    ::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(file, MF_STRING, IDM_FILE_EXIT,     TR(L"退出(&X)", L"Exit(&X)"));
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, TR(L"文件(&F)", L"File(&F)"));

    HMENU vid = ::CreatePopupMenu();
    ::AppendMenuW(vid, MF_STRING, IDM_VID_DETECT,     TR(L"检测黑屏（跳过已检测）(&B)\tF6", L"Detect black (skip done)(&B)\tF6"));
    ::AppendMenuW(vid, MF_STRING, IDM_VID_REDETECT,   TR(L"重新检测全部黑屏(&R)\tShift+F6", L"Re-detect everything(&R)\tShift+F6"));
    ::AppendMenuW(vid, MF_STRING, IDM_VID_ANALYZE_ONE, TR(L"重新分析选中的视频(&N)\tCtrl+F6", L"Re-analyse selected(&N)\tCtrl+F6"));
    ::AppendMenuW(vid, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(vid, MF_STRING, IDM_EXP_CANCEL,     TR(L"停止当前分析(&C)\tEsc", L"Stop the current job(&C)\tEsc"));
    ::AppendMenuW(vid, MF_STRING, IDM_VID_UP,         TR(L"上移(&U)", L"Move up(&U)"));
    ::AppendMenuW(vid, MF_STRING, IDM_VID_DOWN,       TR(L"下移(&W)", L"Move down(&W)"));
    ::AppendMenuW(vid, MF_STRING, IDM_VID_SORTNAME,   TR(L"按文件名排序(&S)", L"Sort by name(&S)"));
    ::AppendMenuW(vid, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(vid, MF_STRING, IDM_VID_RESET,      TR(L"重置选择（整段保留）(&T)", L"Reset selection (keep all)(&T)"));
    ::AppendMenuW(vid, MF_STRING, IDM_VID_CLEARCACHE, TR(L"清理缩略图缓存(&L)", L"Clear thumbnail cache(&L)"));
    ::AppendMenuW(vid, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(vid, MF_STRING, IDM_VID_SETTINGS,   TR(L"设置(&G)...", L"Settings(&G)..."));
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)vid, TR(L"视频(&V)", L"Video(&V)"));
    vidMenu_ = vid;

    HMENU sel = ::CreatePopupMenu();
    ::AppendMenuW(sel, MF_STRING, IDM_SEL_BODY,  TR(L"保留主体（首末非黑屏段之间）(&B)\tCtrl+B", L"Keep the body(&B)\tCtrl+B"));
    ::AppendMenuW(sel, MF_STRING, IDM_SEL_ALL,   TR(L"整段保留（含黑屏）(&K)\tCtrl+A", L"Keep everything(&K)\tCtrl+A"));
    ::AppendMenuW(sel, MF_STRING, IDM_SEL_CLEAR, TR(L"清除选择(&C)\tCtrl+R", L"Clear selection(&C)\tCtrl+R"));
    ::AppendMenuW(sel, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(sel, MF_STRING, 0,
                  TR(L"左键=保留起点，右键=保留终点，Ctrl+左键=单段保留",
                     L"Left click = keep start, right click = keep end, Ctrl+click = one segment"));
    ::EnableMenuItem(sel, GetMenuItemCount(sel) - 1, MF_BYPOSITION | MF_GRAYED);
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)sel, TR(L"选择(&S)", L"Select(&S)"));

    HMENU exp = ::CreatePopupMenu();
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_EACH,  TR(L"每个视频单独导出（切掉未选段，无损）(&E)\tF7", L"Export each video (lossless)(&E)\tF7"));
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_MERGE, TR(L"按列表顺序合并为一个视频（无损）(&M)\tF8", L"Merge into one file (lossless)(&M)\tF8"));
    ::AppendMenuW(exp, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_ALL,   TR(L"两个都导出(&B)", L"Do both(&B)"));
    ::AppendMenuW(exp, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_OPEN,  TR(L"打开输出文件夹(&D)", L"Open the output folder(&D)"));
    ::AppendMenuW(exp, MF_STRING, IDM_EXP_CANCEL,TR(L"取消当前任务(&C)\tEsc", L"Cancel the current job(&C)\tEsc"));
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)exp, TR(L"导出(&E)", L"Export(&E)"));

    HMENU view = ::CreatePopupMenu();
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_LIST,       TR(L"显示文件列表框(&L)\tCtrl+L", L"Show the file list(&L)\tCtrl+L"));
    ::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_FIT,        TR(L"适应窗口(&F)\tF5", L"Fit to window(&F)\tF5"));
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_ZIN,        TR(L"放大(&I)", L"Zoom in(&I)"));
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_ZOUT,       TR(L"缩小(&O)", L"Zoom out(&O)"));
    ::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_THUMB_BIG,  TR(L"缩略图更大(&B)", L"Bigger thumbnails(&B)"));
    ::AppendMenuW(view, MF_STRING, IDM_VIEW_THUMB_SMALL,TR(L"缩略图更小(&S)", L"Smaller thumbnails(&S)"));
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, TR(L"视图(&W)", L"View(&W)"));
    viewMenu_ = view;

    HMENU help = ::CreatePopupMenu();
    ::AppendMenuW(help, MF_STRING, IDM_HELP_INFO,  TR(L"使用说明(&H)", L"Usage guide(&H)"));
    ::AppendMenuW(help, MF_STRING, IDM_HELP_ABOUT, TR(L"关于(&A)", L"About(&A)"));
    ::AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, TR(L"帮助(&H)", L"Help(&H)"));

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
    // 顺序与 ToolbarButtons() 一致：添加/移除/清空/上移/下移/自动分析/导出/
    // 清理缩略图/设置/说明/列表
    int widths[] = { 96, 62, 78, 62, 62, 92, 92, 104, 62, 62, 74 };
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
                                  TR(L"当前还有任务在运行，确定要退出吗？",
                                     L"A job is still running. Quit anyway?"),
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
            // 双击 = 重新分析这一个视频（已分析过的也强制重跑）
            StartDetectOne(SelectedItem());
            return 0;
        }
        if (nh && nh->idFrom == IDC_LIST && nh->code == LVN_KEYDOWN)
        {
            NMLVKEYDOWN* kd = (NMLVKEYDOWN*)lp;
            if (kd->wVKey == VK_DELETE) { OnCommand(IDM_FILE_REMOVE); return 0; }
        }
        if (nh && nh->idFrom == IDC_LIST && nh->code == NM_RCLICK)
        {
            // 右键：把右键所在的那一行设为选中，再弹出“重新分析该视频”菜单
            NMITEMACTIVATE* act = (NMITEMACTIVATE*)lp;
            if (act && act->iItem >= 0 && act->iItem < (int)project_.items.size())
            {
                int cur = SelectedItem();
                if (cur != act->iItem)
                {
                    ::SendMessageW(list_, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)nullptr);
                    LVITEMW item;
                    ::ZeroMemory(&item, sizeof(item));
                    item.mask = LVIF_STATE;
                    item.state = LVIS_SELECTED | LVIS_FOCUSED;
                    item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
                    ::SendMessageW(list_, LVM_SETITEMSTATE, (WPARAM)act->iItem, (LPARAM)&item);
                    timeline_.SetCurrentItem(act->iItem);
                }

                HMENU m = ::CreatePopupMenu();
                ::AppendMenuW(m, MF_STRING, IDM_VID_ANALYZE_ONE,
                              TR(L"重新分析这个视频(&N)", L"Re-analyse this video(&N)"));
                ::AppendMenuW(m, MF_STRING, IDM_SEL_BODY,
                              TR(L"保留主体（首末非黑屏段之间）(&B)", L"Keep the body(&B)"));
                ::AppendMenuW(m, MF_STRING, IDM_SEL_ALL,
                              TR(L"整段保留（含黑屏）(&K)", L"Keep everything(&K)"));
                ::AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                ::AppendMenuW(m, MF_STRING, IDM_FILE_REMOVE, TR(L"移除(&R)", L"Remove(&R)"));
                POINT pt;
                ::GetCursorPos(&pt);
                int cmd = (int)::TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN,
                                                pt.x, pt.y, 0, hwnd_, nullptr);
                ::DestroyMenu(m);
                if (cmd) OnCommand(cmd);
            }
            return 0;
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

// 重建整个列表。selectRow >= 0 时把该行设为选中（移动/排序后要保持选中项
// 跟着视频走；传 -1 表示不主动选中，由调用方自己处理）。
// 之前这里无条件选中第 0 行，导致上移/下移后高亮跑回顶部，看起来像“选中没跟着走”。
void MainWindow::RebuildList(int selectRow)
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

    int row = selectRow;
    if (row < 0 && !project_.items.empty()) row = 0;   // 默认仍选中第一行
    if (row >= 0 && row < (int)project_.items.size())
    {
        // 先清空所有选中/焦点，再只选中目标行：多选或旧选中残留都会让高亮看起来错位
        LVITEMW item;
        ::ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_STATE;
        item.state = 0;
        item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        ::SendMessageW(list_, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&item);

        ::ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_STATE;
        item.state = LVIS_SELECTED | LVIS_FOCUSED;
        item.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        ::SendMessageW(list_, LVM_SETITEMSTATE, (WPARAM)row, (LPARAM)&item);
        ::SendMessageW(list_, LVM_ENSUREVISIBLE, (WPARAM)row, FALSE);

        timeline_.SetCurrentItem(row);
    }
}

void MainWindow::UpdateListRow(int index)
{
    if (!list_ || index < 0 || index >= (int)project_.items.size()) return;
    const VideoItem& it = project_.items[index];

    std::wstring texts[12];
    texts[0] = it.name;
    texts[1] = it.statusText();
    texts[2] = it.info.duration > 0.0 ? FormatClock(it.info.duration) : L"-";
    texts[3] = it.info.width > 0 ? FormatString(L"%dx%d", it.info.width, it.info.height) : L"-";
    // HDR/SDR + 位深（未分析时还没有 ffprobe 结果）
    texts[4] = it.info.valid() ? Utf8ToWide(it.info.formatLabel()) : L"-";
    texts[5] = FormatString(L"%d", it.blackCount());
    texts[6] = FormatClock(it.blackDuration());
    // 选择起始时间 / 结束时间（还没有点选过时显示 “-”）
    double keepT0 = it.keepStartTime();
    double keepT1 = it.keepEndTime();
    texts[7] = keepT0 >= 0.0 ? FormatTimecode(keepT0) : L"-";
    texts[8] = keepT1 >= 0.0 ? FormatTimecode(keepT1) : L"-";
    texts[9] = FormatClock(it.selectedDuration());
    texts[10] = FormatString(L"%d/%d", it.selectedSegmentCount(), (int)it.segments.size());
    texts[11] = it.info.sizeBytes > 0 ? FormatSize(it.info.sizeBytes) : L"-";

    for (int i = 0; i < 12; ++i)
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
    // 标题栏不再显示黑屏段数（导出前的分析中间量，对用户没有决策价值）
    std::wstring title = FormatString(
        TR(L"FastVideoCut - %d 个视频 | 总时长 %s | 保留 %s",
           L"FastVideoCut - %d videos | total %s | kept %s"),
        n, FormatClock(total).c_str(), FormatClock(keep).c_str());
    ::SetWindowTextW(hwnd_, title.c_str());

    std::wstring ffName = ffmpeg_.available()
                              ? PathGetFileName(ffmpeg_.paths().ffmpeg)
                              : std::wstring(TR(L"未找到 (请在设置里指定)",
                                                L"not found (set it in Settings)"));
    double pct = (total > 0.0) ? (keep * 100.0 / total) : 0.0;
    SetStatus(FormatString(TR(L"已选 %d 段 · 保留 %s (%.1f%%) · ffmpeg: %s",
                               L"%d segments kept - %s (%.1f%%) - ffmpeg: %s"),
                           project_.SelectedSegmentCount(), FormatClock(keep).c_str(),
                           pct, ffName.c_str()));

    ::SetWindowTextW(help_,
        TR(L"左键分段 = 保留起点，右键分段 = 保留终点（两者之间全部保留）| Ctrl+左键 = 单段保留/取消 | "
           L"双击 = 只保留该段 | 列表双击/右键 = 重新分析这个视频 | 点击任一分段都会在右侧预览播放 | "
           L"滚轮 = 上下滚动视频 | Ctrl+滚轮 = 缩放，Shift+滚轮 = 横向平移，中键拖动 = 平移 | "
           L"工具栏“列表” = 文件列表/视频列表切换",
           L"Left click = keep start, right click = keep end | Ctrl+click = toggle one segment | "
           L"double click = keep only that segment | double click / right click a row = re-analyse it | "
           L"clicking a segment previews it on the right | wheel = scroll videos | Ctrl+wheel = zoom | "
           L"Shift+wheel or middle drag = pan | toolbar “List” = file list / video strip"));
}

void MainWindow::UpdateButtonStates()
{
    bool busy     = jobRunning_;
    bool hasItems = !project_.items.empty();
    bool hasSel   = project_.SelectedSegmentCount() > 0;
    bool hasFf    = ffmpeg_.available();
    int  sel      = SelectedItem();
    bool detecting = busy && currentJob_ == JobDetect;

    // 分析进行中：工具栏按钮变成“停止分析”并保持可点，随时可以中断
    if (buttonCount_ >= 5 && buttons_[TB_Detect])
        ::SetWindowTextW(buttons_[TB_Detect], detecting ? TR(L"停止分析", L"Stop")
                                                    : TR(L"自动分析", L"Analyse"));

    ::EnableWindow(buttons_[TB_Add],    !busy);
    ::EnableWindow(buttons_[TB_Remove], !busy && hasItems);
    ::EnableWindow(buttons_[TB_Clear],  !busy && hasItems);
    ::EnableWindow(buttons_[TB_Up],     !busy && sel > 0);
    ::EnableWindow(buttons_[TB_Down],   !busy && sel >= 0 && sel + 1 < (int)project_.items.size());
    ::EnableWindow(buttons_[TB_Detect], detecting || (!busy && hasItems && hasFf));
    ::EnableWindow(buttons_[TB_Export], !busy && hasItems && hasFf && hasSel);
    // 没开“生成缩略图”时，清理缩略图缓存没有意义，直接置灰
    ::EnableWindow(buttons_[TB_ClearCache], !busy && settings_.makeThumbs);
    ::EnableWindow(buttons_[TB_Settings],   !busy);
    ::EnableWindow(buttons_[TB_Info],      TRUE);
    ::EnableWindow(buttons_[TB_List],      TRUE);      // 列表/视频 视图切换随时可用

    if (vidMenu_)
    {
        // 单个重分析需要选中一行，且任务空闲、ffmpeg 可用
        bool canOne = !busy && hasFf && sel >= 0;
        ::EnableMenuItem(vidMenu_, IDM_VID_ANALYZE_ONE,
                         MF_BYCOMMAND | (canOne ? MF_ENABLED : MF_GRAYED));
        ::EnableMenuItem(vidMenu_, IDM_EXP_CANCEL,
                         MF_BYCOMMAND | (busy ? MF_ENABLED : MF_GRAYED));
        ::EnableMenuItem(vidMenu_, IDM_VID_CLEARCACHE,
                         MF_BYCOMMAND | (settings_.makeThumbs ? MF_ENABLED : MF_GRAYED));
    }
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
        AppendLog(FormatString(TR(L"添加 %d 个视频（重复 %d，不支持 %d）",
                                L"added %d video(s) (%d duplicate, %d unsupported)"),
                           added, dup, bad));
    }
    else
    {
        AppendLog(FormatString(TR(L"没有添加视频（重复 %d，不支持/不存在 %d）",
                                L"nothing added (%d duplicate, %d unsupported or missing)"), dup, bad));
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
            AppendLog(FormatString(TR(L"保留：%s → %s  （%d/%d 段，共 %s）",
                                L"keep: %s -> %s  (%d/%d segments, %s)"),
                                   it.name.c_str(), range.c_str(),
                                   it.selectedSegmentCount(), (int)it.segments.size(),
                                   FormatClock(it.selectedDuration()).c_str()));
        }
        return;
    }

    if (itemIndex < 0)
    {
        int n = (int)project_.items.size();
        AppendLog(FormatString(TR(L"保留：已更新 %d 个视频，合计保留 %s",
                                L"keep: updated %d video(s), %s kept in total"),
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
                  FormatString(TR(L"%s · 第 %d 段%s", L"%s - part %d%s"),
                               it.name.c_str(), segIndex + 1,
                               s.kind == SegKind::Black ? TR(L"（黑屏）", L" (black)") : L""));
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
            TR(L"视频文件\0*.mp4;*.mkv;*.mov;*.avi;*.flv;*.wmv;*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.m4v;*.webm;*.rmvb;*.3gp;*.vob;*.mxf\0"
           L"所有文件\0*.*\0\0",
           L"Video files\0*.mp4;*.mkv;*.mov;*.avi;*.flv;*.wmv;*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.m4v;*.webm;*.rmvb;*.3gp;*.vob;*.mxf\0"
           L"All files\0*.*\0\0");
        ofn.lpstrFile = &buf[0];
        ofn.nMaxFile = (DWORD)buf.size();
        ofn.lpstrTitle = TR(L"选择视频文件（可多选）", L"Pick video files (multi-select)");
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
        bi.lpszTitle = TR(L"选择包含视频文件的文件夹", L"Pick a folder that contains video files");
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

    case IDB_CLEARLIST:
    case IDM_FILE_CLEAR:
        if (!project_.items.empty())
        {
            if (::MessageBoxW(hwnd_,
                              TR(L"确定清空视频列表吗？",
                                 L"Clear the whole video list?"),
                              L"FastVideoCut",
                              MB_ICONQUESTION | MB_YESNO) != IDYES)
                return;
            project_.Clear();
            RebuildList();
            timeline_.SetCurrentItem(-1);
            timeline_.Refresh();
            AppendLog(TR(L"已清空视频列表", L"Video list cleared"));
        }
        break;

    case IDB_UP:
    case IDM_VID_UP:
    {
        int sel = SelectedItem();
        if (project_.MoveUp(sel))
        {
            // 视频已经换到上一行，选中必须跟着它走，否则高亮留在原地造成错觉
            RebuildList(sel - 1);
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
            RebuildList(sel + 1);
            timeline_.Refresh();
        }
        break;
    }

    case IDM_VID_SORTNAME:
        project_.SortByName();
        RebuildList(SelectedItem());
        timeline_.Refresh();
        break;

    case IDB_DETECT:
    case IDM_VID_DETECT:
        // 分析中再点一次 = 停止分析（按钮已变成“停止分析”）
        if (jobRunning_ && currentJob_ == JobDetect)
        {
            CancelJob();
            return;
        }
        StartDetect(false);
        return;

    case IDM_VID_REDETECT:
        if (jobRunning_) { CancelJob(); return; }
        StartDetect(true);
        return;

    case IDM_VID_ANALYZE_ONE:
        StartDetectOne(SelectedItem());
        return;

    case IDB_LISTVIEW:
    case IDM_VIEW_LIST:
        ToggleFileList();
        return;

    case IDB_EXPORT:
    {
        HMENU m = ::CreatePopupMenu();
        ::AppendMenuW(m, MF_STRING, IDM_EXP_EACH, TR(L"每个视频单独导出（切掉未选段，无损）", L"Export each video (lossless)"));
        ::AppendMenuW(m, MF_STRING, IDM_EXP_MERGE, TR(L"按列表顺序合并为一个视频（无损）", L"Merge into one file (lossless)"));
        ::AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(m, MF_STRING, IDM_EXP_ALL, TR(L"两个都导出", L"Do both"));
        RECT rc;
        ::GetWindowRect(buttons_[TB_Export], &rc);
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
        AppendLog(TR(L"缩略图缓存已清理", L"Thumbnail cache cleared"));
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
                      TR(L"FastVideoCut 1.4.0\n\n"
                         L"用 ffmpeg 做后端的黑屏自动剪辑工具：\n"
                         L"  · 黑屏检测 blackdetect\n"
                         L"  · 帧流缩略图 tile 快速展开\n"
                         L"  · 无损剪切 -c copy + concat 合并\n\n"
                         L"界面: Win32 / C++ (VC++)    后端: ffmpeg.exe",
                         L"FastVideoCut 1.4.0\n\n"
                         L"Black frame auto cutter built on ffmpeg:\n"
                         L"  - black frame detection (blackdetect)\n"
                         L"  - timeline thumbnails via tile mosaics\n"
                         L"  - lossless trimming (-c copy) + concat merge\n\n"
                         L"UI: Win32 / C++      Backend: ffmpeg.exe"),
                      TR(L"关于 FastVideoCut", L"About FastVideoCut"), MB_ICONINFORMATION);
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
void MainWindow::StartJobInternal(int job, bool detectAll, int onlyIndex)
{
    if (jobRunning_) return;
    preview_.Stop();               // decoding is expensive - stop previewing first
    if (worker_.joinable()) worker_.join();

    currentJob_ = job;
    detectOnlyIndex_ = onlyIndex;
    if (cancel_.ev) ::ResetEvent(cancel_.ev);

    if (job == JobDetect)
    {
        // 只重跑一个视频：forceAll 语义，todo 只有它自己
        std::vector<int> todo;
        if (onlyIndex >= 0)
        {
            if (onlyIndex >= (int)project_.items.size()) return;
            todo.push_back(onlyIndex);
        }
        else
        {
            todo = Project::PendingDetect(project_, detectAll);
        }
        int skipped = (int)project_.items.size() - (int)todo.size();

        SetBusy(true, TR(L"正在检测黑屏…", L"Analysing black frames..."));
        if (onlyIndex >= 0)
            AppendLog(FormatString(TR(L"开始重新分析单个视频：%s (d=%.2fs pix_th=%.2f pic_th=%.2f)",
                               L"Re-analysing one video: %s (d=%.2fs pix_th=%.2f pic_th=%.2f)"),
                                   project_.items[(size_t)onlyIndex].name.c_str(),
                                   settings_.blackMinDuration, settings_.blackPixTh,
                                   settings_.blackPicTh));
        else if (detectAll)
            AppendLog(FormatString(TR(L"开始检测黑屏：全部 %d 个视频 (d=%.2fs pix_th=%.2f pic_th=%.2f)",
                               L"Analysing all %d video(s) (d=%.2fs pix_th=%.2f pic_th=%.2f)"),
                                   (int)project_.items.size(),
                                   settings_.blackMinDuration, settings_.blackPixTh, settings_.blackPicTh));
        else
            AppendLog(FormatString(TR(L"开始检测黑屏：%d 个待检测，跳过 %d 个已检测 (d=%.2fs pix_th=%.2f pic_th=%.2f)",
                               L"Analysing: %d to do, %d already done (d=%.2fs pix_th=%.2f pic_th=%.2f)"),
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
        SetBusy(true, TR(L"正在导出…", L"Exporting..."));
        AppendLog(job == JobCutEach
                      ? TR(L"开始导出：每个视频单独裁剪（切掉未选段，无损流复制）",
                           L"Export: cut each video separately (lossless stream copy)")
                      : TR(L"开始导出：按列表顺序合并为一个视频（无损流复制）",
                           L"Export: merge all videos in list order (lossless stream copy)"));
    }

    timeline_.Refresh();
    UpdateButtonStates();
    worker_ = std::thread(&MainWindow::JobThreadMain, this, job, detectAll, onlyIndex);
}

void MainWindow::StartDetect(bool forceAll)
{
    if (jobRunning_) return;
    if (project_.items.empty())
    {
        Notify(TR(L"请先添加视频文件（“添加视频”按钮或直接把文件拖进窗口）。",
                    L"Add some video files first (button or drag & drop)."), MB_ICONINFORMATION);
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
            AppendLog(TR(L"所有视频都已经检测过，无需重复检测（如需强制重检请用 Shift+F6）。",
                   L"Every video was analysed already (use Shift+F6 to force a full re-check)."));
            return;
        }
        int r = ::MessageBoxW(hwnd_,
                              TR(L"所有视频都已经检测过黑屏了。\n\n"
                                 L"是：重新检测全部视频\n否：什么都不做",
                                 L"Every video was analysed already.\n\n"
                                 L"Yes: re-analyse all of them\nNo: do nothing"),
                              TR(L"检测黑屏", L"Detect black"), MB_ICONQUESTION | MB_YESNO);
        if (r != IDYES) return;
        forceAll = true;
    }

    if (args_.autoExport) nextJob_ = args_.mergeAll ? JobMergeAll : JobCutEach;
    StartJobInternal(JobDetect, forceAll);
}

// 重新分析单个视频：只跑这一条，其它视频的检测结果保持不动。
void MainWindow::StartDetectOne(int index)
{
    if (jobRunning_) return;
    if (index < 0 || index >= (int)project_.items.size())
    {
        Notify(TR(L"请先在列表里选中一个视频。", L"Select a video in the list first."),
             MB_ICONINFORMATION);
        return;
    }
    if (!ffmpeg_.available())
    {
        Notify(TR(L"没有找到 ffmpeg.exe / ffprobe.exe。\n\n"
                     L"请把 ffmpeg 的 bin 目录放到程序目录下，或在“设置”里指定路径。",
                     L"ffmpeg.exe / ffprobe.exe not found.\n\n"
                     L"Put the ffmpeg bin folder next to the exe, or set it in Settings."),
                 MB_ICONWARNING);
        return;
    }

    // 不打断“检测完自动导出”的批处理链
    StartJobInternal(JobDetect, true, index);
}

// 合并前的确认框。MessageBoxW 的按钮文字和排列顺序是系统写死的，没法把
// “智能合并”放到最左边，所以自己画一个对话框模板（settings.rc）。
// 两个模式共用一个模板：remuxFixable = false 时把“智能合并”藏掉，
// 剩下两个按钮往左挪，占住原来的位置。
namespace
{
    struct MergeAskCtx
    {
        const std::wstring* text;
        bool                 remuxFixable;
    };

    // 把一个子控件挪到客户区里的某个位置
    void MoveTo(HWND ctl, int x, int y)
    {
        ::SetWindowPos(ctl, nullptr, x, y, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // 读出控件在对话框客户区里的左上角
    POINT ClientTopLeft(HWND dlg, HWND ctl)
    {
        RECT rc;
        ::GetWindowRect(ctl, &rc);
        POINT p = { rc.left, rc.top };
        ::ScreenToClient(dlg, &p);
        return p;
    }

    INT_PTR CALLBACK MergeAskProc(HWND dlg, UINT uMsg, WPARAM wp, LPARAM lp)
    {
        switch (uMsg)
        {
        case WM_INITDIALOG:
        {
            MergeAskCtx* c = (MergeAskCtx*)lp;
            if (c && c->text)
                ::SetDlgItemTextW(dlg, IDC_MERGE_TEXT, c->text->c_str());

            // 警告图标放标题栏（体内那个 SS_ICON 静态控件实测画不出来，见 settings.rc 注释）
            ::SendMessageW(dlg, WM_SETICON, ICON_BIG,
                           (LPARAM)::LoadIconW(nullptr, IDI_WARNING));

            HWND smart  = ::GetDlgItem(dlg, IDC_MERGE_SMART);
            HWND cancel = ::GetDlgItem(dlg, IDC_MERGE_CANCEL);
            HWND anyway = ::GetDlgItem(dlg, IDC_MERGE_ANYWAY);

            const bool canRemux = (c && c->remuxFixable);
            if (canRemux)
            {
                ::SetWindowTextW(smart,  TR(L"智能合并", L"Merge smartly"));
                ::SetWindowTextW(cancel, TR(L"取消合并", L"Cancel merge"));
                ::SetWindowTextW(anyway, TR(L"强行合并", L"Merge anyway"));
                // 回车 = 推荐做法，转封装只换容器不重编码，几秒就好
                ::SetFocus(smart);
                ::SendMessageW(dlg, DM_SETDEFID, IDC_MERGE_SMART, 0);
            }
            else
            {
                // 只有编码参数不一致时，转封装解决不了，给了也是骗人
                ::ShowWindow(smart, SW_HIDE);
                // 两个按钮各占“智能合并”和自己原来的位置：既顶到最左边，
                // 又不会在中间留一个大空洞（只挪“取消合并”会留下 ~200px 空白）
                const POINT pSmart = ClientTopLeft(dlg, smart);
                const POINT pCancel = ClientTopLeft(dlg, cancel);
                MoveTo(cancel, pSmart.x, pSmart.y);
                MoveTo(anyway, pCancel.x, pCancel.y);
                ::SetWindowTextW(cancel, TR(L"取消合并", L"Cancel merge"));
                ::SetWindowTextW(anyway, TR(L"强行合并", L"Merge anyway"));
                // 这次两个按钮都不该是默认动作，回车 = 安全地取消
                ::SetFocus(cancel);
                ::SendMessageW(dlg, DM_SETDEFID, IDC_MERGE_CANCEL, 0);
            }
            return TRUE;
        }

        case WM_COMMAND:
            switch (LOWORD(wp))
            {
            case IDC_MERGE_SMART:
            case IDC_MERGE_CANCEL:
            case IDC_MERGE_ANYWAY:
                ::EndDialog(dlg, LOWORD(wp));
                return TRUE;
            }
            break;

        case WM_CLOSE:
            ::EndDialog(dlg, IDC_MERGE_CANCEL);   // 关窗等同取消合并
            return TRUE;
        }
        return FALSE;
    }
}

// 无损合并前逐项比对参与合并的视频格式。返回用户的选择：
//   · Proceed     —— 差异可以接受（或差异只在容器层面且用户不想转封装），照常合并
//   · Cancel      —— 取消这次导出
//   · RemuxFirst  —— 先把源转封装统一成 MP4，再走原来的无损合并
// --nogui / --auto-export 下不能弹框，只把差异写进日志并放行。
MergePlan MainWindow::ConfirmMergeFormats()
{
    if (args_.noGui) return MergePlan::Proceed;

    // 只有真正要拼到一起（多个视频合成一个）才需要问。单个视频走的是“裁切”路径，
    // 参数本来就要按它自己来，不存在拼接错配。
    if (args_.mergeAll || project_.items.size() < 2) return MergePlan::Proceed;

    // 重编码会先统一分辨率/帧率/音频，容器和时基也跟着重写，不需要问。
    if (settings_.mergeReencode) return MergePlan::Proceed;

    std::vector<const VideoInfo*> infos;
    for (const VideoItem& it : project_.items)
        if (!it.path.empty()) infos.push_back(&it.info);
    if (infos.size() < 2) return MergePlan::Proceed;

    std::vector<FormatMismatch> diffs;
    if (VideoFormatsMatch(infos, diffs)) return MergePlan::Proceed;

    const std::wstring list = DescribeFormatMismatch(diffs);

    // 差异全是容器/时基/布局这类时，多给一条“智能合并”的出路：只换容器不重编码，
    // 几秒就能让 concat 对上时基。真正的编码/分辨率差异只能重编码，给了也没用。
    const bool canRemux = MismatchIsRemuxFixable(diffs);

    std::wstring msg = canRemux
        ? std::wstring(TR(L"列表里的视频封装格式或时基不一致，直接无损合并会算错时间轴：\n"
                          L"导出文件的时长会比实际长很多，超出的部分没有内容、播不出来。",
                          L"The videos in the list have different containers or time bases. A plain lossless merge "
                          L"computes a wrong timeline: the exported file will be far longer than the real content, "
                          L"and the extra part will not play."))
        : std::wstring(TR(L"列表里的视频格式不一致，直接无损合并会花屏或断音。",
                          L"The videos in the list do not match; merging them with a plain lossless copy will produce "
                          L"corrupted video or broken audio."));
    msg += L"\n\n" + list;

    AppendLog(std::wstring(TR(L"合并前检查：", L"Merge pre-check: ")) + list);

    MergeAskCtx ctx{ &msg, canRemux };
    INT_PTR r = ::DialogBoxParamW(::GetModuleHandleW(nullptr),
                                  MAKEINTRESOURCEW(IDD_MERGEASK), hwnd_,
                                  MergeAskProc, (LPARAM)&ctx);

    if (r == IDC_MERGE_SMART)  return MergePlan::RemuxFirst;
    if (r == IDC_MERGE_ANYWAY) return MergePlan::Proceed;
    return MergePlan::Cancel;      // 取消合并，或直接关了窗口
}

void MainWindow::StartExport(int job)
{
    if (jobRunning_) return;
    if (project_.items.empty())
    {
        Notify(TR(L"请先添加视频文件。", L"Add some video files first."), MB_ICONINFORMATION);
        return;
    }
    if (!ffmpeg_.available())
    {
        Notify(TR(L"没有找到 ffmpeg.exe / ffprobe.exe，无法导出。",
                     L"ffmpeg.exe / ffprobe.exe not found, cannot export."),
                 MB_ICONWARNING);
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
                              FormatString(TR(L"还有 %d 个视频没有检测黑屏。\n\n"
                                           L"是：先检测黑屏，然后继续导出\n"
                                           L"否：直接导出（这些视频整段保留）\n"
                                           L"取消：什么都不做",
                                           L"%d video(s) have not been analysed yet.\n\n"
                                           L"Yes: analyse them first, then export\n"
                                           L"No: export anyway (those are kept whole)\n"
                                           L"Cancel: do nothing"), pending).c_str(),
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
        Notify(TR(L"当前没有任何选中（高亮）的分段，导出结果会是空的。\n\n"
                    L"提示：默认会保留所有非黑屏分段，黑屏分段默认不选中。",
                    L"No segment is selected, so the export would be empty.\n\n"
                    L"Tip: by default every non-black segment is kept, black ones are not."),
                 MB_ICONINFORMATION);
        return;
    }

    // 多个视频要拼成一个时，先问用户格式对不对得上
    if (job == JobMergeAll)
    {
        MergePlan plan = ConfirmMergeFormats();
        if (plan == MergePlan::Cancel)
        {
            AppendLog(TR(L"已取消合并。", L"Merge cancelled."));
            return;
        }
        mergePlan_ = plan;
    }

    StartJobInternal(job);
}

void MainWindow::CancelJob()
{
    if (!jobRunning_) return;
    if (cancel_.ev) ::SetEvent(cancel_.ev);
    AppendLog(TR(L"已请求取消当前任务…", L"Cancel requested..."));
    SetStatus(TR(L"正在取消…", L"Cancelling..."));
}

void MainWindow::OnJobFinished(int job, bool ok, const std::wstring& summary)
{
    if (worker_.joinable()) worker_.join();

    jobRunning_ = false;
    detectOnlyIndex_ = -1;
    UpdateButtonStates();
    ::SendMessageW(progress_, PBM_SETPOS, ok ? 100 : 0, 0);

    if (!summary.empty()) AppendLog(summary);
    AppendLog(ok ? TR(L"任务完成。", L"Task finished.")
                   : TR(L"任务结束（失败或已取消）。", L"Task ended (failed or cancelled)."));
    if (!ok) exitCode_ = 1;

    int chain = nextJob_;
    nextJob_ = JobNone;

    // 任务跑完的提示音：分析完成“叮铃铃”，导出完成“叮咚咚”（设置里可关）。
    // 几种情况不出声：
    //   · 失败或被取消 —— 响了反而像成功，误导；日志里已经写明结果了
    //   · 后面还接着任务（“两个都导出”、--auto-export 的分析→导出）——
    //     等整串任务跑完再响一次，不然两声会撞在一起
    //   · --nogui / --quit —— 没窗口或者马上就要退出，响了也听不见
    if (ok && chain == JobNone && !args_.noGui && !args_.quitOnEnd)
    {
        if (job == JobDetect && settings_.soundDetectDone)
            Sound::PlayDetectDone();
        else if ((job == JobCutEach || job == JobMergeAll) && settings_.soundExportDone)
            Sound::PlayExportDone();
    }

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
        AppendLog(TR(L"上一个任务失败，跳过后续任务。",
                   L"The previous task failed, skipping the rest."));
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
                                  FormatString(TR(L"导出完成：\n%s\n\n是否打开输出文件夹？",
                                             L"Export finished:\n%s\n\nOpen the output folder?"),
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

void MainWindow::JobThreadMain(int job, bool detectAll, int onlyIndex)
{
    bool ok = true;
    std::wstring summary;

    if (job == JobDetect)
    {
        BlackParams bp;
        bp.minDuration  = settings_.blackMinDuration;
        bp.pixThreshold = settings_.blackPixTh;
        bp.picThreshold = settings_.blackPicTh;
        bp.headScanSec  = settings_.blackHeadScan;
        bp.tailScanSec  = settings_.blackTailScan;

        // 默认只检测还没分析过的视频（已经检测过的直接跳过，保留原结果）；
        // onlyIndex >= 0 时只重跑这一个。
        std::vector<int> todo;
        if (onlyIndex >= 0)
        {
            if (onlyIndex < (int)project_.items.size()) todo.push_back(onlyIndex);
        }
        else
        {
            todo = Project::PendingDetect(project_, detectAll);
        }
        int all = (int)project_.items.size();
        int skipped = all - (int)todo.size();
        int total = (int)todo.size();
        int blackTotal = 0;
        int failed = 0;
        double totalBlackDuration = 0.0;

        for (int idx = 0; idx < total; ++idx)
        {
            if (cancel_.IsCancelled()) { ok = false; summary = TR(L"检测已取消", L"Detection cancelled"); break; }

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
                it.message = TR(L"读取失败: ", L"Probe failed: ") + Utf8ToWide(err);
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

            if (cancel_.IsCancelled()) { ok = false; summary = TR(L"检测已取消", L"Detection cancelled"); break; }

            if (!detOk)
                PostUiMessage(hwnd_, UiLog,
                              FormatString(TR(L"%s 黑屏检测失败（按无黑屏处理）：%s",
                                     L"%s black detection failed (treated as no black): %s"),
                                           it.name.c_str(), Utf8ToWide(derr).c_str()));

            it.blacks = blacks;
            Project::RebuildSegments(it, true);       // 保留用户已有的选择
            it.status = ItemStatus::Ready;
            it.detectProgress = 1.0;
            blackTotal += (int)blacks.size();
            totalBlackDuration += it.blackDuration();

            PostUiMessage(hwnd_, UiItemUpdated, L"", i);
            PostUiMessage(hwnd_, UiLog,
                          FormatString(TR(L"%s：%s，%s，黑屏 %d 段（合计 %s）",
                                       L"%s: %s, %s, %d black segment(s) (%s)"),
                                       it.name.c_str(), it.summaryText().c_str(),
                                       Utf8ToWide(it.info.formatLabel()).c_str(),
                                       (int)blacks.size(),
                                       FormatClock(it.blackDuration()).c_str()));
            if (!blacks.empty())
            {
                PostUiMessage(hwnd_, UiLog,
                              FormatString(TR(L"  · 黑屏位置：%s", L"  - black ranges: %s"),
                                           BlackRangesText(blacks, 12).c_str()));
            }
            PostUiMessage(hwnd_, UiSelectItem, L"", i);
        }

        if (cancel_.IsCancelled())
            ok = false;
        else
            ok = (failed == 0);

        if (summary.empty())
        {
            if (onlyIndex >= 0)
                summary = FormatString(TR(L"重新分析完成：黑屏 %d 段（%s）",
                                 L"Re-analysis finished: %d black segment(s) (%s)"),
                                       blackTotal, FormatClock(totalBlackDuration).c_str());
            else
                summary = FormatString(TR(L"检测结束：%d 个视频（跳过 %d 个已检测），黑屏共 %d 段（失败 %d 个）",
                                 L"Detection finished: %d video(s) (%d skipped), %d black segment(s), %d failed"),
                                       total, skipped, blackTotal, failed);
        }
    }
    else if (job == JobOpenOnly)
    {
        summary = TR(L"输出目录：\n", L"Output folder:\n") + lastOutputDir_;
    }
    else
    {
        std::vector<std::wstring> segments;
        std::vector<std::wstring> outputs;
        std::wstring err;
        PostUiMessage(hwnd_, UiStatus,
                      job == JobCutEach ? TR(L"正在导出（单独裁剪）…", L"Exporting (cut each)...")
                                       : TR(L"正在导出（合并）…", L"Exporting (merge)..."));
        ok = BuildOutputs(job, segments, outputs, err);
        if (!ok)
        {
            summary = err;
        }
        else
        {
            if (job == JobCutEach)
            {
                summary = FormatString(TR(L"导出完成，共 %d 个文件：", L"Export finished, %d file(s):"), (int)outputs.size());
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
        err = TR(L"无法创建输出目录：", L"cannot create the output folder: ") + outDir;
        return false;
    }
    lastOutputDir_ = outDir;

    std::wstring workDir = PathCombine(GetLocalAppDataDir(), L"FastVideoCut\\tmp");
    DeleteDirectoryRecursive(workDir);
    if (!EnsureDirectory(workDir))
    {
        err = TR(L"无法创建临时目录：", L"cannot create the temp folder: ") + workDir;
        return false;
    }

    int total = (int)project_.items.size();
    int seq = 0;
    int produced = 0;
    std::vector<std::wstring> mergeParts;

    for (int i = 0; i < total; ++i)
    {
        if (cancel_.IsCancelled()) { err = TR(L"导出已取消", L"Export cancelled"); return false; }

        VideoItem& it = project_.items[i];
        PostUiMessage(hwnd_, UiProgress, L"", (int)((long long)i * 100 / (total > 0 ? total : 1)), i);
        PostUiMessage(hwnd_, UiStatus, FormatString(L"[%d/%d] 处理：%s", i + 1, total, it.name.c_str()));

        if (it.info.duration <= 0.0)
        {
            PostUiMessage(hwnd_, UiLog, it.name + TR(L"：没有读取到视频信息，跳过",
                                                     L": no video info, skipped"));
            continue;
        }
        if (it.selectedSegmentCount() == 0)
        {
            PostUiMessage(hwnd_, UiLog, it.name + TR(L"：没有选中任何分段，跳过",
                                                     L": nothing selected, skipped"));
            continue;
        }

        std::vector<std::wstring> parts;

        if (it.hasContiguousFullSelection())
        {
            parts.push_back(it.path);       // 整段保留：直接使用源文件
            PostUiMessage(hwnd_, UiLog, it.name + TR(L"：整段保留（无需裁剪）",
                                                     L": kept whole (nothing to cut)"));
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
                              FormatString(TR(L"  · %d 个连续片段合并为 %d 次裁切",
                           L"  - %d adjacent runs merged into %d cuts"),
                                           it.selectedSegmentCount(), (int)runs.size()));
            }
            else
            {
                PostUiMessage(hwnd_, UiLog,
                              FormatString(TR(L"  · %d 次裁切", L"  - %d cut(s)"), (int)runs.size()));
            }

            int idx = 0;
            for (size_t k = 0; k < runs.size(); ++k)
            {
                if (cancel_.IsCancelled()) { err = TR(L"导出已取消", L"Export cancelled"); return false; }
                const KeepRun& s = runs[k];
                ++idx;
                ++seq;

                std::wstring seg = PathCombine(workDir, FormatString(L"seg_%04d.mp4", seq));
                PostUiMessage(hwnd_, UiStatus,
                              FormatString(TR(L"[%d/%d] %s 第 %d/%d 段 %s → %s",
                                         L"[%d/%d] %s part %d/%d %s -> %s"),
                                           i + 1, total, it.name.c_str(), idx, (int)runs.size(),
                                           FormatClock(s.t0).c_str(), FormatClock(s.t1).c_str()));

                std::string e;
                if (!ffmpeg_.Trim(it.path, s.t0, s.t1, seg, enc, e))
                {
                    err = FormatString(TR(L"裁剪失败：%s（%s - %s）\n%s",
                               L"Trim failed: %s (%s - %s)\n%s"),
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
                err = TR(L"输出失败：", L"Write failed: ") + out + L"\n" + Utf8ToWide(e);
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
            err = TR(L"没有可合并的内容（没有选中任何分段）。",
             L"Nothing to merge (no segment is selected).");
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
                if (cancel_.IsCancelled()) { err = TR(L"导出已取消", L"Export cancelled"); DeleteDirectoryRecursive(workDir); return false; }
                PostUiMessage(hwnd_, UiStatus,
                              FormatString(TR(L"统一格式 %d/%d …", L"Normalising %d/%d ..."), (int)i + 1, (int)concatInputs.size()));
                std::wstring n = PathCombine(workDir, FormatString(L"norm_%04d.mp4", (int)i));
                std::string e;
                if (!ffmpeg_.Normalize(concatInputs[i], n, w, h, fps, true, enc, e))
                {
                    err = TR(L"统一格式失败：\n", L"Normalising failed:\n") + Utf8ToWide(e);
                    DeleteDirectoryRecursive(workDir);
                    return false;
                }
                segments.push_back(n);
                norm.push_back(n);
            }
            concatInputs = norm;
            concatEnc.reencode = false;     // 归一化之后可以无损拼接
        }
        else if (mergePlan_ == MergePlan::RemuxFirst)
        {
            // 用户选了“先快速转封装”。只换容器、统一时基，不动码流（-c copy），
            // 几秒就好；源文件不动，中间文件都在临时目录里。
            std::vector<std::wstring> remuxed;
            for (size_t i = 0; i < concatInputs.size(); ++i)
            {
                if (cancel_.IsCancelled())
                {
                    err = TR(L"导出已取消", L"Export cancelled");
                    DeleteDirectoryRecursive(workDir);
                    return false;
                }
                PostUiMessage(hwnd_, UiStatus,
                              FormatString(TR(L"快速转封装 %d/%d …", L"Remuxing %d/%d ..."),
                                           (int)i + 1, (int)concatInputs.size()));
                std::wstring n = PathCombine(workDir, FormatString(L"remux_%04d.mp4", (int)i));
                std::string e;
                if (!ffmpeg_.RemuxToMp4(concatInputs[i], n, cancel_, e))
                {
                    err = TR(L"快速转封装失败：\n", L"Remuxing failed:\n") + Utf8ToWide(e);
                    DeleteDirectoryRecursive(workDir);
                    return false;
                }
                segments.push_back(n);
                remuxed.push_back(n);
            }
            concatInputs = remuxed;
            concatEnc.reencode = false;
        }

        // 合并文件名按“第一个 + 最后一个”视频自动生成（公共前缀只写一次）：
        // 001…010 -> 001-010.mp4；视频001…视频010 -> 视频001-010.mp4。
        // 只统计真正参与合并的视频（跳过了裁剪的、没有保留片段的都不算）。
        std::wstring firstName, lastName;
        for (size_t i = 0; i < project_.items.size(); ++i)
        {
            const VideoItem& it = project_.items[i];
            if (it.selectedSegmentCount() == 0) continue;
            if (firstName.empty()) firstName = it.name;
            lastName = it.name;
        }
        if (firstName.empty() && !concatInputs.empty())
        {
            firstName = concatInputs.front();
            lastName  = concatInputs.back();
        }

        std::wstring name = SanitizeFileName(MakeMergeName(firstName, lastName, L".mp4"));
        if (name.empty() || name == L".mp4" || name == L"merged") name = L"merged.mp4";

        std::wstring out = UniquePath(PathCombine(outDir, name));
        std::wstring listFile = PathCombine(workDir, L"concat_list.txt");

        PostUiMessage(hwnd_, UiStatus, FormatString(TR(L"拼接 %d 段 …", L"Concatenating %d part(s) ..."), (int)concatInputs.size()));
        std::string e;
        if (!ffmpeg_.Concat(concatInputs, listFile, out, concatEnc, e))
        {
            err = TR(L"合并失败：\n", L"Merge failed:\n") + Utf8ToWide(e);
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
        err = TR(L"没有任何视频被导出（请检查选中情况）。",
             L"No video was exported (check the selection).");
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
    bi.lpszTitle = TR(L"选择导出目录", L"Pick the output folder");
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
    const AppLang oldLang    = settings_.lang;
    const bool    oldThumbs  = settings_.makeThumbs;
    if (ShowSettingsDialogModal(hwnd_, settings_, ffmpeg_))
    {
        settingsDirty_ = true;
        SaveSettings(settings_);
        timeline_.SetThumbHeight(settings_.thumbHeight);
        timeline_.SetThumbsEnabled(settings_.makeThumbs);
        if (settings_.lang != oldLang)
        {
            // 语言变了：立刻重建所有静态文字，不要求重启
            Loc::Apply(settings_.lang);
            ApplyLanguage();
            AppendLog(FormatString(L"界面语言：%s", Loc::Describe(settings_.lang).c_str()));
        }
        if (settings_.makeThumbs != oldThumbs)
        {
            AppendLog(settings_.makeThumbs
                          ? TR(L"已开启缩略图生成", L"Thumbnail generation enabled")
                          : TR(L"已关闭缩略图生成（只检测黑屏）",
                               L"Thumbnail generation disabled (black detection only)"));
        }
        UpdateTitles();
        UpdateButtonStates();
        AppendLog(TR(L"设置已保存", L"Settings saved"));
    }
}

// 语言切换后把菜单、工具栏按钮、列表表头、状态栏、帮助行全部重新写一遍。
// 菜单必须整个重建：菜单项文字是创建时定下的，改文字只能重建。
void MainWindow::ApplyLanguage()
{
    ::SetWindowTextW(hwnd_,
                      TR(L"FastVideoCut - 黑屏自动剪辑工具 (ffmpeg 无损剪切)",
                         L"FastVideoCut - black frame cutter (ffmpeg lossless trim)"));

    HMENU old = ::GetMenu(hwnd_);
    BuildMenu();
    HMENU bar = ::GetMenu(hwnd_);
    if (bar) ::SetMenu(hwnd_, bar);
    if (old) ::DestroyMenu(old);

    const BtnDef* btns = ToolbarButtons(btnDefs_);
    for (int i = 0; i < buttonCount_; ++i)
        ::SetWindowTextW(buttons_[i], btns[i].text);

    // 列表表头
    struct ColDef { const wchar_t* text; int width; int fmt; };
    const ColDef cols[] =
    {
        { L"#",                 36,  LVCFMT_RIGHT  },
        { TR(L"文件", L"File"),           250, LVCFMT_LEFT   },
        { TR(L"状态", L"Status"),          96,  LVCFMT_LEFT   },
        { TR(L"时长", L"Length"),          80,  LVCFMT_RIGHT  },
        { TR(L"分辨率", L"Resolution"),    86,  LVCFMT_LEFT   },
        { TR(L"规格", L"Format"),          96,  LVCFMT_LEFT   },
        { TR(L"黑屏段", L"Black"),         60,  LVCFMT_RIGHT  },
        { TR(L"黑屏时长", L"Black len"),   80,  LVCFMT_RIGHT  },
        { TR(L"起始时间", L"Start"),      104, LVCFMT_RIGHT  },
        { TR(L"结束时间", L"End"),        104, LVCFMT_RIGHT  },
        { TR(L"保留时长", L"Kept len"),    80,  LVCFMT_RIGHT  },
        { TR(L"已选段", L"Segments"),      64,  LVCFMT_RIGHT  },
        { TR(L"大小", L"Size"),            82,  LVCFMT_RIGHT  }
    };
    for (int i = 0; i < (int)(sizeof(cols) / sizeof(cols[0])); ++i)
    {
        LVCOLUMNW col;
        ::ZeroMemory(&col, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = (LPWSTR)cols[i].text;
        ::SendMessageW(list_, LVM_SETCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }

    ApplyListMode(false);      // 顺带刷新“列表/视频”按钮的文字
    RebuildList();             // 状态/摘要等文字也依赖语言
    timeline_.Refresh();
    preview_.SetLanguage();    // 预览窗的按钮与占位文字
}

void MainWindow::ShowInfoDialog()
{
    ::MessageBoxW(hwnd_,
        TR(L"FastVideoCut 使用说明\n"
           L"─────────────────────────────\n"
        L"1) 添加视频：点“添加视频”或把文件/文件夹直接拖进窗口。每个视频占一行。\n\n"
        L"2) 自动分析：点“自动分析”（F6）。程序用 ffmpeg 的 blackdetect 滤镜找出每一段黑屏的\n"
        L"   起止时间与长度，显示在“时长/黑屏段/黑屏时长”列，并直接画在帧流上\n"
        L"   （红色斜纹 = 黑屏段）。黑屏只是帮你快速找到“片头片尾”与“主体”的\n"
        L"   分界线，它本身并不是要删掉的内容。\n"
        L"   左侧每行还会显示画面规格，例如“HDR10 · 10bit”或“SDR · 8bit”。\n"
        L"   · 默认只检测还没检测过的视频，新增/移除文件后再点不会全部重算\n"
        L"   · 分析进行中“自动分析”会变成“停止分析”，再点一次即可中断（Esc 也可以）\n"
        L"   · 只想重跑一个视频：选中该行后按 Ctrl+F6、点右键菜单“重新分析这个视频”，\n"
        L"     或者直接双击该行；其它视频的结果不受影响\n"
        L"   · 需要重算全部时用 Shift+F6 或菜单“视频 → 重新检测全部黑屏”\n\n"
        L"   扫描范围：长视频默认只解码片头 180 秒与片尾 180 秒（片头结束/片尾开始基本\n"
        L"   都在这里），两项在“设置”里可以分别修改。填 0 = 该侧完全不扫，填负数 =\n"
        L"   该侧不限制（整段扫描）；视频短于 片头+片尾 时自动整段扫描。\n\n"
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
        L"7) 界面与缩略图：\n"
        L"      · “设置”里可以切换界面语言（简体中文 / English）；第一次启动会\n"
        L"        按系统语言自动选择，之后按你的选择固定。\n"
        L"      · “生成视频流缩略图”默认不勾选：只做黑屏检测，不抽帧拼图，\n"
        L"        长视频列表会明显更快。需要看画面时再打开。\n\n"
        L"开源授权：FastVideoCut 基于 GNU GPL v3.0 或更高版本发布，\n"
        L"Copyright (C) 2026 dzdhome。可自由使用与修改；对外分发时\n"
        L"必须附带许可证全文并提供完整源码（https://github.com/dzdhome/FastVideoCut）。\n"
        L"本程序按“不附带任何担保”提供。ffmpeg 为独立进程调用，未静态链接、不随本程序分发。",

        L"FastVideoCut - usage guide\n"
        L"-----------------------------\n"
        L"1) Add videos: click \"Add\" or drag files / folders onto the window.\n\n"
        L"2) Analyse (F6): ffmpeg's blackdetect filter finds every black range; it\n"
        L"   is listed in the Length / Black / Black len columns and drawn on the\n"
        L"   strip (red hatching = black). Black frames only mark the boundary\n"
        L"   between intro/outro and body - they are not the content you want to\n"
        L"   delete. Each row also shows the picture format, e.g. \"HDR10 - 10bit\".\n"
        L"   - By default only videos that were never analysed are re-checked\n"
        L"   - While it runs, \"Analyse\" becomes \"Stop\" - click it again to stop\n"
        L"   - To redo one video: select the row, then Ctrl+F6 / right-click / double-click\n"
        L"   - Shift+F6 re-checks everything\n\n"
        L"   Scan window: long videos only decode the first 180 s and the last 180 s.\n"
        L"   Both values live in Settings. 0 = that side is not scanned at all, a\n"
        L"   negative number = no limit (whole file). Videos shorter than head+tail\n"
        L"   are scanned completely.\n\n"
        L"3) Choose what to keep (click the strip: left = start, right = end):\n"
        L"      - Left click a segment = keep start, right click = keep end\n"
        L"      - Everything between the two ends is kept, the rest is cut\n"
        L"      - Ctrl+click = toggle one segment, double click = keep only it\n"
        L"      - Clicking a segment also previews it on the right\n"
        L"      - Ctrl+B (Select menu) keeps the body in one go\n"
        L"      - Grey = will be cut away\n\n"
        L"4) Export:\n"
        L"      - \"Export each video\": cuts away every unselected segment.\n"
        L"      - \"Merge into one file\": concatenates all videos in list order.\n"
        L"   Both use ffmpeg stream copy (-c copy): lossless and fast. If the\n"
        L"   sources disagree on codec parameters, tick \"Re-encode\" in Settings.\n\n"
        L"5) View:\n"
        L"      - The toolbar \"List\" button (Ctrl+L) switches the left pane between\n"
        L"        the file list and the video strip\n"
        L"      - Wheel = scroll videos, Ctrl+wheel = zoom\n"
        L"      - Shift+wheel or middle drag = pan, F5 = fit to window\n\n"
        L"6) ffmpeg: the bin folder next to the exe is preferred, then PATH; you\n"
        L"   can also set it by hand in Settings.\n\n"
        L"7) Language and thumbnails:\n"
        L"      - The interface language (Simplified Chinese / English) is in\n"
        L"        Settings. The first launch picks it from the system locale.\n"
        L"      - \"Build timeline thumbnails\" is OFF by default: only black\n"
        L"        detection runs, with no frame sampling, which is much faster on\n"
        L"        long lists. Turn it on to see the pictures.\n\n"
        L"Licence: FastVideoCut is released under the GNU GPL v3.0 or later,\n"
        L"Copyright (C) 2026 dzdhome. Free to use and modify; when you redistribute\n"
        L"it you must ship the full licence and the complete source\n"
        L"(https://github.com/dzdhome/FastVideoCut). Provided \"as is\". ffmpeg is\n"
        L"invoked as a separate process, is not linked statically and is not\n"
        L"distributed with this program."),
        TR(L"FastVideoCut 使用说明", L"FastVideoCut usage guide"), MB_ICONINFORMATION);
}

// __FVC_MAINWND_CHUNK_END__
