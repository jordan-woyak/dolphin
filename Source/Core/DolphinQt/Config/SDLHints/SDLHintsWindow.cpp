// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/SDLHints/SDLHintsWindow.h"

#include "Common/Config/Config.h"
#include "Core/Config/MainSettings.h"
#include "DolphinQt/Config/ToolTipControls/ToolTipCheckBox.h"
#include "DolphinQt/QtUtils/QtUtils.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

SDLHintsWindow::SDLHintsWindow(QWidget* parent) : QDialog(parent)
{
  CreateMainLayout();

  setWindowTitle(tr("SDL Controller Settings"));
}

QSize SDLHintsWindow::sizeHint() const
{
  return QSize(450, 0);
}

void SDLHintsWindow::CreateMainLayout()
{
  setMinimumWidth(300);
  setMinimumHeight(270);

  m_button_box = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(m_button_box, &QDialogButtonBox::rejected, this, &SDLHintsWindow::OnClose);

  // Create hints table
  m_hints_table = new QTableWidget(0, 2);
  QHeaderView* hints_table_header = m_hints_table->horizontalHeader();
  m_hints_table->setHorizontalHeaderLabels({tr("Name"), tr("Value")});
  m_hints_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_hints_table->setSelectionMode(QTableWidget::SingleSelection);
  hints_table_header->setSectionResizeMode(0, QHeaderView::Interactive);
  hints_table_header->setSectionResizeMode(1, QHeaderView::Fixed);
  hints_table_header->setMinimumSectionSize(60);
  m_hints_table->verticalHeader()->setVisible(false);
  hints_table_header->installEventFilter(this);
  QObject::connect(hints_table_header, &QHeaderView::sectionResized, this,
                   &SDLHintsWindow::SectionResized);

  PopulateTable();

  // Create table buttons
  auto* add_row_btn = new QPushButton(tr("Add"));
  connect(add_row_btn, &QPushButton::pressed, this, &SDLHintsWindow::AddRow);
  add_row_btn->setAutoDefault(false);
  add_row_btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  add_row_btn->setFixedWidth(70);
  add_row_btn->setMaximumWidth(70);
  add_row_btn->setMinimumWidth(70);
  add_row_btn->setMaximumHeight(35);

  m_rem_row_btn = new QPushButton(tr("Remove"));
  m_rem_row_btn->setEnabled(false);
  connect(m_rem_row_btn, &QPushButton::pressed, this, &SDLHintsWindow::RemoveRow);
  connect(m_hints_table, &QTableWidget::itemSelectionChanged, this,
          &SDLHintsWindow::SelectionChanged);
  m_rem_row_btn->setAutoDefault(false);
  m_rem_row_btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_rem_row_btn->setFixedWidth(70);
  m_rem_row_btn->setMaximumWidth(70);
  m_rem_row_btn->setMinimumWidth(70);
  m_rem_row_btn->setMaximumHeight(35);

  auto* btns_layout = new QHBoxLayout();
  btns_layout->setContentsMargins(0, 0, 2, 0);
  btns_layout->addStretch(1);
  btns_layout->addWidget(m_rem_row_btn, 0, Qt::AlignRight | Qt::AlignTop);
  btns_layout->addWidget(add_row_btn, 0, Qt::AlignRight | Qt::AlignTop);

  // Create advanced tab
  auto* advanced_layout = new QVBoxLayout();
  advanced_layout->addWidget(m_hints_table);
  advanced_layout->addLayout(btns_layout);

  auto* advanced_frame = new QFrame();
  advanced_frame->setLayout(advanced_layout);

  // Create default tab
  m_directinput_detection = new ToolTipCheckBox(tr("Enable DirectInput Detection"));
  m_directinput_detection->SetDescription(
      tr("Controls whether SDL should use DirectInput for detecting controllers. Enabling this "
         "fixes hotplug detection issues with DualSense controllers but causes Dolphin to hang up "
         "on shutdown when using certain 8BitDo controllers.<br><br><dolphin_emphasis>If unsure, "
         "leave this checked.</dolphin_emphasis>"));
  connect(m_directinput_detection, &ToolTipCheckBox::toggled, this,
          &SDLHintsWindow::DirectinputDetectionToggled);

  m_combine_joy_cons = new ToolTipCheckBox(tr("Use Joy-Con Pairs as a Single Controller"));
  m_combine_joy_cons->SetDescription(
      tr("Controls whether SDL should treat a pair of Joy-Con as a single controller or as two "
         "separate controllers.<br><br><dolphin_emphasis>If unsure, leave this "
         "checked.</dolphin_emphasis>"));
  connect(m_combine_joy_cons, &ToolTipCheckBox::toggled, this,
          &SDLHintsWindow::CombineJoyConsToggled);

  m_horizontal_joy_cons = new ToolTipCheckBox(tr("Sideways Joy-Con"));
  m_horizontal_joy_cons->SetDescription(
      tr("Defines the default orientation for individual Joy-Con. This setting has no effect when "
         "Use Joy-Con Pairs as a Single Controller is "
         "enabled.<br><br><dolphin_emphasis>If unsure, "
         "leave this checked.</dolphin_emphasis>"));
  connect(m_horizontal_joy_cons, &ToolTipCheckBox::toggled, this,
          &SDLHintsWindow::HorizontalJoyConsToggled);

  m_dualsense_player_led = new ToolTipCheckBox(tr("Enable DualSense Player LEDs"));
  m_dualsense_player_led->SetDescription(
      tr("Controls whether the player LEDs should be lit to indicate which player is associated "
         "with a DualSense controller.<br><br><dolphin_emphasis>If unsure, leave this "
         "unchecked.</dolphin_emphasis>"));
  connect(m_dualsense_player_led, &ToolTipCheckBox::toggled, this,
          &SDLHintsWindow::DualSensePLayerLedToggled);

  auto* default_layout = new QVBoxLayout();
  default_layout->setContentsMargins(10, 10, 10, 10);
  default_layout->addWidget(m_directinput_detection);
  default_layout->addWidget(m_combine_joy_cons);
  default_layout->addWidget(m_horizontal_joy_cons);
  default_layout->addWidget(m_dualsense_player_led);
  default_layout->addStretch(1);

  auto* default_frame = new QFrame();
  default_frame->setLayout(default_layout);

  PopulateChecklist();

  // Create the tab widget
  m_tab_widget = new QTabWidget();
  m_tab_widget->addTab(default_frame, tr("Main"));
  m_tab_widget->addTab(advanced_frame, tr("Advanced"));

  cur_tab_idx = 0;
  m_tab_widget->setCurrentIndex(cur_tab_idx);

  connect(m_tab_widget, &QTabWidget::currentChanged, this, &SDLHintsWindow::TabChanged);

  // Create bottom row
  auto* warning_text =
      new QLabel(tr("Dolphin must be restarted for these changes to take effect."));
  warning_text->setWordWrap(true);

  // Create main layout
  auto* main_layout = new QVBoxLayout();
  main_layout->addWidget(
      QtUtils::CreateIconWarning(this, QStyle::SP_MessageBoxWarning, warning_text), 1);
  main_layout->addWidget(m_tab_widget);
  main_layout->addWidget(m_button_box, 0, Qt::AlignBottom | Qt::AlignRight);
  setLayout(main_layout);
}

