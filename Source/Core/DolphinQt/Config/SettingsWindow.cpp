// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Config/SettingsWindow.h"

#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "DolphinQt/QtUtils/WrapInScrollArea.h"
#include "DolphinQt/Settings/AdvancedPane.h"
#include "DolphinQt/Settings/AudioPane.h"
#include "DolphinQt/Settings/GameCubePane.h"
#include "DolphinQt/Settings/GeneralPane.h"
#include "DolphinQt/Settings/InterfacePane.h"
#include "DolphinQt/Settings/PathPane.h"
#include "DolphinQt/Settings/WiiPane.h"

namespace
{

class NavigationList final : public QListWidget
{
public:
  NavigationList() { setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding); }
  QSize sizeHint() const override { return minimumSize(); }
};

}  // namespace

SettingsWindow::SettingsWindow(QWidget* parent) : QDialog(parent)
{
  // Set Window Properties
  setWindowTitle(tr("Settings"));
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  // Main Layout
  auto* const layout = new QVBoxLayout{this};

  auto* const navigation_and_panes = new QHBoxLayout;
  layout->addLayout(navigation_and_panes);

  m_navigation_list = new NavigationList;

  navigation_and_panes->addWidget(m_navigation_list);

  m_stacked_panes = new QStackedWidget;
  navigation_and_panes->addWidget(m_stacked_panes);

  AddSettingsWidget(new GeneralPane, tr("General"));
  AddSettingsWidget(new InterfacePane, tr("Interface"));
  AddSettingsWidget(new AudioPane, tr("Audio"));
  AddSettingsWidget(new PathPane, tr("Paths"));
  AddSettingsWidget(new GameCubePane, tr("GameCube"));
  AddSettingsWidget(new WiiPane, tr("Wii"));
  AddSettingsWidget(new AdvancedPane, tr("Advanced"));

  connect(m_navigation_list, &QListWidget::currentRowChanged, m_stacked_panes,
          &QStackedWidget::setCurrentIndex);

  // Make sure the first item is actually selected by default.
  m_navigation_list->setCurrentRow(0);

  // Dialog box buttons
  auto* const close_box = new QDialogButtonBox(QDialogButtonBox::Close);

  connect(close_box, &QDialogButtonBox::rejected, this, &QDialog::reject);

  layout->addWidget(close_box);
}

void SettingsWindow::AddSettingsWidget(QWidget* widget, const QString& name)
{
  m_stacked_panes->addWidget(widget);
  m_navigation_list->addItem(name);
}

void SettingsWindow::SelectAudioPane()
{
  m_navigation_list->setCurrentRow(static_cast<int>(TabIndex::Audio));
}

void SettingsWindow::SelectGeneralPane()
{
  m_navigation_list->setCurrentRow(static_cast<int>(TabIndex::General));
}
