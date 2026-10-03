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
// Preview.cpp - embedded preview playback (ffmpeg pipes -> GDI / waveOut)
// ---------------------------------------------------------------------------
#include "Preview.h"

#include "Utf.h"

#include <windowsx.h>
#include <mmsystem.h>

#include <cstdio>
#include <cstring>

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

namespace
{
    // 44100 Hz stereo s16le, 0.1 s per block
    const int    kAudioRate   = 44100;
    const DWORD  kAudioBlock  = 44100 * 4 / 10;
    const int    kAudioBufs   = 16;

    bool ReadExact(HANDLE h, void* dst, size_t n)
    {
        BYTE* p = (BYTE*)dst;
        size_t got = 0;
        while (got < n)
        {
            DWORD r = 0;
            if (!::ReadFile(h, p + got, (DWORD)(n - got), &r, nullptr) || r == 0)
                return false;
            got += r;
        }
        return true;
    }

    // Spawns `exe` with `args`: stdout -> data pipe, stderr -> error file,
    // stdin -> NUL. Returns false when the process could not be started.
    bool Spawn(const std::wstring& exe, const std::wstring& args,
               const std::wstring& errFile, HANDLE& outRead,
               HANDLE& proc, std::string& fail)
    {
        outRead = nullptr;
        proc    = nullptr;

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = nullptr;
        sa.bInheritHandle = TRUE;

        HANDLE rd = nullptr, wr = nullptr;
        if (!::CreatePipe(&rd, &wr, &sa, 512 * 1024))
        {
            fail = "CreatePipe failed";
            return false;
        }
        ::SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

        HANDLE nul = ::CreateFileW(L"NUL", GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                   OPEN_EXISTING, 0, nullptr);

        HANDLE err = nullptr;
        if (!errFile.empty())
        {
            err = ::CreateFileW(errFile.c_str(), GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                CREATE_ALWAYS, 0, nullptr);
        }
        if (!err) err = nul;

        STARTUPINFOW si;
        ::ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = nul;
        si.hStdOutput = wr;
        si.hStdError = err;

        PROCESS_INFORMATION pi;
        ::ZeroMemory(&pi, sizeof(pi));

        std::wstring cmdLine = QuoteArg(exe);
        if (!args.empty())
        {
            cmdLine.push_back(L' ');
            cmdLine += args;
        }
        std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back(L'\0');

        BOOL created = ::CreateProcessW(nullptr, &cmdBuf[0], nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

        ::CloseHandle(wr);
        if (nul) ::CloseHandle(nul);
        if (err && err != nul) ::CloseHandle(err);

        if (!created)
        {
            ::CloseHandle(rd);
            fail = "CreateProcess failed";
            return false;
        }
        ::CloseHandle(pi.hThread);
        outRead = rd;
        proc    = pi.hProcess;
        return true;
    }

    std::wstring StateSuffix(int state)
    {
        switch (state)
        {
        case 1:  return L"播放中";
        case 2:  return L"已暂停";
        case 3:  return L"已结束";
        case 4:  return L"失败";
        default: return L"就绪";
        }
    }
}

// ---------------------------------------------------------------------------
// window creation / layout
// ---------------------------------------------------------------------------
bool PreviewPane::Create(HWND owner, int id, HINSTANCE inst)
{
    owner_ = owner;
    inst_  = inst;

    HDC dc = ::GetDC(owner_);
    dpi_ = dc ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ::ReleaseDC(owner_, dc);
    if (dpi_ <= 0) dpi_ = 96;
    barH_ = MulDiv(46, dpi_, 96);

    WNDCLASSEXW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &PreviewPane::WndProcStatic;
    wc.hInstance     = inst_;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = ClassName();
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    pane_ = ::CreateWindowExW(0, ClassName(), L"",
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, 0, 10, 10, owner_, (HMENU)(INT_PTR)id, inst_, this);
    if (!pane_) return false;

    btnPause_ = ::CreateWindowExW(0, L"BUTTON", L"暂停",
                                  WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
                                  0, 0, 10, 10, pane_, (HMENU)(INT_PTR)IDC_PV_PAUSE,
                                  inst_, nullptr);
    btnStop_ = ::CreateWindowExW(0, L"BUTTON", L"停止",
                                 WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON,
                                 0, 0, 10, 10, pane_, (HMENU)(INT_PTR)IDC_PV_STOP,
                                 inst_, nullptr);

    errFile_ = PathCombine(GetLocalAppDataDir(), L"FastVideoCut\\preview_err.txt");
    EnsureDirectory(PathCombine(GetLocalAppDataDir(), L"FastVideoCut"));

    UpdateBarMetrics();
    LayoutControls();
    return true;
}

void PreviewPane::SetFont(HFONT font)
{
    font_ = font;
    if (btnPause_) ::SendMessageW(btnPause_, WM_SETFONT, (WPARAM)font_, TRUE);
    if (btnStop_)  ::SendMessageW(btnStop_,  WM_SETFONT, (WPARAM)font_, TRUE);
    UpdateBarMetrics();
    if (pane_) ::InvalidateRect(pane_, nullptr, FALSE);
}

void PreviewPane::UpdateBarMetrics()
{
    if (!pane_ || !font_) return;
    HDC dc = ::GetDC(pane_);
    HGDIOBJ old = ::SelectObject(dc, font_);
    TEXTMETRICW tm;
    ::GetTextMetricsW(dc, &tm);
    int h = tm.tmHeight + MulDiv(18, dpi_, 96);
    if (old) ::SelectObject(dc, old);
    ::ReleaseDC(pane_, dc);
    barH_ = h;
    LayoutControls();
}

RECT PreviewPane::VideoRect() const
{
    RECT rc = { 0, 0, 0, 0 };
    if (pane_) ::GetClientRect(pane_, &rc);
    rc.bottom -= barH_;
    if (rc.bottom < rc.top) rc.bottom = rc.top;
    return rc;
}

void PreviewPane::LayoutControls()
{
    if (!pane_) return;
    RECT rc;
    ::GetClientRect(pane_, &rc);

    int gap = MulDiv(6, dpi_, 96);
    int bw  = MulDiv(70, dpi_, 96);
    int bh  = barH_ - 2 * gap;
    if (bh < 18) bh = 18;
    int y   = rc.bottom - barH_ + gap;

    ::MoveWindow(btnStop_,  rc.right - gap - bw, y, bw, bh, TRUE);
    ::MoveWindow(btnPause_, rc.right - 2 * gap - 2 * bw, y, bw, bh, TRUE);
}

void PreviewPane::UpdateButtons()
{
    bool on = (state_ == 1 || state_ == 2);
    if (btnPause_)
    {
        ::SetWindowTextW(btnPause_, paused_ ? L"继续" : L"暂停");
        ::EnableWindow(btnPause_, on ? TRUE : FALSE);
    }
    if (btnStop_)
        ::EnableWindow(btnStop_, on ? TRUE : FALSE);
}

LRESULT CALLBACK PreviewPane::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PreviewPane* self = (PreviewPane*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (PreviewPane*)cs->lpCreateParams;
        if (self)
        {
            self->pane_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        }
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(msg, wp, lp);
}

LRESULT PreviewPane::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        LayoutControls();
        ::InvalidateRect(pane_, nullptr, FALSE);
        return 0;

