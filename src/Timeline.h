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
// Timeline.h - the frame stream view: thumbnails, black marks, segments
// ---------------------------------------------------------------------------
#pragma once

#include "Utf.h"
#include "Project.h"
#include "Ffmpeg.h"
#include "Settings.h"

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>
#include <map>
#include <list>
#include <deque>
#include <set>
#include <mutex>
#include <thread>
#include <condition_variable>

// Cache key for one thumbnail mosaic.
struct ThumbKey
{
    std::wstring file;
    long long    mtime = 0;
    double       t0    = 0.0;
    double       t1    = 0.0;
    int          cols  = 0;
    int          rows  = 0;
    int          tw    = 0;
    int          th    = 0;

    bool operator<(const ThumbKey& o) const
    {
        if (file != o.file) return file < o.file;
        if (mtime != o.mtime) return mtime < o.mtime;
        if (t0 != o.t0) return t0 < o.t0;
        if (t1 != o.t1) return t1 < o.t1;
        if (cols != o.cols) return cols < o.cols;
        if (rows != o.rows) return rows < o.rows;
        if (tw != o.tw) return tw < o.tw;
        return th < o.th;
    }
    bool operator==(const ThumbKey& o) const
    {
        return file == o.file && mtime == o.mtime && t0 == o.t0 && t1 == o.t1 &&
               cols == o.cols && rows == o.rows && tw == o.tw && th == o.th;
    }
    std::wstring Describe() const;
};

struct ThumbImage
{
    HBITMAP bmp   = nullptr;
    int     w     = 0;
    int     h     = 0;
};

// ---------------------------------------------------------------------------
// thumbnail cache helpers (also used by the main window menus)
// ---------------------------------------------------------------------------
std::wstring ThumbCacheDir();
void ClearThumbCache();
void PruneThumbCache(size_t maxFiles);

class TimelineView
{
public:
    TimelineView();
    ~TimelineView();

    bool Create(HWND parent, int id, HINSTANCE hInst);
    HWND hwnd() const { return hwnd_; }

    void Attach(Project* project, Ffmpeg* ffmpeg, AppSettings* settings);
    // 生成缩略图开关（设置里的“生成视频流缩略图”）。关闭时不再向 worker
    // 提交抽帧任务，帧流里只画黑屏/分段标记。
    void SetThumbsEnabled(bool on);
    void Refresh();
    // Called by the main window after it has moved/resized the strip: as long as
    // the user has not zoomed or scrolled manually, keep the whole timeline fitted.
    void NotifyResized();
    void SetThumbHeight(int h);
    void FitToWidth();
    void ZoomBy(double factor, int anchorX);
    void ScrollPixels(int dx);
    void Shutdown();

    // selection helpers used by the main window / menus
    void SetCurrentItem(int index);
    int  CurrentItem() const { return currentItem_; }
    // bulk keep-selection commands (applied to every analysed video)
    void SelectBodyAll();      // 保留主体（首末非黑屏段）
    void SelectAllAll();       // 整段保留
    void ClearKeepAll();       // 什么都不保留

    double pixelsPerSecond() const { return pxPerSec_; }
    static const wchar_t* ClassName() { return L"FastVideoCutTimelineWnd"; }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void LayoutChildren();
    void UpdateScrollBar();
    void UpdateVScrollBar();
    void OnPaint();
    void OnLButtonDown(int x, int y);
    void OnLButtonDblClk(int x, int y);
    void OnRButtonUp(int x, int y);
    void OnMouseMove(int x, int y);
    void OnMouseWheel(int x, int y, int delta, bool ctrl, bool shift);
    void OnDropFiles(HDROP drop);
    // Vertical scrolling of the video rows (mouse wheel / vertical scrollbar).
    void ScrollRows(int dy);
    void EnsureRowVisible(int index);
    void StartMouseTracking();
    int  MaxRowOffset() const;
    int  ViewportH() const;

