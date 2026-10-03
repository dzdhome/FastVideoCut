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

    bool valid() const { return duration > 0.0 && width > 0 && height > 0; }
};

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
    // Only the head and the tail of the file are scanned (intro / outro almost
    // always end within the first/last few minutes). 0 = scan the whole file.
    double edgeScanSec    = 180.0;
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