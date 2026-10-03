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
// Settings.h - persisted application settings (INI file in %LOCALAPPDATA%)
// ---------------------------------------------------------------------------
#pragma once

#include "Utf.h"

#include <string>

struct AppSettings
{
    std::wstring ffmpegDir;
    std::wstring outputDir;
    // 合并输出的文件名不再由设置决定，而是按首尾视频自动生成（见 MakeMergeName）

    // blackdetect parameters
    double blackMinDuration      = 0.10;
    double blackPixTh            = 0.10;
    double blackPicTh            = 0.98;
    // 只扫描片头/片尾各多少秒（片头结束/片尾开始基本都在这里）；0 = 整段扫描
    double blackEdgeScan          = 180.0;

    // timeline
    int    thumbHeight           = 64;

    // ui state
    // false = 左侧只显示视频列表（帧流），文件列表框隐藏（工具栏“列表”按钮切换）
    bool   showFileList          = false;

    // export
    bool   reencodeExport        = false;    // false => lossless stream copy
    bool   mergeReencode         = false;    // re-encode when sources mismatch
    int    crf                   = 18;
    std::string preset           = "veryfast";
    bool   faststart             = true;
    bool   confirmBeforeExport   = true;

    // ui state
    std::wstring lastAddDir;

    std::wstring iniPath;
};

std::wstring DefaultSettingsPath();
bool LoadSettings(AppSettings& s);
bool SaveSettings(const AppSettings& s);
std::wstring DefaultOutputDir();