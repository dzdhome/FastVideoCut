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
// Utf.cpp - string / path / time helper utilities
// ---------------------------------------------------------------------------
#include "Utf.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cmath>

// ---------------------------------------------------------------------------
// string conversion
// ---------------------------------------------------------------------------
std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int need = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out((size_t)need, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], need, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out((size_t)need, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], need);
    return out;
}

std::string WideToAnsi(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int need = ::WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out((size_t)need, '\0');
    ::WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &out[0], need, nullptr, nullptr);
    return out;
}

std::wstring AnsiToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int need = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (need <= 0) return std::wstring();
    std::wstring out((size_t)need, L'\0');
    ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &out[0], need);
    return out;
}

std::wstring FormatString(const wchar_t* fmt, ...)
{
    wchar_t buf[4096];
    va_list ap;
    va_start(ap, fmt);
#ifdef _MSC_VER
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
#else
    _vsnwprintf(buf, (size_t)_countof(buf) - 1, fmt, ap);
#endif
    va_end(ap);
    buf[_countof(buf) - 1] = L'\0';
    return std::wstring(buf);
}

// ---------------------------------------------------------------------------
// command line
// ---------------------------------------------------------------------------
// -----------------------------------------------------------------------
// text layout
// -----------------------------------------------------------------------
std::wstring WrapTextToWidth(HDC dc, const std::wstring& text, int availPx)
{
    if (text.empty() || availPx <= 0 || !dc) return text;

    // GetTextExtentExPointW reports how many leading characters fit within
    // availPx, which is exactly the break point we want.
    SIZE whole{};
    if (::GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &whole) &&
        whole.cx <= availPx)
    {
        return text;                       // already fits, nothing to do
    }

    std::wstring out;
    out.reserve(text.size() + text.size() / 40 + 16);

    const size_t n = text.size();
    size_t pos = 0;
    while (pos < n)
    {
        // Binary-search the break point with GetTextExtentPoint32W rather than
        // GetTextExtentExPointW: the latter returns FALSE on a window DC (it
        // works on a screen DC, which is what the unit test happens to use) and
        // then leaves lpnFit untouched, so every line degenerates to one glyph
        // per row. GetTextExtentPoint32W is reliable on both.
        size_t lo = 1, hi = n - pos, best = 1;
        while (lo <= hi)
        {
            const size_t mid = lo + (hi - lo) / 2;
            SIZE s{};
            if (::GetTextExtentPoint32W(dc, text.c_str() + pos, (int)mid, &s) && s.cx <= availPx)
            {
                best = mid;
                lo = mid + 1;
            }
            else
            {
                if (mid == 1) { best = 1; break; }     // even one glyph is too wide
                hi = mid - 1;
            }
        }

        size_t take = best;
        // Never split a surrogate pair, or the next line would start with a
        // lone low surrogate and render as tofu.
        if (take > 1 && pos + take < n && (text[pos + take - 1] & 0xFC00) == 0xD800)
            --take;

        out.append(text, pos, take);
        pos += take;
        if (pos < n) out += L"\r\n";
    }
    return out;
}

// -----------------------------------------------------------------------
// command line
// -----------------------------------------------------------------------
std::wstring QuoteArg(const std::wstring& arg)
{
    bool needQuote = arg.empty();
    for (size_t i = 0; i < arg.size() && !needQuote; ++i)
    {
        if (arg[i] == L' ' || arg[i] == L'\t' || arg[i] == L'"')
            needQuote = true;
    }
    if (!needQuote) return arg;

    std::wstring out;
    out.reserve(arg.size() + 8);
    out.push_back(L'"');
    size_t backslashes = 0;
    for (size_t i = 0; i < arg.size(); ++i)
    {
        wchar_t c = arg[i];
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"')
        {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
            continue;
        }
        if (backslashes) { out.append(backslashes, L'\\'); backslashes = 0; }
        out.push_back(c);
    }
    if (backslashes) out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring JoinArgs(const std::vector<std::wstring>& args)
{
    std::wstring out;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i) out.push_back(L' ');
        out += QuoteArg(args[i]);
    }
    return out;
}

std::wstring EscapeConcatPath(const std::wstring& path)
{
    // ffmpeg concat demuxer line: file 'path'   (a ' inside becomes '\'')
    std::wstring out = L"file '";
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == L'\'') out += L"'\\''";
        else out.push_back(path[i]);
    }
    out += L"'";
    return out;
}

