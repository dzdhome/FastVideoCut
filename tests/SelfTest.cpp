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
// SelfTest.cpp - console harness that exercises the core modules with real
// ffmpeg calls.
//     g++ -std=c++17 -municode tests/SelfTest.cpp src/*.cpp -o SelfTest.exe
// Usage:
//     SelfTest.exe <video1> [video2] [outDir]
// ---------------------------------------------------------------------------
#include "../src/Utf.h"
#include "../src/Process.h"
#include "../src/Ffmpeg.h"
#include "../src/Project.h"
#include "../src/Loc.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool cond, const char* what, const std::wstring& detail = std::wstring())
{
    if (cond)
    {
        ++g_pass;
        ::wprintf(L"  [ OK ] %hs\n", what);
    }
    else
    {
        ++g_fail;
        ::wprintf(L"  [FAIL] %hs   %ls\n", what, detail.c_str());
    }
}

static bool Nearly(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

static bool FindTestMedia(const std::wstring& exeDir, std::wstring& out)
{
    std::wstring dir = exeDir;
    for (int i = 0; i < 4; ++i)
    {
        std::wstring cand = PathCombine(dir, L"_test\\media\\v1.mp4");
        if (FileExists(cand)) { out = cand; return true; }
        dir = PathGetDirectory(dir);
        if (dir.empty()) break;
    }
    return false;
}

int wmain(int argc, wchar_t** argv)
{
    ::wprintf(L"==== FastVideoCut self test ====\n\n");

    // -----------------------------------------------------------------------
    // 1. helpers
    // -----------------------------------------------------------------------
    ::wprintf(L"[1] helper functions\n");
    Check(QuoteArg(L"abc") == L"abc", "QuoteArg keeps plain args");
    Check(QuoteArg(L"a b") == L"\"a b\"", "QuoteArg quotes spaces");
    Check(QuoteArg(L"C:\\dir\\file.mp4") == L"C:\\dir\\file.mp4", "QuoteArg keeps backslashes");
    Check(QuoteArg(L"a\\\"b") == L"\"a\\\\\\\"b\"", "QuoteArg escapes quote",
          QuoteArg(L"a\\\"b"));
    Check(FormatTimecode(3661.5) == L"01:01:01.500", "FormatTimecode",
          FormatTimecode(3661.5));
    double t = 0.0;
    Check(ParseTimecode(L"01:02:03.250", t) && Nearly(t, 3723.25, 1e-6), "ParseTimecode hh:mm:ss");
    Check(ParseTimecode(L"12.5", t) && Nearly(t, 12.5, 1e-6), "ParseTimecode seconds");
    Check(EscapeConcatPath(L"C:\\a b\\c.mp4") == L"file 'C:\\a b\\c.mp4'", "EscapeConcatPath");

    // ---- merged output name: first + last video, shared prefix written once ---
    Check(MakeMergeName(L"001", L"010") == L"001-010.mp4",
          "001 + 010 -> 001-010.mp4", MakeMergeName(L"001", L"010"));
    Check(MakeMergeName(L"\u89c6\u9891001", L"\u89c6\u9891010") == L"\u89c6\u9891001-010.mp4",
          "shared CJK prefix kept once",
          MakeMergeName(L"\u89c6\u9891001", L"\u89c6\u9891010"));
    // 第1集 / 第2集 share the prefix "第", which is folded away once.
    Check(MakeMergeName(L"\u7b2c1\u96c6", L"\u7b2c2\u96c6") == L"\u7b2c1\u96c6-2\u96c6.mp4",
          "shared CJK prefix folded once",
          MakeMergeName(L"\u7b2c1\u96c6", L"\u7b2c2\u96c6"));
    Check(MakeMergeName(L"aaa", L"bbb") == L"aaa-bbb.mp4",
          "no shared prefix -> plain join",
          MakeMergeName(L"aaa", L"bbb"));
    Check(MakeMergeName(L"clip_1", L"clip_2") == L"clip_1-2.mp4",
          "partial ASCII prefix",
          MakeMergeName(L"clip_1", L"clip_2"));
    Check(MakeMergeName(L"C:\\a\\001.mp4", L"D:\\b\\010.mkv") == L"001-010.mp4",
          "paths and extensions are stripped",
          MakeMergeName(L"C:\\a\\001.mp4", L"D:\\b\\010.mkv"));
    Check(MakeMergeName(L"movie", L"movie") == L"movie.mp4",
          "identical names collapse",
          MakeMergeName(L"movie", L"movie"));
    Check(MakeMergeName(L"abc", L"abcd") == L"abcd.mp4",
          "prefix covering one name leaves no dangling dash",
          MakeMergeName(L"abc", L"abcd"));
    Check(MakeMergeName(L"", L"010") == L"010.mp4",
          "empty first name falls back",
          MakeMergeName(L"", L"010"));
    Check(MakeMergeName(L"001", L"") == L"001.mp4",
          "empty last name falls back",
          MakeMergeName(L"001", L""));
    Check(MakeMergeName(L"", L"") == L"merged.mp4",
          "both empty -> merged",
          MakeMergeName(L"", L""));
    Check(MakeMergeName(L"001", L"010", L"mkv") == L"001-010.mkv",
          "extension is honoured",
          MakeMergeName(L"001", L"010", L"mkv"));
    {
        // U+1F3AC (clapper board) written as UTF-8, then widened, so the literal
        // does not rely on a surrogate-pair escape (not valid in C++).
        const unsigned char clapper[] = { 0xF0, 0x9F, 0x8E, 0xAC };
        std::wstring emojiPrefix;
        for (size_t i = 0; i < sizeof(clapper); ++i)
            emojiPrefix += (wchar_t)clapper[i];
        std::wstring a = emojiPrefix + L"001";
        std::wstring b = emojiPrefix + L"010";
        std::wstring emoji = MakeMergeName(a, b);
        Check(emoji == a + L"-010.mp4", "emoji prefix stays intact", emoji);
    }

    std::vector<BlackRange> parsed;
    std::string fake =
        "[Parsed_blackdetect_0 @ 0x1] black_start:6 black_end:8 black_duration:2\n"
        "[Parsed_blackdetect_0 @ 0x1] black_start:14 black_end:15.52 black_duration:1.52\n";
    ParseBlackDetectOutput(fake, parsed);
    Check(parsed.size() == 2, "ParseBlackDetectOutput finds 2 ranges");
    Check(parsed.size() == 2 && Nearly(parsed[0].start, 6.0, 1e-6) &&
          Nearly(parsed[0].end, 8.0, 1e-6), "first range 6 -> 8");
    Check(parsed.size() == 2 && Nearly(parsed[1].end, 15.52, 1e-3), "second range ends 15.52");
    Check(Nearly(ParseRational("30000/1001"), 29.97, 0.01), "ParseRational");
    Check(ParseRational("25/1") == 25.0, "ParseRational integer");

    // %s in a wide format string must mean wchar_t* on both MinGW (ISO) and
    // MSVC - the whole UI builds its text through FormatString this way.
    Check(FormatString(L"%s|%d|%.2f", std::wstring(L"abc").c_str(), 7, 1.5) == L"abc|7|1.50",
          "FormatString %s + wide args",
          FormatString(L"got [%ls]", FormatString(L"%s", std::wstring(L"abc").c_str()).c_str()));
    Check(FormatString(L"%s", L"wide") == L"wide", "FormatString literal wide arg");

    // -----------------------------------------------------------------------
    // 2. ffmpeg discovery
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[2] ffmpeg discovery\n");
    Ffmpeg ff;
    bool located = ff.Locate(std::wstring());
    Check(located, "ffmpeg + ffprobe located", ff.paths().binDir);
    if (!located)
    {
        ::wprintf(L"\nCannot continue without ffmpeg.\n");
        return 1;
    }
    ::wprintf(L"       ffmpeg : %ls\n", ff.paths().ffmpeg.c_str());
    ::wprintf(L"       version: %ls\n", ff.Version().c_str());

    // -----------------------------------------------------------------------
    // 3. media inputs
    // -----------------------------------------------------------------------
    std::vector<std::wstring> media;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring a = argv[i];
        if (DirectoryExists(a))
        {
            std::vector<std::wstring> found = ListFilesByExt(a, SupportedMediaExtensions());
            for (size_t k = 0; k < found.size(); ++k) media.push_back(found[k]);
        }
        else
        {
            media.push_back(a);
        }
    }
    if (media.empty())
    {
        std::wstring autoFile;
        if (FindTestMedia(GetExeDir(), autoFile)) media.push_back(autoFile);
    }
    if (media.empty())
    {
        ::wprintf(L"\nNo input media given and no _test\\media\\v1.mp4 found.\n");
        return 1;
    }

    std::wstring outDir = PathCombine(GetExeDir(), L"selftest_out");
    EnsureDirectory(outDir);

    ::wprintf(L"\n[3] inputs\n");
    for (size_t i = 0; i < media.size(); ++i)
        ::wprintf(L"       %ls\n", media[i].c_str());
    ::wprintf(L"       output dir: %ls\n", outDir.c_str());

    // -----------------------------------------------------------------------
    // 4. probe
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[4] ffprobe\n");
    VideoInfo info;
    std::string err;
    bool probed = ff.Probe(media[0], info, err);
    Check(probed, "Probe succeeded", Utf8ToWide(err));
    if (!probed) return 1;
    ::wprintf(L"       %ls | %dx%d | %.3f fps | %ls | audio=%hs\n",
              FormatClock(info.duration).c_str(), info.width, info.height, info.fps,
              Utf8ToWide(info.vcodec).c_str(), info.hasAudio ? "yes" : "no");
    Check(info.width > 0 && info.height > 0, "resolution parsed");
    Check(info.duration > 0.0, "duration parsed");
    Check(info.hasAudio, "audio stream detected");

    // ---- HDR / bit depth --------------------------------------------------
    ::wprintf(L"       format: %hs (pix_fmt=%hs transfer=%hs)\n",
              info.formatLabel().c_str(), info.pixFmt.c_str(),
              info.colorTransfer.c_str());
    Check(info.bitDepth() >= 8 && info.bitDepth() <= 16, "bit depth in range",
          Utf8ToWide(info.bitDepthLabel()));
    Check(!info.hdrLabel().empty(), "hdr label is never empty");

    // pixel format -> bit depth (HEVC usually reports no bits_per_raw_sample)
    Check(BitDepthFromPixFmt("yuv420p") == 0,     "yuv420p -> 8 bit by default");
    Check(BitDepthFromPixFmt("yuv420p10le") == 10, "yuv420p10le -> 10 bit");
    Check(BitDepthFromPixFmt("yuv422p12le") == 12, "yuv422p12le -> 12 bit");
    Check(BitDepthFromPixFmt("p010le") == 10,      "p010le -> 10 bit");
    Check(BitDepthFromPixFmt("") == 0,             "empty pix_fmt -> unknown");

    // HDR detection by transfer characteristic
    {
        VideoInfo h;
        h.colorTransfer = "smpte2084";
        Check(h.isHdr() && h.hdrLabel() == "HDR10", "PQ transfer -> HDR10");
        h.colorTransfer = "arib-std-b67";
        Check(h.isHdr() && h.hdrLabel() == "HLG", "HLG transfer -> HLG");
        h.colorTransfer = "bt709";
        Check(!h.isHdr() && h.hdrLabel() == "SDR", "bt709 transfer -> SDR");
        h.pixFmt = "yuv420p10le";
        h.bitsPerRawSample = 10;
        Check(h.formatLabel() == "SDR · 10bit", "format label combines both",
              Utf8ToWide(h.formatLabel()));
    }

// -----------------------------------------------------------------------
    // 5. black detection on the real file
    // -----------------------------------------------------------------------
    // ---- 编码规格的显示名（左侧面板与合并检查都靠这些标签）----------------
    ::wprintf(L"       spec: %hs | %hs | %hs | %hs | %hs | %hs | %hs\n",
              info.videoCodecLabel().c_str(), info.profileLevelLabel().c_str(),
              info.pixFmtLabel().c_str(), info.fpsModeLabel().c_str(),
              info.audioCodecLabel().c_str(), info.channelLabel().c_str(),
              info.sampleRateLabel().c_str());

    Check(VideoCodecDisplayName("h264") == "AVC",    "h264 -> AVC");
    Check(VideoCodecDisplayName("hevc") == "HEVC",   "hevc -> HEVC");
    Check(VideoCodecDisplayName("AV1")  == "AV1",     "av1 -> AV1 (case insensitive)");
    Check(VideoCodecDisplayName("vp9")  == "VP9",     "vp9 -> VP9");
    Check(VideoCodecDisplayName("mpeg4")== "MPEG-4",  "mpeg4 -> MPEG-4");
    Check(VideoCodecDisplayName("")     == "",        "empty codec -> empty");
    Check(VideoCodecDisplayName("ffv1") == "FFV1",    "unknown codec -> upper case");

    Check(AudioCodecDisplayName("aac", "LC")     == "AAC LC", "aac + LC -> AAC LC");
    Check(AudioCodecDisplayName("aac", "HE-AAC") == "AAC HE", "aac + HE-AAC -> AAC HE");
    Check(AudioCodecDisplayName("aac", "")       == "AAC",    "aac without profile");
    Check(AudioCodecDisplayName("mp3", "")       == "MP3",    "mp3 -> MP3");
    Check(AudioCodecDisplayName("flac", "")      == "FLAC",   "flac -> FLAC");

    // level 值按编码换算：同是 4 级，h264 报 41，hevc 报 120
    Check(LevelDisplay("h264", 41) == "L4.1", "h264 level 41 -> L4.1");
    Check(LevelDisplay("h264", 31) == "L3.1", "h264 level 31 -> L3.1");
    Check(LevelDisplay("h264", 40) == "L4",   "h264 level 40 -> L4");
    Check(LevelDisplay("hevc", 120) == "L4",  "hevc level 120 -> L4");
    Check(LevelDisplay("hevc", 93) == "L3.1", "hevc level 93 -> L3.1");
    Check(LevelDisplay("hevc", 150) == "L5",  "hevc level 150 -> L5");
    Check(LevelDisplay("h264", 0) == "",      "level 0 -> unknown");
    Check(ProfileLevelDisplay("hevc", "Main", 120) == "Main@L4",   "Main@L4");
    Check(ProfileLevelDisplay("h264", "High", 41)  == "High@L4.1", "High@L4.1");
    Check(ProfileLevelDisplay("h264", "unknown", 0) == "",          "unknown profile -> empty");

    Check(ChannelLayoutDisplay("stereo", 2) == "2.0", "stereo -> 2.0");
    Check(ChannelLayoutDisplay("5.1", 6)     == "5.1", "5.1 -> 5.1");
    Check(ChannelLayoutDisplay("", 6)        == "5.1", "no layout, 6 channels -> 5.1");
    Check(ChannelLayoutDisplay("", 0)        == "",    "no layout, no channels");
    Check(SampleRateDisplay(44100) == "44.1K", "44100 -> 44.1K");
    Check(SampleRateDisplay(48000) == "48K",   "48000 -> 48K");
    Check(SampleRateDisplay(0)     == "",      "0 -> unknown");
    Check(FpsDisplay(25.0)   == "25fps",     "25 -> 25fps");
    Check(FpsDisplay(23.976) == "23.976fps", "23.976 -> 23.976fps");
    Check(FpsDisplay(0.0)    == "",          "0 fps -> unknown");

    Check(FpsModeDisplay(25.0, 25.0) == "CFR", "r == avg -> CFR");
    Check(FpsModeDisplay(25.0, 24.3) == "VBR", "avg != r -> VBR");
    Check(FpsModeDisplay(25.0, 0.0)  == "CFR", "avg missing -> CFR");
    Check(FpsModeDisplay(0.0, 0.0)   == "",    "no frame rate info -> unknown");

    // 时间线左侧三行的排版：空字段不能留下 " |  | " 这样的空洞
    {
        VideoItem bare;
        Check(bare.streamLine().empty() && bare.videoLine().empty() && bare.audioLine().empty(),
              "an unprobed item shows no spec lines");

        VideoItem full;
        full.info = info;
        ::wprintf(L"       row1: %ls\n       row2: %ls\n       row3: %ls\n",
                  full.streamLine().c_str(), full.videoLine().c_str(),
                  full.audioLine().c_str());
        Check(full.streamLine().find(L"x") != std::wstring::npos &&
              full.streamLine().find(L"fps") != std::wstring::npos,
              "row 1 is resolution + frame rate", full.streamLine());
        Check(full.videoLine() == Utf8ToWide(info.videoCodecLabel()) + L" | " +
                                Utf8ToWide(info.profileLevelLabel()) + L" | " +
                                Utf8ToWide(info.pixFmtLabel()),
              "row 2 is codec + profile@level + pixel format", full.videoLine());
        Check(full.audioLine() == Utf8ToWide(info.audioCodecLabel()) + L" | " +
                                Utf8ToWide(info.channelLabel()) + L" | " +
                                Utf8ToWide(info.sampleRateLabel()),
              "row 3 is audio codec + channels + sample rate", full.audioLine());

        // 缺字段时只显示有的那几个，不能出现空的竖线
        VideoInfo sparse;
        sparse.duration = 10.0; sparse.width = 1280; sparse.height = 720;
        sparse.vcodec = "av1";
        VideoItem s;
        s.info = sparse;
        Check(s.streamLine() == L"1280x720", "missing fps fields are dropped",
              s.streamLine());
        Check(s.videoLine() == L"AV1", "missing profile/pix_fmt are dropped",
              s.videoLine());
        Check(s.audioLine() == TR(L"无音频", L"no audio"), "no audio track is spelled out",
              s.audioLine());
    }

    ::wprintf(L"\n[5] blackdetect\n");
    BlackParams bp;
    std::vector<BlackRange> blacks;
    int progressCalls = 0;
    bool detOk = ff.DetectBlack(media[0], info.duration, bp, blacks,
                                [&](double p) { (void)p; ++progressCalls; },
                                CancelToken(), err);
    Check(detOk, "DetectBlack succeeded", Utf8ToWide(err));
    ::wprintf(L"       %d black range(s), progress callbacks: %d\n",
              (int)blacks.size(), progressCalls);
    for (size_t i = 0; i < blacks.size(); ++i)
        ::wprintf(L"         %.2f -> %.2f  (%.2fs)\n", blacks[i].start, blacks[i].end,
                  blacks[i].length());
    Check(progressCalls > 0, "progress reporting works");

    // -----------------------------------------------------------------------
    // 6. project model / segments
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[6] project model\n");
    Project proj;
    int idx = proj.AddFile(media[0]);
    Check(idx == 0, "AddFile");
    Check(proj.AddFile(media[0]) == -1, "duplicate rejected");
    proj.items[0].info = info;
    proj.items[0].blacks = blacks;
    proj.items[0].status = ItemStatus::Ready;
    Project::RebuildSegments(proj.items[0], false);

    int mediaSegs = 0, blackSegs = 0;
    for (size_t i = 0; i < proj.items[0].segments.size(); ++i)
    {
        if (proj.items[0].segments[i].kind == SegKind::Black) ++blackSegs;
        else ++mediaSegs;
    }
    ::wprintf(L"       segments: %d media + %d black\n", mediaSegs, blackSegs);
    int nseg = (int)proj.items[0].segments.size();
    Check(blackSegs == (int)blacks.size(), "one black segment per detected range");
    Check(mediaSegs == (int)blacks.size() + 1, "media segments = black + 1");

    // 黑屏只是"片头/片尾分界线"的提示，默认整段保留，不会被自动删掉
    Check(proj.items[0].selectedSegmentCount() == nseg, "default keeps every segment (black included)");
    Check(Nearly(proj.items[0].selectedDuration(), info.duration, 0.05), "default keep == full duration");
    Check(proj.items[0].hasContiguousFullSelection(), "default is a full-file selection");

    // 左键 = 保留起点，右键 = 保留终点，两者之间（含）全部保留
    Project::ClearSelection(proj.items[0]);
    Check(proj.items[0].selectedSegmentCount() == 0, "nothing selected after clear");
    Project::ClickKeepStart(proj.items[0], 2);
    Check(proj.items[0].selectedSegmentCount() == 1, "first left click keeps only that segment");
    Project::ClickKeepEnd(proj.items[0], nseg - 1);
    Check(proj.items[0].selectedSegmentCount() == nseg - 2, "right click extends the range to the end");

    // 默认（整段保留）时，左键只移动起点，终点仍停在最后一段
    Project::SelectAll(proj.items[0]);
    Project::ClickKeepStart(proj.items[0], 1);
    Check(proj.items[0].selectedSegmentCount() == nseg - 1, "left click moves the start anchor only");
    Project::ClickKeepEnd(proj.items[0], 0);
    Check(proj.items[0].selectedSegmentCount() == 2, "right click closes the range at segment 0");

    Project::ClickKeepStart(proj.items[0], 1);
    Project::ClickKeepEnd(proj.items[0], 2);
    Check(proj.items[0].selectedSegmentCount() == 2, "range [start..end] keeps 2 segments");
    Check(proj.items[0].keepRangeText() ==
              FormatString(L"%s - %s",
                           FormatTimecode(proj.items[0].segments[1].t0).c_str(),
                           FormatTimecode(proj.items[0].segments[2].t1).c_str()),
          "keepRangeText reports the boundaries");
    Check(!proj.items[0].hasContiguousFullSelection(), "partial range -> no full-file fast path");

    // 先点终点再点起点也按 [min..max] 处理
    Project::ClickKeepStart(proj.items[0], nseg - 1);
    Check(proj.items[0].selectedSegmentCount() == nseg - 2, "reversed anchors normalise to min..max");

    // 双击 = 只保留这一段
    Project::SelectOnlySegment(proj.items[0], 2);
    Check(proj.items[0].selectedSegmentCount() == 1, "double click keeps one segment only");

    // Ctrl+左键：区间内 = 单独删除，区间外 = 单独保留
    Project::ClickKeepStart(proj.items[0], 0);
    Project::ClickKeepEnd(proj.items[0], 3);
    int before = proj.items[0].selectedSegmentCount();
    Project::ToggleKeepSegment(proj.items[0], 2);
    Check(proj.items[0].selectedSegmentCount() == before - 1, "ctrl+click drops a segment inside the range");
    Project::ToggleKeepSegment(proj.items[0], 2);
    Check(proj.items[0].selectedSegmentCount() == before, "ctrl+click again restores it");

    Project::ClearSelection(proj.items[0]);
    Check(proj.items[0].selectedSegmentCount() == 0, "clear selection keeps nothing");
    Project::ToggleKeepSegment(proj.items[0], 1);
    Check(proj.items[0].selectedSegmentCount() == 1, "ctrl+click keeps one segment outside the range");
    Project::ToggleKeepSegment(proj.items[0], 1);
    Check(proj.items[0].selectedSegmentCount() == 0, "ctrl+click again cancels it");

    // 保留主体 = 首末非黑屏段之间
    Project::SelectBody(proj.items[0]);
    Check(proj.items[0].keepStart == 0 && proj.items[0].keepEnd == nseg - 1,
          "SelectBody spans first..last non-black segment");
    Check(proj.items[0].selectedSegmentCount() == nseg, "SelectBody keeps every segment of this file");

    // 重新检测（keepSelection = true）必须保留用户已经选好的区间
    Project::ClickKeepStart(proj.items[0], 1);
    Project::ClickKeepEnd(proj.items[0], 2);
    Project::RebuildSegments(proj.items[0], true);
    Check(proj.items[0].keepStart == 1 && proj.items[0].keepEnd == 2, "re-detect keeps the keep range");
    Check(proj.items[0].selectedSegmentCount() == 2, "re-detect keeps the same segments");

    Check(Nearly(proj.SelectedDuration(), proj.items[0].selectedDuration(), 1e-9),
          "project selected duration");

    // -----------------------------------------------------------------------
    // 6b. 列表里的“选择起始时间 / 选择结束时间”两列 + 检测跳过已检测视频
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[6b] keep time columns / pending detection\n");
    Project::ClickKeepStart(proj.items[0], 1);
    Project::ClickKeepEnd(proj.items[0], 2);
    Check(Nearly(proj.items[0].keepStartTime(), proj.items[0].segments[1].t0, 1e-9),
          "keepStartTime == start of the first kept segment");
    Check(Nearly(proj.items[0].keepEndTime(), proj.items[0].segments[2].t1, 1e-9),
          "keepEndTime == end of the last kept segment");

    // 反向点选（先终点后起点）也要给出正确的起止时间
    Project::ClickKeepEnd(proj.items[0], 0);
    Project::ClickKeepStart(proj.items[0], 2);
    Check(Nearly(proj.items[0].keepStartTime(), proj.items[0].segments[0].t0, 1e-9),
          "reversed anchors still report the earlier start");
    Check(Nearly(proj.items[0].keepEndTime(), proj.items[0].segments[2].t1, 1e-9),
          "reversed anchors still report the later end");

    Project::ClearSelection(proj.items[0]);
    Check(proj.items[0].keepStartTime() < 0.0 && proj.items[0].keepEndTime() < 0.0,
          "cleared selection reports no start/end time");

    // 检测黑屏：默认只处理还没分析过的视频（新增/移除文件后不用全部重算）
    Project proj2;
    Check(Project::PendingDetect(proj2, false).empty(), "empty project -> nothing to detect");
    proj2.AddFile(media[0]);
    proj2.AddFile(media.size() > 1 ? media[1] : media[0]);
    if (proj2.items.size() > 1)
    {
        proj2.items[1].status = ItemStatus::Ready;            // 第二个已经检测过
        std::vector<int> pend = Project::PendingDetect(proj2, false);
        Check(pend.size() == 1 && pend[0] == 0,
              "detection skips the already analysed video");
        Check(Project::PendingDetect(proj2, true).size() == proj2.items.size(),
              "force re-detect covers every video");

        proj2.items[0].status = ItemStatus::Ready;            // 现在全部检测过了
        Check(Project::PendingDetect(proj2, false).empty(),
              "nothing pending once every video is analysed");
    }
    for (size_t i = 0; i < proj2.items.size(); ++i)
        proj2.items[i].status = ItemStatus::Ready;
    proj2.AddFile(media[0]);                                // 重复文件会被拒绝
    Check(proj2.items.size() == 1 || Project::PendingDetect(proj2, false).empty(),
          "adding an already analysed file again is a no-op");

    // -----------------------------------------------------------------------
    // 7. thumbnail mosaic
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[7] thumbnail mosaic\n");
    std::wstring mosaic = PathCombine(outDir, L"mosaic.bmp");
    ::DeleteFileW(mosaic.c_str());
    bool mosOk = ff.MakeMosaic(media[0], 0.0, (std::min)(20.0, info.duration),
                               4, 2, 160, 90, mosaic, CancelToken(), err);
    Check(mosOk, "MakeMosaic succeeded", Utf8ToWide(err));
    long long mosSize = FileSizeBytes(mosaic);
    Check(mosSize == 54 + (long long)640 * 180 * 3, "mosaic size matches 4x2 of 160x90",
          FormatString(L"got %lld bytes", mosSize));

    // -----------------------------------------------------------------------
    // 8. lossless trim + concat (what the exporter does)
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[8] lossless export path\n");
    // 按新交互选区：左键起点=第 1 段，右键终点=第 3 段（0 - 14s，中间含一段黑屏）
    Project::ClickKeepStart(proj.items[0], 0);
    Project::ClickKeepEnd(proj.items[0], 2);
    const double selDur = proj.items[0].selectedDuration();
    const int    selCount = proj.items[0].selectedSegmentCount();
    ::wprintf(L"       keep %s in %d segments\n",
              FormatTimecode(selDur).c_str(), selCount);
    Check(selCount == 3, "range selection covers 3 segments");
    Check(Nearly(selDur, 14.0, 0.05), "range selection covers 0 - 14s");

    // 连续片段必须合并成一次裁切（否则拼接后播放会闪烁）
    std::vector<KeepRun> runs = proj.items[0].selectedRuns();
    ::wprintf(L"       %d segments -> %d continuous run(s)\n", selCount, (int)runs.size());
    Check(runs.size() == 1, "contiguous segments merge into a single run");
    Check(Nearly(runs[0].t0, 0.0, 0.001) && Nearly(runs[0].t1, 14.0, 0.05),
          "the run spans from the first to the last selected segment");

    // 中间挖掉一段后必须变成两段
    Project::ToggleKeepSegment(proj.items[0], 1);          // 黑屏段单独删除
    std::vector<KeepRun> twoRuns = proj.items[0].selectedRuns();
    Check(twoRuns.size() == 2, "a dropped segment splits the run in two");
    Check(Nearly(twoRuns[0].t1, 6.0, 0.05) && Nearly(twoRuns[1].t0, 8.0, 0.05),
          "the two runs end/start at the gap");
    Project::ToggleKeepSegment(proj.items[0], 1);          // 恢复
    Check(proj.items[0].selectedRuns().size() == 1, "re-adding the segment merges them again");

    EncodeOptions enc;   // reencode == false  =>  -c copy
    std::vector<std::wstring> parts;
    int segNo = 0;
    for (size_t i = 0; i < runs.size(); ++i)
    {
        const KeepRun& s = runs[i];
        ++segNo;
        std::wstring part = PathCombine(outDir, FormatString(L"part_%02d.mp4", segNo));
        ::DeleteFileW(part.c_str());
        if (!ff.Trim(media[0], s.t0, s.t1, part, enc, err))
        {
            Check(false, "Trim failed", Utf8ToWide(err));
            break;
        }
        parts.push_back(part);
    }
    Check((int)parts.size() == 1, "one trimmed file per continuous run");

    std::wstring single = PathCombine(outDir, L"export_single.mp4");
    ::DeleteFileW(single.c_str());
    bool catOk = false;
    if (parts.size() == 1)
        catOk = ::CopyFileW(parts[0].c_str(), single.c_str(), FALSE) != 0;
    else
        catOk = ff.Concat(parts, PathCombine(outDir, L"list_single.txt"), single, enc, err);
    Check(catOk, "concat / copy produced a file", Utf8ToWide(err));

    VideoInfo outInfo;
    if (ff.Probe(single, outInfo, err))
    {
        ::wprintf(L"       exported duration %.2fs (expected ~%.2fs)\n",
                  outInfo.duration, selDur);
        Check(Nearly(outInfo.duration, selDur, 1.2), "exported duration matches selection");
    }
    else
    {
        Check(false, "probe exported file", Utf8ToWide(err));
    }

    // The exported file must start on a keyframe, otherwise players show no
    // picture (audio only) until the next one.
    {
        double kf = 0.0;
        bool kfOk = ff.KeyframeTimeBefore(media[0], 9.0, kf);
        Check(kfOk, "KeyframeTimeBefore found a keyframe",
              FormatString(L"t=%.3f", kf));
        Check(kf <= 9.0 + 0.001, "keyframe is not after the requested time",
              FormatString(L"got %.3f", kf));

        // first packet of the exported part has to be a key frame
        std::wstring probeArgs =
            L"-v error -select_streams v:0 -show_entries packet=flags"
            L" -read_intervals %+1 -of csv=p=0 " + QuoteArg(single);
        ProcessResult pr;
        bool firstIsKey = false;
        if (RunProcessCapture(ff.paths().ffprobe, probeArgs, std::wstring(),
                              CancelToken(), pr))
        {
            std::vector<std::string> pls = SplitOutputLines(pr.output);
            for (size_t i = 0; i < pls.size(); ++i)
            {
                if (pls[i].find('K') != std::string::npos) { firstIsKey = true; break; }
                if (!pls[i].empty()) break;      // first packet only
            }
        }
        Check(firstIsKey, "exported file starts on a keyframe");
    }

    // -----------------------------------------------------------------------
    // 9. merge several videos (if more than one input was given)
    // -----------------------------------------------------------------------
    if (media.size() >= 2)
    {
        ::wprintf(L"\n[9] merge %d videos\n", (int)media.size());
        double expected = 0.0;
        std::vector<std::wstring> mergeParts;

        for (size_t i = 0; i < media.size(); ++i)
        {
            VideoInfo vi;
            if (!ff.Probe(media[i], vi, err)) continue;

            VideoItem item;
            item.path = media[i];
            item.name = PathGetFileName(media[i]);
            item.info = vi;
            std::vector<BlackRange> bl;
            if (!ff.DetectBlack(media[i], vi.duration, bp, bl, nullptr, CancelToken(), err))
            {
                Check(false, "merge detect failed", Utf8ToWide(err));
                continue;
            }
            item.blacks = bl;
            item.status = ItemStatus::Ready;
            Project::RebuildSegments(item, false);
            // 同样走新交互：起点=第 2 段，终点=最后一段（丢掉片头）
            if (item.segments.size() > 1)
            {
                Project::ClickKeepStart(item, 1);
                Project::ClickKeepEnd(item, (int)item.segments.size() - 1);
            }
            expected += item.selectedDuration();

            int n = 0;
            std::vector<KeepRun> runs = item.selectedRuns();
            for (size_t k = 0; k < runs.size(); ++k)
            {
                const KeepRun& s = runs[k];
                std::wstring part = PathCombine(outDir,
                                    FormatString(L"m_%d_%02d.mp4", (int)i, ++n));
                ::DeleteFileW(part.c_str());
                if (ff.Trim(media[i], s.t0, s.t1, part, enc, err))
                    mergeParts.push_back(part);
                else
                    Check(false, "merge trim failed", Utf8ToWide(err));
            }
        }

        std::wstring merged = PathCombine(outDir, L"export_merged.mp4");
        ::DeleteFileW(merged.c_str());
        bool mOk = ff.Concat(mergeParts, PathCombine(outDir, L"list_merge.txt"),
                             merged, enc, err);
        Check(mOk, "merged concat succeeded", Utf8ToWide(err));
        if (mOk && ff.Probe(merged, outInfo, err))
        {
            ::wprintf(L"       merged duration %.2fs (expected ~%.2fs, %d parts)\n",
                      outInfo.duration, expected, (int)mergeParts.size());
            Check(Nearly(outInfo.duration, expected, 2.0), "merged duration matches selection");
        }
    }

    // -----------------------------------------------------------------------
    // 9b. lossless merge format check
    //     The GUI warns (or refuses) before merging sources whose format does
    //     not match. That comparison is pure logic, so it is asserted here
    //     without involving ffmpeg at all.
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[9b] merge format check\n");
    {
        Loc::Apply(AppLang::English);        // deterministic field labels

        VideoInfo a;                          // reference: 1080p25 AVC High + AAC LC 2.0/48k
        a.duration = 10.0;
        a.width = 1920;  a.height = 1080;
        a.fps = 25.0;    a.rFps = 25.0; a.avgFps = 25.0;
        a.vcodec = "h264"; a.profile = "High"; a.level = 41;
        a.pixFmt = "yuv420p";
        a.colorTransfer = "bt709"; a.colorPrimaries = "bt709"; a.colorSpace = "bt709";
        a.hasAudio = true; a.acodec = "aac"; a.audioProfile = "LC";
        a.sampleRate = 48000; a.channels = 2; a.channelLayout = "stereo";
        // concat 按流序号对齐，所以容器 / 时基 / 流布局也必须一致
        a.container = "mov,mp4,m4a,3gp,3g2,mj2";
        a.videoTimeBase = "1/90000"; a.audioTimeBase = "1/48000";
        a.streamLayout = "v,a";

        std::vector<FormatMismatch> diffs;

        std::vector<const VideoInfo*> one;
        one.push_back(&a);
        Check(VideoFormatsMatch(one, diffs), "a single video always matches");

        VideoInfo b = a;
        std::vector<const VideoInfo*> same;
        same.push_back(&a); same.push_back(&b);
        Check(VideoFormatsMatch(same, diffs) && diffs.empty(),
              "two identical videos match");

        // helper: mutating exactly one field has to be reported under that field
        VideoInfo o;
        std::vector<const VideoInfo*> pair;
        auto Caught = [&](const VideoInfo& other, const wchar_t* field) {
            pair.clear();
            pair.push_back(&a); pair.push_back(&other);
            if (VideoFormatsMatch(pair, diffs)) return false;
            for (size_t i = 0; i < diffs.size(); ++i)
                if (diffs[i].label == field) return true;
            return false;
        };

        o = a; o.width = 1280; o.height = 720;
        Check(Caught(o, TR(L"分辨率", L"Resolution")), "different resolution is caught");
        o = a; o.vcodec = "hevc"; o.profile = "Main"; o.level = 120;
        Check(Caught(o, TR(L"视频编码器", L"Video codec")), "different video codec is caught");
        Check(Caught(o, TR(L"编码档次与级别", L"Profile / level")),
              "different profile / level is caught");
        o = a; o.fps = 30.0; o.rFps = 30.0; o.avgFps = 30.0;
        Check(Caught(o, TR(L"帧率", L"Frame rate")), "different frame rate is caught");
        o = a; o.avgFps = 24.3;                    // same nominal fps, but VFR
        Check(Caught(o, TR(L"帧率", L"Frame rate")), "CFR vs VBR is caught");
        o = a; o.pixFmt = "yuv420p10le";
        Check(Caught(o, TR(L"像素格式", L"Pixel format")), "different pixel format is caught");
        o = a; o.colorTransfer = "smpte2084"; o.colorPrimaries = "bt2020";
        Check(Caught(o, TR(L"色彩空间", L"Colour space")), "different colour space is caught");
        o = a; o.acodec = "mp3"; o.audioProfile.clear();
        Check(Caught(o, TR(L"音频编码器", L"Audio codec")), "different audio codec is caught");
        o = a; o.hasAudio = false; o.acodec.clear(); o.audioProfile.clear();
        Check(Caught(o, TR(L"音频编码器", L"Audio codec")), "a missing audio track is caught");
        o = a; o.sampleRate = 44100;
        Check(Caught(o, TR(L"音频采样率", L"Audio sample rate")), "different sample rate is caught");
        o = a; o.channels = 6; o.channelLayout = "5.1";
        Check(Caught(o, TR(L"声道数与声道布局", L"Channels / layout")),
              "different channel count / layout is caught");
        o = a; o.channels = 2; o.channelLayout = "5.1(side)";
        Check(Caught(o, TR(L"声道数与声道布局", L"Channels / layout")),
              "same channel count but a different layout is caught");

        // ffprobe 没报出来的字段不参与这一项的比较，不能误报成不一致
        VideoInfo c = a;
        c.pixFmt.clear(); c.profile.clear(); c.level = 0;
        std::vector<const VideoInfo*> partial;
        partial.push_back(&a); partial.push_back(&c);
        Check(VideoFormatsMatch(partial, diffs),
              "a field ffprobe did not report is skipped, not a mismatch");

        // 色彩空间是例外：未标注和已标注确实不同，必须报出来
        c = a; c.colorSpace = "unknown"; c.colorTransfer = "unknown";
        c.colorPrimaries = "unknown";
        partial.clear();
        partial.push_back(&a); partial.push_back(&c);
        Check(Caught(c, TR(L"色彩空间", L"Colour space")),
              "an untagged colour space is not the same as a tagged one");

        // 两条都没标注 -> 三段都是“未标注”，不算差异
        VideoInfo d = a;
        d.colorSpace = "unknown"; d.colorTransfer = "unknown"; d.colorPrimaries = "unknown";
        partial.clear();
        partial.push_back(&c); partial.push_back(&d);
        Check(VideoFormatsMatch(partial, diffs),
              "two untagged colour spaces still match");

        // 提示文本
        o = a; o.width = 1280; o.height = 720; o.sampleRate = 44100;
        pair.clear(); pair.push_back(&a); pair.push_back(&o);
        VideoFormatsMatch(pair, diffs);
        Check(diffs.size() == 2, "two fields differ in this pair",
              FormatString(L"%d", (int)diffs.size()));
        std::wstring text = DescribeFormatMismatch(diffs);
        Check(!text.empty() && text.find(L"1920x1080") != std::wstring::npos &&
              text.find(L"1280x720") != std::wstring::npos,
              "mismatch text lists both values", text);

        // ---- 容器 / 时基 / 流布局 -------------------------------------------
        // 这三项决定 concat 能不能对上流：1/1000 (mkv) 和 1/90000 (mp4) 混拼会
        // 算出几百小时的假时长，流数不一致则整条流错位。都必须拦住。
        o = a; o.container = "matroska,webm";
        Check(Caught(o, TR(L"封装格式", L"Container")),
              "an .mp4 that is really MKV is caught as a container mismatch");
        o = a; o.videoTimeBase = "1/1000";
        Check(Caught(o, TR(L"视频时基", L"Video time base")),
              "a different video time base is caught");
        o = a; o.audioTimeBase = "1/1000";
        Check(Caught(o, TR(L"音频时基", L"Audio time base")),
              "a different audio time base is caught");
        o = a; o.streamLayout = "v,a,s";              // 多一条字幕流
        Check(Caught(o, TR(L"流布局", L"Stream layout")),
              "an extra subtitle stream is caught as a layout mismatch");

        // 时基没报出来时跳过，不能误报
        c = a; c.videoTimeBase.clear(); c.audioTimeBase.clear(); c.streamLayout.clear();
        partial.clear(); partial.push_back(&a); partial.push_back(&c);
        Check(VideoFormatsMatch(partial, diffs),
              "unreported time base / layout is skipped, not a mismatch");

        // ---- 哪些差异可以靠“转封装”修好 -------------------------------------
        // 只有容器/时基/布局差异才值得在对话框里多给一个“快速转封装”的选项；
        // 真正的编码/分辨率差异只能重编码，给了也没用。
        o = a; o.container = "matroska,webm"; o.videoTimeBase = "1/1000";
        pair.clear(); pair.push_back(&a); pair.push_back(&o);
        VideoFormatsMatch(pair, diffs);
        Check(MismatchIsRemuxFixable(diffs),
              "container / time base differences are offered as remux-fixable");

        o = a; o.width = 1280; o.height = 720;
        pair.clear(); pair.push_back(&a); pair.push_back(&o);
        VideoFormatsMatch(pair, diffs);
        Check(!MismatchIsRemuxFixable(diffs),
              "a plain resolution difference is not offered as remux-fixable");

        o = a; o.vcodec = "hevc";
        pair.clear(); pair.push_back(&a); pair.push_back(&o);
        VideoFormatsMatch(pair, diffs);
        Check(!MismatchIsRemuxFixable(diffs),
              "a plain codec difference is not offered as remux-fixable");

        // 混在一起时必须整体否掉：只要掺一个分辨率/编码差异，转封装就修不好，
        // 这时还推荐“智能合并”是在骗用户
        o = a; o.container = "matroska,webm"; o.videoTimeBase = "1/1000";
        o.width = 1280; o.height = 720;
        pair.clear(); pair.push_back(&a); pair.push_back(&o);
        VideoFormatsMatch(pair, diffs);
        Check(diffs.size() == 3, "container, time base and resolution are all reported",
              FormatString(L"%d", (int)diffs.size()));
        Check(!MismatchIsRemuxFixable(diffs),
              "a resolution difference spoils the remux option even when the container also differs");

        std::vector<FormatMismatch> none;
        Check(!MismatchIsRemuxFixable(none),
              "no difference at all is not 'remux fixable'");

        Loc::Apply(Loc::Configured());
    }

    // -----------------------------------------------------------------------
    // 9c. container label + the info line
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[9c] container label\n");
    {
        VideoInfo c;
        c.container = "matroska,webm";
        Check(c.containerLabel() == "MKV", "matroska -> MKV", Utf8ToWide(c.containerLabel()));
        c.container = "mov,mp4,m4a,3gp,3g2,mj2";
        Check(c.containerLabel() == "MP4", "mp4 family -> MP4", Utf8ToWide(c.containerLabel()));
        c.container = "avi";
        Check(c.containerLabel() == "AVI", "avi -> AVI");
        c.container = "mpegts";
        Check(c.containerLabel() == "TS", "mpegts -> TS");
        c.container = "somethingnew";
        Check(c.containerLabel() == "Somethingnew",
              "an unknown container still shows something", Utf8ToWide(c.containerLabel()));
        c.container.clear();
        Check(c.containerLabel().empty(), "no container -> empty label");

        // 时间线第一行：容器在最左，其次分辨率 / 帧率格式 / 帧率
        VideoItem it;
        it.info.duration = 10.0; it.info.width = 1920; it.info.height = 1080;
        it.info.fps = 25.0; it.info.rFps = 25.0; it.info.avgFps = 25.0;
        it.info.container = "matroska,webm";
        std::wstring line = it.streamLine();
        Check(line.find(L"MKV") != std::wstring::npos &&
              line.find(L"1920x1080") != std::wstring::npos &&
              line.find(L"CFR") != std::wstring::npos &&
              line.find(L"25fps") != std::wstring::npos,
              "the info line shows MKV | 1920x1080 | CFR | 25fps", line);
        Check(line.rfind(L"MKV") < line.find(L"1920x1080"),
              "the container comes before the resolution", line);

        // 没容器就整段省略，不能留下 " | 1920x1080" 这种开头空洞
        it.info.container.clear();
        line = it.streamLine();
        Check(line.compare(0, 3, L" | ") != 0, "no leading separator", line);
        Check(line.find(L"1920x1080") != std::wstring::npos, "resolution still shown", line);
    }

    // -----------------------------------------------------------------------
    // 10. long GOP: cutting where there is no keyframe loses the first seconds
    //     of picture (audio only) - the cut start has to move to a keyframe
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[10] long GOP cut start\n");
    {
        std::wstring gop = PathCombine(outDir, L"gop10.mp4");
        ::DeleteFileW(gop.c_str());

        // 2 s black + 10 s picture, one keyframe every 10 s (g=250 @ 25 fps)
        std::wstring mk = L"-y -hide_banner -loglevel error ";
        mk += L"-f lavfi -i color=c=black:s=320x180:r=25:d=2 ";
        mk += L"-f lavfi -i testsrc=s=320x180:r=25:d=10 ";
        mk += L"-filter_complex [0:v][1:v]concat=n=2:v=1[out] ";
        mk += L"-map [out] -c:v libx264 -preset ultrafast -crf 30 -g 250 -keyint_min 250 ";
        mk += L"-sc_threshold 0 -pix_fmt yuv420p ";
        mk += QuoteArg(gop);

        ProcessResult pr;
        bool made = RunProcessCapture(ff.paths().ffmpeg, mk, std::wstring(),
                                      CancelToken(), pr);
        Check(made && pr.exitCode == 0 && FileExists(gop), "built a 10 s GOP test clip",
              Utf8ToWide(pr.output));

        if (FileExists(gop))
        {
            VideoInfo gi;
            BlackParams gp;
            std::vector<BlackRange> gb;
            bool okInfo = ff.Probe(gop, gi, err);
            bool okDet  = ff.DetectBlack(gop, gi.duration, gp, gb, nullptr,
                                         CancelToken(), err);
            Check(okInfo && okDet, "probed the long GOP clip", Utf8ToWide(err));

            VideoItem item;
            item.path = gop;
            item.name = L"gop10.mp4";
            item.info = gi;
            item.blacks = gb;
            item.status = ItemStatus::Ready;
            Project::RebuildSegments(item, false);

            // user keeps everything except the 2 s black head
            int firstMedia = -1;
            for (size_t i = 0; i < item.segments.size(); ++i)
                if (item.segments[i].kind == SegKind::Media) { firstMedia = (int)i; break; }
            Project::ClickKeepStart(item, firstMedia);
            Project::ClickKeepEnd(item, (int)item.segments.size() - 1);

            double wanted = item.segments[(size_t)firstMedia].t0;
            std::string notes;
            std::vector<KeepRun> cut = Project::BuildCopyRuns(ff, item, &notes);
            ::wprintf(L"       keep starts at %.3fs, cut starts at %.3fs\n",
                      wanted, cut.empty() ? 0.0 : cut[0].t0);
            Check(!cut.empty(), "cut plan created");
            Check(!notes.empty(), "the adjustment is reported to the user");

            // the cut must not start later than the request and has to be on a
            // keyframe, otherwise the file opens with audio but no picture
            Check(cut[0].t0 <= wanted + 0.001, "cut start was not moved forward");
            double kf = 0.0;
            bool onKey = ff.KeyframeTimeBefore(gop, cut[0].t0 + 0.001, kf) &&
                         Nearly(kf, cut[0].t0, 0.05);
            Check(onKey, "cut start sits on a keyframe",
                  FormatString(L"cut=%.3f kf=%.3f", cut[0].t0, kf));

            // and the produced file really starts with a key frame
            std::wstring outFile = PathCombine(outDir, L"gop_cut.mp4");
            ::DeleteFileW(outFile.c_str());
            EncodeOptions copyEnc;                 // -c copy
            if (ff.Trim(gop, cut[0].t0, cut[0].t1, outFile, copyEnc, err))
            {
                ProcessResult kr;
                bool firstIsKey = false;
                std::wstring args =
                    L"-v error -select_streams v:0 -show_entries packet=flags"
                    L" -read_intervals %+1 -of csv=p=0 " + QuoteArg(outFile);
                if (RunProcessCapture(ff.paths().ffprobe, args, std::wstring(),
                                      CancelToken(), kr))
                {
                    std::vector<std::string> l = SplitOutputLines(kr.output);
                    for (size_t i = 0; i < l.size(); ++i)
                    {
                        if (l[i].find('K') != std::string::npos) { firstIsKey = true; break; }
                        if (!l[i].empty()) break;
                    }
                }
                Check(firstIsKey, "exported long GOP clip starts on a keyframe");

                // The video track must start right away - otherwise the file
                // plays seconds of audio with no picture at the beginning.
                VideoInfo oi;
                if (ff.Probe(outFile, oi, err))
                {
                    ::wprintf(L"       stream starts: v=%.3fs a=%.3fs\n",
                               oi.videoStartTime, oi.audioStartTime);
                    Check(oi.videoStartTime < 0.6,
                          "exported video starts immediately (no audio-only head)",
                          FormatString(L"video starts at %.3fs", oi.videoStartTime));
                }
            }
            else
            {
                Check(false, "trim of long GOP clip failed", Utf8ToWide(err));
            }
        }
    }


    // -----------------------------------------------------------------------
    // 11. head/tail window scan: long files only decode the first/last N
    //     seconds, short files are scanned completely
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[11] head/tail scan window\n");
    {
        // 70 s clip (10 s GOP): black at the start (0-2), middle (30-32), end (60-70)
        std::wstring clip = PathCombine(outDir, L"gop_long.mp4");
        ::DeleteFileW(clip.c_str());

        std::wstring mk = L"-y -hide_banner -loglevel error ";
        mk += L"-f lavfi -i color=c=black:s=320x180:r=25:d=2 ";
        mk += L"-f lavfi -i testsrc=s=320x180:r=25:d=28 ";
        mk += L"-f lavfi -i color=c=black:s=320x180:r=25:d=2 ";
        mk += L"-f lavfi -i testsrc=s=320x180:r=25:d=28 ";
        mk += L"-f lavfi -i color=c=black:s=320x180:r=25:d=10 ";
        mk += L"-filter_complex [0:v][1:v][2:v][3:v][4:v]concat=n=5:v=1[out] ";
        mk += L"-map [out] -c:v libx264 -preset ultrafast -crf 32 -g 250 -keyint_min 250 ";
        mk += L"-sc_threshold 0 -pix_fmt yuv420p ";
        mk += QuoteArg(clip);

        ProcessResult pr;
        bool made = RunProcessCapture(ff.paths().ffmpeg, mk, std::wstring(),
                                      CancelToken(), pr);
        Check(made && pr.exitCode == 0 && FileExists(clip),
              "built a 70 s long GOP clip", Utf8ToWide(pr.output));

        if (FileExists(clip))
        {
            VideoInfo ci;
            Check(ff.Probe(clip, ci, err), "probed the clip", Utf8ToWide(err));

            std::vector<BlackRange> all, edges, shortAll;

            // (a) -1 / -1 -> no limit on either side -> whole file, all three black parts
            BlackParams pFull;
            pFull.headScanSec = -1.0;
            pFull.tailScanSec = -1.0;
            Check(ff.DetectBlack(clip, ci.duration, pFull, all, nullptr,
                                 CancelToken(), err), "full scan", Utf8ToWide(err));
            ::wprintf(L"       whole file: %d range(s)\n", (int)all.size());
            Check(all.size() == 3, "full scan finds head, middle and tail black");

            // (b) 10 s windows -> head and tail, but not the middle one
            BlackParams pEdge;
            pEdge.headScanSec = 10.0;
            pEdge.tailScanSec = 10.0;
            Check(ff.DetectBlack(clip, ci.duration, pEdge, edges, nullptr,
                                 CancelToken(), err), "head/tail scan", Utf8ToWide(err));
            ::wprintf(L"       10s windows: %d range(s)\n", (int)edges.size());

            bool hasHead = false, hasTail = false, hasMiddle = false;
            for (size_t i = 0; i < edges.size(); ++i)
            {
                if (edges[i].start < 1.0) hasHead = true;
                if (edges[i].end > ci.duration - 11.0) hasTail = true;
                if (edges[i].start > 25.0 && edges[i].start < 40.0) hasMiddle = true;
            }
            Check(hasHead, "window scan finds the intro black");
            Check(hasTail, "window scan finds the outro black");
            Check(!hasMiddle, "window scan skips the middle (as designed)");

            // (b2) head and tail are configured separately: a wide head window
            //      reaches the middle black while a narrow tail window still
            //      covers the outro - the two sides are truly independent
            BlackParams pSplit;
            pSplit.headScanSec = 40.0;
            pSplit.tailScanSec = 2.0;
            std::vector<BlackRange> split;
            Check(ff.DetectBlack(clip, ci.duration, pSplit, split, nullptr,
                                 CancelToken(), err), "split head/tail scan", Utf8ToWide(err));
            bool splitMiddle = false, splitTail = false;
            for (size_t i = 0; i < split.size(); ++i)
            {
                if (split[i].start > 25.0 && split[i].start < 40.0) splitMiddle = true;
                if (split[i].end > ci.duration - 11.0) splitTail = true;
            }
            ::wprintf(L"       head 40s / tail 2s: %d range(s)\n", (int)split.size());
            Check(splitMiddle, "wide head window reaches the middle black");
            Check(splitTail, "narrow tail window still covers the outro");

            // the mirror image: narrow head + wide tail must NOT reach the middle
            BlackParams pSplit2;
            pSplit2.headScanSec = 3.0;
            pSplit2.tailScanSec = 20.0;
            std::vector<BlackRange> split2;
            Check(ff.DetectBlack(clip, ci.duration, pSplit2, split2, nullptr,
                                 CancelToken(), err), "split head/tail scan (reversed)",
                  Utf8ToWide(err));
            bool split2Middle = false;
            for (size_t i = 0; i < split2.size(); ++i)
                if (split2[i].start > 25.0 && split2[i].start < 40.0) split2Middle = true;
            ::wprintf(L"       head 3s / tail 20s: %d range(s)\n", (int)split2.size());
            Check(!split2Middle, "narrow head window never reaches the middle black");

            // (b3) 0 = that side is not scanned at all: head only
            BlackParams pHeadOnly;
            pHeadOnly.headScanSec = 3.0;
            pHeadOnly.tailScanSec = 0.0;       // 0 = the tail is not scanned
            std::vector<BlackRange> headOnly;
            Check(ff.DetectBlack(clip, ci.duration, pHeadOnly, headOnly, nullptr,
                                 CancelToken(), err), "head-only scan", Utf8ToWide(err));
            ::wprintf(L"       head 3s / tail 0: %d range(s)\n", (int)headOnly.size());
            bool headOnlyHasIntro = false, headOnlyReachedFar = false;
            for (size_t i = 0; i < headOnly.size(); ++i)
            {
                if (headOnly[i].start < 1.0) headOnlyHasIntro = true;
                if (headOnly[i].end > 10.0) headOnlyReachedFar = true;
            }
            Check(headOnlyHasIntro, "head-only scan finds the intro black");
            Check(!headOnlyReachedFar, "tail = 0 means the outro is never scanned");

            // (b4) the mirror image: head = 0 scans the tail side only
            BlackParams pTailOnly;
            pTailOnly.headScanSec = 0.0;
            pTailOnly.tailScanSec = 20.0;
            std::vector<BlackRange> tailOnly;
            Check(ff.DetectBlack(clip, ci.duration, pTailOnly, tailOnly, nullptr,
                                 CancelToken(), err), "tail-only scan", Utf8ToWide(err));
            ::wprintf(L"       head 0 / tail 20s: %d range(s)\n", (int)tailOnly.size());
            bool tailOnlyHasOutro = false, tailOnlyReachedStart = false;
            for (size_t i = 0; i < tailOnly.size(); ++i)
            {
                if (tailOnly[i].end > ci.duration - 11.0) tailOnlyHasOutro = true;
                if (tailOnly[i].start < 10.0) tailOnlyReachedStart = true;
            }
            Check(tailOnlyHasOutro, "tail-only scan finds the outro black");
            Check(!tailOnlyReachedStart, "head = 0 means the intro is never scanned");

            // (b5) both sides 0 -> there is nothing left to scan: fail loudly
            // instead of silently reporting "0 black segments"
            BlackParams pNone;
            pNone.headScanSec = 0.0;
            pNone.tailScanSec = 0.0;
            std::vector<BlackRange> none;
            std::string noneErr;
            Check(!ff.DetectBlack(clip, ci.duration, pNone, none, nullptr,
                                  CancelToken(), noneErr) && none.empty(),
                  "head 0 + tail 0 is refused", Utf8ToWide(noneErr));

            // (c) every reported range must belong to the full scan
            bool subset = true;
            for (size_t i = 0; i < edges.size() && subset; ++i)
            {
                bool found = false;
                for (size_t j = 0; j < all.size(); ++j)
                {
                    if (Nearly(edges[i].start, all[j].start, 0.6) &&
                        Nearly(edges[i].end, all[j].end, 0.6))
                    {
                        found = true;
                        break;
                    }
                }
                if (!found) subset = false;
            }
            Check(subset, "window ranges are a subset of the full scan");

            // (d) a clip shorter than head+tail is scanned completely
            BlackParams pBig;
            pBig.headScanSec = 1000.0;     // longer than the file
            pBig.tailScanSec = 1000.0;
            Check(ff.DetectBlack(clip, ci.duration, pBig, shortAll, nullptr,
                                 CancelToken(), err), "short clip scan", Utf8ToWide(err));
            Check(shortAll.size() == all.size(),
                  "clip shorter than 2x the window is scanned completely");
        }
    }

    // -----------------------------------------------------------------------
    // 12. embedded cover art (mp4 attached_pic) must not leak into the output
    // -----------------------------------------------------------------------
    ::wprintf(L"\n[12] embedded cover art\n");
    {
        // Build a clip carrying an extra mjpeg stream marked attached_pic. It has
        // to go in as an attachment rather than a mapped stream, otherwise the
        // cover takes over the container duration.
        std::wstring jpg = PathCombine(outDir, L"cover.jpg");
        std::wstring src = PathCombine(outDir, L"cover_src.mp4");
        std::wstring outT = PathCombine(outDir, L"cover_trim.mp4");
        std::wstring outC = PathCombine(outDir, L"cover_merge.mp4");
        std::wstring outN = PathCombine(outDir, L"cover_norm.mp4");
        std::wstring lst  = PathCombine(outDir, L"cover_list.txt");
        ::DeleteFileW(jpg.c_str());
        ::DeleteFileW(src.c_str());
        ::DeleteFileW(outT.c_str());
        ::DeleteFileW(outC.c_str());
        ::DeleteFileW(outN.c_str());

        // Counts streams flagged attached_pic; 1 means "this file has a cover".
        struct CoverCounter
        {
            static int Count(const Ffmpeg& ffm, const std::wstring& file)
            {
                if (!FileExists(file)) return -1;
                std::wstring args = L"-v error -show_entries ";
                args += L"stream=index:stream_disposition=attached_pic -of default=nw=1 ";
                args += QuoteArg(file);
                ProcessResult pr;
                if (!RunProcessCapture(ffm.paths().ffprobe, args, std::wstring(),
                                       CancelToken(), pr) || pr.exitCode != 0)
                    return -1;
                int n = 0;
                const std::string& o = pr.output;
                size_t pos = 0;
                while ((pos = o.find("attached_pic=1", pos)) != std::string::npos)
                {
                    ++n;
                    pos += 14;
                }
                return n;
            }
        };

        ProcessResult pr;

        std::wstring jpgCmd = L"-y -hide_banner -loglevel error ";
        jpgCmd += L"-f lavfi -i color=c=red:s=240x240:d=1:r=1 -frames:v 1 -update 1 ";
        jpgCmd += QuoteArg(jpg);
        RunProcessCapture(ff.paths().ffmpeg, jpgCmd, std::wstring(), CancelToken(), pr);
        Check(pr.exitCode == 0 && FileExists(jpg), "built the cover image",
              Utf8ToWide(pr.output));

        std::wstring addCmd = L"-y -hide_banner -loglevel error -i ";
        addCmd += QuoteArg(media[0]) + L" -i " + QuoteArg(jpg);
        addCmd += L" -map 0 -map 1 -c copy -c:v:1 mjpeg -disposition:v:1 attached_pic ";
        addCmd += QuoteArg(src);
        RunProcessCapture(ff.paths().ffmpeg, addCmd, std::wstring(), CancelToken(), pr);

        VideoInfo ci;
        Check(FileExists(src) && ff.Probe(src, ci, err), "built a clip with a cover",
              Utf8ToWide(pr.output) + Utf8ToWide(err));
        Check(CoverCounter::Count(ff, src) == 1,
              "source really carries one cover stream");

        EncodeOptions cEnc;                       // lossless stream copy

        Check(ff.Trim(src, 0.0, 4.0, outT, cEnc, err), "trim of a covered clip",
              Utf8ToWide(err));
        Check(FileExists(outT) && CoverCounter::Count(ff, outT) == 0,
              "trim drops the embedded cover");

        std::vector<std::wstring> parts;
        parts.push_back(src);
        Check(ff.Concat(parts, lst, outC, cEnc, err), "concat of covered clips",
              Utf8ToWide(err));
        Check(FileExists(outC) && CoverCounter::Count(ff, outC) == 0,
              "concat drops the embedded cover");

        EncodeOptions nEnc;
        nEnc.reencode = true;
        nEnc.preset   = "ultrafast";
        Check(ff.Normalize(src, outN, 320, 180, 25.0, true, nEnc, err),
              "normalize of a covered clip", Utf8ToWide(err));
        Check(FileExists(outN) && CoverCounter::Count(ff, outN) == 0,
              "normalize drops the embedded cover");

        VideoInfo oi;
        if (ff.Probe(outT, oi, err))
            Check(Nearly(oi.duration, 4.0, 0.6), "trim output keeps the cut duration",
                  FormatString(L"%.2fs", oi.duration).c_str());
    }

    // -----------------------------------------------------------------------
    ::wprintf(L"\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 2;
}
