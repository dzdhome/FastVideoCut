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
// Theme.cpp - the two palettes + the Win32 dark-mode plumbing
// ---------------------------------------------------------------------------
#include "Theme.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <cstring>

namespace
{
    // Neutral grey dark palette modelled on Premiere Pro: panels sit at
    // #232323/#2D2D2D, content wells at #1D1D1D, the timeline canvas at
    // #262626, and the only saturated colour is the #318CE8 accent.
    const ThemeColors kDark =
    {
        true,
        /*windowBg      */ RGB(35, 35, 35),
        /*text          */ RGB(214, 214, 214),
        /*textDim       */ RGB(143, 143, 143),
        /*panelBg       */ RGB(45, 45, 45),
        /*panelBorder   */ RGB(28, 28, 28),

        /*tlBack        */ RGB(38, 38, 38),
        /*tlRuler       */ RGB(45, 45, 45),
        /*tlRulerLine   */ RGB(74, 74, 74),
        /*tlRulerText   */ RGB(175, 175, 175),
        /*tlRowA        */ RGB(41, 41, 41),
        /*tlRowB        */ RGB(38, 38, 38),
        /*tlRowBrd      */ RGB(55, 55, 55),
        /*tlPanel       */ RGB(45, 45, 45),
        /*tlText        */ RGB(214, 214, 214),
        /*tlTextDim     */ RGB(143, 143, 143),
        /*tlSelFill     */ RGB(49, 140, 232),
        /*tlSelBrd      */ RGB(90, 170, 245),
        /*tlBlackFill   */ RGB(150, 30, 30),
        /*tlBlackBrd    */ RGB(226, 86, 86),
        /*tlDropBrd     */ RGB(105, 105, 105),
        /*tlCurrent     */ RGB(255, 200, 60),
        /*tlPlaceholder */ RGB(48, 48, 48),
        /*tlShade       */ RGB(12, 12, 12),
        /*tlPlayhead    */ RGB(220, 220, 220),
        /*tlHdr         */ RGB(255, 168, 60),
        /*tlManual      */ RGB(255, 140, 40),
        /*tlThumbBack   */ RGB(22, 22, 22),

        /*logBg         */ RGB(29, 29, 29),
        /*logText       */ RGB(205, 205, 205),
        /*logSelBg      */ RGB(45, 92, 140),
        /*logSelText    */ RGB(255, 255, 255),

        /*sbTrack       */ RGB(45, 45, 45),
        /*sbThumb       */ RGB(95, 95, 95),
        /*sbThumbHover  */ RGB(115, 115, 115),
        /*sbThumbDown   */ RGB(135, 135, 135),
        /*sbBorder      */ RGB(28, 28, 28),

        /*pvBarBg       */ RGB(35, 35, 35),
        /*pvBarText     */ RGB(200, 200, 200),
        /*pvBarSep      */ RGB(70, 70, 70),
        /*pvTrack       */ RGB(70, 70, 70),
        /*pvFill        */ RGB(49, 140, 232),
        /*pvThumb       */ RGB(224, 224, 224),
        /*pvThumbDrag   */ RGB(255, 255, 255),
        /*pvPlaceholder */ RGB(130, 130, 130),
        /*pvEmptyBg     */ RGB(29, 29, 29),

        /*btnBg         */ RGB(58, 58, 58),
        /*btnHover      */ RGB(70, 70, 70),
        /*btnPressed    */ RGB(45, 45, 45),
        /*btnBorder     */ RGB(74, 74, 74),
        /*btnText       */ RGB(220, 220, 220),
        /*btnTextOff    */ RGB(125, 125, 125),

        /*progressBg    */ RGB(56, 56, 56),
        /*progressFill  */ RGB(49, 140, 232),

        /*listBg        */ RGB(29, 29, 29),
        /*listText      */ RGB(220, 220, 220),
        /*listLine      */ RGB(52, 52, 52),
        /*listHdrBg     */ RGB(45, 45, 45),
        /*listHdrText   */ RGB(200, 200, 200),

        /*dlgBg         */ RGB(50, 50, 50),
        /*dlgText       */ RGB(214, 214, 214),
        /*dlgEditBg     */ RGB(29, 29, 29),
        /*dlgEditText   */ RGB(226, 226, 226)
    };

