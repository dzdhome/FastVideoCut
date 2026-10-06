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

// ---------------------------------------------------------------------------
// 手动插入的分割点
//
// 正常的分段边界来自黑屏检测，但转场紧凑的片子根本没有黑屏帧，这时用户在
// 预览窗暂停后手工切一刀。分割点本身是一个时间（秒），落在哪个非黑屏段里就
// 把那个段一分为二 —— 分出来的两半仍是普通媒体段，只是多了一个可以点选的边。
// ---------------------------------------------------------------------------
// 暂停位置离已有分割点这么近时视为"同一个"（按钮文字在插入/删除之间切换）
constexpr double kManualSplitTol = 0.25;
// 距离已有分段边界小于这个值就不值得再切一刀（也用来拒绝贴边的插入）
constexpr double kManualSplitMinGap = 0.05;

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
    // 手动插入的分割点（秒，升序去重）。转场没有黑屏帧时由用户在预览窗里手工
    // 插入，重新检测黑屏后依然生效（Project::RebuildSegments 会重新套用）。
    std::vector<double>      manualSplits;
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
    // 时间线左侧的技术信息三行（未分析时为空）：
    //   1920x1080 | VBR | 25fps
    //   HEVC | Main@L4 | yuv420p
    //   AAC LC | 2.0 | 48K
    // 空字段会被跳过（" | | " 这种空洞的写法比缺一项更难看）。
    std::wstring streamLine() const;      // 分辨率 | 帧率格式 | 帧率
    std::wstring videoLine() const;       // 视频编码 | 档次@级别 | 像素格式
    std::wstring audioLine() const;       // 音频编码 | 声道 | 采样率
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

    // ---- 手动分割（黑屏检测找不到转场时人工插入） ------------------------
    // 在 t 处把所在的非黑屏段一分为二。返回 false 时 *why 是给用户看的原因。
    // segIndex 可选：返回被切开那一段的下标（新段 = segIndex + 1），
    // 调用方据此修正自己缓存的段号。
    static bool InsertManualSplit(VideoItem& item, double t, std::wstring* why = nullptr,
                                  int* segIndex = nullptr);
    // 删除离 t 最近（tol 以内）的手动分割点，并把两段合并回去。
    static bool RemoveManualSplitNear(VideoItem& item, double t, double tol = kManualSplitTol);
    // t 附近有没有手动分割点 / 它在 manualSplits 里的下标（没有则 -1）
    static bool HasManualSplitNear(const VideoItem& item, double t, double tol = kManualSplitTol);
    static int  ManualSplitNear(const VideoItem& item, double t, double tol = kManualSplitTol);

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

// -----------------------------------------------------------------------
// 无损合并前的格式一致性检查
//
// concat 的 stream copy 不会重新编码，参数不一致时不会报错，但播出来的是
// 花屏 / 断续的声音，所以必须在合并前拦住，逐项告诉用户差在哪。
// -----------------------------------------------------------------------
// 一处不一致的字段：label 是显示名（已本地化），values 是列表里出现过的取值。
struct FormatMismatch
{
    std::wstring              label;
    std::vector<std::wstring> values;
};

// 逐项比对：视频编码器 / 音频编码器 / 分辨率 / 帧率 / 像素格式 / 编码档次与级别 /
// 色彩空间 / 音频采样率 / 声道数与声道布局。返回 true 表示全部一致。
// 某一项在某个视频上 ffprobe 没报出来（空串）时该视频不参与这一项的比较，
// 免得因为“缺字段”误报不一致。
bool VideoFormatsMatch(const std::vector<const VideoInfo*>& infos,
                       std::vector<FormatMismatch>& diffs);
// 把 diffs 拼成给用户看的提示文本（每行 "· 字段：A / B"）。
std::wstring DescribeFormatMismatch(const std::vector<FormatMismatch>& diffs);

// diffs 里是否含“转封装就能修好”的差异（封装格式 / 视频时基 / 音频时基 / 流布局）。
// 这类差异不动码流，只换容器 + 统一时基就能修好，几秒跑完，所以对话框里可以多给
// 用户一个“快速转封装”选项。真正的编码/分辨率/采样率差异则只能重编码，给了也没用。
bool MismatchIsRemuxFixable(const std::vector<FormatMismatch>& diffs);