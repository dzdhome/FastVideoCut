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
// Loc.cpp - language detection / activation for the TR() macro
// ---------------------------------------------------------------------------
#include "Loc.h"
#include "Utf.h"

#include <windows.h>
#include <cstdlib>

namespace
{
    AppLang g_configured = AppLang::Auto;
    AppLang g_current    = AppLang::Chinese;
}

namespace Loc
{

AppLang Detect()
{
    // GetUserDefaultLocaleName 返回形如 "zh-CN" / "en-US" 的 BCP-47 名字。
    // 这里只区分“中文”与“其它”：其它语言先回落到英文界面，
    // 免得给非中文用户弹出一屏看不懂的提示。
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = { 0 };
    int n = ::GetUserDefaultLocaleName(name, (int)_countof(name));
    if (n > 0 && (name[0] == L'z' || name[0] == L'Z') &&
        (name[1] == L'h' || name[1] == L'H'))
    {
        return AppLang::Chinese;
    }
    return AppLang::English;
}

AppLang Resolve(AppLang configured)
{
    if (configured == AppLang::Chinese || configured == AppLang::English)
        return configured;
    return Detect();
}

void Apply(AppLang configured)
{
    g_configured = configured;
    g_current    = Resolve(configured);
}

AppLang Configured() { return g_configured; }
AppLang Current()   { return g_current; }
bool   IsEnglish()  { return g_current == AppLang::English; }

const wchar_t* LangName(AppLang l)
{
    switch (l)
    {
    case AppLang::Chinese: return L"简体中文";
    case AppLang::English: return L"English";
    default: break;
    }
    return TR(L"跟随系统", L"Follow system");
}

std::wstring Describe(AppLang configured)
{
    const wchar_t* eff = (Resolve(configured) == AppLang::Chinese)
                           ? LangName(AppLang::Chinese)
                           : LangName(AppLang::English);
    if (configured == AppLang::Auto)
        return FormatString(TR(L"跟随系统（当前：%s）", L"Follow system (current: %s)"), eff);
    return FormatString(L"%s", eff);
}

}   // namespace Loc