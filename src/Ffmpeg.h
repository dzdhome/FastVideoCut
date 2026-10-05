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
// Ffmpeg.h - thin C++ facade over the ffmpeg / ffprobe command line tools
// ---------------------------------------------------------------------------
#pragma once

#include "Utf.h"
#include "Process.h"

#include <string>
#include <vector>
#include <functional>
#include <utility>

// Basic properties of one media file (probed with ffprobe).
struct VideoInfo
{
    double      duration   = 0.0;
    int         width      = 0;
    int         height     = 0;
    double      fps        = 0.0;
    int         nbFrames   = 0;
    long long   sizeBytes  = 0;
    std::string vcodec;
    std::string acodec;
    std::string container;
    int         sampleRate = 0;
    int         channels   = 0;
    bool        hasAudio   = false;
    double      videoStartTime = 0.0;   // start_time of the video track
    double      audioStartTime = 0.0;   // start_time of the audio track

    // colour / bit depth (shown in the list as "HDR · 10bit")
    std::string pixFmt;                   // e.g. yuv420p10le
    std::string colorTransfer;            // bt709 / smpte2084 (PQ) / arib-std-b67 (HLG)
    std::string colorPrimaries;           // bt709 / bt2020
    std::string colorSpace;               // bt709 / bt2020nc ...
    int         bitsPerRawSample = 0;     // as reported by ffprobe (0 = unknown)

    // ---- 编码规格 -------------------------------------------------------
    // 列表/时间线上要显示的“这一条片子到底是什么编码”，也是无损合并前
    // 逐项比对的内容，所以原始字段全部保留，显示名交给下面的 label 函数。
    std::string profile;                  // 视频档次: High / Main / Main 10 ...
    int         level      = 0;           // 原始 level 值 (h264: 41 -> L4.1, hevc: 120 -> L4.0)
    std::string audioProfile;             // 音频档次: LC / HE-AAC / ...
    std::string channelLayout;            // stereo / 5.1 / mono ...
    double      rFps        = 0.0;        // r_frame_rate（基准帧率）
    double      avgFps      = 0.0;        // avg_frame_rate（实际平均帧率）
    // 时基（time_base），如 "1/1000" / "1/90000"。concat 解复用器按流序号对齐，
    // 时基不一致时它会把两种单位的 PTS 混算，产出几百小时的假时长 —— 这是
    // “扩展名是 .mp4、实际是 mkv”这类文件合并后播不完的真正原因。
    std::string videoTimeBase;            // 视频流时基
    std::string audioTimeBase;            // 音频流时基
    // 流布局签名，如 "v,a,s"（按流序号）。concat 同样按序号对齐，多一条字幕
    // 就会让后面所有流整体错位，所以也要参与合并前的比对。
    std::string streamLayout;

    bool valid() const { return duration > 0.0 && width > 0 && height > 0; }

    // Bit depth per channel. Falls back to the pixel format name when
    // bits_per_raw_sample is missing (which is the usual case for HEVC).
    int  bitDepth() const;
    // True for HDR content: PQ (smpte2084) or HLG (arib-std-b67).
    bool isHdr() const;
    std::string hdrLabel() const;         // "HDR10" / "HLG" / "SDR"
    std::string bitDepthLabel() const;    // "10bit" / "8bit" / ""
    std::string formatLabel() const;      // "HDR · 10bit"