    const ThemeColors kLight =
    {
        false,
        /*windowBg      */ RGB(245, 246, 248),
        /*text          */ RGB(32, 34, 38),
        /*textDim       */ RGB(110, 115, 124),
        /*panelBg       */ RGB(238, 240, 243),
        /*panelBorder   */ RGB(214, 218, 224),

        /*tlBack        */ RGB(245, 246, 248),
        /*tlRuler       */ RGB(234, 236, 240),
        /*tlRulerLine   */ RGB(190, 196, 204),
        /*tlRulerText   */ RGB(90, 96, 106),
        /*tlRowA        */ RGB(255, 255, 255),
        /*tlRowB        */ RGB(249, 250, 252),
        /*tlRowBrd      */ RGB(226, 229, 234),
        /*tlPanel       */ RGB(240, 242, 245),
        /*tlText        */ RGB(40, 42, 47),
        /*tlTextDim     */ RGB(120, 126, 136),
        /*tlSelFill     */ RGB(51, 122, 205),
        /*tlSelBrd      */ RGB(30, 110, 200),
        /*tlBlackFill   */ RGB(248, 206, 206),
        /*tlBlackBrd    */ RGB(205, 66, 66),
        /*tlDropBrd     */ RGB(150, 158, 168),
        /*tlCurrent     */ RGB(230, 150, 20),
        /*tlPlaceholder */ RGB(232, 235, 240),
        /*tlShade       */ RGB(196, 200, 206),
        /*tlPlayhead    */ RGB(60, 64, 72),
        /*tlHdr         */ RGB(198, 118, 18),
        /*tlManual      */ RGB(220, 110, 30),
        /*tlThumbBack   */ RGB(216, 219, 225),

        /*logBg         */ RGB(255, 255, 255),
        /*logText       */ RGB(16, 16, 16),
        /*logSelBg      */ RGB(204, 228, 247),
        /*logSelText    */ RGB(0, 0, 0),

        /*sbTrack       */ RGB(240, 240, 240),
        /*sbThumb       */ RGB(160, 160, 160),
        /*sbThumbHover  */ RGB(120, 120, 120),
        /*sbThumbDown   */ RGB(90, 90, 90),
        /*sbBorder      */ RGB(200, 200, 200),

        /*pvBarBg       */ RGB(240, 242, 245),
        /*pvBarText     */ RGB(60, 64, 72),
        /*pvBarSep      */ RGB(210, 214, 220),
        /*pvTrack       */ RGB(210, 214, 220),
        /*pvFill        */ RGB(51, 122, 205),
        /*pvThumb       */ RGB(90, 96, 106),
        /*pvThumbDrag   */ RGB(40, 44, 52),
        /*pvPlaceholder */ RGB(120, 126, 136),
        /*pvEmptyBg     */ RGB(0, 0, 0),

        /*btnBg         */ RGB(250, 250, 252),
        /*btnHover      */ RGB(238, 241, 246),
        /*btnPressed    */ RGB(222, 226, 233),
        /*btnBorder     */ RGB(200, 205, 212),
        /*btnText       */ RGB(40, 42, 47),
        /*btnTextOff    */ RGB(160, 165, 172),

        /*progressBg    */ RGB(225, 228, 233),
        /*progressFill  */ RGB(51, 122, 205),

        /*listBg        */ RGB(255, 255, 255),
        /*listText      */ RGB(25, 27, 31),
        /*listLine      */ RGB(230, 232, 236),
        /*listHdrBg     */ RGB(243, 244, 247),
        /*listHdrText   */ RGB(38, 42, 48),

        /*dlgBg         */ RGB(245, 246, 248),
        /*dlgText       */ RGB(32, 34, 38),
        /*dlgEditBg     */ RGB(255, 255, 255),
        /*dlgEditText   */ RGB(20, 20, 20)
    };

    AppTheme    g_mode   = AppTheme::Dark;   // matches the historic dark chrome
    bool        g_dark   = true;
    HBRUSH      g_dlgBrush = nullptr;
    HBRUSH      g_editBrush = nullptr;

    //
    // uxtheme dark-mode entry points. They are undocumented ordinals but have
    // been stable since Windows 10 1809; when they are missing (older builds,
    // Wine) every call becomes a no-op and the palette alone still themes the
    // self-painted panes.
    //
    typedef BOOL (WINAPI* FnAllowDarkModeForWindow)(HWND, BOOL);
    typedef int  (WINAPI* FnSetPreferredAppMode)(int);
    typedef void (WINAPI* FnFlushMenuThemes)();

    struct UxThemeApi
    {
        HMODULE dll = nullptr;
        FnAllowDarkModeForWindow allow = nullptr;
        FnSetPreferredAppMode    pref  = nullptr;
        FnFlushMenuThemes        flush = nullptr;
    };

    // FARPROC -> typed pointer. memcpy keeps GCC's -Wcast-function-type quiet
    // and is the same thing the cast does, just without the warning.
    template <typename T>
    T Proc(HMODULE m, LPCSTR name)
    {
        FARPROC p = m ? ::GetProcAddress(m, name) : nullptr;
        T f{};
        static_assert(sizeof(f) == sizeof(p), "function pointer size mismatch");
        ::memcpy(&f, &p, sizeof(f));
        return f;
    }

