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
// Ffmpeg.cpp - ffmpeg / ffprobe command line facade
// ---------------------------------------------------------------------------
#include "Ffmpeg.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// parsing helpers
// ---------------------------------------------------------------------------
double ParseRational(const std::string& s)
{
    if (s.empty() || s == "N/A" || s == "0/0") return 0.0;
    size_t slash = s.find('/');
    if (slash == std::string::npos)
    {
        char* endp = nullptr;
        double v = strtod(s.c_str(), &endp);
        return (endp == s.c_str()) ? 0.0 : v;
    }
    double num = strtod(s.substr(0, slash).c_str(), nullptr);
    double den = strtod(s.substr(slash + 1).c_str(), nullptr);
    if (den == 0.0) return 0.0;
    return num / den;
}

std::vector<std::pair<std::string, std::string> > ParseKeyValueLines(const std::string& text)
{
    std::vector<std::pair<std::string, std::string> > out;
    std::vector<std::string> lines = SplitOutputLines(text);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string& ln = lines[i];
        size_t eq = ln.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string key = ln.substr(0, eq);
        std::string val = ln.substr(eq + 1);
        while (!key.empty() && (key[key.size() - 1] == ' ' || key[key.size() - 1] == '\t'))
            key.erase(key.size() - 1);
        while (!val.empty() && (val[0] == ' ' || val[0] == '\t'))
            val.erase(0, 1);
        if (val == "N/A") val.clear();
        out.push_back(std::make_pair(key, val));
    }
    return out;
}

bool ParseBlackDetectLine(const std::string& line, bool& haveStart, double& start,
                          bool& haveEnd, double& end, bool& haveDuration, double& duration)
{
    haveStart = haveEnd = haveDuration = false;
    start = end = duration = 0.0;

    size_t p = line.find("black_start:");
    if (p != std::string::npos)
    {
        start = strtod(line.c_str() + p + 12, nullptr);
        haveStart = true;
    }
    p = line.find("black_end:");
    if (p != std::string::npos)
    {
        end = strtod(line.c_str() + p + 10, nullptr);
        haveEnd = true;
    }
    p = line.find("black_duration:");
    if (p != std::string::npos)
    {
        duration = strtod(line.c_str() + p + 15, nullptr);
        haveDuration = true;
    }
    return haveStart || haveEnd || haveDuration;
}

bool ParseBlackDetectOutput(const std::string& text, std::vector<BlackRange>& out)
{
    out.clear();
    std::vector<std::string> lines = SplitOutputLines(text);
    bool haveStart = false;
    bool haveEnd = false;
    double start = 0.0;
    double end = 0.0;

    for (size_t i = 0; i < lines.size(); ++i)
    {
        bool hs = false, he = false, hd = false;
        double s = 0.0, e = 0.0, d = 0.0;
        if (!ParseBlackDetectLine(lines[i], hs, s, he, e, hd, d)) continue;

        if (hs) { start = s; haveStart = true; }
        if (he) { end = e; haveEnd = true; }
        else if (hs && hd) { end = start + d; haveEnd = true; }

        if (haveStart && haveEnd)
        {
            if (end > start)
            {
                BlackRange r;
                r.start = start;
                r.end = end;
                out.push_back(r);
            }
            haveStart = haveEnd = false;
        }
    }
    return !out.empty();
}

void MergeBlackRanges(std::vector<BlackRange>& ranges, double joinGap, double minDuration)
{
    if (ranges.empty()) return;

    std::sort(ranges.begin(), ranges.end(),
              [](const BlackRange& a, const BlackRange& b) { return a.start < b.start; });

    std::vector<BlackRange> merged;
    merged.push_back(ranges[0]);
    for (size_t i = 1; i < ranges.size(); ++i)
    {
        BlackRange& last = merged[merged.size() - 1];
        if (ranges[i].start - last.end <= joinGap)
        {
            if (ranges[i].end > last.end) last.end = ranges[i].end;
        }
        else
        {
            merged.push_back(ranges[i]);
        }
    }

    ranges.clear();
    for (size_t i = 0; i < merged.size(); ++i)
    {
        if (merged[i].length() + 1e-6 >= minDuration)
            ranges.push_back(merged[i]);
    }
}

