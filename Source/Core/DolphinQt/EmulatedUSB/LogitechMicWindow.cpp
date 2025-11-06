// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/EmulatedUSB/LogitechMicWindow.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QString>

#ifdef HAVE_CUBEB
#include "AudioCommon/CubebUtils.h"
#endif
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/System.h"
#include "DolphinQt/Resources.h"
#include "DolphinQt/Settings.h"

LogitechMicWindow::LogitechMicWindow(QWidget* parent) : QWidget(parent)
{
  setWindowTitle(tr("Logitech USB Microphone Manager"));
  setWindowIcon(Resources::GetAppIcon());
  setObjectName(QStringLiteral("logitech_mic_manager"));
  setMinimumSize(QSize(700, 200));

  CreateMainWindow();

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this,
          &LogitechMicWindow::OnEmulationStateChanged);

  OnEmulationStateChanged(Core::GetState(Core::System::GetInstance()));
}

LogitechMicWindow::~LogitechMicWindow() = default;

void LogitechMicWindow::CreateMainWindow()
{
  auto* main_layout = new QVBoxLayout();
  auto* label = new QLabel();
  label->setText(QStringLiteral("<center><i>%1</i></center>")
                     .arg(tr("Some settings cannot be changed when emulation is running.")));
  main_layout->addWidget(label);

  CreateCheckboxGroup(main_layout);

  CreateMicrophoneConfigurationGroup(main_layout);

  setLayout(main_layout);
}

void LogitechMicWindow::CreateCheckboxGroup(QVBoxLayout* main_layout)
{
  auto* checkbox_group = new QGroupBox();
  auto* checkbox_layout = new QHBoxLayout();
  checkbox_layout->setAlignment(Qt::AlignHCenter);

  m_checkbox_mic_1_enabled = new QCheckBox(tr("Emulate Logitech USB Mic 1"), this);
  m_checkbox_mic_1_enabled->setChecked(Config::Get(Config::MAIN_EMULATE_LOGITECH_MIC_1));
  connect(m_checkbox_mic_1_enabled, &QCheckBox::toggled, this,
          &LogitechMicWindow::EmulateLogitechMic1);
  checkbox_layout->addWidget(m_checkbox_mic_1_enabled);

  m_checkbox_mic_2_enabled = new QCheckBox(tr("Emulate Logitech USB Mic 2"), this);
  m_checkbox_mic_2_enabled->setChecked(Config::Get(Config::MAIN_EMULATE_LOGITECH_MIC_2));
  connect(m_checkbox_mic_2_enabled, &QCheckBox::toggled, this,
          &LogitechMicWindow::EmulateLogitechMic2);
  checkbox_layout->addWidget(m_checkbox_mic_2_enabled);

  m_checkbox_mic_3_enabled = new QCheckBox(tr("Emulate Logitech USB Mic 3"), this);
  m_checkbox_mic_3_enabled->setChecked(Config::Get(Config::MAIN_EMULATE_LOGITECH_MIC_3));
  connect(m_checkbox_mic_3_enabled, &QCheckBox::toggled, this,
          &LogitechMicWindow::EmulateLogitechMic3);
  checkbox_layout->addWidget(m_checkbox_mic_3_enabled);

  m_checkbox_mic_4_enabled = new QCheckBox(tr("Emulate Logitech USB Mic 4"), this);
  m_checkbox_mic_4_enabled->setChecked(Config::Get(Config::MAIN_EMULATE_LOGITECH_MIC_4));
  connect(m_checkbox_mic_4_enabled, &QCheckBox::toggled, this,
          &LogitechMicWindow::EmulateLogitechMic4);
  checkbox_layout->addWidget(m_checkbox_mic_4_enabled);

  checkbox_group->setLayout(checkbox_layout);
  main_layout->addWidget(checkbox_group);
}

