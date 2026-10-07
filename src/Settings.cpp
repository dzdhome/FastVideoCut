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

    s.blackMinDuration = IniGetDouble(f, L"blackdetect", L"minDuration", s.blackMinDuration);
    s.blackPixTh       = IniGetDouble(f, L"blackdetect", L"pixTh", s.blackPixTh);
    s.blackPicTh       = IniGetDouble(f, L"blackdetect", L"picTh", s.blackPicTh);

    // 迁移链：edgeScan（两侧共用，最老）→ headScan/tailScan（单值窗口）→
    // headStart/headEnd + tailBackMax/tailBackMin（范围）。每一级旧值作为下一级
    // 新键的默认，老配置读进来语义不变：
    //   headScan N   → 片头 [0, N]（0 = 不扫，负数 = 不限）
    //   tailScan N   → 片尾倒退 [N, 0]（0 = 不扫，负数 = 不限）
    {
        wchar_t legacy[64];
        DWORD n = ::GetPrivateProfileStringW(L"blackdetect", L"edgeScan", L"",
                                             legacy, (DWORD)_countof(legacy), f.c_str());
        std::wstring oldScan(legacy, n);
        double legacyEdge = s.blackHeadEnd;
        bool hasLegacy = false;
        if (!oldScan.empty())
        {
            wchar_t* endp = nullptr;
            double v = wcstod(oldScan.c_str(), &endp);
            if (endp != oldScan.c_str()) { legacyEdge = v; hasLegacy = true; }
        }
        int defHead = hasLegacy ? (int)legacyEdge : s.blackHeadEnd;
        int defTail = hasLegacy ? (int)legacyEdge : s.blackTailBackMax;
        defHead = IniGetInt(f, L"blackdetect", L"headScan", defHead);
        defTail = IniGetInt(f, L"blackdetect", L"tailScan", defTail);

        s.blackHeadStart   = IniGetInt(f, L"blackdetect", L"headStart", s.blackHeadStart);
        s.blackHeadEnd     = IniGetInt(f, L"blackdetect", L"headEnd", defHead);
        s.blackTailBackMax = IniGetInt(f, L"blackdetect", L"tailBackMax", defTail);
        s.blackTailBackMin = IniGetInt(f, L"blackdetect", L"tailBackMin", s.blackTailBackMin);
    }

    s.thumbHeight    = IniGetInt(f, L"ui", L"thumbHeight", s.thumbHeight);
    s.showFileList   = IniGetBool(f, L"ui", L"showFileList", s.showFileList);
    s.makeThumbs     = IniGetBool(f, L"ui", L"makeThumbs", s.makeThumbs);
    // 语言存的是数字；越界就退回“跟随系统”
    int lang = IniGetInt(f, L"ui", L"lang", (int)s.lang);
    if (lang < 0 || lang > 2) lang = 0;
    s.lang           = (AppLang)lang;

    s.reencodeExport      = IniGetBool(f, L"export", L"reencode", s.reencodeExport);
    s.mergeReencode       = IniGetBool(f, L"export", L"mergeReencode", s.mergeReencode);
    s.crf                 = IniGetInt(f, L"export", L"crf", s.crf);
    s.faststart           = IniGetBool(f, L"export", L"faststart", s.faststart);
    s.confirmBeforeExport = IniGetBool(f, L"export", L"confirm", s.confirmBeforeExport);

    std::wstring preset = IniGetString(f, L"export", L"preset", Utf8ToWide(s.preset));
    s.preset = WideToAnsi(preset);

    s.lastAddDir = IniGetString(f, L"ui", L"lastAddDir", s.lastAddDir);

    // 提示音开关单独放一节，跟界面/导出参数分开
    s.soundDetectDone = IniGetBool(f, L"sound", L"detectDone", s.soundDetectDone);
    s.soundExportDone = IniGetBool(f, L"sound", L"exportDone", s.soundExportDone);

    if (s.thumbHeight < 24) s.thumbHeight = 24;
    if (s.thumbHeight > 240) s.thumbHeight = 240;
    if (s.crf < 0) s.crf = 0;
    if (s.crf > 51) s.crf = 51;
    if (s.blackMinDuration < 0.0) s.blackMinDuration = 0.0;
    if (s.blackPixTh < 0.0) s.blackPixTh = 0.0;
    if (s.blackPicTh < 0.0) s.blackPicTh = 0.0;
    // 起始 / 倒退最小不能为负（UI 上也限制了整数非负）；倒退最大和片头结束的
    // 负数保留 = 该侧不限制（兼容旧配置）。两个范围都空 = 没有任何可扫区域，
    // 多半是 INI 被手改坏了，退回默认值，免得点“自动分析”什么都不扫还看不出原因。
    if (s.blackHeadStart < 0) s.blackHeadStart = 0;
    if (s.blackTailBackMin < 0) s.blackTailBackMin = 0;
    {
        bool headEmpty = (s.blackHeadEnd >= 0) && (s.blackHeadStart >= s.blackHeadEnd);
        bool tailEmpty = (s.blackTailBackMax >= 0) &&
                         (s.blackTailBackMax <= s.blackTailBackMin);
        if (headEmpty && tailEmpty)
        {
            const AppSettings def;
            s.blackHeadStart   = def.blackHeadStart;
            s.blackHeadEnd     = def.blackHeadEnd;
            s.blackTailBackMax = def.blackTailBackMax;
            s.blackTailBackMin = def.blackTailBackMin;
        }
    }
    return true;
}

