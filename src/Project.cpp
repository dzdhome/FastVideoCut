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
// Project.cpp - document model implementation
// ---------------------------------------------------------------------------
#include "Project.h"

#include <algorithm>
#include <cmath>

static const double kMinSegment = 0.02;     // 20 ms: ignore smaller slivers

// ---------------------------------------------------------------------------
// VideoItem
// ---------------------------------------------------------------------------
int VideoItem::selectedSegmentCount() const
{
    int n = 0;
    for (size_t i = 0; i < segments.size(); ++i)
        if (segments[i].selected) ++n;
    return n;
}

int VideoItem::selectedMediaCount() const
{
    int n = 0;
    for (size_t i = 0; i < segments.size(); ++i)
        if (segments[i].selected && segments[i].kind == SegKind::Media) ++n;
    return n;
}

double VideoItem::selectedDuration() const
{
    double sum = 0.0;
    for (size_t i = 0; i < segments.size(); ++i)
        if (segments[i].selected) sum += segments[i].length();
    return sum;
}

double VideoItem::blackDuration() const
{
    double sum = 0.0;
    for (size_t i = 0; i < blacks.size(); ++i) sum += blacks[i].length();
    return sum;
}

// Segments tile the file without gaps, so any consecutive selected segments can
// be cut in one go. Cutting them separately and concatenating would restart the
// decoder at every keyframe -> visible stutter in the exported file.
std::vector<KeepRun> VideoItem::selectedRuns() const
{
    std::vector<KeepRun> runs;
    const double kJoin = 0.02;      // 20 ms: treat "almost touching" as continuous

    for (size_t i = 0; i < segments.size(); ++i)
    {
        if (!segments[i].selected) continue;

        if (!runs.empty() && segments[i].t0 <= runs.back().t1 + kJoin)
        {
            if (segments[i].t1 > runs.back().t1) runs.back().t1 = segments[i].t1;
            continue;
        }
        KeepRun r;
        r.t0 = segments[i].t0;
        r.t1 = segments[i].t1;
        runs.push_back(r);
    }
    return runs;
}

bool VideoItem::hasContiguousFullSelection() const
{
    if (segments.empty()) return false;
    double dur = info.duration;
    if (dur <= 0.0) return false;

    // everything is kept -> there is nothing to cut, just copy the source
    double covered = 0.0;
    for (size_t i = 0; i < segments.size(); ++i)
    {
        if (!segments[i].selected) return false;
        covered += segments[i].length();
    }
    return std::fabs(covered - dur) < 0.25;
}

std::wstring VideoItem::keepRangeText() const
{
    if (keepStart < 0 || keepEnd < 0) return std::wstring();
    int a = std::min(keepStart, keepEnd);
    int b = std::max(keepStart, keepEnd);
    if (a < 0 || b >= (int)segments.size()) return std::wstring();
    return FormatString(L"%s - %s",
                        FormatTimecode(segments[a].t0).c_str(),
                        FormatTimecode(segments[b].t1).c_str());
}

double VideoItem::keepStartTime() const
{
    int a = std::min(keepStart, keepEnd);
    int b = std::max(keepStart, keepEnd);
    if (a < 0 || a >= (int)segments.size()) return -1.0;
    if (b < 0 || b >= (int)segments.size()) return -1.0;
    return segments[a].t0;
}

double VideoItem::keepEndTime() const
{
    int a = std::min(keepStart, keepEnd);
    int b = std::max(keepStart, keepEnd);
    if (a < 0 || a >= (int)segments.size()) return -1.0;
    if (b < 0 || b >= (int)segments.size()) return -1.0;
    return segments[b].t1;
}

std::wstring VideoItem::statusText() const
{
    switch (status)
    {
    case ItemStatus::Pending:   return L"待检测";
    case ItemStatus::Probing:   return L"读取信息";
    case ItemStatus::Detecting: return FormatString(L"检测中 %d%%", (int)(detectProgress * 100.0 + 0.5));
    case ItemStatus::Ready:     return L"已就绪";
    case ItemStatus::Error:     return L"出错";
    }
    return L"";
}

std::wstring VideoItem::summaryText() const
{
    if (status == ItemStatus::Error) return message;
    if (info.duration <= 0.0) return L"-";
    return FormatString(L"%s | %dx%d | %.3gfps | 黑屏%d段",
                        FormatClock(info.duration).c_str(),
                        info.width, info.height, info.fps, blackCount());
}

