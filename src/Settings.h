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
#include "Loc.h"

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
    // 片头/片尾扫描范围（整数秒），两段重叠或相接时自动合并成一段扫描：
    //   片头 = 视频绝对时间 [blackHeadStart, blackHeadEnd]
    //   片尾 = 从片尾倒退 [blackTailBackMax, blackTailBackMin]，
    //          即 [时长-blackTailBackMax, 时长-blackTailBackMin]
    //   blackHeadEnd / blackTailBackMax < 0 = 该侧不限制（兼容旧配置负数 = 整段）
    int    blackHeadStart         = 0;
    int    blackHeadEnd           = 180;
    int    blackTailBackMax       = 180;
    int    blackTailBackMin       = 0;

    // timeline
    int    thumbHeight           = 64;

    // ui state
    // false = 左侧只显示视频列表（帧流），文件列表框隐藏（工具栏“列表”按钮切换）
    bool   showFileList          = false;

    // 界面语言。Auto = 跟随系统（首次启动按区域设置自动选中文或英文）
    AppLang lang                 = AppLang::Auto;

    // 是否生成视频流缩略图。默认关闭：黑屏检测本身不需要缩略图，关掉可以
    // 少跑一遍 ffmpeg 抽帧 + 拼图，长视频列表明显更快。
    bool   makeThumbs            = false;

    // 任务完成提示音（设置里可以单独开关，见 Sound.h）
    //   soundDetectDone = 全部分析完成后响“叮铃铃”
    //   soundExportDone  = 导出完成后响“叮咚咚”
    // 默认都开：分析/导出动辄几分钟，等的时候人在别的窗口，响一声才知道完了。
    bool   soundDetectDone       = true;
    bool   soundExportDone       = true;

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