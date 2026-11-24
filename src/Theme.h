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
// Theme.h - one palette for every self-painted pane, and the native windows
// pieces that can be told to go dark (title bar, menus, list view, dialogs)
//
// The panes (timeline, log, preview bar, scroll bars, toolbar) read their
// colours from here at paint time, so switching the theme only needs an
// InvalidateRect - there is no per-control state to migrate. Native controls
// that cannot be palette-painted (buttons, check boxes) are owner-drawn with
// the same palette; the list view and dialogs get the uxtheme dark-mode hints
// on top of explicit background/text colours.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

// Stored as an int in settings.ini; the order must stay stable.
enum class AppTheme
{
    FollowSystem = 0,
    Light        = 1,
    Dark         = 2
};

struct ThemeColors
{
    bool dark;

    // ---- main window ------------------------------------------------------
    COLORREF windowBg;
    COLORREF text;
    COLORREF textDim;
    COLORREF panelBg;
    COLORREF panelBorder;

    // ---- timeline ---------------------------------------------------------
    COLORREF tlBack;
    COLORREF tlRuler;
    COLORREF tlRulerLine;
    COLORREF tlRulerText;
    COLORREF tlRowA;
    COLORREF tlRowB;
    COLORREF tlRowBrd;
    COLORREF tlPanel;
    COLORREF tlText;
    COLORREF tlTextDim;
    COLORREF tlSelFill;
    COLORREF tlSelBrd;
    COLORREF tlBlackFill;
    COLORREF tlBlackBrd;
    COLORREF tlDropBrd;
    COLORREF tlCurrent;
    COLORREF tlPlaceholder;
    COLORREF tlShade;          // translucent veil over discarded segments
    COLORREF tlPlayhead;       // the thin "you are here" line
    COLORREF tlHdr;
    COLORREF tlManual;
    COLORREF tlThumbBack;

    // ---- log pane ---------------------------------------------------------
    COLORREF logBg;
    COLORREF logText;
    COLORREF logSelBg;
    COLORREF logSelText;

    // ---- scroll bars ------------------------------------------------------
    COLORREF sbTrack;
    COLORREF sbThumb;
    COLORREF sbThumbHover;
    COLORREF sbThumbDown;
    COLORREF sbBorder;

    // ---- preview pane -----------------------------------------------------
    COLORREF pvBarBg;
    COLORREF pvBarText;
    COLORREF pvBarSep;
    COLORREF pvTrack;
    COLORREF pvFill;
    COLORREF pvThumb;
    COLORREF pvThumbDrag;
    COLORREF pvPlaceholder;
    COLORREF pvEmptyBg;        // empty monitor; letterboxing around frames stays black

    // ---- owner-drawn buttons / check boxes --------------------------------
    COLORREF btnBg;
    COLORREF btnHover;
    COLORREF btnPressed;
    COLORREF btnBorder;
    COLORREF btnText;
    COLORREF btnTextOff;

    // ---- progress bar -----------------------------------------------------
    COLORREF progressBg;
    COLORREF progressFill;

    // ---- list view --------------------------------------------------------
    COLORREF listBg;
    COLORREF listText;
    COLORREF listLine;
    COLORREF listHdrBg;        // column header strip (custom drawn, see MainWindow)
    COLORREF listHdrText;

    // ---- dialogs ----------------------------------------------------------
    COLORREF dlgBg;
    COLORREF dlgText;
    COLORREF dlgEditBg;
    COLORREF dlgEditText;
};

namespace Theme
{
    AppTheme Mode();
    void     SetMode(AppTheme mode);      // resolves FollowSystem immediately
    bool     Dark();
    const ThemeColors& C();

    // 0 = dark, 1 = light per HKCU\...\Themes\Personalize\AppsUseLightTheme
    bool SystemPrefersDark();

    // Dark title bar (DWM) + the uxtheme dark-mode hint for this window.
    void ApplyWindow(HWND hwnd, bool dark);
    // Tell uxtheme about the list view / its header. Separate because the
    // header is a child window and needs its own theme name.
    void ApplyListTheme(HWND list, bool dark);
    // SetPreferredAppMode + FlushMenuThemes; call before creating menus, and
    // again whenever the mode changes.
    void ApplyMenuTheme(bool dark);

    // Self-drawn controls (WM_DRAWITEM). The text comes from the control.
    void DrawButton(const DRAWITEMSTRUCT* dis);
    void DrawCheckBox(const DRAWITEMSTRUCT* dis);

    // Shared brushes for WM_CTLCOLORDLG / WM_CTLCOLORSTATIC / WM_CTLCOLOREDIT.
    HBRUSH DlgBrush();
    HBRUSH EditBrush();
    void   SetupTextColor(HDC dc);        // dlg text colour, transparent bg
}
