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
// resource.h - control / command identifiers
// ---------------------------------------------------------------------------
#define IDI_APPICON            101

// ---- menus ---------------------------------------------------------------
#define IDM_FILE_ADD           40001
#define IDM_FILE_ADDDIR        40002
#define IDM_FILE_REMOVE        40003
#define IDM_FILE_CLEAR         40004
#define IDM_FILE_EXIT          40005

#define IDM_VID_UP             40101
#define IDM_VID_DOWN           40102
#define IDM_VID_SORTNAME       40103
#define IDM_VID_DETECT         40104
#define IDM_VID_SETTINGS       40105
#define IDM_VID_RESET          40106
#define IDM_VID_CLEARCACHE     40107

#define IDM_SEL_BODY   40201   // 保留主体（首末非黑屏段之间）
#define IDM_SEL_ALL    40204   // 整段保留（含黑屏）
#define IDM_SEL_CLEAR  40206   // 清除选择

#define IDM_EXP_EACH           40301
#define IDM_EXP_MERGE          40302
#define IDM_EXP_ALL            40303
#define IDM_EXP_OPEN           40304
#define IDM_EXP_CANCEL         40305

#define IDM_HELP_INFO          40401
#define IDM_HELP_ABOUT         40402

#define IDM_VIEW_FIT           40501
#define IDM_VIEW_ZIN           40502
#define IDM_VIEW_ZOUT          40503
#define IDM_VIEW_THUMB_BIG     40504
#define IDM_VIEW_THUMB_SMALL   40505
#define IDM_VIEW_LIST          40506   // 文件列表 / 视频列表 切换
#define IDM_VID_REDETECT       40108   // 重新检测全部黑屏（不跳过已检测）

// ---- controls ------------------------------------------------------------
#define IDC_LIST               41001
#define IDC_TIMELINE           41002
#define IDC_LOG                41003
#define IDC_STATUS             41004
#define IDC_HELPBOX            41005
#define IDC_PROGRESS           41006
#define IDC_PREVIEW            41007

#define IDB_ADD                41101
#define IDB_REMOVE             41102
#define IDB_UP                 41103
#define IDB_DOWN               41104
#define IDB_DETECT             41105
#define IDB_EXPORT             41106
#define IDB_SETTINGS           41107
#define IDB_INFO               41108
#define IDB_CLEARCACHE         41109
#define IDB_LISTVIEW           41110   // 文件列表 / 视频列表 切换

// ---- settings dialog -----------------------------------------------------
#define IDD_SETTINGS           42001
#define IDC_SET_FFDIR          42010
#define IDC_SET_FFBROWSE       42011
#define IDC_SET_FFINFO         42012
#define IDC_SET_OUTDIR         42013
#define IDC_SET_OUTBROWSE      42014
#define IDC_SET_NAME           42015
#define IDC_SET_MINDUR         42016
#define IDC_SET_PIXTH          42017
#define IDC_SET_PICTH          42018
#define IDC_SET_THUMBH         42019
#define IDC_SET_REENC          42020
#define IDC_SET_CRF            42021
#define IDC_SET_PRESET         42022
#define IDC_SET_FASTSTART      42023
#define IDC_SET_CONFIRM        42024
#define IDC_SET_RESET          42025
#define IDC_SET_EDGESCAN       42026   // 只扫片头/片尾多少秒（0 = 整段）
#define IDC_SET_FASTDETECT     42026   // 快速检测（关键帧预扫）