// ---------------------------------------------------------------------------
// file helpers
// ---------------------------------------------------------------------------
bool FileExists(const std::wstring& path)
{
    DWORD a = ::GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(const std::wstring& path)
{
    DWORD a = ::GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

long long FileSizeBytes(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return ((long long)fad.nFileSizeHigh << 32) | (long long)fad.nFileSizeLow;
}

long long FileModifiedTime(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return ((long long)fad.ftLastWriteTime.dwHighDateTime << 32) |
           (long long)fad.ftLastWriteTime.dwLowDateTime;
}

std::wstring GetExePath()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, (DWORD)_countof(buf));
    if (n == 0) return std::wstring();
    buf[_countof(buf) - 1] = L'\0';
    return std::wstring(buf, n);
}

std::wstring GetExeDir()
{
    return PathGetDirectory(GetExePath());
}

std::wstring PathCombine(const std::wstring& dir, const std::wstring& leaf)
{
    if (dir.empty()) return leaf;
    std::wstring out = dir;
    if (out[out.size() - 1] != L'\\' && out[out.size() - 1] != L'/') out.push_back(L'\\');
    out += leaf;
    return out;
}

std::wstring PathGetDirectory(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    return path.substr(0, pos);
}

std::wstring PathGetFileName(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

std::wstring PathGetFileNameNoExt(const std::wstring& path)
{
    std::wstring name = PathGetFileName(path);
    size_t pos = name.find_last_of(L'.');
    if (pos == std::wstring::npos) return name;
    return name.substr(0, pos);
}

std::wstring PathGetExtension(const std::wstring& path)
{
    std::wstring name = PathGetFileName(path);
    size_t pos = name.find_last_of(L'.');
    if (pos == std::wstring::npos) return std::wstring();
    return ToLowerW(name.substr(pos));
}

// ---------------------------------------------------------------------------
// MakeMergeName - merged output name built from the first and last video.
//
// Both inputs may be a full path and may carry an extension; both are reduced
// to their base names first. The longest shared prefix is emitted once and the
// two remainders follow, joined with '-':
//     001       + 010      -> 001-010.mp4
//     视频001    + 视频010   -> 视频001-010.mp4
//     clip_1    + clip_2   -> clip_1-2.mp4
//     第1集     + 第2集     -> 第1集-2集.mp4
//     同样的视频  + 同样的视频  -> 同样的视频.mp4
// A shared run of digits is handed back to both sides, since the digits are
// part of the serial number ("001" + "010" stays "001-010").
// separator, and a common prefix is never allowed to cut a UTF-16 surrogate or
// CJK character in half.
// ---------------------------------------------------------------------------
std::wstring MakeMergeName(const std::wstring& firstName,
                           const std::wstring& lastName,
                           const std::wstring& ext)
{
    std::wstring a = PathGetFileNameNoExt(firstName);
    std::wstring b = PathGetFileNameNoExt(lastName);

    std::wstring base;
    if (a.empty() && b.empty())  base = L"merged";
    else if (a.empty())          base = b;
    else if (b.empty())          base = a;
    else if (a == b)             base = a;      // identical names, nothing to join
    else
    {
        size_t common = 0;
        size_t maxc = (std::min)(a.size(), b.size());
        while (common < maxc && a[common] == b[common]) ++common;

        // Never cut a character in half (low/high surrogate, CJK lead vs trail).
        while (common > 0 && common < maxc)
        {
            wchar_t c = a[common];
            if ((c & 0xFC00) != 0xDC00) break;
            --common;
        }

        // A shared run of digits belongs to the serial number, not to a name
        // A shared run of digits belongs to the serial number, not to a name
        // prefix: "001" + "010" must stay "001-010" and 视频001 + 视频010
        // must stay 视频001-010. The digits continuing past a text prefix are
        // part of the number on both sides, so hand that shared digit run back.
        // 第1集 + 第2集 stops right after the text prefix 第 and yields
        // 第1集-2集.
        while (common > 0 && a[common - 1] >= L'0' && a[common - 1] <= L'9')
            --common;

        std::wstring head  = a.substr(0, common);
        std::wstring tailA = a.substr(common);
        std::wstring tailB = b.substr(common);

        if (head.empty())
        {
            base = tailA + L"-" + tailB;
        }
        else
        {
            // The prefix already completes the first name, so it needs a single
            // separator only in front of the second remainder:
            // "clip_" + "1"/"2" -> "clip_1-2", "abc" + "d" -> "abcd".
            base = head + tailA;
            if (!tailA.empty() && !tailB.empty()) base += L"-";
            base += tailB;
        }
    }

    if (!ext.empty())
    {
        if (ext[0] != L'.') base += L".";
        base += ext;
    }
    return base;
}


std::wstring PathGetFull(const std::wstring& path)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = ::GetFullPathNameW(path.c_str(), (DWORD)_countof(buf), buf, nullptr);
    if (n == 0 || n >= _countof(buf)) return path;
    return std::wstring(buf, n);
}

