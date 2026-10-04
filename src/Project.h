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
// Project.h - document model: list of videos split into selectable segments
// ---------------------------------------------------------------------------
#pragma once

#include "Utf.h"
#include "Ffmpeg.h"

#include <string>
#include <vector>
#include <map>

enum class SegKind
{
    Media,
    Black
};

// One clickable piece of the frame stream.
struct Segment
{
    double  t0       = 0.0;
    double  t1       = 0.0;
    SegKind kind     = SegKind::Media;
    bool    selected = true;    // derived from VideoItem::keepStart/keepEnd/keepOverride

    double length() const { return t1 > t0 ? t1 - t0 : 0.0; }
};

// A continuous time range that will be cut as one piece (used by the exporter:
// neighbouring selected segments must not be cut apart and concatenated, that
// would restart the decoder/keyframes and make the result stutter).
struct KeepRun
{
    double t0 = 0.0;
    double t1 = 0.0;

    double length() const { return t1 > t0 ? t1 - t0 : 0.0; }
};

enum class ItemStatus
{
    Pending,        // added, not analysed yet
    Probing,        // ffprobe running
    Detecting,      // blackdetect running
    Ready,          // analysed, segments available
    Error
};

struct VideoItem
{
    std::wstring             path;
    std::wstring             name;
    VideoInfo                info;
    std::vector<BlackRange>  blacks;
    std::vector<Segment>     segments;
    ItemStatus               status         = ItemStatus::Pending;
    std::wstring             message;
    double                   detectProgress = 0.0;

    // ---- keep selection -------------------------------------------------
    // 黑屏只是"片头/片尾与主体的分界线"，不是要删掉的东西。保留哪些片段完全
    // 由用户点击决定：
    //   左键点击  = 指定保留起点 (keepStart)
    //   右键点击  = 指定保留终点 (keepEnd)
    //   两端之间（含）的所有分段都保留，其余默认删除
    //   Ctrl+左键 = 单段覆盖 (keepOverride)，再次点击取消
    // Segment::selected 由这三者派生，见 Project::RecomputeSelection。
    int                       keepStart = -1;
    int                       keepEnd   = -1;
    std::map<int, bool>       keepOverride;   // 分段下标 -> 显式 保留/删除

    int    selectedSegmentCount() const;
    int    selectedMediaCount() const;
    int    blackCount() const { return (int)blacks.size(); }
    double selectedDuration() const;
    double blackDuration() const;
    // Selected segments merged into as few continuous ranges as possible.
    std::vector<KeepRun> selectedRuns() const;
    bool   isAnalysed() const { return status == ItemStatus::Ready; }
    // true when the whole file is selected (nothing has to be cut at all)
    bool   hasContiguousFullSelection() const;
    std::wstring statusText() const;
    std::wstring summaryText() const;
    // 人类可读的保留区间，例如 "00:00:12.500 - 00:01:03.000"（未选择时为空）
    std::wstring keepRangeText() const;
    // 画面规格（"HDR10 · 10bit"），未分析时为空；左侧面板单独占一行显示
    std::wstring formatText() const;
    // 保留起点 / 终点（秒）。< 0 表示该端还没有指定。
    double keepStartTime() const;
    double keepEndTime() const;
};

class Project
{
public:
    std::vector<VideoItem> items;

    int  AddFile(const std::wstring& path);              // -1 when duplicate/invalid
    void RemoveAt(int index);
    void Clear();
    bool MoveUp(int index);
    bool MoveDown(int index);
    void SortByName();
    int  ItemIndexByPath(const std::wstring& path) const;

    double MaxDuration() const;
    double TotalDuration() const;
    double SelectedDuration() const;
    int    SelectedSegmentCount() const;

    // Splits a freshly analysed item into media / black segments. Everything is
    // kept by default - black frames only mark the intro/outro boundaries.
    static void RebuildSegments(VideoItem& item, bool keepSelection = false);

    // Indices that still need a blackdetect pass. Detection is expensive (a 16
    // minute 1080p clip takes ~6 s), so the default is to keep the result of the
    // videos that were already analysed and only work on the new ones.
    static std::vector<int> PendingDetect(const Project& project, bool forceAll = false);

    // ---- keep selection ---------------------------------------------------
    // Segment::selected is derived from keepStart / keepEnd / keepOverride.
    static void RecomputeSelection(VideoItem& item);
    static void ClickKeepStart(VideoItem& item, int segIndex);    // 左键
    static void ClickKeepEnd(VideoItem& item, int segIndex);      // 右键
    static void ToggleKeepSegment(VideoItem& item, int segIndex); // Ctrl+左键
    static void SelectOnlySegment(VideoItem& item, int segIndex); // 双击
    static void SelectAll(VideoItem& item);                      // 整段保留
    static void SelectBody(VideoItem& item);                     // 保留主体（首末非黑屏段）
    static void ClearSelection(VideoItem& item);                 // 什么都不保留

    // Ranges that are actually cut with stream copy (-c copy):
    //   1) neighbouring selected segments are merged into one range
    //   2) if the keep range does not start at the first segment, the cut start
    //      moves back to the beginning of the preceding black segment
    //   3) every start is snapped back to the previous keyframe - otherwise the
    //      exported file starts with audio only (no picture until the next I frame)
    // Human readable adjustments are appended to `notes` (newline separated).
    static std::vector<KeepRun> BuildCopyRuns(Ffmpeg& ff, const VideoItem& item,
                                              std::string* notes = nullptr,
                                              const CancelToken& cancel = CancelToken());
};

// File type helpers
bool IsSupportedMediaFile(const std::wstring& path);
std::vector<std::wstring> SupportedMediaExtensions();