    UxThemeApi& Ux()
    {
        static UxThemeApi api = []()
        {
            UxThemeApi a;
            a.dll = ::LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!a.dll) a.dll = ::LoadLibraryW(L"uxtheme.dll");
            if (a.dll)
            {
                a.allow = Proc<FnAllowDarkModeForWindow>(a.dll, MAKEINTRESOURCEA(133));
                a.pref  = Proc<FnSetPreferredAppMode>(a.dll, MAKEINTRESOURCEA(135));
                a.flush = Proc<FnFlushMenuThemes>(a.dll, MAKEINTRESOURCEA(136));
            }
            return a;
        }();
        return api;
    }

    typedef HRESULT (WINAPI* FnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);

    void SetDarkTitleBar(HWND hwnd, bool dark)
    {
        static FnDwmSetWindowAttribute setAttr = []()
        {
            HMODULE dwm = ::LoadLibraryW(L"dwmapi.dll");
            return Proc<FnDwmSetWindowAttribute>(dwm, "DwmSetWindowAttribute");
        }();

        if (!setAttr || !hwnd) return;

        BOOL on = dark ? TRUE : FALSE;
        // 20 is the documented value on Windows 10 2004+; 19 covers 1809-1909.
        if (FAILED(setAttr(hwnd, 20, &on, sizeof(on))))
            setAttr(hwnd, 19, &on, sizeof(on));
    }

    void ResetBrushes()
    {
        if (g_dlgBrush)  { ::DeleteObject(g_dlgBrush);  g_dlgBrush = nullptr; }
        if (g_editBrush) { ::DeleteObject(g_editBrush); g_editBrush = nullptr; }
    }

    int ScaleFor(HWND hwnd, int px)
    {
        UINT dpi = 96;
        if (hwnd)
        {
            HDC dc = ::GetDC(hwnd);
            if (dc)
            {
                dpi = (UINT)::GetDeviceCaps(dc, LOGPIXELSY);
                ::ReleaseDC(hwnd, dc);
            }
        }
        if (dpi == 0) dpi = 96;
        return MulDiv(px, (int)dpi, 96);
    }

    void Fill(HDC dc, const RECT& rc, COLORREF c)
    {
        HBRUSH b = ::CreateSolidBrush(c);
        ::FillRect(dc, &rc, b);
        ::DeleteObject(b);
    }

    void DrawCheckMark(HDC dc, const RECT& box, COLORREF color)
    {
        const int w = box.right - box.left;
        HPEN pen = ::CreatePen(PS_SOLID, (w >= 15 ? 2 : 1), color);
        HGDIOBJ old = ::SelectObject(dc, pen);
        ::MoveToEx(dc, box.left + w * 22 / 100, box.top + w * 52 / 100, nullptr);
        ::LineTo(dc, box.left + w * 42 / 100, box.top + w * 72 / 100);
        ::LineTo(dc, box.left + w * 78 / 100, box.top + w * 28 / 100);
        ::SelectObject(dc, old);
        ::DeleteObject(pen);
    }
}

namespace Theme
{
    AppTheme Mode() { return g_mode; }
    bool     Dark() { return g_dark; }
    const ThemeColors& C() { return g_dark ? kDark : kLight; }

    bool SystemPrefersDark()
    {
        DWORD light = 1;
        DWORD cb    = sizeof(light);
        LONG r = ::RegGetValueW(HKEY_CURRENT_USER,
                                L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                L"AppsUseLightTheme",
                                RRF_RT_REG_DWORD, nullptr, &light, &cb);
        return r == ERROR_SUCCESS && light == 0;
    }

    void SetMode(AppTheme mode)
    {
        g_mode = mode;

        bool dark;
        if (mode == AppTheme::Dark)              dark = true;
        else if (mode == AppTheme::Light)        dark = false;
        else                                     dark = SystemPrefersDark();

        if (dark == g_dark && g_dlgBrush && g_editBrush) return;

        g_dark = dark;
        ResetBrushes();
        ApplyMenuTheme(dark);
    }

    void ApplyWindow(HWND hwnd, bool dark)
    {
        if (!hwnd) return;
        // The caption deliberately follows the Windows setting instead of the
        // app palette, so a dark-UI app on a light desktop keeps the native
        // light title bar (and vice versa) like every other window.
        SetDarkTitleBar(hwnd, SystemPrefersDark());
        UxThemeApi& api = Ux();
        if (api.allow) api.allow(hwnd, dark ? TRUE : FALSE);
        ::SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }

    namespace
    {
        struct ListThemeCtx
        {
            UxThemeApi* api;
            bool        dark;
        };