// HDR / SDR + 位深，例如 "HDR10 · 10bit"（未分析时为空）
std::wstring VideoItem::formatText() const
{
    if (!info.valid()) return std::wstring();
    return Utf8ToWide(info.formatLabel());
}

// ---------------------------------------------------------------------------
// Project
// ---------------------------------------------------------------------------
int Project::ItemIndexByPath(const std::wstring& path) const
{
    std::wstring want = ToLowerW(PathGetFull(path));
    for (size_t i = 0; i < items.size(); ++i)
        if (ToLowerW(PathGetFull(items[i].path)) == want) return (int)i;
    return -1;
}

int Project::AddFile(const std::wstring& path)
{
    if (path.empty()) return -1;
    if (!FileExists(path)) return -1;
    if (!IsSupportedMediaFile(path)) return -1;
    if (ItemIndexByPath(path) >= 0) return -1;

    VideoItem it;
    it.path = PathGetFull(path);
    it.name = PathGetFileName(it.path);
    it.status = ItemStatus::Pending;
    items.push_back(it);
    return (int)items.size() - 1;
}

void Project::RemoveAt(int index)
{
    if (index < 0 || index >= (int)items.size()) return;
    items.erase(items.begin() + index);
}

void Project::Clear()
{
    items.clear();
}

bool Project::MoveUp(int index)
{
    if (index <= 0 || index >= (int)items.size()) return false;
    std::swap(items[index - 1], items[index]);
    return true;
}

bool Project::MoveDown(int index)
{
    if (index < 0 || index + 1 >= (int)items.size()) return false;
    std::swap(items[index], items[index + 1]);
    return true;
}

void Project::SortByName()
{
    std::sort(items.begin(), items.end(),
              [](const VideoItem& a, const VideoItem& b)
              {
                  return ToLowerW(a.name) < ToLowerW(b.name);
              });
}

double Project::MaxDuration() const
{
    double m = 0.0;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].info.duration > m) m = items[i].info.duration;
    return m;
}

double Project::TotalDuration() const
{
    double m = 0.0;
    for (size_t i = 0; i < items.size(); ++i) m += items[i].info.duration;
    return m;
}

double Project::SelectedDuration() const
{
    double m = 0.0;
    for (size_t i = 0; i < items.size(); ++i) m += items[i].selectedDuration();
    return m;
}

int Project::SelectedSegmentCount() const
{
    int n = 0;
    for (size_t i = 0; i < items.size(); ++i) n += items[i].selectedSegmentCount();
    return n;
}

void Project::RebuildSegments(VideoItem& item, bool keepSelection)
{
    std::vector<Segment> old;
    if (keepSelection) old = item.segments;

    item.segments.clear();

    double dur = item.info.duration;
    if (dur <= 0.0) return;

    std::vector<BlackRange> blacks = item.blacks;
    std::sort(blacks.begin(), blacks.end(),
              [](const BlackRange& a, const BlackRange& b) { return a.start < b.start; });

    double cursor = 0.0;
    for (size_t i = 0; i < blacks.size(); ++i)
    {
        double bs = ClampValue(blacks[i].start, 0.0, dur);
        double be = ClampValue(blacks[i].end, 0.0, dur);
        if (be - bs < kMinSegment) continue;
        if (bs - cursor > kMinSegment)
        {
            Segment s;
            s.t0 = cursor;
            s.t1 = bs;
            s.kind = SegKind::Media;
            item.segments.push_back(s);
        }
        Segment b;
        b.t0 = bs;
        b.t1 = be;
        b.kind = SegKind::Black;
        item.segments.push_back(b);
        cursor = be;
    }
    if (dur - cursor > kMinSegment)
    {
        Segment s;
        s.t0 = cursor;
        s.t1 = dur;
        s.kind = SegKind::Media;
        item.segments.push_back(s);
    }
    if (item.segments.empty())
    {
        Segment s;
        s.t0 = 0.0;
        s.t1 = dur;
        s.kind = SegKind::Media;
        item.segments.push_back(s);
    }

    // 默认整段保留：黑屏只是"片头/片尾分界线"的提示，不会被自动删掉
    item.keepStart = 0;
    item.keepEnd   = (int)item.segments.size() - 1;
    item.keepOverride.clear();
    RecomputeSelection(item);

    // 重新检测：把用户之前的选区还原成"区间 + 单段覆盖"，起/止标记不会丢
    // （首次检测时没有旧分段，old 为空 -> 保持上面的整段保留默认）
    if (keepSelection && !old.empty() && !item.segments.empty())
    {
        // 尽量还原成"区间 + 单段覆盖"，这样起/止标记不会丢
        std::vector<char> flags(item.segments.size(), 0);
        for (size_t i = 0; i < item.segments.size(); ++i)
        {
            for (size_t j = 0; j < old.size(); ++j)
            {
                if (old[j].kind == item.segments[i].kind &&
                    std::fabs(old[j].t0 - item.segments[i].t0) < 0.005 &&
                    std::fabs(old[j].t1 - item.segments[i].t1) < 0.005)
                {
                    if (old[j].selected) flags[i] = 1;
                    break;
                }
            }
        }
        int first = -1, last = -1;
        for (size_t i = 0; i < flags.size(); ++i)
        {
            if (!flags[i]) continue;
            if (first < 0) first = (int)i;
            last = (int)i;
        }
        item.keepOverride.clear();
        if (first < 0)
        {
            item.keepStart = item.keepEnd = -1;
        }
        else
        {
            item.keepStart = first;
            item.keepEnd   = last;
            for (size_t i = 0; i < flags.size(); ++i)
            {
                if (flags[i] && ((int)i < first || (int)i > last)) item.keepOverride[(int)i] = true;
                if (!flags[i] && (int)i > first && (int)i < last) item.keepOverride[(int)i] = false;
            }
        }
        RecomputeSelection(item);
    }
}

