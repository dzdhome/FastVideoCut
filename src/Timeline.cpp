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
// Timeline.cpp - frame stream view (thumbnails + black marks + segments)
// ---------------------------------------------------------------------------
#include "Timeline.h"
#include "Messages.h"
#include "Loc.h"

#include <windowsx.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#ifdef _MSC_VER
#pragma comment(lib, "msimg32.lib")
#endif

// ---------------------------------------------------------------------------
// palette
// ---------------------------------------------------------------------------
static const COLORREF kClrBack      = RGB(24, 26, 31);
static const COLORREF kClrRuler     = RGB(34, 37, 44);
static const COLORREF kClrRulerLine = RGB(70, 75, 86);
static const COLORREF kClrRulerText = RGB(170, 176, 188);
static const COLORREF kClrRowA      = RGB(30, 33, 39);
static const COLORREF kClrRowB      = RGB(27, 30, 36);
static const COLORREF kClrRowBrd    = RGB(48, 52, 61);
static const COLORREF kClrPanel     = RGB(36, 39, 46);
static const COLORREF kClrText      = RGB(214, 218, 226);
static const COLORREF kClrTextDim   = RGB(138, 144, 156);
static const COLORREF kClrSelFill   = RGB(0, 110, 190);
static const COLORREF kClrSelBrd    = RGB(0, 190, 255);
static const COLORREF kClrBlackFill = RGB(150, 30, 30);
static const COLORREF kClrBlackBrd  = RGB(235, 80, 80);
static const COLORREF kClrDropBrd   = RGB(96, 100, 110);
static const COLORREF kClrCurrent   = RGB(255, 200, 60);
static const COLORREF kClrPlaceholder = RGB(44, 48, 56);
static const COLORREF kClrHdr        = RGB(255, 168, 60);   // HDR 标识高亮色

static void AlphaFillRect(HDC dc, const RECT& rc, COLORREF c, BYTE alpha)
{
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1;
    bi.bmiHeader.biHeight = 1;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC mdc = ::CreateCompatibleDC(dc);
    HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!mdc || !bmp || !bits)
    {
        if (bmp) ::DeleteObject(bmp);
        if (mdc) ::DeleteDC(mdc);
        return;
    }
    BYTE* px = (BYTE*)bits;
    px[0] = (BYTE)(GetBValue(c) * alpha / 255);
    px[1] = (BYTE)(GetGValue(c) * alpha / 255);
    px[2] = (BYTE)(GetRValue(c) * alpha / 255);
    px[3] = alpha;

    HGDIOBJ old = ::SelectObject(mdc, bmp);
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    ::AlphaBlend(dc, rc.left, rc.top, w, h, mdc, 0, 0, 1, 1, bf);
    ::SelectObject(mdc, old);
    ::DeleteObject(bmp);
    ::DeleteDC(mdc);
}

static void FillSolid(HDC dc, const RECT& rc, COLORREF c)
{
    HBRUSH b = ::CreateSolidBrush(c);
    ::FillRect(dc, &rc, b);
    ::DeleteObject(b);
}

static void FrameRectColor(HDC dc, const RECT& rc, COLORREF c, int thickness)
{
    HBRUSH b = ::CreateSolidBrush(c);
    RECT r = rc;
    for (int i = 0; i < thickness; ++i)
    {
        ::FrameRect(dc, &r, b);
        ::InflateRect(&r, -1, -1);
    }
    ::DeleteObject(b);
}

std::wstring ThumbKey::Describe() const
{
    return FormatString(L"%s|%lld|%.3f|%.3f|%d|%d|%d|%d",
                        file.c_str(), mtime, t0, t1, cols, rows, tw, th);
}

std::wstring ThumbCacheDir()
{
    std::wstring dir = PathCombine(GetLocalAppDataDir(), L"FastVideoCut\\cache");
    EnsureDirectory(dir);
    return dir;
}

static std::wstring HashName(const std::wstring& key)
{
    std::hash<std::wstring> h;
    size_t a = h(key);
    size_t b = h(key + L"#salt");
    return FormatString(L"fvc_%08x%08x.bmp", (unsigned)(a & 0xffffffffu), (unsigned)(b & 0xffffffffu));
}

void ClearThumbCache()
{
    std::wstring dir = ThumbCacheDir();
    std::vector<std::wstring> files = ListFilesByExt(dir, std::vector<std::wstring>(1, L".bmp"));
    for (size_t i = 0; i < files.size(); ++i)
    {
        ::SetFileAttributesW(files[i].c_str(), FILE_ATTRIBUTE_NORMAL);
        ::DeleteFileW(files[i].c_str());
    }
}

void PruneThumbCache(size_t maxFiles)
{
    std::wstring dir = ThumbCacheDir();
    std::vector<std::wstring> files = ListFilesByExt(dir, std::vector<std::wstring>(1, L".bmp"));
    if (files.size() <= maxFiles) return;

    std::vector<std::pair<long long, std::wstring> > byTime;
    for (size_t i = 0; i < files.size(); ++i)
        byTime.push_back(std::make_pair(FileModifiedTime(files[i]), files[i]));
    std::sort(byTime.begin(), byTime.end());

    size_t remove = byTime.size() - maxFiles;
    for (size_t i = 0; i < remove; ++i)
    {
        ::SetFileAttributesW(byTime[i].second.c_str(), FILE_ATTRIBUTE_NORMAL);
        ::DeleteFileW(byTime[i].second.c_str());
    }
}

// ---------------------------------------------------------------------------
// construction / window plumbing
// ---------------------------------------------------------------------------
TimelineView::TimelineView()
{
}

TimelineView::~TimelineView()
{
    Shutdown();
}

bool TimelineView::Create(HWND parent, int id, HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &TimelineView::WndProcStatic;
    wc.hInstance = hInst;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = ClassName();
    ::RegisterClassExW(&wc);        // fine if already registered

    hwnd_ = ::CreateWindowExW(0, ClassName(), L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                              0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, hInst, this);
    if (!hwnd_) return false;

    // Both bars are self-painted now (same look as the log view). See
    // ScrollBarView.h - it reports WM_HSCROLL / WM_VSCROLL with an emulated
    // SB_* code, so the handlers below are unchanged apart from where they get
    // their range/page numbers from.
    hBar_.Create(hwnd_, 1, hInst, false);
    // 视频行很多时（10 个以上）下面的行点不到，用垂直滚动条 + 滚轮上下翻
    vBar_.Create(hwnd_, 2, hInst, true);
    StartMouseTracking();
    ::DragAcceptFiles(hwnd_, TRUE);

    HDC dc = ::GetDC(hwnd_);
    int dpi = dc ? ::GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ::ReleaseDC(hwnd_, dc);
    if (dpi <= 0) dpi = 96;

    LOGFONTW lf;
    ::ZeroMemory(&lf, sizeof(lf));
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    ::lstrcpynW(lf.lfFaceName, L"Microsoft YaHei UI", LF_FACESIZE);
    lf.lfHeight = -MulDiv(13, dpi, 96);
    fontNormal_ = ::CreateFontIndirectW(&lf);
    lf.lfWeight = FW_SEMIBOLD;
    fontBold_ = ::CreateFontIndirectW(&lf);
    lf.lfWeight = FW_NORMAL;
    lf.lfHeight = -MulDiv(11, dpi, 96);
    fontSmall_ = ::CreateFontIndirectW(&lf);

    cacheDir_ = ThumbCacheDir();

    // worker 永远启动：它 cv_.wait 阻塞在空队列上，不消耗 CPU。之所以不按
    // thumbsEnabled_ 决定，是为了让运行中改设置能立刻生效（否则先关后开
    // 就再也没有线程去抽帧了）。
    worker_ = std::thread(&TimelineView::WorkerMain, this);
    return true;
}

