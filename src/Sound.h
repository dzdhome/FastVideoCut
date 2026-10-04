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
// Sound.h - 任务完成提示音（自己合成的铃声，不依赖任何外部 wav 资源）
//
//   PlayDetectDone()  全部分析完成 -> "叮铃铃"（上行三音，像门铃连响三下）
//   PlayExportDone()   导出完成     -> "叮咚"（高中两音，第二个拖长）
//
// 播放走后台线程 + waveOut（winmm，build.ps1 已经链了 -lwinmm），
// 所以不会卡住界面；没有可用音频设备时静默跳过。
// 是否启用由 AppSettings::soundDetectDone / soundExportDone 控制，
// 见 SettingsDialog —— 这里只管响。
// ---------------------------------------------------------------------------
#pragma once

namespace Sound
{
    // 全部分析完成后的“叮铃铃”
    void PlayDetectDone();

    // 导出完成后的“叮咚咚”
    void PlayExportDone();

    // 等当前正在响的提示音播完再返回（退出前调用，避免声音被硬生生掐断）
    void Shutdown();
}