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
// Process.h - child process execution with streamed output capture
// ---------------------------------------------------------------------------
#pragma once

#include "Utf.h"

#include <string>
#include <vector>
#include <functional>

// Cancellation token: a manual reset event. When it is signalled the child
// process is terminated as soon as possible.
struct CancelToken
{
    HANDLE ev = nullptr;

    bool IsCancelled() const
    {
        return ev != nullptr && ::WaitForSingleObject(ev, 0) == WAIT_OBJECT_0;
    }
};

struct ProcessResult
{
    bool      started   = false;
    bool      cancelled = false;
    DWORD     exitCode  = (DWORD)-1;
    long long elapsedMs = 0;
    std::string output;     // merged stdout + stderr (raw bytes, usually UTF-8)
};

// Runs `exePath` with the given (already quoted) argument string.
// Every line of merged stdout/stderr is forwarded to `onLine` (may be null).
// Returns true when the process could be started (result.exitCode holds the
// exit code; 0 == success).
bool RunProcess(const std::wstring& exePath,
                const std::wstring& args,
                const std::wstring& workDir,
                const CancelToken&  cancel,
                const std::function<void(const std::string&)>& onLine,
                ProcessResult& result);

// Convenience: run and collect all output, no per line callback.
bool RunProcessCapture(const std::wstring& exePath,
                       const std::wstring& args,
                       const std::wstring& workDir,
                       const CancelToken&  cancel,
                       ProcessResult& result);

// Splits a raw output buffer into lines ('\r' and '\n' both act as separators,
// empty lines are dropped). Useful for parsing ffmpeg output.
std::vector<std::string> SplitOutputLines(const std::string& text);