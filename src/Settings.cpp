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
// Settings.cpp - INI based settings persistence
// ---------------------------------------------------------------------------
#include "Settings.h"

#include <cstdlib>

namespace
{
    std::wstring IniGetString(const std::wstring& file, const wchar_t* section,
                              const wchar_t* key, const std::wstring& def)
    {
        wchar_t buf[2048];
        DWORD n = ::GetPrivateProfileStringW(section, key, def.c_str(), buf,
                                             (DWORD)_countof(buf), file.c_str());
        if (n == 0) return def;
        return std::wstring(buf, n);
    }

    int IniGetInt(const std::wstring& file, const wchar_t* section, const wchar_t* key, int def)
    {
        return (int)::GetPrivateProfileIntW(section, key, def, file.c_str());
    }

    double IniGetDouble(const std::wstring& file, const wchar_t* section, const wchar_t* key, double def)
    {
        std::wstring s = IniGetString(file, section, key, NumberText(def, 3));
        wchar_t* endp = nullptr;
        double v = wcstod(s.c_str(), &endp);
        if (endp == s.c_str()) return def;
        return v;
    }

    bool IniGetBool(const std::wstring& file, const wchar_t* section, const wchar_t* key, bool def)
    {
        return IniGetInt(file, section, key, def ? 1 : 0) != 0;
    }

    void IniSetString(const std::wstring& file, const wchar_t* section,
                      const wchar_t* key, const std::wstring& value)
    {
        ::WritePrivateProfileStringW(section, key, value.c_str(), file.c_str());
    }

    void IniSetInt(const std::wstring& file, const wchar_t* section, const wchar_t* key, int value)
    {
        IniSetString(file, section, key, FormatString(L"%d", value));
    }

    void IniSetBool(const std::wstring& file, const wchar_t* section, const wchar_t* key, bool value)
    {
        IniSetInt(file, section, key, value ? 1 : 0);
    }
}

std::wstring DefaultSettingsPath()
{
    std::wstring dir = PathCombine(GetLocalAppDataDir(), L"FastVideoCut");
    EnsureDirectory(dir);
    return PathCombine(dir, L"settings.ini");
}

std::wstring DefaultOutputDir()
{
    std::wstring dir = PathCombine(GetLocalAppDataDir(), L"FastVideoCut\\output");
    EnsureDirectory(dir);
    return dir;
}

bool LoadSettings(AppSettings& s)
{
    s.iniPath = DefaultSettingsPath();
    if (!FileExists(s.iniPath)) return false;

    const std::wstring& f = s.iniPath;

    s.ffmpegDir      = IniGetString(f, L"ffmpeg", L"dir", s.ffmpegDir);
    s.outputDir      = IniGetString(f, L"export", L"outputDir", s.outputDir);
    s.mergeFileName  = IniGetString(f, L"export", L"mergeFileName", s.mergeFileName);

    s.blackMinDuration = IniGetDouble(f, L"blackdetect", L"minDuration", s.blackMinDuration);
    s.blackPixTh       = IniGetDouble(f, L"blackdetect", L"pixTh", s.blackPixTh);
    s.blackPicTh       = IniGetDouble(f, L"blackdetect", L"picTh", s.blackPicTh);
    s.blackEdgeScan    = IniGetDouble(f, L"blackdetect", L"edgeScan", s.blackEdgeScan);

    s.thumbHeight    = IniGetInt(f, L"ui", L"thumbHeight", s.thumbHeight);
    s.showFileList   = IniGetBool(f, L"ui", L"showFileList", s.showFileList);

    s.reencodeExport      = IniGetBool(f, L"export", L"reencode", s.reencodeExport);
    s.mergeReencode       = IniGetBool(f, L"export", L"mergeReencode", s.mergeReencode);
    s.crf                 = IniGetInt(f, L"export", L"crf", s.crf);
    s.faststart           = IniGetBool(f, L"export", L"faststart", s.faststart);
    s.confirmBeforeExport = IniGetBool(f, L"export", L"confirm", s.confirmBeforeExport);

    std::wstring preset = IniGetString(f, L"export", L"preset", Utf8ToWide(s.preset));
    s.preset = WideToAnsi(preset);

    s.lastAddDir = IniGetString(f, L"ui", L"lastAddDir", s.lastAddDir);

    if (s.thumbHeight < 24) s.thumbHeight = 24;
    if (s.thumbHeight > 240) s.thumbHeight = 240;
    if (s.crf < 0) s.crf = 0;
    if (s.crf > 51) s.crf = 51;
    if (s.blackMinDuration < 0.0) s.blackMinDuration = 0.0;
    if (s.blackPixTh < 0.0) s.blackPixTh = 0.0;
    if (s.blackPicTh < 0.0) s.blackPicTh = 0.0;
    return true;
}

bool SaveSettings(const AppSettings& s)
{
    AppSettings tmp = s;
    if (tmp.iniPath.empty()) tmp.iniPath = DefaultSettingsPath();
    const std::wstring& f = tmp.iniPath;

    IniSetString(f, L"ffmpeg", L"dir", tmp.ffmpegDir);
    IniSetString(f, L"export", L"outputDir", tmp.outputDir);
    IniSetString(f, L"export", L"mergeFileName", tmp.mergeFileName);
    IniSetString(f, L"export", L"preset", Utf8ToWide(tmp.preset));
    IniSetInt(f, L"export", L"crf", tmp.crf);
    IniSetBool(f, L"export", L"reencode", tmp.reencodeExport);
    IniSetBool(f, L"export", L"mergeReencode", tmp.mergeReencode);
    IniSetBool(f, L"export", L"faststart", tmp.faststart);
    IniSetBool(f, L"export", L"confirm", tmp.confirmBeforeExport);

    IniSetString(f, L"blackdetect", L"minDuration", NumberText(tmp.blackMinDuration, 3));
    IniSetString(f, L"blackdetect", L"pixTh", NumberText(tmp.blackPixTh, 3));
    IniSetString(f, L"blackdetect", L"picTh", NumberText(tmp.blackPicTh, 3));
    IniSetString(f, L"blackdetect", L"edgeScan", NumberText(tmp.blackEdgeScan, 3));

    IniSetInt(f, L"ui", L"thumbHeight", tmp.thumbHeight);
    IniSetBool(f, L"ui", L"showFileList", tmp.showFileList);
    IniSetString(f, L"ui", L"lastAddDir", tmp.lastAddDir);
    return true;
}