std::wstring ToLowerW(const std::wstring& s)
{
    std::wstring out = s;
    if (!out.empty()) ::CharLowerBuffW(&out[0], (DWORD)out.size());
    return out;
}

bool EnsureDirectory(const std::wstring& path)
{
    if (DirectoryExists(path)) return true;
    if (path.empty()) return false;
    std::wstring parent = PathGetDirectory(path);
    if (!parent.empty() && !DirectoryExists(parent)) EnsureDirectory(parent);
    return ::CreateDirectoryW(path.c_str(), nullptr) != 0 || DirectoryExists(path);
}

bool DeleteDirectoryRecursive(const std::wstring& path)
{
    if (!DirectoryExists(path)) return true;
    std::wstring pattern = PathCombine(path, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring child = PathCombine(path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                DeleteDirectoryRecursive(child);
            }
            else
            {
                ::SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                ::DeleteFileW(child.c_str());
            }
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    return ::RemoveDirectoryW(path.c_str()) != 0;
}

std::wstring GetLocalAppDataDir()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)_countof(buf));
    if (n == 0 || n >= _countof(buf))
        n = ::GetEnvironmentVariableW(L"TEMP", buf, (DWORD)_countof(buf));
    if (n == 0) return GetExeDir();
    return std::wstring(buf, n);
}

std::vector<std::wstring> ListFilesByExt(const std::wstring& dir,
                                         const std::vector<std::wstring>& exts)
{
    std::vector<std::wstring> out;
    std::wstring pattern = PathCombine(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring ext = PathGetExtension(fd.cFileName);
        for (size_t i = 0; i < exts.size(); ++i)
        {
            if (ext == exts[i]) { out.push_back(PathCombine(dir, fd.cFileName)); break; }
        }
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return out;
}

// ---------------------------------------------------------------------------
// time
// ---------------------------------------------------------------------------
std::wstring FormatTimecode(double seconds)
{
    if (seconds < 0) seconds = 0;
    double total = seconds;
    int hours = (int)(total / 3600.0);
    total -= hours * 3600.0;
    int minutes = (int)(total / 60.0);
    total -= minutes * 60.0;
    int secs = (int)total;
    int ms = (int)((total - secs) * 1000.0 + 0.5);
    if (ms >= 1000) { ms -= 1000; ++secs; }
    if (secs >= 60) { secs -= 60; ++minutes; }
    if (minutes >= 60) { minutes -= 60; ++hours; }
    return FormatString(L"%02d:%02d:%02d.%03d", hours, minutes, secs, ms);
}

std::wstring FormatClock(double seconds)
{
    if (seconds < 0) seconds = 0;
    double total = seconds;
    int hours = (int)(total / 3600.0);
    total -= hours * 3600.0;
    int minutes = (int)(total / 60.0);
    total -= minutes * 60.0;
    int secs = (int)(total + 0.5);
    if (secs >= 60) { secs -= 60; ++minutes; }
    if (minutes >= 60) { minutes -= 60; ++hours; }
    return FormatString(L"%02d:%02d:%02d", hours, minutes, secs);
}

std::string FormatSecondsUtf8(double seconds, int decimals)
{
    return WideToUtf8(NumberText(seconds, decimals));
}

bool ParseTimecode(const std::wstring& text, double& out)
{
    // accepts "12.5", "00:12.5", "1:02:03.250"
    std::wstring s;
    s.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (!iswspace(text[i])) s.push_back(text[i]);
    }
    if (s.empty()) return false;

    double parts[3] = { 0, 0, 0 };
    int idx = 0;
    size_t pos = 0;
    while (pos <= s.size() && idx < 3)
    {
        size_t colon = s.find(L':', pos);
        std::wstring token = (colon == std::wstring::npos) ? s.substr(pos) : s.substr(pos, colon - pos);
        if (token.empty()) return false;
        wchar_t* endp = nullptr;
        double v = wcstod(token.c_str(), &endp);
        if (endp == token.c_str()) return false;
        parts[idx++] = v;
        if (colon == std::wstring::npos) break;
        pos = colon + 1;
    }
    if (idx == 0) return false;
    out = 0.0;
    for (int i = 0; i < idx; ++i) out = out * 60.0 + parts[i];
    if (out < 0) out = 0;
    return true;
}

std::wstring NumberText(double value, int decimals)
{
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;
    return FormatString(L"%.*f", decimals, value);
}