// ---------------------------------------------------------------------------
// discovery
// ---------------------------------------------------------------------------
namespace
{
    void AddCandidateDir(std::vector<std::wstring>& list, const std::wstring& dir)
    {
        if (dir.empty()) return;
        std::wstring d = dir;
        while (d.size() > 3 && (d[d.size() - 1] == L'\\' || d[d.size() - 1] == L'/'))
            d.erase(d.size() - 1);
        std::wstring low = ToLowerW(d);
        for (size_t i = 0; i < list.size(); ++i)
            if (ToLowerW(list[i]) == low) return;
        list.push_back(d);
    }

    // Adds every "<base>\*ffmpeg*\bin" folder found one level below `base`.
    void AddFfmpegFoldersNear(std::vector<std::wstring>& list, const std::wstring& base)
    {
        if (!DirectoryExists(base)) return;
        std::wstring pattern = PathCombine(base, L"*");
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do
        {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring name = ToLowerW(fd.cFileName);
            if (name.find(L"ffmpeg") == std::wstring::npos) continue;
            std::wstring full = PathCombine(base, fd.cFileName);
            AddCandidateDir(list, PathCombine(full, L"bin"));
            AddCandidateDir(list, full);
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }

    void AddPathEntries(std::vector<std::wstring>& list)
    {
        wchar_t buf[32767];
        DWORD n = ::GetEnvironmentVariableW(L"PATH", buf, (DWORD)_countof(buf));
        if (n == 0 || n >= _countof(buf)) return;
        std::wstring path(buf, n);
        size_t pos = 0;
        while (pos <= path.size())
        {
            size_t sep = path.find(L';', pos);
            std::wstring item = (sep == std::wstring::npos) ? path.substr(pos) : path.substr(pos, sep - pos);
            if (!item.empty()) AddCandidateDir(list, item);
            if (sep == std::wstring::npos) break;
            pos = sep + 1;
        }
    }

    std::wstring EnvString(const wchar_t* name)
    {
        wchar_t buf[32767];
        DWORD n = ::GetEnvironmentVariableW(name, buf, (DWORD)_countof(buf));
        if (n == 0 || n >= _countof(buf)) return std::wstring();
        return std::wstring(buf, n);
    }
}

bool Ffmpeg::SetBinDir(const std::wstring& dir)
{
    if (dir.empty()) return false;
    std::wstring d = dir;
    if (FileExists(d)) d = PathGetDirectory(d);          // a full exe path was given
    if (d.empty()) return false;
    std::wstring ff = PathCombine(d, L"ffmpeg.exe");
    if (!FileExists(ff)) return false;
    std::wstring fp = PathCombine(d, L"ffprobe.exe");

    paths_.binDir = d;
    paths_.ffmpeg = ff;
    paths_.ffprobe = FileExists(fp) ? fp : std::wstring();
    return true;
}

bool Ffmpeg::Locate(const std::wstring& preferredDir)
{
    std::vector<std::wstring> candidates;

    AddCandidateDir(candidates, preferredDir);
    AddCandidateDir(candidates, EnvString(L"FASTVIDEOCUT_FFMPEG"));

    std::wstring exeDir = GetExeDir();
    AddCandidateDir(candidates, exeDir);
    AddCandidateDir(candidates, PathCombine(exeDir, L"bin"));
    AddCandidateDir(candidates, PathCombine(exeDir, L"ffmpeg"));
    AddCandidateDir(candidates, PathCombine(exeDir, L"ffmpeg\\bin"));

    std::wstring walk = exeDir;
    for (int i = 0; i < 4 && !walk.empty(); ++i)
    {
        walk = PathGetDirectory(walk);
        if (walk.empty()) break;
        AddCandidateDir(candidates, PathCombine(walk, L"bin"));
        AddFfmpegFoldersNear(candidates, walk);
    }

    AddPathEntries(candidates);

    std::wstring pf = EnvString(L"ProgramFiles");
    std::wstring pf86 = EnvString(L"ProgramFiles(x86)");
    std::wstring lad = EnvString(L"LOCALAPPDATA");
    std::wstring up = EnvString(L"USERPROFILE");
    std::wstring pd = EnvString(L"ProgramData");
    AddCandidateDir(candidates, PathCombine(pf, L"ffmpeg\\bin"));
    AddCandidateDir(candidates, PathCombine(pf86, L"ffmpeg\\bin"));
    AddCandidateDir(candidates, L"C:\\ffmpeg\\bin");
    AddCandidateDir(candidates, PathCombine(lad, L"Microsoft\\WinGet\\Links"));
    AddCandidateDir(candidates, PathCombine(up, L"scoop\\shims"));
    AddCandidateDir(candidates, PathCombine(pd, L"chocolatey\\bin"));

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (SetBinDir(candidates[i]) && available())
            return true;
    }

