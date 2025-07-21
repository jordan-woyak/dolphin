// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class QObject;
class QRect;

namespace QtUtils
{

QObject* ClipCursor(QObject* obj, const QRect& rect);

}  // namespace QtUtils