        // ListView scrollbars are classic child windows. Without their own
        // dark-mode hint they stay bright grey underneath a dark list.
        BOOL CALLBACK ThemeListChild(HWND child, LPARAM lp)
        {
            const ListThemeCtx* c = (const ListThemeCtx*)lp;
            wchar_t cls[64] = {};
            ::GetClassNameW(child, cls, 64);
            if (::lstrcmpiW(cls, L"SysHeader32") == 0) return TRUE;   // handled below
            if (c->api->allow) c->api->allow(child, c->dark ? TRUE : FALSE);
            ::SetWindowTheme(child, c->dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
            return TRUE;
        }
    }

    void ApplyListTheme(HWND list, bool dark)
    {
        if (!list) return;
        UxThemeApi& api = Ux();
        // uxtheme wants the opt-in first; doing it after SetWindowTheme makes
        // some builds ignore the hint and leave the header/scrollbars light.
        if (api.allow) api.allow(list, dark ? TRUE : FALSE);
        ::SetWindowTheme(list, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);

        ListThemeCtx ctx = { &api, dark };
        ::EnumChildWindows(list, ThemeListChild, (LPARAM)&ctx);

        // The header is a separate window; DarkMode_ItemsView paints it and the
        // column dividers dark. Without it a dark list gets a bright grey band
        // on top of it.
        if (HWND hdr = (HWND)::SendMessageW(list, LVM_GETHEADER, 0, 0))
        {
            if (api.allow) api.allow(hdr, dark ? TRUE : FALSE);
            ::SetWindowTheme(hdr, dark ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);
        }
    }

    void ApplyMenuTheme(bool dark)
    {
        UxThemeApi& api = Ux();
        if (api.pref)  api.pref(dark ? 2 : 0);   // 2 = ForceDark
        if (api.flush) api.flush();
    }

    HBRUSH DlgBrush()
    {
        if (!g_dlgBrush) g_dlgBrush = ::CreateSolidBrush(C().dlgBg);
        return g_dlgBrush;
    }

    HBRUSH EditBrush()
    {
        if (!g_editBrush) g_editBrush = ::CreateSolidBrush(C().dlgEditBg);
        return g_editBrush;
    }

    void SetupTextColor(HDC dc)
    {
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, C().dlgText);
    }

    void DrawButton(const DRAWITEMSTRUCT* dis)
    {
        if (!dis) return;
        const ThemeColors& c = C();
        HDC dc = dis->hDC;
        RECT rc = dis->rcItem;

        const bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
        const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

        Fill(dc, rc, c.btnBorder);
        RECT inner = rc;
        ::InflateRect(&inner, -1, -1);
        Fill(dc, inner, pressed ? c.btnPressed : c.btnBg);

        wchar_t text[256];
        text[0] = 0;
        ::GetWindowTextW(dis->hwndItem, text, (int)_countof(text));

        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, disabled ? c.btnTextOff : c.btnText);
        HGDIOBJ oldFont = (HGDIOBJ)::SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
        if (oldFont) oldFont = ::SelectObject(dc, oldFont);

        RECT tr = rc;
        ::DrawTextW(dc, text, -1, &tr,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (oldFont) ::SelectObject(dc, oldFont);

        if ((dis->itemState & ODS_FOCUS) && !disabled)
        {
            RECT fr = rc;
            ::InflateRect(&fr, -3, -3);
            ::DrawFocusRect(dc, &fr);
        }
    }

    void DrawCheckBox(const DRAWITEMSTRUCT* dis)
    {
        if (!dis) return;
        const ThemeColors& c = C();
        HDC dc = dis->hDC;
        RECT rc = dis->rcItem;

        const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
        const bool checked  = ::SendMessageW(dis->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;

        Fill(dc, rc, c.dlgBg);

        HGDIOBJ oldFont = (HGDIOBJ)::SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
        if (oldFont) oldFont = ::SelectObject(dc, oldFont);

        TEXTMETRICW tm{};
        ::GetTextMetricsW(dc, &tm);
        const int box = ScaleFor(dis->hwndItem, 13);
        const int top = rc.top + ((rc.bottom - rc.top) - box) / 2;

        RECT br = { rc.left, top, rc.left + box, top + box };
        Fill(dc, br, c.btnBorder);
        RECT bi = br;
        ::InflateRect(&bi, -1, -1);
        Fill(dc, bi, c.dlgEditBg);
        if (checked) DrawCheckMark(dc, bi, disabled ? c.btnTextOff : c.pvFill);

        wchar_t text[512];
        text[0] = 0;
        ::GetWindowTextW(dis->hwndItem, text, (int)_countof(text));

        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, disabled ? c.btnTextOff : c.dlgText);
        RECT tr = rc;
        tr.left = br.right + ScaleFor(dis->hwndItem, 6);
        ::DrawTextW(dc, text, -1, &tr,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (oldFont) ::SelectObject(dc, oldFont);

        if ((dis->itemState & ODS_FOCUS) && !disabled)
        {
            RECT fr = rc;
            ::InflateRect(&fr, -1, -1);
            ::DrawFocusRect(dc, &fr);
        }
    }
}