void LogitechMicWindow::CreateMicrophoneConfigurationGroup(QVBoxLayout* main_layout)
{
  auto* main_config_group = new QGroupBox(tr("Microphone Configuration"));
  auto* main_config_layout = new QVBoxLayout();

  for (u8 index = 0; index < 4; index++)
  {
    auto* config_group =
        new QGroupBox(tr(std::format("Microphone {} Configuration", index + 1).c_str()));
    auto* config_layout = new QHBoxLayout();

    auto checkbox_mic_muted = new QCheckBox(tr("Mute"), this);
    if (index == 0)
    {
      checkbox_mic_muted->setChecked(Settings::Instance().IsLogitechMic1Muted());
      connect(&Settings::Instance(), &Settings::LogitechMic1MuteChanged, checkbox_mic_muted,
              &QCheckBox::setChecked);
      connect(checkbox_mic_muted, &QCheckBox::toggled, &Settings::Instance(),
              &Settings::SetLogitechMic1Muted);
    }
    else if (index == 1)
    {
      checkbox_mic_muted->setChecked(Settings::Instance().IsLogitechMic2Muted());
      connect(&Settings::Instance(), &Settings::LogitechMic2MuteChanged, checkbox_mic_muted,
              &QCheckBox::setChecked);
      connect(checkbox_mic_muted, &QCheckBox::toggled, &Settings::Instance(),
              &Settings::SetLogitechMic2Muted);
    }
    else if (index == 2)
    {
      checkbox_mic_muted->setChecked(Settings::Instance().IsLogitechMic3Muted());
      connect(&Settings::Instance(), &Settings::LogitechMic3MuteChanged, checkbox_mic_muted,
              &QCheckBox::setChecked);
      connect(checkbox_mic_muted, &QCheckBox::toggled, &Settings::Instance(),
              &Settings::SetLogitechMic3Muted);
    }
    else if (index == 3)
    {
      checkbox_mic_muted->setChecked(Settings::Instance().IsLogitechMic4Muted());
      connect(&Settings::Instance(), &Settings::LogitechMic4MuteChanged, checkbox_mic_muted,
              &QCheckBox::setChecked);
      connect(checkbox_mic_muted, &QCheckBox::toggled, &Settings::Instance(),
              &Settings::SetLogitechMic4Muted);
    }
    checkbox_mic_muted->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    config_layout->addWidget(checkbox_mic_muted);

    static constexpr int FILTER_MIN = -50;
    static constexpr int FILTER_MAX = 50;

    const std::array<Config::Info<s16> const*, 4> volume_modifier_configs{
        &Config::MAIN_LOGITECH_MIC_1_VOLUME_MODIFIER, &Config::MAIN_LOGITECH_MIC_2_VOLUME_MODIFIER,
        &Config::MAIN_LOGITECH_MIC_3_VOLUME_MODIFIER, &Config::MAIN_LOGITECH_MIC_4_VOLUME_MODIFIER};

    auto* volume_layout = new QGridLayout();
    const int volume_modifier =
        std::clamp<int>(Config::Get(*volume_modifier_configs[index]), FILTER_MIN, FILTER_MAX);
    auto filter_slider = new QSlider(Qt::Horizontal, this);
    auto slider_label = new QLabel(tr("Volume modifier (value: %1dB)").arg(volume_modifier));
    connect(filter_slider, &QSlider::valueChanged, this,
            [slider_label, volume_modifier_configs, index](int value) {
              Config::SetBaseOrCurrent(*volume_modifier_configs[index], value);
              slider_label->setText(tr("Volume modifier (value: %1dB)").arg(value));
            });
    filter_slider->setMinimum(FILTER_MIN);
    filter_slider->setMaximum(FILTER_MAX);
    filter_slider->setValue(volume_modifier);
    filter_slider->setTickPosition(QSlider::TicksBothSides);
    filter_slider->setTickInterval(10);
    filter_slider->setSingleStep(1);
    volume_layout->addWidget(new QLabel(QStringLiteral("%1dB").arg(FILTER_MIN)), 0, 0,
                             Qt::AlignLeft);
    volume_layout->addWidget(slider_label, 0, 1, Qt::AlignCenter);
    volume_layout->addWidget(new QLabel(QStringLiteral("%1dB").arg(FILTER_MAX)), 0, 2,
                             Qt::AlignRight);
    volume_layout->addWidget(filter_slider, 1, 0, 1, 3);
    config_layout->addLayout(volume_layout);
    config_layout->setStretch(1, 3);

    const std::array<QComboBox**, 4> microphone_comboboxes{
        &m_combobox_1_microphones, &m_combobox_2_microphones, &m_combobox_3_microphones,
        &m_combobox_4_microphones};
    QComboBox** indexed_microphone_combobox = microphone_comboboxes[index];
    *indexed_microphone_combobox = new QComboBox();
#ifndef HAVE_CUBEB
    (*indexed_microphone_combobox)
        ->addItem(QLatin1String("(%1)").arg(tr("Audio backend unsupported")), QString{});
#else
    (*indexed_microphone_combobox)
        ->addItem(QLatin1String("(%1)").arg(tr("Autodetect preferred microphone")), QString{});
    for (auto& [device_id, device_name] : CubebUtils::ListInputDevices())
    {
      const auto user_data = QString::fromStdString(device_id);
      (*indexed_microphone_combobox)->addItem(QString::fromStdString(device_name), user_data);
    }
#endif
    if (index == 0)
      connect(*indexed_microphone_combobox, &QComboBox::currentIndexChanged, this,
              &LogitechMicWindow::OnInputDevice1Change);
    else if (index == 1)
      connect(*indexed_microphone_combobox, &QComboBox::currentIndexChanged, this,
              &LogitechMicWindow::OnInputDevice2Change);
    else if (index == 2)
      connect(*indexed_microphone_combobox, &QComboBox::currentIndexChanged, this,
              &LogitechMicWindow::OnInputDevice3Change);
    else if (index == 3)
      connect(*indexed_microphone_combobox, &QComboBox::currentIndexChanged, this,
              &LogitechMicWindow::OnInputDevice4Change);

    const std::array<Config::Info<std::string> const*, 4> microphone_configs{
        &Config::MAIN_LOGITECH_MIC_1_MICROPHONE, &Config::MAIN_LOGITECH_MIC_2_MICROPHONE,
        &Config::MAIN_LOGITECH_MIC_3_MICROPHONE, &Config::MAIN_LOGITECH_MIC_4_MICROPHONE};

    auto current_device_id = QString::fromStdString(Config::Get(*microphone_configs[index]));
    (*indexed_microphone_combobox)
        ->setCurrentIndex((*indexed_microphone_combobox)->findData(current_device_id));
    config_layout->addWidget(*indexed_microphone_combobox);

    config_group->setLayout(config_layout);
    main_config_layout->addWidget(config_group);
  }

  main_config_group->setLayout(main_config_layout);
  main_layout->addWidget(main_config_group);
}