void TimelineView::SetThumbsEnabled(bool on)
{
    if (on == thumbsEnabled_) return;
    thumbsEnabled_ = on;
    if (!on)
    {
        // 关掉时把已经解码到内存里的位图释放掉（否则会一直占着几百 MB）
        ClearThumbs();
        std::lock_guard<std::mutex> lk(mtx_);
        queue_.clear();
        pendingKeys_.clear();
    }
    Refresh();
}

void TimelineView::Shutdown()
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
        queue_.clear();
        pendingKeys_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();

    ClearThumbs();
    if (fontNormal_) { ::DeleteObject(fontNormal_); fontNormal_ = nullptr; }
    if (fontBold_) { ::DeleteObject(fontBold_); fontBold_ = nullptr; }
    if (fontSmall_) { ::DeleteObject(fontSmall_); fontSmall_ = nullptr; }
    if (memBmp_) { ::DeleteObject(memBmp_); memBmp_ = nullptr; }
    if (memDC_) { ::DeleteDC(memDC_); memDC_ = nullptr; }
}

void TimelineView::Attach(Project* project, Ffmpeg* ffmpeg, AppSettings* settings)
{
    project_ = project;
    ffmpeg_ = ffmpeg;
    settings_ = settings;
    if (settings_) thumbH_ = settings_->thumbHeight;
    pendingFit_ = true;
    Refresh();
}

