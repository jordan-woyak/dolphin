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
  ~LogitechMicWindow() override;

private:
  void CreateMainWindow();
  void CreateCheckboxGroup(QVBoxLayout* main_layout);
  void CreateMicrophoneConfigurationGroup(QVBoxLayout* main_layout);
  void OnEmulationStateChanged(Core::State state);
  void EmulateLogitechMic1(bool emulate);
  void EmulateLogitechMic2(bool emulate);
  void EmulateLogitechMic3(bool emulate);
  void EmulateLogitechMic4(bool emulate);
  void OnInputDevice1Change();
  void OnInputDevice2Change();
  void OnInputDevice3Change();
  void OnInputDevice4Change();

  QCheckBox* m_checkbox_mic_1_enabled;
  QCheckBox* m_checkbox_mic_2_enabled;
  QCheckBox* m_checkbox_mic_3_enabled;
  QCheckBox* m_checkbox_mic_4_enabled;
  QComboBox* m_combobox_1_microphones;
  QComboBox* m_combobox_2_microphones;
  QComboBox* m_combobox_3_microphones;
  QComboBox* m_combobox_4_microphones;
};
