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
// Messages.h - window messages and payloads shared between UI and workers
// ---------------------------------------------------------------------------
#pragma once

#include "Utf.h"

#include <string>
#include <vector>

#define WM_FVC_UI      (WM_APP + 100)   // lParam = UiMessage*   (receiver owns it)
#define WM_FVC_THUMBS  (WM_APP + 101)   // wParam = epoch (timeline internal)
#define WM_FVC_SELCHG  (WM_APP + 102)   // timeline -> main window
#define WM_FVC_ADD     (WM_APP + 103)   // lParam = std::vector<std::wstring>* (dropped files)
#define WM_FVC_AUTOSTART (WM_APP + 104) // start the --auto-detect/--auto-export job after files are queued
#define WM_FVC_PREVIEW (WM_APP + 105)   // wParam = item index, lParam = segment index (timeline click)
#define WM_FVC_KEEPCHG (WM_APP + 106)   // wParam = item index (-1 = all items): keep selection changed

enum UiMessageKind
{
    UiLog = 1,          // text  : append to the log panel
    UiStatus,           // text  : status line
    UiProgress,         // a = percent (0..100), b = item index (-1 = overall)
    UiItemUpdated,      // a = item index
    UiJobDone,          // a = 1 success / 0 failure, text = summary
    UiSelectItem        // a = item index (list view selection)
};

struct UiMessage
{
    UiMessageKind kind = UiLog;
    int           a    = 0;
    int           b    = 0;
    std::wstring  text;
};

inline void PostUiMessage(HWND hwnd, UiMessageKind kind, const std::wstring& text = std::wstring(),
                          int a = 0, int b = 0)
{
    UiMessage* m = new UiMessage();
    m->kind = kind;
    m->text = text;
    m->a = a;
    m->b = b;
    if (!::PostMessageW(hwnd, WM_FVC_UI, 0, (LPARAM)m))
        delete m;
}