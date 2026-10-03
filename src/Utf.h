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
// Utf.h - string / path / time helper utilities for FastVideoCut
// ---------------------------------------------------------------------------
#pragma once

// This application is Unicode only - make sure the SDK headers agree even when
// the compiler is invoked without -DUNICODE (e.g. a bare g++ command line).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>
#include <vector>

// ------------------------------- conversions -------------------------------
std::string  WideToUtf8(const std::wstring& w);
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToAnsi(const std::wstring& w);
std::wstring AnsiToWide(const std::string& s);

// printf style formatting into a std::wstring (MSVC / MinGW compatible)
std::wstring FormatString(const wchar_t* fmt, ...);

// ------------------------------- command line ------------------------------
// Quote a single argument following the MSVCRT / CommandLineToArgvW rules.
std::wstring QuoteArg(const std::wstring& arg);
std::wstring JoinArgs(const std::vector<std::wstring>& args);
// Escape a path so it can be written inside an ffmpeg concat demuxer list.
std::wstring EscapeConcatPath(const std::wstring& path);

// ------------------------------- file helpers ------------------------------
bool         FileExists(const std::wstring& path);
bool         DirectoryExists(const std::wstring& path);
long long    FileSizeBytes(const std::wstring& path);
long long    FileModifiedTime(const std::wstring& path);
std::wstring GetExePath();
std::wstring GetExeDir();
std::wstring PathCombine(const std::wstring& dir, const std::wstring& leaf);
std::wstring PathGetDirectory(const std::wstring& path);
std::wstring PathGetFileName(const std::wstring& path);
std::wstring PathGetFileNameNoExt(const std::wstring& path);
std::wstring PathGetExtension(const std::wstring& path);        // ".mp4", lower case
std::wstring PathGetFull(const std::wstring& path);
std::wstring ToLowerW(const std::wstring& s);
bool         EnsureDirectory(const std::wstring& path);
bool         DeleteDirectoryRecursive(const std::wstring& path);
std::wstring GetLocalAppDataDir();
std::vector<std::wstring> ListFilesByExt(const std::wstring& dir,
                                         const std::vector<std::wstring>& exts);

// --------------------------------- time -----------------------------------
std::wstring FormatTimecode(double seconds);                    // HH:MM:SS.mmm
std::wstring FormatClock(double seconds);                       // HH:MM:SS
std::string  FormatSecondsUtf8(double seconds, int decimals = 3);
bool         ParseTimecode(const std::wstring& text, double& out);
std::wstring NumberText(double value, int decimals = 3);

// --------------------------- merge output name -------------------------------
// Builds the merged file name from the first and last video: a shared prefix is
// written once and the two remainders are joined with '-'.
//   001 + 010       -> 001-010.mp4
//   视频001 + 视频010 -> 视频001-010.mp4
//   第1集 + 第2集     -> 第1集-第2集.mp4      (nothing in common)
// Extensions on the inputs are ignored; `ext` is appended to the result.
std::wstring MakeMergeName(const std::wstring& firstName,
                           const std::wstring& lastName,
                           const std::wstring& ext = L".mp4");

template <class T>
inline T ClampValue(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }