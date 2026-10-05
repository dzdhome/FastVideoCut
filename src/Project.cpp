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
#include "Loc.h"

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
    case ItemStatus::Pending:   return TR(L"待检测", L"Pending");
    case ItemStatus::Probing:   return TR(L"读取信息", L"Probing");
    case ItemStatus::Detecting: return FormatString(TR(L"检测中 %d%%", L"Analysing %d%%"),
                                                    (int)(detectProgress * 100.0 + 0.5));
    case ItemStatus::Ready:     return TR(L"已就绪", L"Ready");
    case ItemStatus::Error:     return TR(L"出错", L"Error");
    }
    return L"";
}

std::wstring VideoItem::summaryText() const
{
    if (status == ItemStatus::Error) return message;
    if (info.duration <= 0.0) return L"-";
    return FormatString(TR(L"%s | %dx%d | %.3gfps | 黑屏%d段",
                            L"%s | %dx%d | %.3gfps | %d black"),
                        FormatClock(info.duration).c_str(),
                        info.width, info.height, info.fps, blackCount());
}

// 时间线左侧的技术信息三行。空字段直接跳过，避免出现 "HEVC |  | yuv420p"
// 这种中间带空洞的写法。
namespace
{
    std::wstring JoinFields(const std::vector<std::string>& parts)
    {
        std::wstring out;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (parts[i].empty()) continue;
            if (!out.empty()) out += L" | ";
            out += Utf8ToWide(parts[i]);
        }
        return out;
    }
}

std::wstring VideoItem::streamLine() const
{
    if (!info.valid()) return std::wstring();
    std::vector<std::string> f;
    // 容器名放最左侧：扩展名会骗人（.mp4 里装 mkv 很常见），而容器/时基不一致
    // 正是无损合并时长出问题的直接原因，放在最前面一眼能看出来。
    f.push_back(info.containerLabel());
    f.push_back(info.resolutionLabel());
    f.push_back(info.fpsModeLabel());
    f.push_back(info.fpsLabel());
    return JoinFields(f);
}

std::wstring VideoItem::videoLine() const
{
    if (!info.valid()) return std::wstring();
    std::vector<std::string> f;
    f.push_back(info.videoCodecLabel());
    f.push_back(info.profileLevelLabel());
    f.push_back(info.pixFmtLabel());
    return JoinFields(f);
}

