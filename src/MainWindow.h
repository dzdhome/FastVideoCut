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
// MainWindow.h - application window, toolbars, worker jobs and export flow
// ---------------------------------------------------------------------------
#pragma once

#include "Utf.h"
#include "Project.h"
#include "Ffmpeg.h"
#include "Settings.h"
#include "Timeline.h"
#include "Preview.h"
#include "Messages.h"
#include "resource.h"

#include <windows.h>
#include <commctrl.h>

#include <string>
#include <vector>
#include <atomic>
#include <thread>

enum FcJob
{
    JobNone      = 0,
    JobDetect    = 1,
    JobCutEach   = 2,
    JobMergeAll  = 3,
    JobOpenOnly  = 4
};

struct AppArgs
{
    std::wstring              ffmpegDir;
    std::vector<std::wstring> files;
    std::wstring              output;
    int                       thumbHeight = 0;
    double                    blackMin    = 0.0;
    double                    blackPix    = 0.0;
    double                    blackPic    = 0.0;
    double                    scanWindow  = -1.0;   // -1 = 跟随设置
    bool                      reencode    = false;
    bool                      mergeAll    = false;
    bool                      autoDetect  = false;
    bool                      autoExport  = false;
    bool                      noGui       = false;
    bool                      quitOnEnd   = false;
    bool                      confirm     = true;
    std::wstring              logFile;    // append the UI/ffmpeg log to this file
};

class MainWindow
{
public:
    static const wchar_t* ClassName() { return L"FastVideoCutMainWnd"; }

    bool Create(HINSTANCE hInst, const AppArgs& args);
    HWND hwnd() const { return hwnd_; }
    void Shutdown();

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void BuildMenu();
    void CreateChildren();
    void LayoutChildren();
    void OnCommand(int id);
    void OnAddFiles(std::vector<std::wstring>* files);
    void OnDropAdd(HDROP drop);
    void StartSegmentPreview(int itemIndex, int segIndex);
    void LogKeepChange(int itemIndex);
    // 工具栏“列表/视频”按钮：左侧在文件列表与视频列表（帧流）之间切换
    void ToggleFileList();
    void ApplyListMode(bool save);

    // ---- list view -------------------------------------------------------
    void RebuildList();
    void UpdateListRow(int index);
    void UpdateTitles();
    int  SelectedItem() const;
    void EnsureVisibleItem(int index);

    // ---- jobs ------------------------------------------------------------
    void StartDetect(bool forceAll = false);
    void StartExport(int job);
    void StartJobInternal(int job, bool detectAll = false);
    void CancelJob();
    void JobThreadMain(int job, bool detectAll);
    bool BuildOutputs(int job,
                      std::vector<std::wstring>& segments,
                      std::vector<std::wstring>& outputs,
                      std::wstring& err);
    void OnJobFinished(int job, bool ok, const std::wstring& summary);

    // ---- ui helpers ------------------------------------------------------
    void AppendLog(const std::wstring& text);
    void WriteLogFile(const std::wstring& text);   // --log <file> mirror
    void Notify(const std::wstring& text, UINT flags);   // MessageBox, or just a log line in --nogui
    void SetStatus(const std::wstring& text);
    void SetBusy(bool busy, const std::wstring& text = std::wstring());
    void OnUiMessage(UiMessage* m);
    void ShowSettingsDialog();
    void ShowInfoDialog();
    std::wstring MakeOutputPath(int index) const;
    std::wstring PreferredOutputDir() const;
    bool AskForOutputDir();
    void UpdateButtonStates();

    // ---- members ---------------------------------------------------------
    HINSTANCE    hInst_     = nullptr;
    HWND         hwnd_      = nullptr;
    HWND         list_      = nullptr;
    HWND         log_       = nullptr;
    HMENU        viewMenu_  = nullptr;      // 视图菜单（切换时要更新勾选状态）
    std::wstring logFilePath_;
    int          exitCode_  = 0;
    HWND         status_    = nullptr;
    HWND         help_      = nullptr;
    HWND         progress_   = nullptr;
    HWND         tooltip_    = nullptr;
    HWND         buttons_[12];
    int          buttonCount_ = 0;
    // true = 左侧显示文件列表；false = 文件列表框隐藏，只显示视频列表（帧流）
    bool         showList_  = false;

    Project      project_;
    Ffmpeg       ffmpeg_;
    AppSettings  settings_;
    TimelineView timeline_;
    PreviewPane  preview_;
    AppArgs      args_;

    std::thread          worker_;
    std::atomic<bool>    jobRunning_;
    CancelToken          cancel_;
    int                  currentJob_  = JobNone;
    int                  nextJob_     = JobNone;
    std::wstring         lastOutput_;
    std::wstring         lastOutputDir_;
    bool                 debugLog_    = false;
    bool                 settingsDirty_ = false;

    HFONT        fontUi_    = nullptr;
    HBRUSH       bgBrush_   = nullptr;
    int          dpi_       = 96;
};