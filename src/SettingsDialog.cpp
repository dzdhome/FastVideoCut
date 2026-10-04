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
// SettingsDialog.cpp - modal settings dialog built from an in-memory template
// ---------------------------------------------------------------------------
#include "SettingsDialog.h"
#include "resource.h"

#include <shlobj.h>
#include <cstdlib>
#include <cwchar>
#include <vector>

namespace
{
    std::wstring GetText(HWND dlg, int id)
    {
        wchar_t buf[2048];
        buf[0] = 0;
        ::GetDlgItemTextW(dlg, id, buf, (int)_countof(buf));
        return std::wstring(buf);
    }
    void SetText(HWND dlg, int id, const std::wstring& s)
    {
        ::SetDlgItemTextW(dlg, id, s.c_str());
    }
    double GetDouble(HWND dlg, int id, double def)
    {
        std::wstring s = GetText(dlg, id);
        if (s.empty()) return def;
        wchar_t* endp = nullptr;
        double v = wcstod(s.c_str(), &endp);
        if (endp == s.c_str()) return def;
        return v;
    }
    int GetInt(HWND dlg, int id, int def)
    {
        std::wstring s = GetText(dlg, id);
        if (s.empty()) return def;
        return _wtoi(s.c_str());
    }
    std::wstring PickFolder(HWND owner, const wchar_t* title)
    {
        BROWSEINFOW bi;
        ::ZeroMemory(&bi, sizeof(bi));
        bi.hwndOwner = owner;
        bi.lpszTitle = title;
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
        LPITEMIDLIST pidl = ::SHBrowseForFolderW(&bi);
        if (!pidl) return std::wstring();
        wchar_t path[MAX_PATH * 2];
        bool ok = ::SHGetPathFromIDListW(pidl, path) != FALSE;
        ::CoTaskMemFree(pidl);
        return ok ? std::wstring(path) : std::wstring();
    }

    void FillControls(HWND dlg, const AppSettings& s)
    {
        SetText(dlg, IDC_SET_FFDIR, s.ffmpegDir);
        SetText(dlg, IDC_SET_OUTDIR, s.outputDir);
        SetText(dlg, IDC_SET_MINDUR, NumberText(s.blackMinDuration, 2));
        SetText(dlg, IDC_SET_PIXTH, NumberText(s.blackPixTh, 2));
        SetText(dlg, IDC_SET_PICTH, NumberText(s.blackPicTh, 2));
        SetText(dlg, IDC_SET_HEADSCAN, NumberText(s.blackHeadScan, 0));
        SetText(dlg, IDC_SET_TAILSCAN, NumberText(s.blackTailScan, 0));
        SetText(dlg, IDC_SET_THUMBH, FormatString(L"%d", s.thumbHeight));
        SetText(dlg, IDC_SET_CRF, FormatString(L"%d", s.crf));
        SetText(dlg, IDC_SET_PRESET, Utf8ToWide(s.preset));
        ::CheckDlgButton(dlg, IDC_SET_REENC, s.reencodeExport ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_SET_FASTSTART, s.faststart ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_SET_CONFIRM, s.confirmBeforeExport ? BST_CHECKED : BST_UNCHECKED);
    }

    INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
    {
        AppSettings* s = (AppSettings*)::GetWindowLongPtrW(dlg, DWLP_USER);

        switch (msg)
        {
        case WM_INITDIALOG:
            s = (AppSettings*)lp;
            ::SetWindowLongPtrW(dlg, DWLP_USER, (LONG_PTR)s);
            if (s) FillControls(dlg, *s);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wp))
            {
            case IDC_SET_FFBROWSE:
            {
                std::wstring d = PickFolder(dlg, L"选择 ffmpeg 的 bin 目录（包含 ffmpeg.exe / ffprobe.exe）");
                if (!d.empty()) SetText(dlg, IDC_SET_FFDIR, d);
                return TRUE;
            }
            case IDC_SET_OUTBROWSE:
            {
                std::wstring d = PickFolder(dlg, L"选择导出目录");
                if (!d.empty()) SetText(dlg, IDC_SET_OUTDIR, d);
                return TRUE;
            }
            case IDC_SET_RESET:
            {
                AppSettings def;
                if (s)
                {
                    def.ffmpegDir = s->ffmpegDir;
                    def.outputDir = s->outputDir;
                }
                FillControls(dlg, def);
                return TRUE;
            }
            case IDOK:
            {
                if (!s) { ::EndDialog(dlg, IDCANCEL); return TRUE; }

                s->ffmpegDir = GetText(dlg, IDC_SET_FFDIR);
                s->outputDir = GetText(dlg, IDC_SET_OUTDIR);
                // 合并文件名已改为按首尾视频自动生成，这里不再有输入框

                s->blackMinDuration = GetDouble(dlg, IDC_SET_MINDUR, s->blackMinDuration);
                s->blackPixTh       = GetDouble(dlg, IDC_SET_PIXTH, s->blackPixTh);
                s->blackPicTh       = GetDouble(dlg, IDC_SET_PICTH, s->blackPicTh);
                s->blackHeadScan    = GetDouble(dlg, IDC_SET_HEADSCAN, s->blackHeadScan);
                s->blackTailScan    = GetDouble(dlg, IDC_SET_TAILSCAN, s->blackTailScan);
                // 0 = 该侧完全不扫，负数 = 该侧不限制。两侧都是 0 就没有任何区域可扫，
                // 与其默默扫出 0 段黑屏，不如直接拦下来告诉用户怎么填。
                if (s->blackHeadScan == 0.0 && s->blackTailScan == 0.0)
                {
                    ::MessageBoxW(dlg,
                        L"“只扫片头”和“只扫片尾”不能同时填 0，那样没有任何区域会被检测。\n\n"
                        L"· 想只扫一侧：另一侧填 0（例如片头 180 / 片尾 0 = 只扫前 180 秒）\n"
                        L"· 想整段检测：任意一侧填负数（例如 -1 = 该侧不限制）",
                        L"FastVideoCut 设置", MB_ICONWARNING | MB_OK);
                    return TRUE;
                }
                if (s->blackMinDuration < 0.0) s->blackMinDuration = 0.0;
                if (s->blackPixTh < 0.0) s->blackPixTh = 0.0;
                if (s->blackPicTh < 0.0) s->blackPicTh = 0.0;

                int th = GetInt(dlg, IDC_SET_THUMBH, s->thumbHeight);
                s->thumbHeight = th < 24 ? 24 : (th > 240 ? 240 : th);

                int crf = GetInt(dlg, IDC_SET_CRF, s->crf);
                s->crf = crf < 0 ? 0 : (crf > 51 ? 51 : crf);

                std::wstring preset = GetText(dlg, IDC_SET_PRESET);
                if (!preset.empty()) s->preset = WideToAnsi(preset);

                s->reencodeExport      = ::IsDlgButtonChecked(dlg, IDC_SET_REENC) == BST_CHECKED;
                s->faststart           = ::IsDlgButtonChecked(dlg, IDC_SET_FASTSTART) == BST_CHECKED;
                s->confirmBeforeExport = ::IsDlgButtonChecked(dlg, IDC_SET_CONFIRM) == BST_CHECKED;

                ::EndDialog(dlg, IDOK);
                return TRUE;
            }
            case IDCANCEL:
                ::EndDialog(dlg, IDCANCEL);
                return TRUE;
            default:
                break;
            }
            break;
        }
        return FALSE;
    }
}

// The dialog itself lives in src/settings.rc - building the binary template by
// hand is error prone (DLGITEMTEMPLATE field widths), so we load it from the
// resource like any normal dialog.
bool ShowSettingsDialogModal(HWND owner, AppSettings& settings, Ffmpeg& ffmpeg)
{
    (void)ffmpeg;

    HINSTANCE inst = ::GetModuleHandleW(nullptr);
    INT_PTR r = ::DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETTINGS), owner,
                                  SettingsProc, (LPARAM)&settings);
    return r == IDOK;
}