void SDLHintsWindow::PopulateTable()
{
  m_hints_table->setRowCount(0);

  // Loop through all the values in the SDL_Hints settings section and load them into the table
  std::shared_ptr<Config::Layer> layer = Config::GetLayer(Config::LayerType::Base);
  const Config::Section& section = layer->GetSection(Config::System::Main, "SDL_Hints");
  for (auto& row_data : section)
  {
    const Config::Location& location = row_data.first;
    const std::optional<std::string>& value = row_data.second;

    if (value)
    {
      m_hints_table->insertRow(m_hints_table->rowCount());
      m_hints_table->setItem(m_hints_table->rowCount() - 1, 0,
                             new QTableWidgetItem(QString::fromStdString(location.key)));
      m_hints_table->setItem(m_hints_table->rowCount() - 1, 1,
                             new QTableWidgetItem(QString::fromStdString(*value)));
    }
  }
}

void SDLHintsWindow::SaveTable()
{
  // Clear all the old values from the SDL_Hints section
  std::shared_ptr<Config::Layer> layer = Config::GetLayer(Config::LayerType::Base);
  Config::Section section = layer->GetSection(Config::System::Main, "SDL_Hints");

  for (auto& row_data : section)
    row_data.second.reset();

  // Add each item still in the table to the config file
  for (int row = 0; row < m_hints_table->rowCount(); ++row)
  {
    QTableWidgetItem* hint_name_item = m_hints_table->item(row, 0);
    QTableWidgetItem* hint_value_item = m_hints_table->item(row, 1);

    if (hint_name_item != nullptr && hint_value_item != nullptr)
    {
      const QString& hint_name = hint_name_item->text().trimmed();
      const QString& hint_value = hint_value_item->text().trimmed();

      if (!hint_name.isEmpty() && !hint_value.isEmpty())
      {
        const Config::Info<std::string> setting{
            {Config::System::Main, "SDL_Hints", hint_name.toStdString()}, ""};
        Config::SetBase(setting, hint_value.toStdString());
      }
    }
  }
}

