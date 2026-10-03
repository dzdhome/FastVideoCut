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
// SettingsDialog.h - modal settings dialog (created from an in-memory template)
// ---------------------------------------------------------------------------
#pragma once

#include "Utf.h"
#include "Settings.h"
#include "Ffmpeg.h"

#include <windows.h>

// Shows the modal settings dialog. Returns true when the user pressed OK and
// settings were modified.
bool ShowSettingsDialogModal(HWND owner, AppSettings& settings, Ffmpeg& ffmpeg);