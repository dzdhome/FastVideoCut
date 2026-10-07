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
#include "Loc.h"
#include "Sound.h"
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

    // 所有可见文字都在这里按当前界面语言重写一遍。对话框模板里写死的是中文，
    // 切到英文时靠这些 SetDlgItemText 覆盖过去（所以模板里的 LTEXT 必须有 ID）。
    void ApplyTexts(HWND dlg)
    {
        // 标题栏来自资源模板里的 CAPTION，这里按语言改写
        ::SetWindowTextW(dlg, TR(L"FastVideoCut 设置", L"FastVideoCut Settings"));

        SetText(dlg, IDC_SET_LB_FFDIR,    TR(L"ffmpeg 目录", L"ffmpeg folder"));
        SetText(dlg, IDC_SET_LB_OUTDIR,   TR(L"输出目录", L"Output folder"));
        SetText(dlg, IDC_SET_LB_UI,       TR(L"---- 界面 ----", L"---- Interface ----"));
        SetText(dlg, IDC_SET_LB_LANG,     TR(L"界面语言", L"Language"));
        SetText(dlg, IDC_SET_LB_DETECT,   TR(L"---- 黑屏检测 blackdetect ----",
                                             L"---- Black detect (blackdetect) ----"));
        SetText(dlg, IDC_SET_LB_MINDUR,   TR(L"最短时长(秒)", L"Min length (s)"));
        SetText(dlg, IDC_SET_LB_PIXTH,    TR(L"像素阈值 pix_th", L"Pixel threshold pix_th"));
        SetText(dlg, IDC_SET_LB_PICTH,    TR(L"比例阈值 pic_th", L"Picture threshold pic_th"));
        SetText(dlg, IDC_SET_LB_THUMBH,   TR(L"缩略图高(像素)", L"Thumbnail height (px)"));
        SetText(dlg, IDC_SET_LB_HEADSCAN, TR(L"片头范围(秒)", L"Head range (s)"));
        SetText(dlg, IDC_SET_LB_TAILSCAN, TR(L"片尾范围(秒)", L"Tail range (s)"));
        SetText(dlg, IDC_SET_LB_SCANHINT,
                TR(L"片头从视频开头算起，片尾从视频结尾倒退；两段重叠或相接时自动合并成一段扫描",
                   L"Head counts from the start, tail counts backwards from the end; "
                   L"overlapping or touching ranges are merged into one scan"));
        SetText(dlg, IDC_SET_LB_EXPORT,   TR(L"---- 导出参数 ----", L"---- Export ----"));
        SetText(dlg, IDC_SET_LB_CRF,      L"CRF");
        SetText(dlg, IDC_SET_LB_PRESET,   L"preset");
        SetText(dlg, IDC_SET_LB_SOUND,
                TR(L"---- 提示音 ----", L"---- Sounds ----"));

        SetText(dlg, IDC_SET_FFBROWSE,  TR(L"浏览...", L"Browse..."));
        SetText(dlg, IDC_SET_OUTBROWSE, TR(L"浏览...", L"Browse..."));
        SetText(dlg, IDC_SET_RESET,     TR(L"恢复默认", L"Defaults"));
        SetText(dlg, IDOK,              TR(L"确定", L"OK"));
        SetText(dlg, IDCANCEL,          TR(L"取消", L"Cancel"));

        SetText(dlg, IDC_SET_THUMBS,
                TR(L"生成视频流缩略图（不勾选时只检测黑屏，不抽帧，更快）",
                   L"Build timeline thumbnails (off = black detection only, faster)"));
        SetText(dlg, IDC_SET_REENC,
                TR(L"导出时重新编码（默认关闭 = ffmpeg 无损流复制）",
                   L"Re-encode on export (off = lossless stream copy)"));
        SetText(dlg, IDC_SET_FASTSTART,
                TR(L"mp4 输出加 +faststart（网络快启，略微增加耗时）",
                   L"Add +faststart to mp4 (faster start, slightly slower)"));
        SetText(dlg, IDC_SET_CONFIRM,
                TR(L"导出完成后询问是否打开输出文件夹",
                   L"Ask to open the output folder when finished"));
        SetText(dlg, IDC_SET_SOUNDDETECT,
                TR(L"分析完成后响“叮铃铃”（全部视频分析完时）",
                   L"Ring \"ding-ding-ding\" when analysis finishes"));
        SetText(dlg, IDC_SET_SOUNDEXPORT,
                TR(L"导出完成后响“叮咚咚”（文件导出完时）",
                   L"Ring \"ding-dong\" when the export finishes"));
    }

    void SelectLang(HWND dlg, AppLang lang)
    {
        HWND cb = ::GetDlgItem(dlg, IDC_SET_LANG);
        if (!cb) return;
        // CBS_DROPDOWNLIST 上 SetWindowText 不会改当前项，必须用 CB_SETCURSEL
        int sel = (lang == AppLang::Chinese) ? 1
                : (lang == AppLang::English) ? 2 : 0;
        ::SendMessageW(cb, CB_SETCURSEL, (WPARAM)sel, 0);
    }

    void FillControls(HWND dlg, const AppSettings& s)
    {
        ApplyTexts(dlg);
        // 语言下拉框：三项顺序与 AppLang 的取值一致（0 跟随系统 / 1 简中 / 2 英文）
        if (HWND cb = ::GetDlgItem(dlg, IDC_SET_LANG))
        {
            ::SendMessageW(cb, CB_RESETCONTENT, 0, 0);
            for (int i = 0; i <= 2; ++i)
                ::SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)Loc::LangName((AppLang)i));
            SelectLang(dlg, s.lang);
        }
        SetText(dlg, IDC_SET_FFDIR, s.ffmpegDir);
        SetText(dlg, IDC_SET_OUTDIR, s.outputDir);
        SetText(dlg, IDC_SET_MINDUR, NumberText(s.blackMinDuration, 2));
        SetText(dlg, IDC_SET_PIXTH, NumberText(s.blackPixTh, 2));
        SetText(dlg, IDC_SET_PICTH, NumberText(s.blackPicTh, 2));
        SetText(dlg, IDC_SET_HEADSTART, NumberText(s.blackHeadStart, 0));
        SetText(dlg, IDC_SET_HEADSCAN, NumberText(s.blackHeadEnd, 0));
        SetText(dlg, IDC_SET_TAILSCAN, NumberText(s.blackTailBackMax, 0));
        SetText(dlg, IDC_SET_TAILBACKMIN, NumberText(s.blackTailBackMin, 0));
        ::CheckDlgButton(dlg, IDC_SET_THUMBS, s.makeThumbs ? BST_CHECKED : BST_UNCHECKED);
        SetText(dlg, IDC_SET_THUMBH, FormatString(L"%d", s.thumbHeight));
        SetText(dlg, IDC_SET_CRF, FormatString(L"%d", s.crf));
        SetText(dlg, IDC_SET_PRESET, Utf8ToWide(s.preset));
        ::CheckDlgButton(dlg, IDC_SET_REENC, s.reencodeExport ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_SET_FASTSTART, s.faststart ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_SET_CONFIRM, s.confirmBeforeExport ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_SET_SOUNDDETECT, s.soundDetectDone ? BST_CHECKED : BST_UNCHECKED);
        ::CheckDlgButton(dlg, IDC_SET_SOUNDEXPORT, s.soundExportDone ? BST_CHECKED : BST_UNCHECKED);
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
                std::wstring d = PickFolder(dlg, TR(L"选择 ffmpeg 的 bin 目录（包含 ffmpeg.exe / ffprobe.exe）",
                                                    L"Pick the ffmpeg bin folder (ffmpeg.exe / ffprobe.exe)"));
                if (!d.empty()) SetText(dlg, IDC_SET_FFDIR, d);
                return TRUE;
            }
            case IDC_SET_OUTBROWSE:
            {
                std::wstring d = PickFolder(dlg, TR(L"选择导出目录", L"Pick the output folder"));
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
            // 勾上提示音就当场响一声：整个程序里只有这里能试听，
            // 否则用户只能等到任务跑完才知道自己打开的是什么声音。
            case IDC_SET_SOUNDDETECT:
                if (::IsDlgButtonChecked(dlg, IDC_SET_SOUNDDETECT) == BST_CHECKED)
                    Sound::PlayDetectDone();
                return TRUE;
            case IDC_SET_SOUNDEXPORT:
                if (::IsDlgButtonChecked(dlg, IDC_SET_SOUNDEXPORT) == BST_CHECKED)
                    Sound::PlayExportDone();
                return TRUE;
            case IDOK:
            {
                if (!s) { ::EndDialog(dlg, IDCANCEL); return TRUE; }

                s->ffmpegDir = GetText(dlg, IDC_SET_FFDIR);
                s->outputDir = GetText(dlg, IDC_SET_OUTDIR);
                // 合并文件名已改为按首尾视频自动生成，这里不再有输入框

                s->blackMinDuration = GetDouble(dlg, IDC_SET_MINDUR, s->blackMinDuration);
                s->blackPixTh       = GetDouble(dlg, IDC_SET_PIXTH, s->blackPixTh);
                s->blackPicTh       = GetDouble(dlg, IDC_SET_PICTH, s->blackPicTh);
                s->blackHeadStart   = GetInt(dlg, IDC_SET_HEADSTART, s->blackHeadStart);
                s->blackHeadEnd     = GetInt(dlg, IDC_SET_HEADSCAN, s->blackHeadEnd);
                s->blackTailBackMax = GetInt(dlg, IDC_SET_TAILSCAN, s->blackTailBackMax);
                s->blackTailBackMin = GetInt(dlg, IDC_SET_TAILBACKMIN, s->blackTailBackMin);
                // 起始 / 倒退最小必须 >= 0（输入框限定了数字，这里兜底）；
                // 片头结束 / 倒退最大允许负数 = 该侧不限制（兼容手改 INI）。
                if (s->blackHeadStart < 0) s->blackHeadStart = 0;
                if (s->blackTailBackMin < 0) s->blackTailBackMin = 0;
                // 区间必须自洽。相等 = 该侧不扫（合法）；两个范围都空 = 没有任何
                // 区域可扫，与其默默扫出 0 段黑屏，不如直接拦下来告诉用户怎么填。
                bool headBad = (s->blackHeadEnd >= 0) &&
                               (s->blackHeadStart > s->blackHeadEnd);
                bool tailBad = (s->blackTailBackMax >= 0) &&
                               (s->blackTailBackMax < s->blackTailBackMin);
                bool headEmpty = (s->blackHeadEnd >= 0) &&
                                 (s->blackHeadStart >= s->blackHeadEnd);
                bool tailEmpty = (s->blackTailBackMax >= 0) &&
                                 (s->blackTailBackMax <= s->blackTailBackMin);
                if (headBad || tailBad || (headEmpty && tailEmpty))
                {
                    ::MessageBoxW(dlg,
                        TR(L"扫描范围填写有误，没有任何区域会被检测。\n\n"
                           L"· 片头范围：起始 <= 结束（起始填 0 = 从视频开头扫）\n"
                           L"· 片尾范围：倒退最大 >= 倒退最小（倒退最小填 0 = 扫到片尾）\n"
                           L"· 两侧范围不能同时为空（相等 = 该侧不扫）\n"
                           L"· 两段范围重叠时会自动合并成一段，中间不会有漏扫",
                           L"Invalid scan range - nothing would be scanned.\n\n"
                           L"- Head range: start <= end (start 0 = from the beginning)\n"
                           L"- Tail range: back max >= back min (back min 0 = to the end)\n"
                           L"- The two ranges must not both be empty (equal = that side is skipped)\n"
                           L"- Overlapping ranges are merged into one scan, nothing is missed"),
                        TR(L"FastVideoCut 设置", L"FastVideoCut Settings"),
                        MB_ICONWARNING | MB_OK);
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
                s->makeThumbs          = ::IsDlgButtonChecked(dlg, IDC_SET_THUMBS) == BST_CHECKED;
                s->soundDetectDone     = ::IsDlgButtonChecked(dlg, IDC_SET_SOUNDDETECT) == BST_CHECKED;
                s->soundExportDone     = ::IsDlgButtonChecked(dlg, IDC_SET_SOUNDEXPORT) == BST_CHECKED;

                // 语言：下拉框里的 3 项顺序与 AppLang 的取值一致
                HWND cb = ::GetDlgItem(dlg, IDC_SET_LANG);
                if (cb)
                {
                    int sel = (int)::SendMessageW(cb, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel <= 2) s->lang = (AppLang)sel;
                }

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