void SDLHintsWindow::PopulateChecklist()
{
  // Populate the checklist and default to the SDL default for an invalid value

  m_directinput_detection->blockSignals(true);
  m_directinput_detection->setChecked(Config::GetBase(Config::MAIN_SDL_HINT_JOYSTICK_DIRECTINPUT) !=
                                      "0");  // Default to checked if incorrectly set
  m_directinput_detection->blockSignals(false);

  m_combine_joy_cons->blockSignals(true);
  m_combine_joy_cons->setChecked(
      Config::GetBase(Config::MAIN_SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS) !=
      "0");  // Default to checked if incorrectly set
  m_combine_joy_cons->blockSignals(false);

  m_horizontal_joy_cons->blockSignals(true);
  m_horizontal_joy_cons->setChecked(
      Config::GetBase(Config::MAIN_SDL_HINT_JOYSTICK_HIDAPI_VERTICAL_JOY_CONS) !=
      "1");  // Default to checked if incorrectly set
  m_horizontal_joy_cons->blockSignals(false);

  m_dualsense_player_led->blockSignals(true);
  m_dualsense_player_led->setChecked(
      Config::GetBase(Config::MAIN_SDL_HINT_JOYSTICK_HIDAPI_PS5_PLAYER_LED) !=
      "0");  // Default to checked if incorrectly set
  m_dualsense_player_led->blockSignals(false);
}

void SDLHintsWindow::AddRow()
{
  m_hints_table->insertRow(m_hints_table->rowCount());
  m_hints_table->scrollToBottom();
}

void SDLHintsWindow::RemoveRow()
{
  QModelIndex index = m_hints_table->selectionModel()->currentIndex();
  m_hints_table->removeRow(index.row());
}

void SDLHintsWindow::SelectionChanged()
{
  m_rem_row_btn->setEnabled(m_hints_table->selectionModel()->hasSelection());
}

void SDLHintsWindow::OnClose()
{
  TabChanged(-1);  // Pass -1 to indicate exit

  reject();
}

void SDLHintsWindow::TabChanged(int new_index)
{
  // Check which tab we're coming from, cur_tab_idx has not been updated yet
  switch (cur_tab_idx)
  {
  case 1:  // Coming from the advanced tab
    SaveTable();
    break;

  default:
    break;
  }

  // Check which tab we're going to
  switch (new_index)
  {
  case 0:  // Going to the main tab
    PopulateChecklist();
    break;

  case 1:  // Going to the advanced tab
    PopulateTable();
    break;

  default:
    break;
  }

  cur_tab_idx = new_index;
}

void SDLHintsWindow::SectionResized(int logical_index, int old_size, int new_size)
{
  if (logical_index == 0 && old_size != new_size)
  {
    QHeaderView* header = m_hints_table->horizontalHeader();
    header->setMaximumSectionSize(header->size().width() - header->minimumSectionSize());
    header->resizeSection(1, header->size().width() - new_size);
  }
}

bool SDLHintsWindow::eventFilter(QObject* obj, QEvent* event)
{
  auto table_widget = qobject_cast<QHeaderView*>(obj);
  if (table_widget)
  {
    if (event->type() == QEvent::Resize)
    {
      QResizeEvent* resize_event = static_cast<QResizeEvent*>(event);
      QHeaderView* header = m_hints_table->horizontalHeader();
      header->setMaximumSectionSize(resize_event->size().width() - header->minimumSectionSize());
      header->resizeSection(0, resize_event->size().width() - header->sectionSize(1));
    }
  }

  return QObject::eventFilter(obj, event);
}

void SDLHintsWindow::DirectinputDetectionToggled(bool checked)
{
  Config::SetBase(Config::MAIN_SDL_HINT_JOYSTICK_DIRECTINPUT, checked ? "1" : "0");
}

void SDLHintsWindow::CombineJoyConsToggled(bool checked)
{
  Config::SetBase(Config::MAIN_SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, checked ? "1" : "0");
}

void SDLHintsWindow::HorizontalJoyConsToggled(bool checked)
{
  Config::SetBase(Config::MAIN_SDL_HINT_JOYSTICK_HIDAPI_VERTICAL_JOY_CONS, checked ? "0" : "1");
}

void SDLHintsWindow::DualSensePLayerLedToggled(bool checked)
{
  Config::SetBase(Config::MAIN_SDL_HINT_JOYSTICK_HIDAPI_PS5_PLAYER_LED, checked ? "1" : "0");
}