bool SaveSettings(const AppSettings& s)
{
    AppSettings tmp = s;
    if (tmp.iniPath.empty()) tmp.iniPath = DefaultSettingsPath();
    const std::wstring& f = tmp.iniPath;

    IniSetString(f, L"ffmpeg", L"dir", tmp.ffmpegDir);
    IniSetString(f, L"export", L"outputDir", tmp.outputDir);
    // 合并文件名不再有设置项（按首尾视频自动生成），无需迁移
    IniSetString(f, L"export", L"preset", Utf8ToWide(tmp.preset));
    IniSetInt(f, L"export", L"crf", tmp.crf);
    IniSetBool(f, L"export", L"reencode", tmp.reencodeExport);
    IniSetBool(f, L"export", L"mergeReencode", tmp.mergeReencode);
    IniSetBool(f, L"export", L"faststart", tmp.faststart);
    IniSetBool(f, L"export", L"confirm", tmp.confirmBeforeExport);

    IniSetString(f, L"blackdetect", L"minDuration", NumberText(tmp.blackMinDuration, 3));
    IniSetString(f, L"blackdetect", L"pixTh", NumberText(tmp.blackPixTh, 3));
    IniSetString(f, L"blackdetect", L"picTh", NumberText(tmp.blackPicTh, 3));
    IniSetInt(f, L"blackdetect", L"headStart", tmp.blackHeadStart);
    IniSetInt(f, L"blackdetect", L"headEnd", tmp.blackHeadEnd);
    IniSetInt(f, L"blackdetect", L"tailBackMax", tmp.blackTailBackMax);
    IniSetInt(f, L"blackdetect", L"tailBackMin", tmp.blackTailBackMin);
    IniSetString(f, L"blackdetect", L"headScan", L"");   // 旧键清空，避免再次迁移
    IniSetString(f, L"blackdetect", L"tailScan", L"");
    IniSetString(f, L"blackdetect", L"edgeScan", L"");

    IniSetInt(f, L"ui", L"thumbHeight", tmp.thumbHeight);
    IniSetBool(f, L"ui", L"showFileList", tmp.showFileList);
    IniSetBool(f, L"ui", L"makeThumbs", tmp.makeThumbs);
    IniSetInt(f, L"ui", L"lang", (int)tmp.lang);
    IniSetString(f, L"ui", L"lastAddDir", tmp.lastAddDir);

    IniSetBool(f, L"sound", L"detectDone", tmp.soundDetectDone);
    IniSetBool(f, L"sound", L"exportDone", tmp.soundExportDone);
    return true;
}