    // ---- 显示用标签（一律 UTF-8；ffprobe 没报到的字段返回空串）----------
    std::string resolutionLabel() const;  // "1920x1080"
    std::string videoCodecLabel() const;  // "AVC" / "HEVC" / "AV1" / "VP9" / "MPEG-4"
    std::string profileLevelLabel() const;// "Main@L4" / "High@L4.1"
    std::string pixFmtLabel() const;      // "yuv420p" / "yuv420p10le"
    std::string fpsModeLabel() const;     // "CFR" / "VBR"
    std::string fpsLabel() const;         // "25fps" / "23.976fps"
    std::string audioCodecLabel() const;  // "AAC LC" / "MP3" / "FLAC"
    std::string channelLabel() const;     // "2.0" / "5.1" / "7.1"
    std::string sampleRateLabel() const;  // "48K" / "44.1K"
    std::string colorSpaceLabel() const;  // "bt709" / "bt2020nc"（unknown 视为空）
    // "matroska,webm" -> "MKV"，"mov,mp4,m4a,..." -> "MP4"。
    // 用 MKV/MP4 这种一眼能懂的简写，而不是 Matroska/MPEG-4：文件名列表里
    // 大家认的就是扩展名，一看就知道能不能合。
    std::string containerLabel() const;   // "MKV" / "MP4" / "AVI" / "TS"
};

// --------------------- label helpers (unit testable) ----------------------
// "h264" -> "AVC"，"hevc" -> "HEVC"，未知的编码名原样转成大写
std::string VideoCodecDisplayName(const std::string& codec);
// "aac" + "LC" -> "AAC LC"；"mp3" -> "MP3"
std::string AudioCodecDisplayName(const std::string& codec, const std::string& profile);
// level 值按编码换算成 "L4.1" / "L4"（h264 除 10，hevc 除 30，av1 用 seq_level_idx）
std::string LevelDisplay(const std::string& codec, int level);
// "High@L4.1"；档次或 level 缺失时只显示已有的一半
std::string ProfileLevelDisplay(const std::string& codec, const std::string& profile, int level);
// "stereo" -> "2.0"，"5.1" -> "5.1"；没有布局时按声道数推
std::string ChannelLayoutDisplay(const std::string& layout, int channels);
// 44100 -> "44.1K"，48000 -> "48K"，22050 -> "22.05K"
std::string SampleRateDisplay(int sampleRate);
// 25.0 -> "25fps"，23.976 -> "23.976fps"
std::string FpsDisplay(double fps);
// r_frame_rate 与 avg_frame_rate 一致 -> "CFR"，否则 "VBR"（可变帧率）
std::string FpsModeDisplay(double rFps, double avgFps);
// Derives the bit depth from a pixel format name ("yuv420p10le" -> 10).
int BitDepthFromPixFmt(const std::string& pixFmt);

struct BlackRange
{
    double start = 0.0;
    double end   = 0.0;
    double length() const { return end > start ? end - start : 0.0; }
};

struct BlackParams
{
    double minDuration    = 0.10;   // blackdetect d=
    double pixThreshold   = 0.10;   // blackdetect pix_th=
    double picThreshold   = 0.98;   // blackdetect pic_th=
    // Only a slice of the file is scanned (the intro / outro boundaries almost
    // always sit near its ends). The two sides are configured on their own,
    // because an intro is usually much shorter than the outro, or vice versa:
    //   > 0 = only that many seconds at the head / at the tail
    //   = 0 = that side is not scanned at all (head 180 / tail 0 = first 180 s)
    //   < 0 = no limit on that side -> the whole file is scanned
    double headScanSec    = 180.0;
    double tailScanSec    = 180.0;
};

// Options for stream-copy (lossless) or re-encoded output.
struct EncodeOptions
{
    bool        reencode       = false;      // false => "-c copy" (lossless)
    int         crf            = 18;
    std::string preset         = "veryfast";
    std::string audioCodec     = "aac";
    int         audioBitrateK  = 192;
    bool        faststart      = true;
};

struct FfmpegPaths
{
    std::wstring ffmpeg;
    std::wstring ffprobe;
    std::wstring binDir;
};

typedef std::function<void(const std::wstring&)> FfmpegLogFn;

class Ffmpeg
{
public:
    // ---------------------------- discovery --------------------------------
    bool SetBinDir(const std::wstring& dir);          // validates ffmpeg+ffprobe
    bool Locate(const std::wstring& preferredDir);    // may be empty
    bool available() const { return !paths_.ffmpeg.empty() && !paths_.ffprobe.empty(); }
    const FfmpegPaths& paths() const { return paths_; }
    void setLog(const FfmpegLogFn& fn) { log_ = fn; }
    std::wstring Version() const;