// 检测黑屏很贵（16 分钟 1080p 素材约 6 秒），所以默认只处理还没分析过的视频，
// 新增/移除文件后再点“检测黑屏”不会把已有结果全部重算。
std::vector<int> Project::PendingDetect(const Project& project, bool forceAll)
{
    std::vector<int> out;
    for (size_t i = 0; i < project.items.size(); ++i)
    {
        const VideoItem& it = project.items[i];
        if (forceAll || !it.isAnalysed()) out.push_back((int)i);
    }
    return out;
}

// ---------------------------------------------------------------------------
// keep selection
// ---------------------------------------------------------------------------
void Project::RecomputeSelection(VideoItem& item)
{
    int a = item.keepStart, b = item.keepEnd;
    if (a > b) std::swap(a, b);
    bool haveRange = (a >= 0 && b >= 0 && b < (int)item.segments.size());

    for (size_t i = 0; i < item.segments.size(); ++i)
        item.segments[i].selected = (haveRange && (int)i >= a && (int)i <= b);

    for (std::map<int, bool>::const_iterator it = item.keepOverride.begin();
         it != item.keepOverride.end(); ++it)
    {
        if (it->first >= 0 && it->first < (int)item.segments.size())
            item.segments[(size_t)it->first].selected = it->second;
    }
}

void Project::ClickKeepStart(VideoItem& item, int segIndex)
{
    if (segIndex < 0 || segIndex >= (int)item.segments.size()) return;
    if (item.keepStart < 0 && item.keepEnd < 0)
    {
        // 第一次点击：先只保留这一段，之后再点另一端扩展区间
        item.keepStart = segIndex;
        item.keepEnd   = segIndex;
    }
    else
    {
        item.keepStart = segIndex;
    }
    RecomputeSelection(item);
}

void Project::ClickKeepEnd(VideoItem& item, int segIndex)
{
    if (segIndex < 0 || segIndex >= (int)item.segments.size()) return;
    if (item.keepStart < 0 && item.keepEnd < 0)
    {
        item.keepStart = segIndex;
        item.keepEnd   = segIndex;
    }
    else
    {
        item.keepEnd = segIndex;
    }
    RecomputeSelection(item);
}

void Project::ToggleKeepSegment(VideoItem& item, int segIndex)
{
    if (segIndex < 0 || segIndex >= (int)item.segments.size()) return;

    RecomputeSelection(item);
    bool inRange = item.segments[(size_t)segIndex].selected;
    if (inRange)
    {
        // 区间内 -> 单独删除这一段
        item.segments[(size_t)segIndex].selected = false;
        item.keepOverride[segIndex] = false;
    }
    else
    {
        // 区间外 -> 单独保留这一段
        item.segments[(size_t)segIndex].selected = true;
        item.keepOverride[segIndex] = true;
    }
}

