// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QVBoxLayout>
#include <QWidget>

#include "Core/Core.h"

class QCheckBox;
class QComboBox;

class LogitechMicWindow : public QWidget
{
  Q_OBJECT
public:
  explicit LogitechMicWindow(QWidget* parent = nullptr);

private:
  void CreateMainWindow();
  void CreateCheckboxGroup(QVBoxLayout* main_layout);
  void CreateMicrophoneConfigurationGroup(QVBoxLayout* main_layout);
  void OnEmulationStateChanged(Core::State state);
  void EmulateLogitechMic(u8 index, bool emulate);
  void OnInputDeviceChange(u8 index);
  void OnMuteChange(u8 index, bool muted);

  std::array<QCheckBox*, 4> m_checkbox_mic_enabled;
  std::array<QCheckBox*, 4> m_checkbox_mic_muted;
  std::array<QComboBox*, 4> m_combobox_microphone;
};