void TimelineView::Refresh()
{
    if (!hwnd_) return;
    // Re-fit whenever the timeline is not under manual control: after a file was
    // added or analysed the real duration is known for the first time.
    if (pendingFit_ || !userZoomed_) FitToWidth();

    int maxOff = MaxRowOffset();        // 行数变化后把纵向滚动位置收回范围内
    if (rowOffset_ > maxOff) rowOffset_ = maxOff;
    if (rowOffset_ < 0) rowOffset_ = 0;

    UpdateScrollBar();
    UpdateVScrollBar();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void TimelineView::NotifyResized()
{
    if (!hwnd_) return;
    // FitToWidth() runs with the placeholder size (10x10) when Create() is
    // called, which would leave the strip squeezed into ~60 pixels. Re-fit as
    // soon as the real layout is known, until the user zooms/scrolls himself.
    if (!userZoomed_) FitToWidth();
    UpdateScrollBar();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void TimelineView::SetThumbHeight(int h)
{
    if (h < 24) h = 24;
    if (h > 240) h = 240;
    thumbH_ = h;
    epoch_++;
    Refresh();
}

void TimelineView::FitToWidth()
{
    if (!project_ || !hwnd_) return;
    double maxDur = project_->MaxDuration();
    if (maxDur <= 0.0) maxDur = 60.0;

    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    int stripPx = rc.right - kLeftPanelW;
    if (stripPx < 60) stripPx = 60;

    viewStart_ = 0.0;
    pxPerSec_ = (double)stripPx / maxDur;
    if (pxPerSec_ < 0.2) pxPerSec_ = 0.2;
    pendingFit_ = false;
    epoch_++;
    UpdateScrollBar();
}

void TimelineView::ZoomBy(double factor, int anchorX)
{
    if (factor <= 0.0) return;
    userZoomed_ = true;                      // stop auto-fitting from now on
    double anchorTime = XToTime(anchorX);
    double newRate = pxPerSec_ * factor;
    if (newRate < 0.2) newRate = 0.2;
    if (newRate > 4000.0) newRate = 4000.0;
    pxPerSec_ = newRate;
    viewStart_ = anchorTime - (double)(anchorX - kLeftPanelW) / pxPerSec_;
    if (viewStart_ < 0.0) viewStart_ = 0.0;

    epoch_++;                       // invalidate pending thumbnail work
    UpdateScrollBar();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void TimelineView::ScrollPixels(int dx)
{
    if (dx != 0) userZoomed_ = true;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    double visSec = (double)(rc.right - kLeftPanelW) / pxPerSec_;

    double maxStart = project_ ? project_->MaxDuration() : 0.0;
    if (maxStart > visSec) maxStart -= visSec; else maxStart = 0.0;

    viewStart_ += (double)dx / pxPerSec_;
    if (viewStart_ < 0.0) viewStart_ = 0.0;
    if (viewStart_ > maxStart) viewStart_ = maxStart;

    epoch_++;
    UpdateScrollBar();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void TimelineView::SetCurrentItem(int index)
{
    currentItem_ = index;
    EnsureRowVisible(index);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void TimelineView::LayoutChildren()
{
    if (!hwnd_) return;
    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    clientW_ = rc.right;
    clientH_ = rc.bottom;
    if (hBar_.hwnd())
        ::MoveWindow(hBar_.hwnd(), 0, rc.bottom - kScrollH, rc.right, kScrollH, TRUE);

    int maxOff = MaxRowOffset();
    if (rowOffset_ > maxOff) rowOffset_ = maxOff;
    if (rowOffset_ < 0) rowOffset_ = 0;
    UpdateVScrollBar();
}

void TimelineView::UpdateScrollBar()
{
    if (!hBar_.hwnd() || !project_) return;
    double maxDur = project_->MaxDuration();
    if (maxDur <= 0.0) maxDur = 1.0;

    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    double visSec = (double)(rc.right - kLeftPanelW) / pxPerSec_;
    if (visSec < 0.001) visSec = 0.001;

    hMin_ = 0;
    hMax_ = (int)(maxDur * 100.0);
    if (hMax_ < 100) hMax_ = 100;
    hPage_ = (int)(visSec * 100.0);
    if (hPage_ < 10) hPage_ = 10;

    hBar_.SetRange(hMin_, hMax_, hPage_);
    hBar_.SetValue((int)(viewStart_ * 100.0));
}

// ---------------------------------------------------------------------------
// vertical scrolling of the video rows
// ---------------------------------------------------------------------------
void TimelineView::StartMouseTracking()
{
    TRACKMOUSEEVENT tme;
    ::ZeroMemory(&tme, sizeof(tme));
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd_;
    ::TrackMouseEvent(&tme);
}

int TimelineView::ViewportH() const
{
    int h = clientH_ - kRulerH - kScrollH;
    return (h > 0) ? h : 0;
}

int TimelineView::MaxRowOffset() const
{
    if (!project_ || project_->items.empty()) return 0;
    int n = (int)project_->items.size();
    int contentH = n * RowHeight() + (n - 1) * kRowGap;
    int maxOff = contentH - ViewportH();
    return (maxOff > 0) ? maxOff : 0;
}

void TimelineView::ScrollRows(int dy)
{
    if (dy == 0) return;
    int before = rowOffset_;
    rowOffset_ += dy;
    int maxOff = MaxRowOffset();
    if (rowOffset_ < 0) rowOffset_ = 0;
    if (rowOffset_ > maxOff) rowOffset_ = maxOff;
    if (rowOffset_ == before) return;
    UpdateVScrollBar();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void TimelineView::EnsureRowVisible(int index)
{
    if (!project_ || index < 0 || index >= (int)project_->items.size()) return;
    int stride = RowHeight() + kRowGap;
    int top = index * stride;
    int view = ViewportH();
    int before = rowOffset_;

    if (top < rowOffset_)                          rowOffset_ = top;
    else if (top + RowHeight() > rowOffset_ + view) rowOffset_ = top + RowHeight() - view;

    int maxOff = MaxRowOffset();
    if (rowOffset_ < 0) rowOffset_ = 0;
    if (rowOffset_ > maxOff) rowOffset_ = maxOff;
    if (rowOffset_ == before) return;

    UpdateVScrollBar();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void TimelineView::UpdateVScrollBar()
{
    if (!vBar_.hwnd() || !hwnd_) return;
    int maxOff = MaxRowOffset();
    int view = ViewportH();
    if (view <= 0) view = 1;

    // 只有真的放不下才显示，否则一直占着右边一条
    BOOL wantVisible = (maxOff > 0) ? TRUE : FALSE;
    if (::IsWindowVisible(vBar_.hwnd()) != wantVisible)
    {
        ::ShowWindow(vBar_.hwnd(), wantVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (!wantVisible) return;

    ::MoveWindow(vBar_.hwnd(), clientW_ - kVScrollW, kRulerH, kVScrollW, view, TRUE);

    vMin_  = 0;
    vMax_  = maxOff;
    vPage_ = view;
    vBar_.SetRange(vMin_, vMax_, vPage_);
    vBar_.SetValue(rowOffset_);
}

// ---------------------------------------------------------------------------
// geometry helpers
// ---------------------------------------------------------------------------
int TimelineView::RowHeight() const
{
    int h = thumbH_ + 14;
    // 左侧面板有 4 行（标题 / 分辨率·帧率 / 视频编码 / 音频编码+状态），最小高度要放得下
    if (h < kMinRowH) h = kMinRowH;
    return h;
}

int TimelineView::TimeToX(double t) const
{
    return kLeftPanelW + (int)((t - viewStart_) * pxPerSec_ + 0.5);
}

double TimelineView::XToTime(int x) const
{
    return viewStart_ + (double)(x - kLeftPanelW) / pxPerSec_;
}

int TimelineView::ThumbWidthFor(const VideoItem& item) const
{
    double aspect = 16.0 / 9.0;
    if (item.info.width > 0 && item.info.height > 0)
        aspect = (double)item.info.width / (double)item.info.height;
    if (aspect < 0.5) aspect = 0.5;
    if (aspect > 3.0) aspect = 3.0;
    int w = (int)(thumbH_ * aspect + 0.5);
    if (w < 40) w = 40;
    if (w > 260) w = 260;
    return w;
}

int TimelineView::RowAt(int y, int* rowTop) const
{
    if (!project_ || project_->items.empty()) return -1;
    if (y < kRulerH) return -1;
    int stride = RowHeight() + kRowGap;
    int idx = (y - kRulerH + rowOffset_) / stride;
    if (idx < 0 || idx >= (int)project_->items.size()) return -1;
    if (rowTop) *rowTop = kRulerH + idx * stride - rowOffset_;
    return idx;
}

int TimelineView::SegmentAt(int index, int x) const
{
    if (!project_ || index < 0 || index >= (int)project_->items.size()) return -1;
    if (x < kLeftPanelW) return -1;
    const VideoItem& it = project_->items[index];
    double t = XToTime(x);
    for (size_t i = 0; i < it.segments.size(); ++i)
    {
        if (t >= it.segments[i].t0 - 0.001 && t <= it.segments[i].t1 + 0.001)
            return (int)i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// window procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK TimelineView::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    TimelineView* self = (TimelineView*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        self = (TimelineView*)cs->lpCreateParams;
        if (self)
        {
            self->hwnd_ = hwnd;
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        }
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(msg, wp, lp);
}

LRESULT TimelineView::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_SIZE:
        LayoutChildren();
        UpdateScrollBar();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONDOWN:
        ::SetFocus(hwnd_);
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONDBLCLK:
        OnLButtonDblClk(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_RBUTTONUP:
        OnRButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        // WM_MOUSEWHEEL 只发给键盘焦点所在的窗口：如果焦点停在工具栏按钮上，
        // 鼠标停在这块区域滚动是没反应的，所以鼠标进来就把焦点拿过来。
        if (::GetFocus() != hwnd_) ::SetFocus(hwnd_);
        StartMouseTracking();      // 指针还在窗口内，重新挂接离开通知
        return 0;

    case WM_MOUSELEAVE:
        // 注意：这里绝对不要再调 TrackMouseEvent 重新挂接。指针已经不在窗口里时
        // 重新挂接会立刻再投一条 WM_MOUSELEAVE，形成死循环（表现为 CPU 跑满、
        // 窗口“卡死”）。需要继续跟踪就在 WM_MOUSEMOVE 里重新挂接。
        return 0;

    case WM_MOUSEWHEEL:
    {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        ::ScreenToClient(hwnd_, &pt);
        UINT keys = GET_KEYSTATE_WPARAM(wp);
        OnMouseWheel(pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wp),
                     (keys & MK_CONTROL) != 0, (keys & MK_SHIFT) != 0);
        return 0;
    }

    case WM_MBUTTONDOWN:
        dragging_ = true;
        dragStartX_ = GET_X_LPARAM(lp);
        dragStartTime_ = viewStart_;
        ::SetCapture(hwnd_);
        return 0;

    case WM_MBUTTONUP:
        if (dragging_)
        {
            dragging_ = false;
            ::ReleaseCapture();
        }
        return 0;

    case WM_HSCROLL:
    {
        // Range/page come from UpdateScrollBar's cached copy; the position
        // rides along in HIWORD(wp) from ScrollBarView.
        const int code = LOWORD(wp);
        int pos = HIWORD(wp);
        if (code != SB_THUMBTRACK && code != SB_THUMBPOSITION) pos = hBar_.Value();
        if (code == SB_LINELEFT)  pos -= 20;
        else if (code == SB_LINERIGHT) pos += 20;
        else if (code == SB_PAGELEFT)  pos -= hPage_;
        else if (code == SB_PAGERIGHT) pos += hPage_;
        else if (code == SB_LEFT)      pos = hMin_;
        else if (code == SB_RIGHT)     pos = hMax_;

        if (pos < hMin_) pos = hMin_;
        if (pos > hMax_ - hPage_ + 1) pos = hMax_ - hPage_ + 1;
        if (pos < 0) pos = 0;

        viewStart_ = (double)pos / 100.0;
        epoch_++;
        UpdateScrollBar();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }

    case WM_VSCROLL:
    {
        const int code = LOWORD(wp);
        int pos = HIWORD(wp);
        if (code != SB_THUMBTRACK && code != SB_THUMBPOSITION) pos = vBar_.Value();
        if (code == SB_LINEUP)     pos -= RowHeight();
        else if (code == SB_LINEDOWN)  pos += RowHeight();
        else if (code == SB_PAGEUP)    pos -= vPage_;
        else if (code == SB_PAGEDOWN)  pos += vPage_;
        else if (code == SB_TOP)       pos = vMin_;
        else if (code == SB_BOTTOM)    pos = vMax_;

        int maxOff = MaxRowOffset();
        if (pos < 0) pos = 0;
        if (pos > maxOff) pos = maxOff;
        if (pos == rowOffset_) return 0;

        rowOffset_ = pos;
        UpdateVScrollBar();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
    {
        bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        int x = (clientW_ > kLeftPanelW) ? (kLeftPanelW + (clientW_ - kLeftPanelW) / 2) : kLeftPanelW + 100;
        switch (wp)
        {
        case VK_ADD: case VK_OEM_PLUS:      ZoomBy(1.25, x); return 0;
        case VK_SUBTRACT: case VK_OEM_MINUS: ZoomBy(1.0 / 1.25, x); return 0;
        case VK_LEFT:  ScrollPixels(-60); return 0;
        case VK_RIGHT: ScrollPixels(60);  return 0;
        case VK_UP:    ScrollRows(-(RowHeight() + kRowGap)); return 0;
        case VK_DOWN:  ScrollRows(RowHeight() + kRowGap);  return 0;
        case VK_PRIOR: ScrollRows(-ViewportH()); return 0;
        case VK_NEXT:  ScrollRows(ViewportH());  return 0;
        case VK_HOME:  viewStart_ = 0.0; epoch_++; UpdateScrollBar(); ::InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case VK_END:
            {
                double visSec = (double)(clientW_ - kLeftPanelW) / pxPerSec_;
                double maxDur = project_ ? project_->MaxDuration() : 0.0;
                viewStart_ = maxDur > visSec ? maxDur - visSec : 0.0;
                epoch_++; UpdateScrollBar(); ::InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
        case 'A':
            if (ctrl) { SelectAllAll(); return 0; }
            break;
        case 'B':
            if (ctrl) { SelectBodyAll(); return 0; }
            break;
        case 'R':
            if (ctrl) { ClearKeepAll(); return 0; }
            break;
        case VK_F5:
            FitToWidth();
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        break;
    }

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_DROPFILES:
        OnDropFiles((HDROP)wp);
        return 0;

    case WM_FVC_THUMBS:
        OnThumbReady((int)wp);
        return 0;

    case WM_DESTROY:
        Shutdown();
        return 0;
    }
    return ::DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// painting
// ---------------------------------------------------------------------------
namespace
{
    double ChooseTickStep(double pxPerSec)
    {
        static const double steps[] = { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0,
                                        60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0 };
        for (size_t i = 0; i < _countof(steps); ++i)
            if (steps[i] * pxPerSec >= 70.0) return steps[i];
        return 7200.0;
    }
}

void TimelineView::OnPaint()
{
    PAINTSTRUCT ps;
    HDC dc = ::BeginPaint(hwnd_, &ps);

    RECT rc;
    ::GetClientRect(hwnd_, &rc);
    if (rc.right < 1) rc.right = 1;
    if (rc.bottom < 1) rc.bottom = 1;

    if (!memDC_) memDC_ = ::CreateCompatibleDC(dc);
    if (memBmp_)
    {
        BITMAP bm;
        if (::GetObjectW(memBmp_, sizeof(bm), &bm) != 0 &&
            (bm.bmWidth != rc.right || bm.bmHeight != rc.bottom))
        {
            ::DeleteObject(memBmp_);
            memBmp_ = nullptr;
        }
    }
    if (!memBmp_) memBmp_ = ::CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = ::SelectObject(memDC_, memBmp_);

    EvictThumbs();

    FillSolid(memDC_, rc, kClrBack);

    RECT paintRc = rc;
    paintRc.bottom -= kScrollH;
    if (paintRc.bottom < kRulerH + 8) paintRc.bottom = kRulerH + 8;
    // 垂直滚动条占掉右侧一条，帧流不要画到它下面
    if (vBar_.hwnd() && ::IsWindowVisible(vBar_.hwnd()))
    {
        paintRc.right -= kVScrollW;
        if (paintRc.right < kLeftPanelW + 20) paintRc.right = kLeftPanelW + 20;
    }

    if (!project_ || project_->items.empty())
    {
        DrawEmptyState(memDC_, paintRc);
    }
    else
    {
        DrawLeftPanel(memDC_, paintRc);

        RECT rulerRc = paintRc;
        rulerRc.bottom = kRulerH;
        DrawRuler(memDC_, rulerRc);

        int stride = RowHeight() + kRowGap;
        int y = kRulerH - rowOffset_;
        for (size_t i = 0; i < project_->items.size(); ++i)
        {
            RECT rowRc;
            rowRc.left = 0;
            rowRc.right = paintRc.right;
            rowRc.top = y;
            rowRc.bottom = y + RowHeight();
            if (rowRc.top < paintRc.bottom && rowRc.bottom > kRulerH)
            {
                // 纵向滚动时上方的行会滑到标尺下面，裁掉不要盖住标尺
                int saved = ::SaveDC(memDC_);
                ::IntersectClipRect(memDC_, 0, kRulerH, paintRc.right, paintRc.bottom);
                DrawRow(memDC_, (int)i, rowRc);
                ::RestoreDC(memDC_, saved);
            }
            y += stride;
            if (y > paintRc.bottom) break;
        }
    }

    ::BitBlt(dc, 0, 0, rc.right, rc.bottom, memDC_, 0, 0, SRCCOPY);
    ::SelectObject(memDC_, oldBmp);
    ::EndPaint(hwnd_, &ps);
}

void TimelineView::DrawEmptyState(HDC dc, const RECT& rc)
{
    HGDIOBJ oldFont = ::SelectObject(dc, fontNormal_);
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, kClrTextDim);

    const wchar_t* line1 = TR(L"把视频文件拖到这里，或点击左上角“添加视频”",
                               L"Drop video files here, or click \"Add videos\" at the top left");
    const wchar_t* line2 = TR(L"支持 mp4 / mkv / mov / avi / flv / ts / wmv ...",
                               L"Supported: mp4 / mkv / mov / avi / flv / ts / wmv ...");
    const wchar_t* line3 = TR(L"自动分析后可点击帧流上的分段进行选择（选中=保留）",
                               L"After analysing, click a segment in the strip to keep it");

    RECT r = rc;
    r.top = rc.top + (rc.bottom - rc.top) / 2 - 46;
    ::DrawTextW(dc, line1, -1, &r, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

    ::SelectObject(dc, fontSmall_);
    ::SetTextColor(dc, RGB(110, 116, 128));
    r.top += 30;
    ::DrawTextW(dc, line2, -1, &r, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    r.top += 22;
    ::DrawTextW(dc, line3, -1, &r, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);

    ::SelectObject(dc, oldFont);
}

void TimelineView::DrawLeftPanel(HDC dc, const RECT& rc)
{
    RECT p = rc;
    p.right = kLeftPanelW;
    FillSolid(dc, p, kClrPanel);

    RECT h = p;
    h.bottom = kRulerH;
    FillSolid(dc, h, kClrRuler);

    HGDIOBJ oldFont = ::SelectObject(dc, fontBold_);
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, kClrText);
    RECT tr = h;
    tr.left += 10;
    tr.top += 3;
    ::DrawTextW(dc, TR(L"视频 / 规格 / 黑屏", L"Video / format / black"),
             -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    ::SelectObject(dc, oldFont);

    RECT b = p;
    b.left = kLeftPanelW - 1;
    FillSolid(dc, b, kClrRowBrd);
}

void TimelineView::DrawRuler(HDC dc, const RECT& rc)
{
    FillSolid(dc, rc, kClrRuler);
    RECT line = rc;
    line.top = rc.bottom - 1;
    FillSolid(dc, line, kClrRulerLine);

    double visEnd = XToTime(rc.right);
    double step = ChooseTickStep(pxPerSec_);

    HGDIOBJ oldFont = ::SelectObject(dc, fontSmall_);
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, kClrRulerText);

    double first = std::floor(viewStart_ / step) * step;
    for (double t = first; t <= visEnd + step; t += step)
    {
        if (t < -0.0001) continue;
        int x = TimeToX(t);
        if (x < kLeftPanelW || x > rc.right) continue;

        RECT tick;
        tick.left = x;
        tick.top = rc.bottom - 6;
        tick.right = x + 1;
        tick.bottom = rc.bottom;
        FillSolid(dc, tick, kClrRulerLine);

        std::wstring label = (step >= 1.0) ? FormatClock(t)
                                           : FormatString(L"%.1fs", t);
        RECT tr;
        tr.left = x + 3;
        tr.top = rc.top + 2;
        tr.right = x + 100;
        tr.bottom = rc.bottom - 4;
        ::DrawTextW(dc, label.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    }

    // cursor line under the mouse
    if (hoverItem_ >= 0)
    {
        POINT pt;
        if (::GetCursorPos(&pt) && ::ScreenToClient(hwnd_, &pt) && pt.x > kLeftPanelW)
        {
            RECT cl;
            cl.left = pt.x;
            cl.top = rc.top;
            cl.right = pt.x + 1;
            cl.bottom = rc.bottom;
            FillSolid(dc, cl, kClrCurrent);
        }
    }

    ::SelectObject(dc, oldFont);
}

void TimelineView::DrawRow(HDC dc, int index, const RECT& rc)
{
    if (!project_ || index < 0 || index >= (int)project_->items.size()) return;
    const VideoItem& it = project_->items[index];

    FillSolid(dc, rc, (index % 2) ? kClrRowB : kClrRowA);

    // ---- left panel text --------------------------------------------------
    HGDIOBJ oldFont = ::SelectObject(dc, fontBold_);
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, (index == currentItem_) ? kClrCurrent : kClrText);

    RECT tr;
    tr.left = 8;
    tr.top = rc.top + 4;
    tr.right = kLeftPanelW - 10;
    tr.bottom = rc.top + 22;
    std::wstring title = FormatString(L"%d. %s", index + 1, it.name.c_str());
    ::DrawTextW(dc, title.c_str(), -1, &tr,
                DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    ::SelectObject(dc, fontSmall_);
    ::SetTextColor(dc, kClrTextDim);

    // 左侧面板四行：
    //   1. 视频名
    //   2. 1920x1080 | VBR | 25fps
    //   3. HEVC | Main@L4 | yuv420p
    //   4. AAC LC | 2.0 | 48K            + 右侧状态
    RECT l2 = tr;
    l2.top = rc.top + 22;
    l2.bottom = rc.top + 38;
    ::DrawTextW(dc, it.streamLine().c_str(), -1, &l2,
                DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    RECT l3 = l2;
    l3.top = rc.top + 38;
    l3.bottom = rc.top + 54;
    ::DrawTextW(dc, it.videoLine().c_str(), -1, &l3,
                DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    RECT l4 = l3;
    l4.top = rc.top + 54;
    l4.bottom = rc.top + 70;
    if (it.status == ItemStatus::Error)
    {
        // 出错时第 4 行整行让给错误详情。状态栏右侧那格此时是空的（错误信息
        // 往往比半行宽，硬塞右半边会被截得看不出是什么错），所以直接占满整行。
        ::SetTextColor(dc, kClrBlackBrd);
        ::DrawTextW(dc, (TR(L"错误: ", L"Error: ") + it.message).c_str(), -1, &l4,
                    DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    else
    {
        std::wstring audio = it.audioLine();
        if (!audio.empty())
        {
            // 位深 > 8 的片子（HDR / 10bit 源）用高亮色标出，一眼能挑出来
            ::SetTextColor(dc, it.info.bitDepth() > 8 ? kClrHdr : kClrTextDim);
            ::DrawTextW(dc, audio.c_str(), -1, &l4,
                        DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        // 状态右对齐放在同一行的右半边，省一行高度
        RECT st = l4;
        st.left = (l4.left + l4.right) / 2;
        ::SetTextColor(dc, kClrTextDim);
        ::DrawTextW(dc, it.statusText().c_str(), -1, &st,
                    DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    // ---- frame strip ------------------------------------------------------
    RECT strip = rc;
    strip.left = kLeftPanelW;
    if (strip.right > strip.left + 2)
    {
        FillSolid(dc, strip, RGB(16, 18, 22));
        double dur = it.info.duration;
        if (dur <= 0.0)
        {
            RECT ph = strip;
            ph.left += 8;
            ph.right -= 8;
            ph.top += 4;
            ph.bottom = rc.top + 4 + thumbH_;
            FillSolid(dc, ph, kClrPlaceholder);
            ::SetTextColor(dc, kClrTextDim);
            ::SetBkMode(dc, TRANSPARENT);
            RECT txt = ph;
            const wchar_t* msg = (it.status == ItemStatus::Error)
                                     ? TR(L"读取失败", L"Probe failed")
                                     : TR(L"待自动分析", L"Not analysed yet");
            ::DrawTextW(dc, msg, -1, &txt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        else
        {
            DrawRowStrip(dc, index, rc, strip);
        }
    }

    RECT brd = rc;
    brd.right = rc.right - 1;
    brd.bottom = rc.bottom - 1;
    if (index == currentItem_) FrameRectColor(dc, brd, kClrCurrent, 1);
    else FrameRectColor(dc, brd, kClrRowBrd, 1);

    ::SelectObject(dc, oldFont);
}

void TimelineView::DrawRowStrip(HDC dc, int index, const RECT& rc, const RECT& strip)
{
    const VideoItem& it = project_->items[index];
    double dur = it.info.duration;

    double t0 = std::max(0.0, viewStart_);
    double t1 = std::min(dur, XToTime(strip.right));
    if (t1 <= t0) return;

    int tw = ThumbWidthFor(it);
    int stripPx = strip.right - strip.left;
    int cols = (int)std::ceil((double)stripPx / (double)tw);
    if (cols < 1) cols = 1;
    if (cols > kMaxTiles) cols = kMaxTiles;

    ThumbKey key;
    key.file = it.path;
    key.mtime = FileModifiedTime(it.path);
    key.t0 = t0;
    key.t1 = t1;
    key.cols = cols;
    key.rows = 1;
    key.tw = tw;
    key.th = thumbH_;

    bool needWorker = false;
    ThumbImage* img = LookupThumb(key, &needWorker);
    if (!img && thumbsEnabled_) EnsureThumbFor(index, t0, t1, cols, 1, tw, thumbH_);

    if (img && img->bmp)
    {
        HDC src = ::CreateCompatibleDC(dc);
        HGDIOBJ ob = ::SelectObject(src, img->bmp);
        int tileW = img->w / cols;
        if (tileW <= 0) tileW = img->w;
        int tileH = img->h;
        if (tileH <= 0) tileH = 1;

        int oldMode = ::SetStretchBltMode(dc, HALFTONE);
        ::SetBrushOrgEx(dc, 0, 0, nullptr);

        double span = (t1 - t0) / (double)cols;
        for (int i = 0; i < cols; ++i)
        {
            int dx0 = TimeToX(t0 + span * i);
            int dx1 = TimeToX(t0 + span * (i + 1));
            if (dx1 <= strip.left || dx0 >= strip.right || dx1 <= dx0) continue;

            int cx0 = std::max(dx0, (int)strip.left);
            int cx1 = std::min(dx1, (int)strip.right);
            if (cx1 <= cx0) continue;

            int sx0 = i * tileW;
            int sx1 = sx0 + tileW;
            if (dx0 < strip.left)
                sx0 += (int)((double)(strip.left - dx0) / (double)(dx1 - dx0) * tileW);
            if (dx1 > strip.right)
                sx1 -= (int)((double)(dx1 - strip.right) / (double)(dx1 - dx0) * tileW);
            if (sx1 <= sx0) continue;

            ::StretchBlt(dc, cx0, rc.top + 4, cx1 - cx0, thumbH_,
                         src, sx0, 0, sx1 - sx0, tileH, SRCCOPY);
        }
        ::SetStretchBltMode(dc, oldMode);
        ::SelectObject(src, ob);
        ::DeleteDC(src);
    }
    else
    {
        RECT ph = strip;
        ph.left += 8;
        ph.right -= 8;
        ph.top += 4;
        ph.bottom = rc.top + 4 + thumbH_;
        FillSolid(dc, ph, kClrPlaceholder);
        ::SetTextColor(dc, kClrTextDim);
        ::SetBkMode(dc, TRANSPARENT);
        RECT txt = ph;
        ::DrawTextW(dc, TR(L"正在展开视频帧流…", L"Expanding frame strip..."), -1, &txt,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    DrawSegments(dc, index, rc, strip.left, strip.right);
}

void TimelineView::DrawSegments(HDC dc, int index, const RECT& rc, int x0, int x1)
{
    const VideoItem& it = project_->items[index];

    ::SetBkMode(dc, TRANSPARENT);

    int top = rc.top + 4;
    int bottom = top + thumbH_;

    for (size_t i = 0; i < it.segments.size(); ++i)
    {
        const Segment& s = it.segments[i];
        int sx0 = TimeToX(s.t0);
        int sx1 = TimeToX(s.t1);
        if (sx1 <= x0 || sx0 >= x1) continue;

        RECT r;
        r.left = std::max(sx0, x0);
        r.right = std::min(sx1, x1);
        r.top = top;
        r.bottom = bottom;
        if (r.right - r.left < 2) r.right = r.left + 2;

        if (s.kind == SegKind::Black)
        {
            AlphaFillRect(dc, r, kClrBlackFill, 80);

            int saved = ::SaveDC(dc);
            ::IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
            HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(130, 34, 34));
            HGDIOBJ op = ::SelectObject(dc, pen);
            int h = r.bottom - r.top;
            for (int x = r.left - h; x < r.right; x += 12)
            {
                ::MoveToEx(dc, x, r.bottom, nullptr);
                ::LineTo(dc, x + h, r.top);
            }
            ::SelectObject(dc, op);
            ::DeleteObject(pen);
            ::RestoreDC(dc, saved);

            FrameRectColor(dc, r, kClrBlackBrd, 2);
        }
        else if (s.selected)
        {
            AlphaFillRect(dc, r, kClrSelFill, 60);
            FrameRectColor(dc, r, kClrSelBrd, 2);
        }
        else
        {
            AlphaFillRect(dc, r, RGB(8, 8, 10), 120);
            FrameRectColor(dc, r, kClrDropBrd, 1);
        }

        int w = r.right - r.left;
        if (w >= 54)
        {
            bool isStart = (index == it.keepStart);
            bool isEnd   = (index == it.keepEnd);
            std::wstring label;
            if (s.kind == SegKind::Black) label = FormatString(TR(L"黑屏 %.2fs", L"black %.2fs"), s.length());
            else if (isStart && isEnd)      label = FormatString(TR(L"起止 %.1fs", L"in+out %.1fs"), s.length());
            else if (isStart)               label = FormatString(TR(L"起 %.1fs", L"in %.1fs"), s.length());
            else if (isEnd)                 label = FormatString(TR(L"止 %.1fs", L"out %.1fs"), s.length());
            else label = FormatString(L"%s %.1fs",
                                      s.selected ? TR(L"保留", L"keep") : TR(L"丢弃", L"drop"),
                                      s.length());

            RECT tr;
            tr.left = r.left + 4;
            tr.right = r.right - 3;
            tr.top = r.bottom - 18;
            tr.bottom = r.bottom - 3;
            if (tr.right > tr.left + 12)
            {
                RECT chip = tr;
                chip.left -= 3;
                chip.top -= 2;
                AlphaFillRect(dc, chip, RGB(0, 0, 0), 150);
                ::SetTextColor(dc, s.kind == SegKind::Black
                                   ? RGB(255, 190, 190)
                                   : (s.selected ? RGB(205, 245, 255) : RGB(186, 190, 198)));
                ::DrawTextW(dc, label.c_str(), -1, &tr,
                            DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
        }

        if (sx0 >= x0 && sx0 <= x1)
        {
            RECT bl;
            bl.left = sx0;
            bl.right = sx0 + 1;
            bl.top = top;
            bl.bottom = bottom;
            FillSolid(dc, bl, RGB(210, 214, 222));
        }
    }
}

// ---------------------------------------------------------------------------
// thumbnail cache / worker
// ---------------------------------------------------------------------------
HBITMAP TimelineView::LoadBmpFile(const std::wstring& path, int* ow, int* oh)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;

    DWORD got = 0;
    BITMAPFILEHEADER fh;
    if (!::ReadFile(h, &fh, sizeof(fh), &got, nullptr) || got != sizeof(fh) ||
        fh.bfType != 0x4D42)
    {
        ::CloseHandle(h);
        return nullptr;
    }
    BITMAPINFOHEADER ih;
    if (!::ReadFile(h, &ih, sizeof(ih), &got, nullptr) || got != sizeof(ih))
    {
        ::CloseHandle(h);
        return nullptr;
    }
    if (ih.biPlanes != 1 || ih.biCompression != BI_RGB || ih.biWidth <= 0 ||
        (ih.biBitCount != 24 && ih.biBitCount != 32) || ih.biHeight == 0)
    {
        ::CloseHandle(h);
        return nullptr;
    }

    int w = ih.biWidth;
    int absH = ih.biHeight < 0 ? -ih.biHeight : ih.biHeight;
    int bpp = ih.biBitCount / 8;
    int stride = ((w * bpp) + 3) & ~3;
    size_t dataSize = (size_t)stride * (size_t)absH;

    std::vector<BYTE> data(dataSize);
    DWORD rd = 0;
    BOOL ok = ::ReadFile(h, &data[0], (DWORD)dataSize, &rd, nullptr);
    ::CloseHandle(h);
    if (!ok || rd < dataSize) return nullptr;

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = ih.biHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dc = ::GetDC(nullptr);
    HBITMAP bmp = ::CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, dc);
    if (!bmp || !bits)
    {
        if (bmp) ::DeleteObject(bmp);
        return nullptr;
    }

    BYTE* dst = (BYTE*)bits;
    for (int y = 0; y < absH; ++y)
    {
        const BYTE* srcRow = &data[(size_t)y * stride];
        BYTE* dstRow = dst + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x)
        {
            dstRow[x * 4 + 0] = srcRow[x * bpp + 0];
            dstRow[x * 4 + 1] = srcRow[x * bpp + 1];
            dstRow[x * 4 + 2] = srcRow[x * bpp + 2];
            dstRow[x * 4 + 3] = 255;
        }
    }

    if (ow) *ow = w;
    if (oh) *oh = absH;
    return bmp;
}

std::wstring TimelineView::CachePathFor(const ThumbKey& key) const
{
    std::wstring name = HashName(key.Describe());
    return PathCombine(cacheDir_, name);
}

ThumbImage* TimelineView::LookupThumb(const ThumbKey& key, bool* needsWorker)
{
    if (needsWorker) *needsWorker = false;

    std::map<ThumbKey, ThumbImage>::iterator it = thumbs_.find(key);
    if (it != thumbs_.end())
    {
        // refresh LRU position
        thumbOrder_.remove(key);
        thumbOrder_.push_back(key);
        return &it->second;
    }

    std::wstring cachePath = CachePathFor(key);
    if (FileExists(cachePath))
    {
        int w = 0, h = 0;
        HBITMAP bmp = LoadBmpFile(cachePath, &w, &h);
        if (bmp)
        {
            ThumbImage img;
            img.bmp = bmp;
            img.w = w;
            img.h = h;
            StoreThumb(key, img);
            return &thumbs_[key];
        }
        ::SetFileAttributesW(cachePath.c_str(), FILE_ATTRIBUTE_NORMAL);
        ::DeleteFileW(cachePath.c_str());
    }

    if (needsWorker) *needsWorker = true;
    return nullptr;
}

void TimelineView::StoreThumb(const ThumbKey& key, ThumbImage img)
{
    std::map<ThumbKey, ThumbImage>::iterator it = thumbs_.find(key);
    if (it != thumbs_.end())
    {
        if (it->second.bmp) ::DeleteObject(it->second.bmp);
        it->second = img;
        thumbOrder_.remove(key);
        thumbOrder_.push_back(key);
        return;
    }
    thumbs_[key] = img;
    thumbOrder_.push_back(key);
}

void TimelineView::EvictThumbs()
{
    while (thumbOrder_.size() > cacheLimit_)
    {
        const ThumbKey& k = thumbOrder_.front();
        std::map<ThumbKey, ThumbImage>::iterator it = thumbs_.find(k);
        if (it != thumbs_.end())
        {
            if (it->second.bmp) ::DeleteObject(it->second.bmp);
            thumbs_.erase(it);
        }
        thumbOrder_.pop_front();
    }
}

void TimelineView::ClearThumbs()
{
    for (std::map<ThumbKey, ThumbImage>::iterator it = thumbs_.begin();
         it != thumbs_.end(); ++it)
    {
        if (it->second.bmp) ::DeleteObject(it->second.bmp);
    }
    thumbs_.clear();
    thumbOrder_.clear();
}

void TimelineView::EnsureThumbFor(int index, double t0, double t1, int cols, int rows,
                                  int tw, int th)
{
    if (!project_ || index < 0 || index >= (int)project_->items.size()) return;
    const VideoItem& it = project_->items[index];

    ThumbKey key;
    key.file = it.path;
    key.mtime = FileModifiedTime(it.path);
    key.t0 = t0;
    key.t1 = t1;
    key.cols = cols;
    key.rows = rows;
    key.tw = tw;
    key.th = th;

    std::wstring id = key.Describe();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stop_) return;
        if (pendingKeys_.find(id) != pendingKeys_.end()) return;
        if (queue_.size() > 64) return;              // do not flood ffmpeg
        pendingKeys_.insert(id);
        ThumbRequest req;
        req.key = key;
        req.epoch = epoch_;
        queue_.push_front(req);
    }
    cv_.notify_one();
}

void TimelineView::OnThumbReady(int epoch)
{
    if (epoch != (int)epoch_) return;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void TimelineView::WorkerMain()
{
    for (;;)
    {
        ThumbRequest req;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this]() { return stop_ || !queue_.empty(); });
            if (stop_) return;
            req = queue_.front();
            queue_.pop_front();
        }

        std::wstring outPath = CachePathFor(req.key);
        bool ok = false;

        if (FileExists(outPath))
        {
            ok = true;
        }
        else if (ffmpeg_ && ffmpeg_->available())
        {
            std::string err;
            ok = ffmpeg_->MakeMosaic(req.key.file, req.key.t0, req.key.t1,
                                     req.key.cols, req.key.rows, req.key.tw, req.key.th,
                                     outPath, CancelToken(), err);
        }

        {
            std::lock_guard<std::mutex> lk(mtx_);
            pendingKeys_.erase(req.key.Describe());
            if (stop_) return;
        }

        if (ok && hwnd_)
            ::PostMessageW(hwnd_, WM_FVC_THUMBS, (WPARAM)req.epoch, 0);
    }
}

// ---------------------------------------------------------------------------
// mouse / keyboard interaction
// ---------------------------------------------------------------------------
void TimelineView::OnLButtonDown(int x, int y)
{
    if (!project_ || !hwnd_) return;

    int row = RowAt(y);
    if (row < 0) return;

    if (x < kLeftPanelW)
    {
        currentItem_ = row;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        ::SendMessageW(::GetParent(hwnd_), WM_FVC_SELCHG, 0, (LPARAM)row);
        return;
    }

    int seg = SegmentAt(row, x);
    if (seg >= 0)
    {
        VideoItem& it = project_->items[row];
        // Ctrl+左键 = 单独切换该段；普通左键 = 保留起点
        if (::GetKeyState(VK_CONTROL) < 0)
            Project::ToggleKeepSegment(it, seg);
        else
            Project::ClickKeepStart(it, seg);

        currentItem_ = row;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        ::PostMessageW(::GetParent(hwnd_), WM_FVC_KEEPCHG, (WPARAM)row, 0);
        ::PostMessageW(::GetParent(hwnd_), WM_FVC_PREVIEW, (WPARAM)row, (LPARAM)seg);
    }
    else
    {
        currentItem_ = row;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        ::SendMessageW(::GetParent(hwnd_), WM_FVC_SELCHG, 0, (LPARAM)row);
    }
}

void TimelineView::OnRButtonUp(int x, int y)
{
    if (!project_ || !hwnd_) return;
    int row = RowAt(y);
    if (row < 0) return;
    if (x < kLeftPanelW)
    {
        currentItem_ = row;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        ::SendMessageW(::GetParent(hwnd_), WM_FVC_SELCHG, 0, (LPARAM)row);
        return;
    }

    int seg = SegmentAt(row, x);
    if (seg < 0) return;

    // 右键 = 保留终点
    Project::ClickKeepEnd(project_->items[row], seg);
    currentItem_ = row;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    ::PostMessageW(::GetParent(hwnd_), WM_FVC_KEEPCHG, (WPARAM)row, 0);
    ::PostMessageW(::GetParent(hwnd_), WM_FVC_PREVIEW, (WPARAM)row, (LPARAM)seg);
}

void TimelineView::OnLButtonDblClk(int x, int y)
{
    if (!project_ || !hwnd_) return;
    int row = RowAt(y);
    if (row < 0 || x < kLeftPanelW) return;
    int seg = SegmentAt(row, x);
    if (seg < 0) return;

    // 双击 = 只保留这一段
    Project::SelectOnlySegment(project_->items[row], seg);
    currentItem_ = row;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    ::PostMessageW(::GetParent(hwnd_), WM_FVC_KEEPCHG, (WPARAM)row, 0);
}

void TimelineView::OnMouseMove(int x, int y)
{
    if (!project_ || !hwnd_) return;

    if (dragging_)
    {
        double dx = (double)(x - dragStartX_) / pxPerSec_;
        double maxDur = project_->MaxDuration();
        double visSec = (double)(clientW_ - kLeftPanelW) / pxPerSec_;
        double maxStart = maxDur > visSec ? maxDur - visSec : 0.0;

        viewStart_ = dragStartTime_ - dx;
        if (viewStart_ < 0.0) viewStart_ = 0.0;
        if (viewStart_ > maxStart) viewStart_ = maxStart;
        epoch_++;
        UpdateScrollBar();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    int row = RowAt(y);
    int seg = (row >= 0) ? SegmentAt(row, x) : -1;
    if (row != hoverItem_ || seg != hoverSeg_)
    {
        hoverItem_ = row;
        hoverSeg_ = seg;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }

    LPCWSTR cur = IDC_ARROW;
    if (row >= 0 && x >= kLeftPanelW && seg >= 0) cur = IDC_HAND;
    ::SetCursor(::LoadCursorW(nullptr, cur));
}

void TimelineView::OnMouseWheel(int x, int y, int delta, bool ctrl, bool shift)
{
    (void)y;
    if (ctrl)
    {
        ZoomBy((delta > 0) ? 1.25 : (1.0 / 1.25), x);
        return;
    }

    // 高精度滚轮一格可能只有几十个单位，累积到一整格再翻页，避免滚不动
    wheelRemainder_ += delta;
    int notch = wheelRemainder_ / WHEEL_DELTA;
    if (notch == 0) return;
    wheelRemainder_ -= notch * WHEEL_DELTA;

    if (shift)
    {
        int step = (notch > 0) ? -WHEEL_DELTA * 2 : WHEEL_DELTA * 2;   // Shift = 横向平移
        ScrollPixels(step);
        return;
    }

    // 默认：上下滚动视频列表（红圈 2 的区域），视频多时也能选到下面的行。
    // 滚轮向下(delta<0) 时 rowOffset_ 增大 = 看下面的行。
    ScrollRows(-notch * RowHeight() * 2);
}


// ---------------------------------------------------------------------------
// keep selection commands (applied to every analysed video)
// ---------------------------------------------------------------------------
void TimelineView::SelectBodyAll()
{
    if (!project_) return;
    for (size_t i = 0; i < project_->items.size(); ++i)
        if (project_->items[i].isAnalysed()) Project::SelectBody(project_->items[i]);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    ::PostMessageW(::GetParent(hwnd_), WM_FVC_KEEPCHG, (WPARAM)-1, 0);
}

void TimelineView::SelectAllAll()
{
    if (!project_) return;
    for (size_t i = 0; i < project_->items.size(); ++i)
        if (project_->items[i].isAnalysed()) Project::SelectAll(project_->items[i]);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    ::PostMessageW(::GetParent(hwnd_), WM_FVC_KEEPCHG, (WPARAM)-1, 0);
}

void TimelineView::ClearKeepAll()
{
    if (!project_) return;
    for (size_t i = 0; i < project_->items.size(); ++i)
        if (project_->items[i].isAnalysed()) Project::ClearSelection(project_->items[i]);
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    ::PostMessageW(::GetParent(hwnd_), WM_FVC_KEEPCHG, (WPARAM)-1, 0);
}
void TimelineView::OnDropFiles(HDROP drop)
{
    std::vector<std::wstring>* files = new std::vector<std::wstring>();
    UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i)
    {
        wchar_t buf[MAX_PATH * 2];
        UINT n = ::DragQueryFileW(drop, i, buf, (UINT)_countof(buf));
        if (n == 0) continue;
        std::wstring path(buf, n);
        if (DirectoryExists(path))
        {
            std::vector<std::wstring> found = ListFilesByExt(path, SupportedMediaExtensions());
            std::sort(found.begin(), found.end());
            for (size_t k = 0; k < found.size(); ++k) files->push_back(found[k]);
        }
        else
        {
            files->push_back(path);
        }
    }
    ::DragFinish(drop);

    HWND parent = ::GetParent(hwnd_);
    if (!::PostMessageW(parent, WM_FVC_ADD, 0, (LPARAM)files))
        delete files;
}