    // keep the first dir that at least had ffmpeg.exe
    for (size_t i = 0; i < candidates.size(); ++i)
        if (SetBinDir(candidates[i])) return false;

    paths_ = FfmpegPaths();
    return false;
}

std::wstring Ffmpeg::Version() const
{
    if (paths_.ffmpeg.empty()) return std::wstring();
    ProcessResult r;
    if (!RunProcessCapture(paths_.ffmpeg, L"-hide_banner -version", std::wstring(),
                           CancelToken(), r))
        return std::wstring();
    std::vector<std::string> lines = SplitOutputLines(r.output);
    if (lines.empty()) return std::wstring();
    return Utf8ToWide(lines[0]);
}

// -----------------------------------------------------------------------
// bit depth / HDR helpers
// -----------------------------------------------------------------------
namespace
{
    inline bool IsDigitC(char c) { return c >= '0' && c <= '9'; }
}

// HEVC/AV1 usually leave bits_per_raw_sample empty, so the pixel format name is
// the reliable source: "yuv420p10le" -> 10, "p010le" -> 10, "yuv420p" -> 8.
int BitDepthFromPixFmt(const std::string& pixFmt)
{
    if (pixFmt.empty()) return 0;
    size_t p = pixFmt.find('p');
    if (p == std::string::npos) return 0;
    size_t i = p + 1;
    while (i < pixFmt.size() && IsDigitC(pixFmt[i])) ++i;
    if (i == p + 1) return 0;                       // "...p" -> plain 8 bit
    int depth = atoi(pixFmt.substr(p + 1, i - p - 1).c_str());
    if (depth < 8 || depth > 16) return 0;
    return depth;
}

int VideoInfo::bitDepth() const
{
    if (bitsPerRawSample >= 8 && bitsPerRawSample <= 16) return bitsPerRawSample;
    int d = BitDepthFromPixFmt(pixFmt);
    return d > 0 ? d : 8;                           // 8 bit is the safe default
}

bool VideoInfo::isHdr() const
{
    return colorTransfer == "smpte2084" || colorTransfer == "arib-std-b67";
}

std::string VideoInfo::hdrLabel() const
{
    if (colorTransfer == "smpte2084") return "HDR10";
    if (colorTransfer == "arib-std-b67") return "HLG";
    return "SDR";
}

std::string VideoInfo::bitDepthLabel() const
{
    return std::to_string(bitDepth()) + "bit";
}

