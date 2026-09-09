// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Settings/AvalonDeckManager.h"

#include "DolphinQt/QtUtils/QtUtils.h"

AvalonDeckManager::AvalonDeckManager(QWidget* parent) : QDialog{parent}
{
  setWindowTitle(tr("Card Deck Manager"));

  QtUtils::AdjustSizeWithinScreen(this);
}
