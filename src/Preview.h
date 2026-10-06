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
// Preview.h - right hand preview pane: plays a segment inside the main window
//
// The pane owns two workers:
//   video: ffmpeg -> rawvideo bgr24 pipe -> paced frame blitting (GDI)
//   audio: ffmpeg -> s16le pipe          -> waveOut playback (winmm)
// Clicking a segment in the timeline asks the main window to call Play().
//
// The bottom bar carries a seek slider and a compact row of transport buttons.
// Seeking restarts the two ffmpeg pipes at the new offset (a pipe cannot jump),
// which is also how the arrow keys, the mouse wheel and slider drags work.
// While paused the "split here" button inserts - or, when the paused position
// already sits on one, removes - a manual segment boundary (see Project.h):
// transitions without a black frame need a human to cut them.
// ---------------------------------------------------------------------------
#pragma once

#include "Messages.h"

#include <windows.h>

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <functional>

// child control ids (children of the pane window itself)
#define IDC_PV_PAUSE 42501
#define IDC_PV_STOP  42502
#define IDC_PV_BACK  42503      // 快退（方向键左同义）
#define IDC_PV_FWD   42504      // 快进（方向键右同义）
#define IDC_PV_SPLIT 42505      // 暂停时：在当前位置插入/删除一个手动分割

// posted to the pane window by a worker: refresh state / buttons on the UI thread
#define WM_PV_REFRESH (WM_APP + 110)

class PreviewPane
{
public:
    bool Create(HWND owner, int id, HINSTANCE inst);
    HWND hwnd() const { return pane_; }

    void SetFont(HFONT font);
    void SetFfmpeg(const std::wstring& exe) { ffmpeg_ = exe; }

    // Starts (or restarts) playback of [t0, t1]. srcW/srcH/srcFps come from the
    // ffprobe result and are only used to pick a sensible output size / rate.
    void Play(const std::wstring& file, double t0, double t1,
              int srcW, int srcH, double srcFps, bool hasAudio,
              const std::wstring& label);

    void TogglePause();
    void Stop();         // user stop / job start: logs "preview stopped"
    void Shutdown();     // silent teardown (window is going away)
    void SetLanguage();  // 界面语言切换后重写按钮文字并重绘

    // ---- 定位 / 快进后退（方向键、滚轮、进度条拖动都走这里） --------------
    // SeekTo 会把两条 ffmpeg 管道停掉再从新位置起（管道跳不了），暂停时定位
    // 则停在那一帧上。
    void SeekBy(double dt);
    void SeekTo(double t);
    double Position();               // 当前播放位置（源文件时间轴上的秒）

    // ---- 手动分割（主窗口提供查询：这个时间点上有没有手动分割） ----------
    // 按钮文字在"插入分割 / 删除分割"之间切换就靠它，改动后主窗口再调
    // RefreshButtons() 让文字立刻跟上。
    typedef std::function<bool(double)> SplitQuery;
    void SetSplitQuery(const SplitQuery& q) { splitQuery_ = q; }
    void RefreshButtons();

    bool playing() const { return playing_.load(); }

    static const wchar_t* ClassName() { return L"FastVideoCutPreviewWnd"; }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void OnPaint();
    void EnsureComposite(int w, int h);      // offscreen copy of the video area
    void RedrawComposite();
    void InvalidateVideo();
    void InvalidateBar();
    void UpdateBarMetrics();
    void LayoutControls();
    void UpdateButtons();
    RECT VideoRect() const;

    // ---- 进度条 ----------------------------------------------------------
    void InvalidateSeek();
    double PosFromX(int x) const;            // 进度条上的像素 -> 时间
    double ShownPos();                   // 拖动中返回拖动位置，否则真实位置
    bool   ComputeOutput(int& ow, int& oh, double& outFps);   // Play/Seek 共用的尺寸计算

    void StartWorkers(int outW, int outH, double outFps);
    void StopWorkers(bool notify);
    void VideoWorker(int outW, int outH, double outFps);
    void AudioWorker();
    void LogLine(const std::wstring& text);

    // ---- window -----------------------------------------------------------
    HWND       owner_  = nullptr;
    HWND       pane_   = nullptr;
    HWND       btnPause_ = nullptr;
    HWND       btnStop_  = nullptr;
    HWND       btnBack_  = nullptr;   // 快退
    HWND       btnFwd_   = nullptr;   // 快进
    HWND       btnSplit_ = nullptr;   // 插入/删除手动分割（只在暂停时出现）
    HFONT      font_   = nullptr;
    HINSTANCE  inst_   = nullptr;
    int        dpi_    = 96;
    int        barH_   = 46;
    RECT       seekRc_ = { 0, 0, 0, 0 };   // 进度条（客户区坐标）
    int        textRight_ = 0;             // 状态文字的右边界（按钮左侧）
    bool       seekDrag_  = false;         // 正在拖进度条
    double     seekDragPos_ = 0.0;         // 拖动中的目标位置

    // ---- current request --------------------------------------------------
    std::wstring ffmpeg_;
    std::wstring file_;
    std::wstring label_;
    std::wstring errFile_;
    double t0_ = 0.0;
    double t1_ = 0.0;
    // ffmpeg 实际的 -ss 起点。t0_ 是分段起点（进度条/文字的参照），
    // SeekTo 改的是 playStart_，两者只在刚点开一个分段时相等。
    double playStart_ = 0.0;
    int    srcW_ = 0;
    int    srcH_ = 0;
    double srcFps_ = 0.0;
    bool   hasAudio_ = false;

    // ---- frame buffer -----------------------------------------------------
    // front_ is written by the worker (swap in), paintBuf_ belongs to the UI
    // thread - handing a frame over is a pointer swap, never a copy.
    std::mutex         frameMx_;
    std::vector<BYTE>  front_;
    std::vector<BYTE>  paintBuf_;
    int                frameW_ = 0;
    int                frameH_ = 0;
    double             pos_     = 0.0;
    bool               hasFrame_ = false;
    long long          seq_         = 0;    // frames handed over to the UI
    long long          paintedSeq_  = -1;   // last seq that reached the screen

    // ---- offscreen composite (removes tearing / flicker) ------------------
    HDC      memDC_    = nullptr;
    HBITMAP  memBmp_   = nullptr;
    HBITMAP  memOld_   = nullptr;
    int      memW_     = 0;
    int      memH_     = 0;
    bool     compositeDirty_ = true;

    // ---- control ----------------------------------------------------------
    std::atomic<bool>  stop_{ true };
    std::atomic<bool>  paused_{ false };
    std::atomic<bool>  playing_{ false };
    // 暂停时定位还要把新位置的那一帧显示出来：置 1 让 video worker 再走一帧，
    // 显示完自动清零。音频不受影响（paused_ 全程为真，waveOut 一直是暂停的）。
    std::atomic<int>   stepOnce_{ 0 };
    std::atomic<long long> frames_{ 0 };
    std::atomic<int>   state_{ 0 };   // 0 idle, 1 playing, 2 paused, 3 ended, 4 error

    std::thread vid_;
    std::thread aud_;

    // 由主窗口注入：t 时刻有没有手动分割点（决定"插入/删除分割"按钮文字）。
    // 只在 UI 线程上被调用。
    SplitQuery  splitQuery_;

    std::mutex  procMx_;
    HANDLE      vidProc_ = nullptr;
    HANDLE      audProc_ = nullptr;
};