std::wstring VideoItem::audioLine() const
{
    if (!info.valid()) return std::wstring();
    if (!info.hasAudio) return TR(L"无音频", L"no audio");
    std::vector<std::string> f;
    f.push_back(info.audioCodecLabel());
    f.push_back(info.channelLabel());
    f.push_back(info.sampleRateLabel());
    return JoinFields(f);
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

// -----------------------------------------------------------------------
// 无损合并前的格式一致性检查
//
// concat demuxer 的流复制只把码流首尾相接，既不会重编码也不会重新协商参数：
// 分辨率/编码/帧率/采样率不同的两段拼在一起，ffmpeg 一样会“成功”，但播出来
// 就是花屏、变色、音画不同步。所以必须在合并之前拦下来，并且逐项告诉用户
// 差在哪里，由用户决定要不要强行合并。
// -----------------------------------------------------------------------
namespace
{
    // 一条有音轨、一条没有音轨，合并后必然缺声，所以“有无音轨”也算编码器差异
    std::wstring AudioCodecDesc(const VideoInfo& v)
    {
        if (!v.hasAudio) return TR(L"无音轨", L"no audio track");
        std::wstring s = Utf8ToWide(v.audioCodecLabel());
        return s.empty() ? TR(L"未知", L"unknown") : s;
    }

    // 声道布局要一并比较：声道数相同但一个是 5.1(side) 一个是 5.1(rear)，
    // 合并出来的环绕声也是错的。括号里的声道数和外层的 " / " 分隔不冲突。
    std::wstring ChannelLayoutDesc(const VideoInfo& v)
    {
        std::wstring lay = Utf8ToWide(v.channelLayout);
        if (lay.empty()) return FormatString(L"%d ch", v.channels);
        return FormatString(L"%s (%d ch)", lay.c_str(), v.channels);
    }

    // 色彩空间固定写成 transfer/primaries/matrix 三段，缺的那段写“未标注”。
    // 不能像别处那样把空段直接跳过：那样 "bt709/bt709/bt709" 和 "bt709/bt709"
    // 会被看成两个不同的串，却说不清到底差在哪一项。
    std::wstring ColourSpaceDesc(const VideoInfo& v)
    {
        const std::string* raw[3] = { &v.colorTransfer, &v.colorPrimaries, &v.colorSpace };
        std::wstring s;
        for (int i = 0; i < 3; ++i)
        {
            if (i) s += L"/";
            const std::string& t = *raw[i];
            s += (t.empty() || t == "unknown") ? TR(L"未标注", L"untagged") : Utf8ToWide(t);
        }
        return s;
    }

    // "v,a,s" -> "视频+音频+字幕"。只看首字母，够用且比 codec 名短得多。
    std::wstring StreamLayoutDesc(const VideoInfo& v)
    {
        if (v.streamLayout.empty()) return std::wstring();
        std::wstring out;
        for (size_t i = 0; i < v.streamLayout.size(); ++i)
        {
            std::wstring one;
            switch (v.streamLayout[i])
            {
            case 'v': one = TR(L"视频", L"video"); break;
            case 'a': one = TR(L"音频", L"audio"); break;
            case 's': one = TR(L"字幕", L"subtitle"); break;
            case 'd': one = TR(L"数据", L"data"); break;
            default:  one = L"?"; break;
            }
            if (!out.empty()) out += L"+";
            out += one;
        }
        return out;
    }
}

bool VideoFormatsMatch(const std::vector<const VideoInfo*>& infos,
                       std::vector<FormatMismatch>& diffs)
{
    diffs.clear();
    if (infos.size() < 2) return true;

    struct Field
    {
        const wchar_t*     label;
        std::wstring (*get)(const VideoInfo&);
    };

    const Field fields[] =
    {
        { TR(L"视频编码器", L"Video codec"),        [](const VideoInfo& v) { return Utf8ToWide(v.videoCodecLabel()); } },
        { TR(L"音频编码器", L"Audio codec"),        [](const VideoInfo& v) { return AudioCodecDesc(v); } },
        { TR(L"分辨率",     L"Resolution"),        [](const VideoInfo& v) { return Utf8ToWide(v.resolutionLabel()); } },
        { TR(L"帧率",       L"Frame rate"),        [](const VideoInfo& v) { return Utf8ToWide(v.fpsLabel()) + L" " + Utf8ToWide(v.fpsModeLabel()); } },
        { TR(L"像素格式",   L"Pixel format"),      [](const VideoInfo& v) { return Utf8ToWide(v.pixFmtLabel()); } },
        { TR(L"编码档次与级别", L"Profile / level"),[](const VideoInfo& v) { return Utf8ToWide(v.profileLevelLabel()); } },
        { TR(L"色彩空间",   L"Colour space"),    [](const VideoInfo& v) { return ColourSpaceDesc(v); } },
        { TR(L"音频采样率", L"Audio sample rate"),[](const VideoInfo& v) { return Utf8ToWide(v.sampleRateLabel()); } },
        { TR(L"声道数与声道布局", L"Channels / layout"), [](const VideoInfo& v) { return ChannelLayoutDesc(v); } },
        // 下面三项不是“画质/音质”差异，而是 concat 拼接本身的前提条件，不一致
        // 时 ffmpeg 不报错但产物时长会错到几百小时、播不完（见 kRemuxFixable）。
        { TR(L"封装格式",   L"Container"),     [](const VideoInfo& v) { return Utf8ToWide(v.containerLabel()); } },
        { TR(L"视频时基",   L"Video time base"),[](const VideoInfo& v) { return Utf8ToWide(v.videoTimeBase); } },
        { TR(L"音频时基",   L"Audio time base"),[](const VideoInfo& v) { return Utf8ToWide(v.audioTimeBase); } },
        { TR(L"流布局",     L"Stream layout"), [](const VideoInfo& v) { return StreamLayoutDesc(v); } },
    };

    for (size_t f = 0; f < sizeof(fields) / sizeof(fields[0]); ++f)
    {
        std::vector<std::wstring> values;
        for (size_t i = 0; i < infos.size(); ++i)
        {
            std::wstring v = fields[f].get(*infos[i]);
            if (v.empty()) continue;                 // ffprobe 没报出来，跳过
            bool seen = false;
            for (size_t k = 0; k < values.size(); ++k)
                if (values[k] == v) { seen = true; break; }
            if (!seen) values.push_back(v);
        }
        if (values.size() > 1)
        {
            FormatMismatch m;
            m.label  = fields[f].label;
            m.values = values;
            diffs.push_back(m);
        }
    }
    return diffs.empty();
}

std::wstring DescribeFormatMismatch(const std::vector<FormatMismatch>& diffs)
{
    std::wstring out;
    for (size_t i = 0; i < diffs.size(); ++i)
    {
        if (!out.empty()) out += L"\n";
        out += FormatString(TR(L"  · %s：", L"  - %s: "), diffs[i].label.c_str());
        for (size_t k = 0; k < diffs[i].values.size(); ++k)
        {
            if (k) out += L" / ";
            out += diffs[i].values[k];
        }
    }
    return out;
}

bool MismatchIsRemuxFixable(const std::vector<FormatMismatch>& diffs)
{
    // 标签在 VideoFormatsMatch 里写死，这里按同样的 TR() 取一遍再比。不能在
    // 命名空间里存成常量：TR() 依赖 Loc::Apply() 的运行期结果，静态初始化早于它。
    const wchar_t* fixable[] = { TR(L"封装格式",   L"Container"),
                                 TR(L"视频时基",   L"Video time base"),
                                 TR(L"音频时基",   L"Audio time base"),
                                 TR(L"流布局",     L"Stream layout") };
    for (size_t i = 0; i < diffs.size(); ++i)
    {
        for (size_t k = 0; k < sizeof(fixable) / sizeof(fixable[0]); ++k)
        {
            if (diffs[i].label == fixable[k]) return true;
        }
    }
    return false;
}