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
// Process.cpp - child process execution with streamed output capture
// ---------------------------------------------------------------------------
#include "Process.h"

#include <vector>
#include <cstring>

namespace
{
    void AppendAndSplit(const std::string& chunk,
                        std::string& pending,
                        const std::function<void(const std::string&)>& onLine)
    {
        for (size_t i = 0; i < chunk.size(); ++i)
        {
            char c = chunk[i];
            if (c == '\n' || c == '\r')
            {
                if (!pending.empty())
                {
                    if (onLine) onLine(pending);
                    pending.clear();
                }
            }
            else
            {
                pending.push_back(c);
            }
        }
    }
}

std::vector<std::string> SplitOutputLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if (c == '\n' || c == '\r')
        {
            if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

bool RunProcess(const std::wstring& exePath,
                const std::wstring& args,
                const std::wstring& workDir,
                const CancelToken&  cancel,
                const std::function<void(const std::string&)>& onLine,
                ProcessResult& result)
{
    result = ProcessResult();

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!::CreatePipe(&rd, &wr, &sa, 256 * 1024))
        return false;
    ::SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    // stdin: give the child a valid handle to NUL (GUI apps have no console)
    HANDLE nul = ::CreateFileW(L"NUL", GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                               OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si;
    ::ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;

    PROCESS_INFORMATION pi;
    ::ZeroMemory(&pi, sizeof(pi));

    std::wstring cmdLine = QuoteArg(exePath);
    if (!args.empty())
    {
        cmdLine.push_back(L' ');
        cmdLine += args;
    }
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    BOOL created = ::CreateProcessW(nullptr, &cmdBuf[0], nullptr, nullptr, TRUE,
                                    CREATE_NO_WINDOW,
                                    nullptr,
                                    workDir.empty() ? nullptr : workDir.c_str(),
                                    &si, &pi);

    if (wr) { ::CloseHandle(wr); wr = nullptr; }
    if (nul) { ::CloseHandle(nul); nul = nullptr; }

    if (!created)
    {
        ::CloseHandle(rd);
        return false;
    }

    result.started = true;
    ::CloseHandle(pi.hThread);

    DWORD startTick = ::GetTickCount();
    std::string pending;
    char buffer[16384];
    bool exited = false;

    for (;;)
    {
        DWORD avail = 0;
        BOOL peekOk = ::PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr);

        if (peekOk && avail > 0)
        {
            DWORD got = 0;
            if (!::ReadFile(rd, buffer, (DWORD)sizeof(buffer), &got, nullptr) || got == 0)
                break;
            std::string chunk(buffer, got);
            result.output += chunk;
            AppendAndSplit(chunk, pending, onLine);
            continue;
        }

        if (!peekOk)
            break;                      // broken pipe: every writer closed

        if (!exited && ::WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
            exited = true;

        if (exited)
            break;                      // drained everything we could get

        if (cancel.IsCancelled())
        {
            result.cancelled = true;
            ::TerminateProcess(pi.hProcess, 1);
            break;
        }

        ::Sleep(10);
    }

    if (!pending.empty() && onLine) onLine(pending);

    if (!exited) ::WaitForSingleObject(pi.hProcess, 5000);

    DWORD code = (DWORD)-1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    result.exitCode = code;
    result.elapsedMs = (long long)(::GetTickCount() - startTick);

    ::CloseHandle(pi.hProcess);
    ::CloseHandle(rd);
    return true;
}

bool RunProcessCapture(const std::wstring& exePath,
                       const std::wstring& args,
                       const std::wstring& workDir,
                       const CancelToken&  cancel,
                       ProcessResult& result)
{
    return RunProcess(exePath, args, workDir, cancel,
                      std::function<void(const std::string&)>(), result);
}