void LogitechMicWindow::EmulateLogitechMic1(bool emulate)
{
  Config::SetBaseOrCurrent(Config::MAIN_EMULATE_LOGITECH_MIC_1, emulate);
}

void LogitechMicWindow::EmulateLogitechMic2(bool emulate)
{
  Config::SetBaseOrCurrent(Config::MAIN_EMULATE_LOGITECH_MIC_2, emulate);
}

void LogitechMicWindow::EmulateLogitechMic3(bool emulate)
{
  Config::SetBaseOrCurrent(Config::MAIN_EMULATE_LOGITECH_MIC_3, emulate);
}

void LogitechMicWindow::EmulateLogitechMic4(bool emulate)
{
  Config::SetBaseOrCurrent(Config::MAIN_EMULATE_LOGITECH_MIC_4, emulate);
}

void LogitechMicWindow::OnEmulationStateChanged(Core::State state)
{
  const bool running = state != Core::State::Uninitialized;

  m_checkbox_mic_1_enabled->setEnabled(!running);
  m_checkbox_mic_2_enabled->setEnabled(!running);
  m_checkbox_mic_3_enabled->setEnabled(!running);
  m_checkbox_mic_4_enabled->setEnabled(!running);
  m_combobox_1_microphones->setEnabled(!running);
  m_combobox_2_microphones->setEnabled(!running);
  m_combobox_3_microphones->setEnabled(!running);
  m_combobox_4_microphones->setEnabled(!running);
}

void LogitechMicWindow::OnInputDevice1Change()
{
  auto user_data_1 = m_combobox_1_microphones->currentData();
  if (user_data_1.isValid())
  {
    const std::string device_id = user_data_1.toString().toStdString();
    Config::SetBaseOrCurrent(Config::MAIN_LOGITECH_MIC_1_MICROPHONE, device_id);
  }
}

void LogitechMicWindow::OnInputDevice2Change()
{
  auto user_data_2 = m_combobox_2_microphones->currentData();
  if (user_data_2.isValid())
  {
    const std::string device_id = user_data_2.toString().toStdString();
    Config::SetBaseOrCurrent(Config::MAIN_LOGITECH_MIC_2_MICROPHONE, device_id);
  }
}

void LogitechMicWindow::OnInputDevice3Change()
{
  auto user_data_3 = m_combobox_3_microphones->currentData();
  if (user_data_3.isValid())
  {
    const std::string device_id = user_data_3.toString().toStdString();
    Config::SetBaseOrCurrent(Config::MAIN_LOGITECH_MIC_3_MICROPHONE, device_id);
  }
}

void LogitechMicWindow::OnInputDevice4Change()
{
  auto user_data_4 = m_combobox_4_microphones->currentData();
  if (user_data_4.isValid())
  {
    const std::string device_id = user_data_4.toString().toStdString();
    Config::SetBaseOrCurrent(Config::MAIN_LOGITECH_MIC_4_MICROPHONE, device_id);
  }
}