    case WM_COMMAND:
    {
        int id = LOWORD(wp);
        if (id == IDC_PV_PAUSE) { TogglePause(); return 0; }
        if (id == IDC_PV_STOP)  { Stop();        return 0; }
        break;
    }

    case WM_PV_REFRESH:
        // a worker finished / failed: update state on the UI thread
        UpdateButtons();
        ::InvalidateRect(pane_, nullptr, FALSE);
        return 0;

    case WM_DESTROY:
        StopWorkers(false);
        if (memOld_ && memDC_) { ::SelectObject(memDC_, memOld_); memOld_ = nullptr; }
        if (memBmp_) { ::DeleteObject(memBmp_); memBmp_ = nullptr; }
        if (memDC_)  { ::DeleteDC(memDC_);      memDC_  = nullptr; }
        memW_ = memH_ = 0;
        return 0;
    }
    return ::DefWindowProcW(pane_, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// painting
//
// Flicker free approach: the current frame is scaled once into an offscreen
// bitmap (composite_) and the window is updated with a single BitBlt. Scaling is
// only redone when a new frame really arrived, and only the video area is
// invalidated per frame.
// ---------------------------------------------------------------------------
void PreviewPane::EnsureComposite(int w, int h)
{
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    if (memDC_ && memW_ == w && memH_ == h) return;

    if (memOld_) { ::SelectObject(memDC_, memOld_); memOld_ = nullptr; }
    if (memBmp_) { ::DeleteObject(memBmp_); memBmp_ = nullptr; }
    if (memDC_)  { ::DeleteDC(memDC_);  memDC_  = nullptr; }

    memDC_ = ::CreateCompatibleDC(::GetDC(pane_));
    if (!memDC_) { memW_ = memH_ = 0; return; }

    BITMAPINFO bmi;
    ::ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;            // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    memBmp_ = ::CreateDIBSection(memDC_, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memBmp_)
    {
        ::DeleteDC(memDC_);
        memDC_ = nullptr;
        memW_ = memH_ = 0;
        return;
    }
    memOld_ = (HBITMAP)::SelectObject(memDC_, memBmp_);
    memW_ = w;
    memH_ = h;

    HBRUSH black = ::CreateSolidBrush(RGB(0, 0, 0));
    RECT all = { 0, 0, w, h };
    ::FillRect(memDC_, &all, black);
    ::DeleteObject(black);
    compositeDirty_ = true;
}

void PreviewPane::RedrawComposite()
{
    if (!memDC_ || !memW_ || !memH_) return;

    int fw = 0, fh = 0;
    {
        std::lock_guard<std::mutex> lk(frameMx_);
        fw = frameW_;
        fh = frameH_;
    }
    if (fw <= 0 || fh <= 0 || paintBuf_.size() < (size_t)fw * fh * 3)
    {
        HBRUSH black = ::CreateSolidBrush(RGB(0, 0, 0));
        RECT all = { 0, 0, memW_, memH_ };
        ::FillRect(memDC_, &all, black);
        ::DeleteObject(black);
        compositeDirty_ = false;
        return;
    }

    int dw = memW_, dh = memH_;
    double sa = (double)fw / (double)fh;
    double da = (double)dw / (double)dh;
    if (sa > da) dh = (int)(dw / sa + 0.5);
    else         dw = (int)(dh * sa + 0.5);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    HBRUSH black = ::CreateSolidBrush(RGB(0, 0, 0));
    RECT all = { 0, 0, memW_, memH_ };
    ::FillRect(memDC_, &all, black);
    ::DeleteObject(black);

    BITMAPINFO bmi;
    ::ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = fw;
    bmi.bmiHeader.biHeight      = -fh;          // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    // COLORONCOLOR is hardware friendly; HALFTONE would resample every frame
    ::SetStretchBltMode(memDC_, COLORONCOLOR);
    ::StretchDIBits(memDC_, (memW_ - dw) / 2, (memH_ - dh) / 2, dw, dh,
                    0, 0, fw, fh,
                    &paintBuf_[0], &bmi, DIB_RGB_COLORS, SRCCOPY);
    compositeDirty_ = false;
}

void PreviewPane::InvalidateVideo()
{
    if (!pane_) return;
    RECT vid = VideoRect();
    ::InvalidateRect(pane_, &vid, FALSE);
}

void PreviewPane::OnPaint()
{
    PAINTSTRUCT ps;
    HDC dc = ::BeginPaint(pane_, &ps);

    RECT rc;
    ::GetClientRect(pane_, &rc);
    RECT vid = VideoRect();
    int vw = vid.right - vid.left;
    int vh = vid.bottom - vid.top;

    // pick up the newest frame (pointer swap, no per paint copy). The composite is
// only rebuilt when a new frame really arrived, otherwise we just blit again.
    bool have = false;
    {
        std::lock_guard<std::mutex> lk(frameMx_);
        if (seq_ != paintedSeq_)
        {
            paintBuf_.swap(front_);
            paintedSeq_ = seq_;
            compositeDirty_ = true;
            have = hasFrame_ && !paintBuf_.empty();
        }
        else
        {
            have = hasFrame_;
        }
    }
    EnsureComposite(vw, vh);
    if (compositeDirty_) RedrawComposite();

    // one blit for the whole video area
    if (memDC_ && memW_ && memH_)
        ::BitBlt(dc, vid.left, vid.top, memW_, memH_, memDC_, 0, 0, SRCCOPY);
    else
    {
        HBRUSH black = ::CreateSolidBrush(RGB(0, 0, 0));
        ::FillRect(dc, &vid, black);
        ::DeleteObject(black);
    }

    // ---- bottom bar -------------------------------------------------------
    RECT bar = rc;
    bar.top = rc.bottom - barH_;
    HBRUSH bg = ::CreateSolidBrush(RGB(24, 26, 31));
    ::FillRect(dc, &bar, bg);
    ::DeleteObject(bg);

    HPEN sep = ::CreatePen(PS_SOLID, 1, RGB(58, 64, 76));
    HGDIOBJ oldPen = ::SelectObject(dc, sep);
    ::MoveToEx(dc, bar.left, bar.top, nullptr);
    ::LineTo(dc, bar.right, bar.top);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(sep);

    double pos = 0.0;
    int state = state_;
    {
        std::lock_guard<std::mutex> lk(frameMx_);
        pos = pos_;
    }

    std::wstring text;
    if (state == 0 && label_.empty())
        text = L"预览就绪";
    else
        text = label_ + L"   " + FormatTimecode(pos) + L" / " + FormatTimecode(t1_)
               + L"   " + StateSuffix(state);

    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, RGB(198, 204, 214));
    HGDIOBJ oldFont = font_ ? ::SelectObject(dc, font_) : nullptr;
    RECT tr = bar;
    tr.left  += MulDiv(8, dpi_, 96);
    tr.right -= MulDiv(170, dpi_, 96);
    tr.top   += MulDiv(2, dpi_, 96);
    tr.bottom-= MulDiv(2, dpi_, 96);
    ::DrawTextW(dc, text.c_str(), -1, &tr,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldFont) ::SelectObject(dc, oldFont);

    // placeholder text when nothing has been decoded yet
    if (!have)
    {
        ::SetTextColor(dc, RGB(126, 134, 148));
        HGDIOBJ of = font_ ? ::SelectObject(dc, font_) : nullptr;
        ::DrawTextW(dc, L"单击时间线上的分段开始预览播放", -1, &vid,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (of) ::SelectObject(dc, of);
    }

    ::EndPaint(pane_, &ps);
}

void PreviewPane::LogLine(const std::wstring& text)
{
    if (owner_) PostUiMessage(owner_, UiLog, text);
}
// ---------------------------------------------------------------------------
// playback control (UI thread only)
// ---------------------------------------------------------------------------
void PreviewPane::Play(const std::wstring& file, double t0, double t1,
                       int srcW, int srcH, double srcFps, bool hasAudio,
                       const std::wstring& label)
{
    if (!pane_) return;

    StopWorkers(false);

    file_     = file;
    t0_       = t0;
    t1_       = t1;
    srcW_     = srcW;
    srcH_     = srcH;
    srcFps_   = srcFps;
    hasAudio_ = hasAudio;
    label_    = label;

    {
        std::lock_guard<std::mutex> lk(frameMx_);
        front_.clear();
        paintBuf_.clear();
        frameW_ = frameH_ = 0;
        hasFrame_ = false;
        pos_ = t0_;
    }
    compositeDirty_ = true;
    frames_ = 0;
    paused_ = false;

    if (ffmpeg_.empty() || file_.empty() || !(t1_ > t0_))
    {
        state_ = 4;
        UpdateButtons();
        ::InvalidateRect(pane_, nullptr, FALSE);
        LogLine(L"预览失败：" + (label_.empty() ? std::wstring(L"(无分段)") : label_) +
                (ffmpeg_.empty() ? L"（未找到 ffmpeg）" : L"（时间区间无效）"));
        return;
    }

    // output size: fit into the video area, never upscale, keep the aspect ratio
    RECT vr = VideoRect();
    int availW = vr.right - vr.left - 8;
    int availH = vr.bottom - vr.top - 8;
    if (availW < 64) availW = 64;
    if (availH < 64) availH = 64;

    int ow = (srcW_ > 0) ? srcW_ : 640;
    int oh = (srcH_ > 0) ? srcH_ : 360;
    double aspect = (oh > 0) ? (double)ow / (double)oh : 16.0 / 9.0;
    if (ow > availW) { ow = availW; oh = (int)(ow / aspect + 0.5); }
    if (oh > availH) { oh = availH; ow = (int)(oh * aspect + 0.5); }
    if (ow < 16) ow = 16;
    if (oh < 16) oh = 16;
    ow &= ~1;
    oh &= ~1;

    double outFps = srcFps_;
    if (outFps < 8.0 || outFps > 60.0) outFps = 25.0;
    if (outFps > 30.0) outFps = 30.0;   // keep the CPU load sane

    stop_ = false;
    state_ = 1;
    playing_ = true;
    UpdateButtons();
    ::InvalidateRect(pane_, nullptr, FALSE);

    LogLine(FormatString(L"预览播放：%s  %s - %s  [%dx%d @ %.3gfps]",
                         label_.c_str(), FormatTimecode(t0_).c_str(),
                         FormatTimecode(t1_).c_str(), ow, oh, outFps));

    StartWorkers(ow, oh, outFps);
}

void PreviewPane::TogglePause()
{
    if (!playing_) return;
    paused_ = !paused_;
    state_ = paused_ ? 2 : 1;
    UpdateButtons();
    ::InvalidateRect(pane_, nullptr, FALSE);
}

void PreviewPane::Stop()
{
    if (!playing_) return;
    StopWorkers(true);
}

void PreviewPane::Shutdown()
{
    StopWorkers(false);
}

// ---------------------------------------------------------------------------
// worker management
// ---------------------------------------------------------------------------
void PreviewPane::StartWorkers(int outW, int outH, double outFps)
{
    vid_ = std::thread(&PreviewPane::VideoWorker, this, outW, outH, outFps);
    if (hasAudio_)
        aud_ = std::thread(&PreviewPane::AudioWorker, this);
}
// ---------------------------------------------------------------------------
// workers (never touch child window controls - they run on their own thread;
// UI updates are marshalled back through WM_PV_REFRESH / PostUiMessage)
// ---------------------------------------------------------------------------
namespace
{
    std::wstring ReadErrorFile(const std::wstring& path)
    {
        std::wstring out;
        FILE* fp = _wfopen(path.c_str(), L"rb");
        if (!fp) return out;
        char buf[600];
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        if (n > 0)
        {
            buf[n] = 0;
            out = Utf8ToWide(std::string(buf, n));
            while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n'))
                out.pop_back();
        }
        return out;
    }
}

void PreviewPane::VideoWorker(int outW, int outH, double outFps)
{
    const double dur = t1_ - t0_;

    std::wstring args = L"-hide_banner -nostdin -loglevel error -nostats";
    args += L" -ss " + NumberText(t0_, 3);
    args += L" -i "  + QuoteArg(file_);
    args += L" -t "  + NumberText(dur, 3);
    args += L" -an -sn -dn";
    args += L" -vf scale=" + FormatString(L"%d:%d", outW, outH);
    args += L" -r " + NumberText(outFps, 3);
    args += L" -pix_fmt bgr24 -f rawvideo pipe:1";

    HANDLE rd = nullptr, proc = nullptr;
    std::string fail;
    if (!Spawn(ffmpeg_, args, errFile_, rd, proc, fail))
    {
        playing_ = false;
        state_ = 4;
        LogLine(L"预览失败：" + label_ + L"（无法启动 ffmpeg）");
        if (pane_) ::PostMessageW(pane_, WM_PV_REFRESH, 0, 0);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(procMx_);
        vidProc_ = proc;
    }

    const size_t bytesPerFrame = (size_t)outW * (size_t)outH * 3;
    std::vector<BYTE> buf(bytesPerFrame);
    ULONGLONG       start    = ::GetTickCount64();
    ULONGLONG       pauseMs  = 0;
    long long       idx      = 0;

    while (!stop_)
    {
        if (paused_)
        {
            ::Sleep(20);
            pauseMs += 20;
            continue;
        }

        // after handing a frame over with swap() the buffer we read into owns the
        // *previous* front buffer (empty on the very first swap), so make sure it
        // is exactly one frame again - otherwise ReadExact writes out of bounds
        if (buf.size() != bytesPerFrame) buf.resize(bytesPerFrame);

        // decode one frame (blocks; the pipe applies back pressure while paused)
        if (!ReadExact(rd, &buf[0], bytesPerFrame))
            break;                                   // end of stream

        // wait until it is time to show it
        for (;;)
        {
            if (stop_) break;
            if (paused_) { ::Sleep(20); pauseMs += 20; continue; }

            ULONGLONG due = start + pauseMs +
                            (ULONGLONG)((double)idx * 1000.0 / outFps);
            ULONGLONG now = ::GetTickCount64();
            if (now >= due) break;
            DWORD wait = (DWORD)(due - now);
            if (wait > 20) wait = 20;                // slice so stop/pause react fast
            ::Sleep(wait);
        }
        if (stop_) break;

        // If decoding could not keep up, do not try to catch up by jumping -
        // skip this frame so playback stays in sync with the audio instead of
        // stuttering / stuttering back and forth.
        {
            ULONGLONG due = start + pauseMs + (ULONGLONG)((double)idx * 1000.0 / outFps);
            ULONGLONG now = ::GetTickCount64();
            if (due + 500 < now)
            {
                start += 500;                       // re-sync the clock
                frames_ = ++idx;                     // frame counted, just not shown
                continue;
            }
        }

        {
            std::lock_guard<std::mutex> lk(frameMx_);
            front_.swap(buf);          // hand the frame over without copying
            frameW_ = outW;
            frameH_ = outH;
            hasFrame_ = true;
            pos_ = t0_ + (double)idx / outFps;
            ++seq_;
        }
        frames_ = ++idx;
        InvalidateVideo();            // only the picture area, the bar is static
    }

    DWORD exitCode = 0;
    if (!stop_)
    {
        ::WaitForSingleObject(proc, 500);
        ::GetExitCodeProcess(proc, &exitCode);
    }
    {
        std::lock_guard<std::mutex> lk(procMx_);
        if (vidProc_ == proc) vidProc_ = nullptr;
    }
    ::CloseHandle(proc);
    ::CloseHandle(rd);

    if (stop_) return;                              // explicit stop: already logged

    if (exitCode == 0)
    {
        state_ = 3;
        LogLine(FormatString(L"预览结束：%s（%lld 帧）", label_.c_str(), (long long)frames_));
    }
    else
    {
        state_ = 4;
        std::wstring detail = ReadErrorFile(errFile_);
        LogLine(FormatString(L"预览失败：%s（ffmpeg 退出码 %u）", label_.c_str(),
                             (unsigned)exitCode) +
                (detail.empty() ? std::wstring() : L"：" + detail));
    }
    playing_ = false;
    if (pane_) ::PostMessageW(pane_, WM_PV_REFRESH, 0, 0);
}

void PreviewPane::AudioWorker()
{
    const double dur = t1_ - t0_;

    std::wstring args = L"-hide_banner -nostdin -loglevel error -nostats";
    args += L" -ss " + NumberText(t0_, 3);
    args += L" -i "  + QuoteArg(file_);
    args += L" -t "  + NumberText(dur, 3);
    args += L" -vn -sn -dn -f s16le";
    args += L" -ar " + NumberText((double)kAudioRate, 0) + L" -ac 2 pipe:1";

    HANDLE rd = nullptr, proc = nullptr;
    std::string fail;
    if (!Spawn(ffmpeg_, args, std::wstring(), rd, proc, fail))
        return;
    {
        std::lock_guard<std::mutex> lk(procMx_);
        audProc_ = proc;
    }

    WAVEFORMATEX wf;
    ::ZeroMemory(&wf, sizeof(wf));
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = 2;
    wf.nSamplesPerSec  = kAudioRate;
    wf.wBitsPerSample  = 16;
    wf.nBlockAlign     = 4;
    wf.nAvgBytesPerSec = kAudioRate * 4;

    HANDLE done = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT dev = nullptr;
    if (::waveOutOpen(&dev, WAVE_MAPPER, &wf, (DWORD_PTR)done, 0, CALLBACK_EVENT)
        != MMSYSERR_NOERROR)
    {
        // no audio device available: drop the decoder, keep the silent preview
        dev = nullptr;
        {
            std::lock_guard<std::mutex> lk(procMx_);
            if (audProc_ == proc) audProc_ = nullptr;
            ::TerminateProcess(proc, 1);
            ::CloseHandle(proc);
        }
        ::CloseHandle(rd);
        if (done) ::CloseHandle(done);
        return;
    }

    struct Buf { std::vector<char> data; WAVEHDR hdr; bool used; };
    std::vector<Buf> bufs(kAudioBufs);
    for (int i = 0; i < kAudioBufs; ++i)
    {
        bufs[i].data.resize(kAudioBlock);
        bufs[i].used = false;
        ::ZeroMemory(&bufs[i].hdr, sizeof(WAVEHDR));
    }

    int   inFlight       = 0;
    bool  eof            = false;
    bool  pausedApplied  = false;

    while (!stop_)
    {
        if (paused_)
        {
            if (!pausedApplied) { ::waveOutPause(dev); pausedApplied = true; }
            ::Sleep(25);
            continue;
        }
        if (pausedApplied) { ::waveOutRestart(dev); pausedApplied = false; }

        // reclaim buffers the device has finished with
        for (int i = 0; i < kAudioBufs; ++i)
        {
            if (bufs[i].used && (bufs[i].hdr.dwFlags & WHDR_DONE))
            {
                ::waveOutUnprepareHeader(dev, &bufs[i].hdr, sizeof(WAVEHDR));
                bufs[i].used = false;
                --inFlight;
            }
        }

        if (eof && inFlight == 0) break;

        Buf* freeBuf = nullptr;
        for (int i = 0; i < kAudioBufs; ++i)
            if (!bufs[i].used) { freeBuf = &bufs[i]; break; }
        if (!freeBuf) { ::WaitForSingleObject(done, 25); continue; }

        DWORD got = 0;
        if (!::ReadFile(rd, &freeBuf->data[0], kAudioBlock, &got, nullptr) || got == 0)
        {
            eof = true;
            continue;
        }
        ::ZeroMemory(&freeBuf->hdr, sizeof(WAVEHDR));
        freeBuf->hdr.lpData        = &freeBuf->data[0];
        freeBuf->hdr.dwBufferLength = got;
        if (::waveOutPrepareHeader(dev, &freeBuf->hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR &&
            ::waveOutWrite(dev, &freeBuf->hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
        {
            freeBuf->used = true;
            ++inFlight;
        }
        else
        {
            ::waveOutUnprepareHeader(dev, &freeBuf->hdr, sizeof(WAVEHDR));
        }
    }

    // teardown
    ::waveOutReset(dev);
    for (int i = 0; i < kAudioBufs; ++i)
    {
        if (bufs[i].used)
        {
            ::waveOutUnprepareHeader(dev, &bufs[i].hdr, sizeof(WAVEHDR));
            bufs[i].used = false;
        }
    }
    ::waveOutClose(dev);
    if (done) ::CloseHandle(done);

    {
        std::lock_guard<std::mutex> lk(procMx_);
        if (audProc_ == proc) audProc_ = nullptr;
        ::TerminateProcess(proc, 1);   // already exited -> harmless
        ::CloseHandle(proc);
    }
    ::CloseHandle(rd);
}



void PreviewPane::StopWorkers(bool notify)
{
    bool wasPlaying = playing_.load();
    stop_   = true;
    paused_ = false;

    {
        std::lock_guard<std::mutex> lk(procMx_);
        if (vidProc_) ::TerminateProcess(vidProc_, 1);
        if (audProc_) ::TerminateProcess(audProc_, 1);
    }
    if (vid_.joinable()) vid_.join();
    if (aud_.joinable()) aud_.join();

    playing_ = false;
    if (notify && wasPlaying)
    {
        state_ = 0;
        UpdateButtons();
        ::InvalidateRect(pane_, nullptr, FALSE);
        LogLine(FormatString(L"预览停止：%s（已播 %lld 帧）",
                             label_.c_str(), (long long)frames_));
    }
}

