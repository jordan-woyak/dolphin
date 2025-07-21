// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class QObject;
class QRect;
class QWidget;

namespace QtUtils
{

QObject* ClipCursor(QWidget* widget, const QRect& rect);

}  // namespace QtUtils