    // ---------------------------- operations -------------------------------
    bool Probe(const std::wstring& file, VideoInfo& info, std::string& err,
               const CancelToken& cancel = CancelToken()) const;

        // Runs `blackdetect` over the whole file, frame by frame. This is the only
    // detection mode: scanning every frame is the one way to be sure no short
    // black part is missed.
    bool DetectBlack(const std::wstring& file, double duration,
                     const BlackParams& params,
                     std::vector<BlackRange>& out,
                     const std::function<void(double)>& onProgress,
                     const CancelToken& cancel, std::string& err) const;

    // Returns the timestamp of the last keyframe at or before `t`. Stream copy
    // can only start a file on a keyframe, otherwise the picture is missing (or
    // garbled) until the next one while the audio already plays.
    bool KeyframeTimeBefore(const std::wstring& file, double t, double& out,
                            const CancelToken& cancel = CancelToken()) const;

    // Builds a cols x rows thumbnail mosaic (one BMP file) of [t0,t1].
    bool MakeMosaic(const std::wstring& file, double t0, double t1,
                    int cols, int rows, int thumbW, int thumbH,
                    const std::wstring& outBmp,
                    const CancelToken& cancel, std::string& err) const;

    // Cuts [t0,t1] out of `in` into `out` (stream copy unless enc.reencode).
    bool Trim(const std::wstring& in, double t0, double t1, const std::wstring& out,
              const EncodeOptions& enc, std::string& err) const;

    // Concatenates `parts` (writes a concat list file) into `out`.
    bool Concat(const std::vector<std::wstring>& parts, const std::wstring& listFile,
                const std::wstring& out, const EncodeOptions& enc, std::string& err) const;

    // Re-encodes a file into a common format (used when merging mismatched sources).
    bool Normalize(const std::wstring& in, const std::wstring& out,
                   int width, int height, double fps, bool hasAudio,
                   const EncodeOptions& enc, std::string& err) const;

    // Remuxes `in` into an MP4 with a fixed timescale, without touching the codec
    // (`-c copy`). This is what makes a MKV + MP4 merge work: concat aligns streams
    // by index and interprets timestamps in each file's own time_base, so a 1/1000
    // (mkv) next to a 1/90000 (mp4) yields a bogus multi-hour duration. Pinning
    // -video_track_timescale makes every part share one time_base, which fixes it
    // while staying far faster than re-encoding.
    bool RemuxToMp4(const std::wstring& in, const std::wstring& out,
                    const CancelToken& cancel, std::string& err) const;

private:
    // One blackdetect pass over [t0, t0+dur]; timestamps stay absolute
    // (-copyts) so the ranges can be merged across windows.
    bool ScanWindow(const std::wstring& file, double t0, double dur,
                    const BlackParams& params,
                    std::vector<BlackRange>& out,
                    const std::function<void(double)>& onProgress,
                    const CancelToken& cancel, std::string& err) const;

    FfmpegPaths paths_;
    FfmpegLogFn log_;
    void Log(const std::wstring& s) const { if (log_) log_(s); }
};

// ----------------------- helpers (unit testable) ---------------------------
// Parses the ffmpeg stderr stream produced by the blackdetect filter.
bool ParseBlackDetectOutput(const std::string& text, std::vector<BlackRange>& out);
// Parses one line; returns true when at least one field was found.
bool ParseBlackDetectLine(const std::string& line, bool& haveStart, double& start,
                          bool& haveEnd, double& end, bool& haveDuration, double& duration);
// Parses "key=value" line blocks (ffprobe -of default=noprint_wrappers=1).
std::vector<std::pair<std::string, std::string> > ParseKeyValueLines(const std::string& text);
// Parses "25/1" style rationals.
double ParseRational(const std::string& s);
// Sorts, merges (gap < joinGap) and drops ranges shorter than minDuration.
void MergeBlackRanges(std::vector<BlackRange>& ranges, double joinGap, double minDuration);