std::string VideoInfo::formatLabel() const
{
    return hdrLabel() + " · " + bitDepthLabel();
}
// ---------------------------------------------------------------------------
// probe
// ---------------------------------------------------------------------------
bool Ffmpeg::Probe(const std::wstring& file, VideoInfo& info, std::string& err,
                   const CancelToken& cancel) const
{
    info = VideoInfo();
    err.clear();

    if (!available())
    {
        err = "ffmpeg/ffprobe not available";
        return false;
    }
    if (!FileExists(file))
    {
        err = "file not found";
        return false;
    }

    info.sizeBytes = FileSizeBytes(file);
    double streamDuration = 0.0;

    // --- video stream ------------------------------------------------------
    {
        std::vector<std::wstring> a;
        a.push_back(L"-v");              a.push_back(L"error");
        a.push_back(L"-select_streams"); a.push_back(L"v:0");
        a.push_back(L"-show_entries");
        a.push_back(L"stream=codec_name,width,height,r_frame_rate,avg_frame_rate,nb_frames,duration,"
                    L"pix_fmt,bits_per_raw_sample,color_transfer,color_primaries,color_space");
        a.push_back(L"-of");             a.push_back(L"default=noprint_wrappers=1");
        a.push_back(file);

        ProcessResult r;
        if (!RunProcessCapture(paths_.ffprobe, JoinArgs(a), std::wstring(), cancel, r))
        {
            err = "failed to start ffprobe";
            return false;
        }
        if (r.exitCode != 0)
        {
            err = "ffprobe error: " + r.output;
            return false;
        }

        std::vector<std::pair<std::string, std::string> > kv = ParseKeyValueLines(r.output);
        for (size_t i = 0; i < kv.size(); ++i)
        {
            const std::string& k = kv[i].first;
            const std::string& v = kv[i].second;
            if (k == "codec_name")          info.vcodec = v;
            else if (k == "width")          info.width = atoi(v.c_str());
            else if (k == "height")         info.height = atoi(v.c_str());
            else if (k == "nb_frames")      info.nbFrames = atoi(v.c_str());
            else if (k == "duration")       streamDuration = atof(v.c_str());
            else if (k == "start_time")     info.videoStartTime = atof(v.c_str());
            else if (k == "r_frame_rate")
            {
                double f = ParseRational(v);
                if (f > 0.0) info.fps = f;
            }
            else if (k == "avg_frame_rate")
            {
                double f = ParseRational(v);
                if (f > 0.0 && f < 1000.0) info.fps = f;
            }
            else if (k == "pix_fmt")             info.pixFmt = v;
            else if (k == "bits_per_raw_sample") info.bitsPerRawSample = atoi(v.c_str());
            else if (k == "color_transfer")      info.colorTransfer = v;
            else if (k == "color_primaries")     info.colorPrimaries = v;
            else if (k == "color_space")         info.colorSpace = v;
        }

        if (info.width <= 0 || info.height <= 0)
        {
            err = "no video stream found";
            return false;
        }
        if (info.fps <= 0.0) info.fps = 25.0;
        if (streamDuration > 0.0) info.duration = streamDuration;
    }

    // --- container ---------------------------------------------------------
    {
        std::vector<std::wstring> a;
        a.push_back(L"-v");              a.push_back(L"error");
        a.push_back(L"-show_entries");   a.push_back(L"format=duration,format_name,size");
        a.push_back(L"-of");             a.push_back(L"default=noprint_wrappers=1");
        a.push_back(file);

        ProcessResult r;
        if (RunProcessCapture(paths_.ffprobe, JoinArgs(a), std::wstring(), cancel, r) &&
            r.exitCode == 0)
        {
            std::vector<std::pair<std::string, std::string> > kv = ParseKeyValueLines(r.output);
            for (size_t i = 0; i < kv.size(); ++i)
            {
                if (kv[i].first == "duration")
                {
                    double d = atof(kv[i].second.c_str());
                    if (d > 0.0) info.duration = d;
                }
                else if (kv[i].first == "format_name")
                {
                    info.container = kv[i].second;
                }
                else if (kv[i].first == "size")
                {
                    long long sz = strtoll(kv[i].second.c_str(), nullptr, 10);
                    if (sz > 0) info.sizeBytes = sz;
                }
            }
        }
    }

    // --- audio stream ------------------------------------------------------
    {
        std::vector<std::wstring> a;
        a.push_back(L"-v");              a.push_back(L"error");
        a.push_back(L"-select_streams"); a.push_back(L"a:0");
        a.push_back(L"-show_entries");   a.push_back(L"stream=codec_name,sample_rate,channels,start_time");
        a.push_back(L"-of");             a.push_back(L"default=noprint_wrappers=1");
        a.push_back(file);

        ProcessResult r;
        if (RunProcessCapture(paths_.ffprobe, JoinArgs(a), std::wstring(), cancel, r) &&
            r.exitCode == 0)
        {
            std::vector<std::pair<std::string, std::string> > kv = ParseKeyValueLines(r.output);
            for (size_t i = 0; i < kv.size(); ++i)
            {
                if (kv[i].first == "codec_name" && !kv[i].second.empty())
                {
                    info.acodec = kv[i].second;
                    info.hasAudio = true;
                }
                else if (kv[i].first == "sample_rate") info.sampleRate = atoi(kv[i].second.c_str());
                else if (kv[i].first == "channels")    info.channels = atoi(kv[i].second.c_str());
                else if (kv[i].first == "start_time")  info.audioStartTime = atof(kv[i].second.c_str());
            }
        }
    }

    if (info.duration <= 0.0)
    {
        err = "unable to determine duration";
        return false;
    }
    return true;
}

