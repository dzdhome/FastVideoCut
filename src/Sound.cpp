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
// Sound.cpp - 合成“叮铃铃 / 叮咚”并通过 waveOut 播放
//
// 做法：直接用正弦分音 + 指数衰减包络合成 PCM，高次分音衰减得更快，
// 听起来就是金属铃声，而不是电脑“滴”一声的那种方波。整段 PCM 只有一两秒，
// 一次性交给 waveOut 播完就关设备 —— 不长期占用音频设备，
// 也就不会和预览窗口的音频抢 WAVE_MAPPER。
// ---------------------------------------------------------------------------
#include "Sound.h"
#include "Utf.h"          // windows.h 就在这儿（mmsystem.h 依赖它）

#include <mmsystem.h>

#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#endif

namespace
{
    const int    kRate       = 44100;    // 采样率
    const int    kChannels   = 2;
    const int    kBlockAlign = kChannels * (int)sizeof(short);
    const double kPeak       = 0.25;     // 主音量：够听见又不刺耳
    const double kTwoPi      = 6.283185307179586;

    // 一次“敲击”：频率 Hz + 时长 ms
    struct Strike { double hz; double ms; };

    // 分析完成 -> 叮铃铃：上行大调三音（880 / 1046.5 / 1318.5 Hz）
    const Strike kDetect[] =
    {
        { 880.0,  240.0 },
        { 1046.5, 240.0 },
        { 1318.5, 300.0 },
    };
    // 导出完成 -> 叮咚：一声高（C6）一声低（G5），后一个音拖得长一些
    const Strike kExport[] =
    {
        { 1046.5, 320.0 },
        { 784.0,  620.0 },
    };
    const double kGapMs = 70.0;          // 两声之间的空隙

    // 把一串敲击混成单声道 s16 PCM。
    std::vector<short> Render(const Strike* strikes, int count)
    {
        // 分音（振幅, 衰减倍率）。衰减倍率越大，这一路分音掉得越快：
        // 起音瞬间以基频为主，尾音慢慢变暗 —— 这正是铃铛的听感。
        struct Partial { double amp; double fade; };
        const Partial kPartials[] =
        {
            { 1.00, 1.00 }, { 0.55, 1.70 }, { 0.30, 2.40 },
            { 0.16, 3.20 }, { 0.08, 4.20 },
        };
        const int nPartials = (int)(sizeof(kPartials) / sizeof(kPartials[0]));

        const double gap    = kGapMs * kRate / 1000.0;
        const double tailMs = 30.0;                      // 末尾留一点静音，收得自然
        double total = tailMs * kRate / 1000.0;
        for (int i = 0; i < count; ++i)
            total += strikes[i].ms * kRate / 1000.0 + (i ? gap : 0.0);
        if (total < 1.0) total = 1.0;

        std::vector<short> out((size_t)total, 0);
        const double attack  = 3.0 * kRate / 1000.0;     // 3 ms 起音，避免“咔”
        const double fadeOut = 300.0;                     // 收尾淡出，防止截断咔哒声

        double start = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const double len  = strikes[i].ms * kRate / 1000.0;
            const double tau  = (strikes[i].ms / 1000.0) / 3.0;   // 衰减时间常数
            const long   last = (long)len;
            for (long n = 0; n < last; ++n)
            {
                const double idx = start + n;
                if (idx >= total) break;

                const double t = (double)n / kRate;       // 秒
                double v = 0.0;
                for (int p = 0; p < nPartials; ++p)
                {
                    v += kPartials[p].amp *
                         std::exp(-t / (tau / kPartials[p].fade)) *
                         std::sin(kTwoPi * strikes[i].hz * t);
                }
                if (n < attack) v *= (double)n / attack;
                if (n > last - fadeOut) v *= (double)(last - n) / fadeOut;

                double s = v * kPeak * 32767.0;
                if (s >  32767.0) s =  32767.0;
                if (s < -32767.0) s = -32767.0;
                out[(size_t)idx] = (short)s;
            }
            start += len + gap;
        }
        return out;
    }

    // 单声道 PCM -> 立体声，交给 waveOut 一次播完。
    void PlayMono(const std::vector<short>& mono)
    {
        if (mono.empty()) return;

        WAVEFORMATEX wf;
        ::ZeroMemory(&wf, sizeof(wf));
        wf.wFormatTag      = WAVE_FORMAT_PCM;
        wf.nChannels       = kChannels;
        wf.nSamplesPerSec  = kRate;
        wf.wBitsPerSample  = 16;
        wf.nBlockAlign     = kBlockAlign;
        wf.nAvgBytesPerSec = kRate * kBlockAlign;

        HWAVEOUT dev = nullptr;
        // 这里刻意用 CALLBACK_NULL：实测有声卡会把 CALLBACK_EVENT 立刻置位，
        // 后面紧接着的 waveOutReset 会把声音掐掉（只剩几十毫秒的“哔”）。
        // 轮询 WHDR_DONE 是确定的，Preview.cpp 回收音频块也是这么做的。
        if (::waveOutOpen(&dev, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL)
            != MMSYSERR_NOERROR)
        {
            // 没有可用音频设备（无声卡 / 远程会话）：不出声，也不出错
            return;
        }

        std::vector<char> pcm(mono.size() * kBlockAlign);
        char* p = &pcm[0];
        for (size_t i = 0; i < mono.size(); ++i)
        {
            ::memcpy(p + i * kBlockAlign,     &mono[i], sizeof(short));
            ::memcpy(p + i * kBlockAlign + 2, &mono[i], sizeof(short));
        }

        WAVEHDR hdr;
        ::ZeroMemory(&hdr, sizeof(hdr));
        hdr.lpData        = &pcm[0];
        hdr.dwBufferLength = (DWORD)pcm.size();

        if (::waveOutPrepareHeader(dev, &hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
        {
            if (::waveOutWrite(dev, &hdr, sizeof(WAVEHDR)) == MMSYSERR_NOERROR)
            {
                // 等这块缓冲真的播完（正常一秒内）。上限 20 秒只是防驱动抽风。
                for (int waited = 0; waited < 2000 && !(hdr.dwFlags & WHDR_DONE); ++waited)
                    ::Sleep(10);
            }
            if (!(hdr.dwFlags & WHDR_DONE)) ::waveOutReset(dev);   // 异常兜底
            ::waveOutUnprepareHeader(dev, &hdr, sizeof(WAVEHDR));
        }
        ::waveOutClose(dev);
    }

    std::mutex  g_mutex;
    std::thread g_player;      // joinable = 还有提示音在响

    void PlayStrikes(const Strike* strikes, int count)
    {
        // 打开/关闭音频设备偶尔会抖一下，别在 UI 线程里做，丢后台线程播放。
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_player.joinable()) g_player.join();   // 上一次早该播完了
        g_player = std::thread([strikes, count] { PlayMono(Render(strikes, count)); });
    }
}   // namespace

void Sound::PlayDetectDone()
{
    PlayStrikes(kDetect, (int)(sizeof(kDetect) / sizeof(kDetect[0])));
}

void Sound::PlayExportDone()
{
    PlayStrikes(kExport, (int)(sizeof(kExport) / sizeof(kExport[0])));
}

void Sound::Shutdown()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_player.joinable()) g_player.join();
}