void Project::SelectOnlySegment(VideoItem& item, int segIndex)
{
    if (segIndex < 0 || segIndex >= (int)item.segments.size()) return;
    item.keepStart = segIndex;
    item.keepEnd   = segIndex;
    item.keepOverride.clear();
    RecomputeSelection(item);
}

void Project::SelectAll(VideoItem& item)
{
    if (item.segments.empty()) return;
    item.keepStart = 0;
    item.keepEnd   = (int)item.segments.size() - 1;
    item.keepOverride.clear();
    RecomputeSelection(item);
}

void Project::SelectBody(VideoItem& item)
{
    int first = -1, last = -1;
    for (size_t i = 0; i < item.segments.size(); ++i)
    {
        if (item.segments[i].kind != SegKind::Media) continue;
        if (first < 0) first = (int)i;
        last = (int)i;
    }
    if (first < 0) { SelectAll(item); return; }     // 全片都是黑屏 -> 保留全部
    item.keepStart = first;
    item.keepEnd   = last;
    item.keepOverride.clear();
    RecomputeSelection(item);
}

void Project::ClearSelection(VideoItem& item)
{
    item.keepStart = -1;
    item.keepEnd   = -1;
    item.keepOverride.clear();
    RecomputeSelection(item);
}

// ---------------------------------------------------------------------------
// what stream copy (-c copy) really cuts
// ---------------------------------------------------------------------------
std::vector<KeepRun> Project::BuildCopyRuns(Ffmpeg& ff, const VideoItem& item,
                                            std::string* notes,
                                            const CancelToken& cancel)
{
    std::vector<KeepRun> runs = item.selectedRuns();
    if (runs.empty()) return runs;
    if (notes) notes->clear();

    // (2) start in the middle of the file -> begin at the preceding black
    //     segment, so the cut lands on a keyframe and no picture is lost
    int firstSel = -1;
    for (size_t i = 0; i < item.segments.size(); ++i)
    {
        if (item.segments[i].selected) { firstSel = (int)i; break; }
    }
    if (firstSel > 0 && item.segments[(size_t)firstSel - 1].kind == SegKind::Black)
    {
        double lead = item.segments[(size_t)firstSel].t0 -
                      item.segments[(size_t)firstSel - 1].t0;
        if (lead > 0.0 && lead < 10.0)
        {
            runs[0].t0 = item.segments[(size_t)firstSel - 1].t0;
            if (notes)
                *notes += FormatSecondsUtf8(lead, 2) +
                          "s moved back to the start of the preceding black segment\n";
        }
    }

    // (3) snap every start back to the previous keyframe
    for (size_t i = 0; i < runs.size(); ++i)
    {
        if (runs[i].t0 <= 0.01) continue;
        double kf = 0.0;
        if (!ff.KeyframeTimeBefore(item.path, runs[i].t0, kf, cancel)) continue;
        if (kf >= runs[i].t0 - 0.001) continue;      // already on a keyframe
        if (kf < runs[i].t0 - 10.0) continue;        // absurdly long GOP, skip

        if (notes)
            *notes += FormatSecondsUtf8(runs[i].t0 - kf, 2) +
                      "s moved back to a keyframe (part " +
                      FormatSecondsUtf8((double)(i + 1), 0) + ")\n";
        runs[i].t0 = kf;
    }
    return runs;
}

// ---------------------------------------------------------------------------
// file types
// ---------------------------------------------------------------------------
std::vector<std::wstring> SupportedMediaExtensions()
{
    std::vector<std::wstring> v;
    v.push_back(L".mp4");
    v.push_back(L".mkv");
    v.push_back(L".mov");
    v.push_back(L".avi");
    v.push_back(L".flv");
    v.push_back(L".wmv");
    v.push_back(L".ts");
    v.push_back(L".m2ts");
    v.push_back(L".mts");
    v.push_back(L".mpg");
    v.push_back(L".mpeg");
    v.push_back(L".m4v");
    v.push_back(L".webm");
    v.push_back(L".rmvb");
    v.push_back(L".3gp");
    v.push_back(L".vob");
    v.push_back(L".mxf");
    return v;
}

bool IsSupportedMediaFile(const std::wstring& path)
{
    std::wstring ext = PathGetExtension(path);
    if (ext.empty()) return false;
    std::vector<std::wstring> all = SupportedMediaExtensions();
    for (size_t i = 0; i < all.size(); ++i)
        if (ext == all[i]) return true;
    return false;
}