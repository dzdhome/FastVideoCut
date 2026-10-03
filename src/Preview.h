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
// ---------------------------------------------------------------------------
#pragma once

#include "Messages.h"

#include <windows.h>

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

// child control ids (children of the pane window itself)
#define IDC_PV_PAUSE 42501
#define IDC_PV_STOP  42502

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
    void Stop();         // user stop / job start: logs "预览停止"
    void Shutdown();     // silent teardown (window is going away)

    bool playing() const { return playing_.load(); }

    static const wchar_t* ClassName() { return L"FastVideoCutPreviewWnd"; }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void OnPaint();
    void EnsureComposite(int w, int h);      // offscreen copy of the video area
    void RedrawComposite();
    void InvalidateVideo();
    void UpdateBarMetrics();
    void LayoutControls();
    void UpdateButtons();
    RECT VideoRect() const;

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
    HFONT      font_   = nullptr;
    HINSTANCE  inst_   = nullptr;
    int        dpi_    = 96;
    int        barH_   = 46;

    // ---- current request --------------------------------------------------
    std::wstring ffmpeg_;
    std::wstring file_;
    std::wstring label_;
    std::wstring errFile_;
    double t0_ = 0.0;
    double t1_ = 0.0;
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
    std::atomic<long long> frames_{ 0 };
    std::atomic<int>   state_{ 0 };   // 0 idle, 1 playing, 2 paused, 3 ended, 4 error

    std::thread vid_;
    std::thread aud_;

    std::mutex  procMx_;
    HANDLE      vidProc_ = nullptr;
    HANDLE      audProc_ = nullptr;
};
