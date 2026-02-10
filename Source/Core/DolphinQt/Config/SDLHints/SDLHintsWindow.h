// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>

class QTabWidget;
class QDialogButtonBox;
class QTableWidget;
class QPushButton;
class ToolTipCheckBox;

class SDLHintsWindow final : public QDialog
{
  Q_OBJECT
public:
  explicit SDLHintsWindow(QWidget* parent);

  QSize sizeHint() const override;

private:
  void CreateMainLayout();

  void PopulateTable();
  void SaveTable();
  void PopulateChecklist();
  void AddRow();
  void RemoveRow();
  void SelectionChanged();
  void OnClose();
  void TabChanged(int new_index);
  void SectionResized(int logical_index, int old_size, int new_sze);

  bool eventFilter(QObject* obj, QEvent* event) override;

  void DirectinputDetectionToggled(bool checked);
  void CombineJoyConsToggled(bool checked);
  void HorizontalJoyConsToggled(bool checked);
  void DualSensePLayerLedToggled(bool checked);

  QTabWidget* m_tab_widget;
  QDialogButtonBox* m_button_box;
  QTableWidget* m_hints_table;
  QPushButton* m_rem_row_btn;

  ToolTipCheckBox* m_directinput_detection;
  ToolTipCheckBox* m_combine_joy_cons;
  ToolTipCheckBox* m_horizontal_joy_cons;
  ToolTipCheckBox* m_dualsense_player_led;

  int cur_tab_idx;
};