bool Ffmpeg::ScanWindow(const std::wstring& file, double t0, double dur,
                        const BlackParams& p,
                        std::vector<BlackRange>& out,
                        const std::function<void(double)>& onProgress,
                        const CancelToken& cancel, std::string& err) const
{
    out.clear();

    std::wstring filter = FormatString(L"blackdetect=d=%.3f:pix_th=%.3f:pic_th=%.3f",
                                       p.minDuration, p.pixThreshold, p.picThreshold);

    std::vector<std::wstring> a;
    a.push_back(L"-hide_banner");
    a.push_back(L"-nostdin");
    a.push_back(L"-loglevel");  a.push_back(L"info");
    if (t0 > 0.001)
    {
        a.push_back(L"-ss");          a.push_back(NumberText(t0, 3));
        a.push_back(L"-copyts");      // absolute timestamps in the report
        a.push_back(L"-t");           a.push_back(NumberText(dur, 3));
    }
    else if (dur > 0.0)
    {
        a.push_back(L"-t");           a.push_back(NumberText(dur, 3));
    }
    a.push_back(L"-i");         a.push_back(file);
    a.push_back(L"-map");       a.push_back(L"0:v:0");
    a.push_back(L"-vf");        a.push_back(filter);
    a.push_back(L"-an");
    a.push_back(L"-sn");
    a.push_back(L"-dn");
    a.push_back(L"-f");         a.push_back(L"null");
    a.push_back(L"-");

    Log(L"ffmpeg " + JoinArgs(a));

    std::string collected;
    ProcessResult r;
    bool started = RunProcess(paths_.ffmpeg, JoinArgs(a), std::wstring(), cancel,
        [&](const std::string& line)
        {
            collected += line;
            collected += '\n';
            if (onProgress && dur > 0.0)
            {
                size_t pos = line.find("time=");
                if (pos != std::string::npos)
                {
                    std::string t = line.substr(pos + 5);
                    size_t sp = t.find(' ');
                    if (sp != std::string::npos) t = t.substr(0, sp);
                    double sec = 0.0;
                    if (ParseTimecode(Utf8ToWide(t), sec))
                        onProgress(ClampValue(sec / dur, 0.0, 1.0));
                }
            }
        }, r);

    if (!started)
    {
        err = "failed to start ffmpeg";
        return false;
    }
    if (r.cancelled)
    {
        err = "cancelled";
        return false;
    }

    ParseBlackDetectOutput(collected, out);

    const double winEnd = t0 + dur;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].start < 0.0) out[i].start = 0.0;
        if (out[i].end > winEnd) out[i].end = winEnd;
    }

    if (r.exitCode != 0 && out.empty())
    {
        std::vector<std::string> lines = SplitOutputLines(collected);
        std::string tail;
        size_t begin = lines.size() > 6 ? lines.size() - 6 : 0;
        for (size_t i = begin; i < lines.size(); ++i) { tail += lines[i]; tail += '\n'; }
        err = "blackdetect failed (exit " + FormatSecondsUtf8((double)r.exitCode, 0) + "): " + tail;
        return false;
    }
    return true;
}

// Long videos only decode a slice of the file, because the intro / outro
// boundaries almost always sit near its ends. Each side is configured on its own
// (an intro is usually much shorter than the outro, or the other way round):
//   > 0 = only that many seconds at the head / at the tail
//   = 0 = that side is not scanned at all (head 180 / tail 0 = first 180 s only)
//   < 0 = no limit on that side -> the whole file is scanned
bool Ffmpeg::DetectBlack(const std::wstring& file, double duration,
                         const BlackParams& p,
                         std::vector<BlackRange>& out,
                         const std::function<void(double)>& onProgress,
                         const CancelToken& cancel, std::string& err) const
{
    out.clear();
    err.clear();

    if (!available())
    {
        err = "ffmpeg not available";
        return false;
    }

    const double head = p.headScanSec;
    const double tail = p.tailScanSec;

    std::vector<std::pair<double, double> > windows;
    if (duration <= 0.0 || head < 0.0 || tail < 0.0 || head + tail >= duration)
    {
        // 时长未知、某一侧不限，或两个窗口加起来已经盖住整段 -> 一次整段扫完
        windows.push_back(std::make_pair(0.0, duration));
    }
    else
    {
        // 两个窗口都不会越过片尾，也不会互相重叠（上面已保证 head + tail < duration）
        if (head > 0.0) windows.push_back(std::make_pair(0.0, head));
        if (tail > 0.0) windows.push_back(std::make_pair(duration - tail, tail));
    }

    if (windows.empty())
    {
        // 两侧都是 0 = 没有任何可扫区域（设置界面已拦下，这里兜底）
        err = "no scan window: head=0 and tail=0";
        return false;
    }

    double total = 0.0;
    for (size_t i = 0; i < windows.size(); ++i) total += windows[i].second;
    if (total <= 0.0) total = 1.0;

    double done = 0.0;
    for (size_t i = 0; i < windows.size(); ++i)
    {
        if (cancel.IsCancelled()) { err = "cancelled"; return false; }

        double w0 = windows[i].first;
        double wd = windows[i].second;
        const double doneBefore = done;

        std::vector<BlackRange> part;
        std::string werr;
        ScanWindow(file, w0, wd, p, part,
                   [&](double f)
                   {
                       if (onProgress)
                           onProgress(ClampValue((doneBefore + wd * f) / total, 0.0, 1.0));
                   }, cancel, werr);
        for (size_t k = 0; k < part.size(); ++k) out.push_back(part[k]);
        done += wd;
        if (!werr.empty() && out.empty()) err = werr;
    }

    MergeBlackRanges(out, 0.05, p.minDuration * 0.5);
    return true;
}