    void DrawEmptyState(HDC dc, const RECT& rc);
    void DrawRuler(HDC dc, const RECT& rc);
    void DrawLeftPanel(HDC dc, const RECT& rc);
    void DrawRow(HDC dc, int index, const RECT& rc);
    void DrawRowStrip(HDC dc, int index, const RECT& rc, const RECT& strip);
    void DrawSegments(HDC dc, int index, const RECT& rc, int x0, int x1);

    int  RowAt(int y, int* rowTop = nullptr) const;
    int  SegmentAt(int index, int x) const;
    int  TimeToX(double t) const;
    double XToTime(int x) const;
    int  RowHeight() const;
    int  ContentLeft() const { return kLeftPanelW; }
    int  ThumbWidthFor(const VideoItem& item) const;

    void EnsureThumbFor(int index, double t0, double t1, int cols, int rows, int tw, int th);
    ThumbImage* LookupThumb(const ThumbKey& key, bool* needsWorker);
    void StoreThumb(const ThumbKey& key, ThumbImage img);
    void EvictThumbs();
    void OnThumbReady(int epoch);
    void WorkerMain();
    void ClearThumbs();
    std::wstring CachePathFor(const ThumbKey& key) const;
    static HBITMAP LoadBmpFile(const std::wstring& path, int* w, int* h);

    // -------- state -------------------------------------------------------
    HWND        hwnd_       = nullptr;
    HWND        scroll_     = nullptr;      // 水平滚动条（时间轴）
    HWND        vscroll_    = nullptr;      // 垂直滚动条（视频行）
    Project*    project_    = nullptr;
    Ffmpeg*     ffmpeg_     = nullptr;
    AppSettings* settings_  = nullptr;

    double      viewStart_  = 0.0;
    double      pxPerSec_   = 20.0;
    int         rowOffset_  = 0;            // 视频行纵向滚动偏移（像素）
    int         wheelRemainder_ = 0;        // 高精度滚轮的余量累积
    int         thumbH_     = 64;
    int         currentItem_ = -1;
    int         hoverItem_  = -1;
    int         hoverSeg_   = -1;
    bool        dragging_   = false;
    bool        userZoomed_ = false;   // set as soon as the user zooms/scrolls
    int         dragStartX_ = 0;
    double      dragStartTime_ = 0.0;
    bool        pendingFit_ = true;
    int         clientW_    = 0;
    int         clientH_    = 0;

    HFONT       fontNormal_ = nullptr;
    HFONT       fontBold_   = nullptr;
    HFONT       fontSmall_  = nullptr;
    HDC         memDC_      = nullptr;
    HBITMAP     memBmp_     = nullptr;

    // -------- thumbnail worker -------------------------------------------
    struct ThumbRequest
    {
        ThumbKey key;
        unsigned epoch = 0;
    };

    std::wstring              cacheDir_;
    std::map<ThumbKey, ThumbImage> thumbs_;       // LRU: most recent at the end
    std::list<ThumbKey>       thumbOrder_;
    std::deque<ThumbRequest>  queue_;
    std::set<std::wstring>    pendingKeys_;
    std::thread               worker_;
    std::mutex                mtx_;
    std::condition_variable   cv_;
    bool                      stop_  = false;
    unsigned                  epoch_ = 1;
    bool                      thumbsEnabled_ = true;
    size_t                    cacheLimit_ = 48;

    static const int kLeftPanelW = 250;
    static const int kRulerH     = 22;
    static const int kScrollH    = 16;
    static const int kVScrollW   = 16;
    static const int kRowGap     = 8;
    static const int kMaxTiles   = 8;
    // 左侧面板四行文字所需的高度：
    //   1. 视频名
    //   2. 分辨率 | 帧率格式 | 帧率
    //   3. 视频编码 | 档次@级别 | 像素格式
    //   4. 音频编码 | 声道 | 采样率 + 状态（出错时整行换成错误详情）
    static const int kMinRowH    = 74;
};