// ---------------------------------------------------------------------------
// keyframe lookup (stream copy can only start on a keyframe)
// ---------------------------------------------------------------------------
bool Ffmpeg::KeyframeTimeBefore(const std::wstring& file, double t, double& out,
                                const CancelToken& cancel) const
{
    out = 0.0;
    if (!available() || file.empty() || t <= 0.0) return false;

    // look back far enough to find a keyframe for sources with a long GOP
    double from = t - 120.0;
    if (from < 0.0) from = 0.0;

    std::wstring args = L"-v error -select_streams v:0 -skip_frame nokey";
    args += L" -read_intervals " + NumberText(from, 3) + L"%" + NumberText(t + 0.25, 3);
    args += L" -show_entries frame=pts_time -of csv=p=0 ";
    args += QuoteArg(file);

    Log(L"ffprobe " + args);

    ProcessResult r;
    if (!RunProcessCapture(paths_.ffprobe, args, std::wstring(), cancel, r))
        return false;
    if (r.cancelled || r.exitCode != 0)
        return false;

    double best = -1.0;
    double limit = t + 0.05;
    std::vector<std::string> lines = SplitOutputLines(r.output);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string& ln = lines[i];
        if (ln.empty()) continue;
        char* endp = nullptr;
        double v = strtod(ln.c_str(), &endp);
        if (endp == ln.c_str()) continue;
        if (v < 0.0 || v > limit) continue;
        if (v > best) best = v;
    }
    if (best < 0.0) return false;

    out = best;
    return true;
}

// ---------------------------------------------------------------------------
// thumbnail mosaic (fast frame stream expansion)
// ---------------------------------------------------------------------------
bool Ffmpeg::MakeMosaic(const std::wstring& file, double t0, double t1,
                        int cols, int rows, int thumbW, int thumbH,
                        const std::wstring& outBmp,
                        const CancelToken& cancel, std::string& err) const
{
    err.clear();

    if (!available())
    {
        err = "ffmpeg not available";
        return false;
    }
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (thumbW < 8) thumbW = 8;
    if (thumbH < 8) thumbH = 8;

    double dur = t1 - t0;
    if (dur <= 0.001)
    {
        err = "empty time range";
        return false;
    }

    int count = cols * rows;
    double fps = (double)count / dur;
    if (fps <= 0.0) fps = 1.0;
    if (fps > 1000.0) fps = 1000.0;

    std::wstring vf = FormatString(
        L"fps=%.6f,scale=%d:%d:force_original_aspect_ratio=decrease,"
        L"pad=%d:%d:(ow-iw)/2:(oh-ih)/2,tile=%dx%d",
        fps, thumbW, thumbH, thumbW, thumbH, cols, rows);

    std::vector<std::wstring> a;
    a.push_back(L"-y");
    a.push_back(L"-hide_banner");
    a.push_back(L"-nostdin");
    a.push_back(L"-loglevel"); a.push_back(L"error");
    a.push_back(L"-ss");       a.push_back(NumberText(t0, 3));
    a.push_back(L"-t");        a.push_back(NumberText(dur, 3));
    a.push_back(L"-i");        a.push_back(file);
    a.push_back(L"-an");
    a.push_back(L"-sn");
    a.push_back(L"-dn");
    a.push_back(L"-vf");       a.push_back(vf);
    a.push_back(L"-frames:v"); a.push_back(L"1");
    a.push_back(L"-pix_fmt");  a.push_back(L"bgr24");
    a.push_back(L"-c:v");      a.push_back(L"bmp");
    a.push_back(L"-f");        a.push_back(L"image2");
    a.push_back(outBmp);

    ProcessResult r;
    if (!RunProcessCapture(paths_.ffmpeg, JoinArgs(a), std::wstring(), cancel, r))
    {
        err = "failed to start ffmpeg";
        return false;
    }
    if (r.cancelled)
    {
        err = "cancelled";
        return false;
    }
    if (r.exitCode != 0 || !FileExists(outBmp))
    {
        err = "mosaic generation failed: " + r.output;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// trimming / concatenating
// ---------------------------------------------------------------------------
namespace
{
    bool IsMp4LikeOutput(const std::wstring& path)
    {
        std::wstring ext = PathGetExtension(path);
        return ext == L".mp4" || ext == L".mov" || ext == L".m4v" || ext == L".m4a";
    }

    void AppendEncodeArgs(std::vector<std::wstring>& a, const EncodeOptions& enc,
                          const std::wstring& outPath, bool withAudio)
    {
        if (!enc.reencode)
        {
            a.push_back(L"-c");
            a.push_back(L"copy");
            a.push_back(L"-avoid_negative_ts");
            a.push_back(L"make_zero");
            return;
        }
        a.push_back(L"-c:v");   a.push_back(L"libx264");
        a.push_back(L"-crf");   a.push_back(FormatString(L"%d", enc.crf));
        a.push_back(L"-preset"); a.push_back(Utf8ToWide(enc.preset));
        a.push_back(L"-pix_fmt"); a.push_back(L"yuv420p");
        if (withAudio)
        {
            a.push_back(L"-c:a"); a.push_back(Utf8ToWide(enc.audioCodec));
            a.push_back(L"-b:a"); a.push_back(FormatString(L"%dk", enc.audioBitrateK));
        }
        else
        {
            a.push_back(L"-an");
        }
        if (enc.faststart && IsMp4LikeOutput(outPath))
        {
            a.push_back(L"-movflags");
            a.push_back(L"+faststart");
        }
    }
}

bool Ffmpeg::Trim(const std::wstring& in, double t0, double t1, const std::wstring& out,
                  const EncodeOptions& enc, std::string& err) const
{
    err.clear();

    if (!available())
    {
        err = "ffmpeg not available";
        return false;
    }

    double dur = t1 - t0;
    if (dur <= 0.0)
    {
        err = "empty segment";
        return false;
    }

    // -ss must stay BEFORE -i:
    //   input side  -> ffmpeg lands on the preceding keyframe, so the output starts
    //                  on a decodable picture (what the reference script does)
    //   output side -> timestamps are only shifted, the video stream really starts
    //                  at the NEXT keyframe -> seconds of audio with no picture
    std::vector<std::wstring> a;
    a.push_back(L"-y");
    a.push_back(L"-hide_banner");
    a.push_back(L"-nostdin");
    a.push_back(L"-loglevel");  a.push_back(L"error");
    a.push_back(L"-ss");        a.push_back(NumberText(t0, 3));
    a.push_back(L"-i");         a.push_back(in);
    a.push_back(L"-t");         a.push_back(NumberText(dur, 3));
    a.push_back(L"-map");       a.push_back(L"0:v:0");
    a.push_back(L"-map");       a.push_back(L"0:a:0?");
    a.push_back(L"-map_metadata"); a.push_back(L"0");
    AppendEncodeArgs(a, enc, out, true);
    a.push_back(out);

    Log(L"ffmpeg " + JoinArgs(a));

    ProcessResult r;
    if (!RunProcessCapture(paths_.ffmpeg, JoinArgs(a), std::wstring(), CancelToken(), r))
    {
        err = "failed to start ffmpeg";
        return false;
    }
    if (r.exitCode != 0 || !FileExists(out))
    {
        err = "trim failed (exit " + FormatSecondsUtf8((double)r.exitCode, 0) + "): " + r.output;
        return false;
    }
    return true;
}

bool Ffmpeg::Concat(const std::vector<std::wstring>& parts, const std::wstring& listFile,
                    const std::wstring& out, const EncodeOptions& enc, std::string& err) const
{
    err.clear();

    if (!available())
    {
        err = "ffmpeg not available";
        return false;
    }
    if (parts.empty())
    {
        err = "nothing to concatenate";
        return false;
    }

    // ---- write the concat demuxer list (UTF-8, no BOM) --------------------
    std::string list;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        list += WideToUtf8(EscapeConcatPath(parts[i]));
        list += "\r\n";
    }
    FILE* fp = _wfopen(listFile.c_str(), L"wb");
    if (!fp)
    {
        err = "cannot write concat list file";
        return false;
    }
    if (!list.empty()) fwrite(list.data(), 1, list.size(), fp);
    fclose(fp);

    std::vector<std::wstring> a;
    a.push_back(L"-y");
    a.push_back(L"-hide_banner");
    a.push_back(L"-nostdin");
    a.push_back(L"-loglevel");  a.push_back(L"error");
    a.push_back(L"-f");         a.push_back(L"concat");
    a.push_back(L"-safe");      a.push_back(L"0");
    a.push_back(L"-i");         a.push_back(listFile);
    // 只取每段的默认视频/音频流。源文件可能带内嵌封面（mp4/mkv 的 attached_pic
    // 图像流），不显式映射时它会被一起搬进输出，个别容器下还会直接报错。
    a.push_back(L"-map");       a.push_back(L"0:v:0");
    a.push_back(L"-map");       a.push_back(L"0:a:0?");

    if (!enc.reencode)
    {
        a.push_back(L"-c");     a.push_back(L"copy");
        a.push_back(L"-avoid_negative_ts"); a.push_back(L"make_zero");
    }
    else
    {
        AppendEncodeArgs(a, enc, out, true);
    }
    a.push_back(out);

    Log(L"ffmpeg " + JoinArgs(a));

    ProcessResult r;
    if (!RunProcessCapture(paths_.ffmpeg, JoinArgs(a), std::wstring(), CancelToken(), r))
    {
        err = "failed to start ffmpeg";
        return false;
    }
    if (r.exitCode != 0 || !FileExists(out))
    {
        err = "concat failed (exit " + FormatSecondsUtf8((double)r.exitCode, 0) + "): " + r.output;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// normalisation (re-encode to a common format before merging)
// ---------------------------------------------------------------------------
bool Ffmpeg::Normalize(const std::wstring& in, const std::wstring& out,
                       int width, int height, double fps, bool hasAudio,
                       const EncodeOptions& enc, std::string& err) const
{
    err.clear();

    if (!available())
    {
        err = "ffmpeg not available";
        return false;
    }
    if (width < 2) width = 2;
    if (height < 2) height = 2;
    if (fps <= 0.0) fps = 25.0;

    std::vector<std::wstring> a;
    a.push_back(L"-y");
    a.push_back(L"-hide_banner");
    a.push_back(L"-nostdin");
    a.push_back(L"-loglevel"); a.push_back(L"error");
    a.push_back(L"-i");        a.push_back(in);
    // 同上：只取默认视频/音频流，内嵌封面（attached_pic）不参与重编码，
    // 否则 ffmpeg 会把封面当成第二路视频流而报错。
    a.push_back(L"-map");      a.push_back(L"0:v:0");
    if (hasAudio)
    {
        a.push_back(L"-map");  a.push_back(L"0:a:0");
    }
    else
    {
        a.push_back(L"-f");    a.push_back(L"lavfi");
        a.push_back(L"-i");    a.push_back(L"anullsrc=r=48000:cl=stereo");
    }

    std::wstring vf = FormatString(
        L"scale=%d:%d:force_original_aspect_ratio=decrease,"
        L"pad=%d:%d:(ow-iw)/2:(oh-ih)/2,fps=%.6f,setsar=1",
        width, height, width, height, fps);

    a.push_back(L"-vf");       a.push_back(vf);
    a.push_back(L"-c:v");      a.push_back(L"libx264");
    a.push_back(L"-crf");      a.push_back(FormatString(L"%d", enc.crf));
    a.push_back(L"-preset");   a.push_back(Utf8ToWide(enc.preset));
    a.push_back(L"-pix_fmt");  a.push_back(L"yuv420p");
    a.push_back(L"-c:a");      a.push_back(L"aac");
    a.push_back(L"-b:a");      a.push_back(L"192k");
    a.push_back(L"-ar");       a.push_back(L"48000");
    a.push_back(L"-ac");       a.push_back(L"2");
    if (!hasAudio)
    {
        a.push_back(L"-shortest");
    }
    if (enc.faststart && IsMp4LikeOutput(out))
    {
        a.push_back(L"-movflags");
        a.push_back(L"+faststart");
    }
    a.push_back(out);

    Log(L"ffmpeg " + JoinArgs(a));

    ProcessResult r;
    if (!RunProcessCapture(paths_.ffmpeg, JoinArgs(a), std::wstring(), CancelToken(), r))
    {
        err = "failed to start ffmpeg";
        return false;
    }
    if (r.exitCode != 0 || !FileExists(out))
    {
        err = "normalize failed (exit " + FormatSecondsUtf8((double)r.exitCode, 0) + "): " + r.output;
        return false;
    }
    return true;
}
