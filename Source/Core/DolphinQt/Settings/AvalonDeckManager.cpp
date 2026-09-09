// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Settings/AvalonDeckManager.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QCollator>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include "Core/HW/Triforce/DeckReader.h"

#include "DolphinQt/QtUtils/QtUtils.h"

namespace
{

constexpr int COLUMN_NUMBER = 0;
constexpr int COLUMN_NAME_ENG = 1;
constexpr int COLUMN_NAME_JPN = 2;
constexpr int COLUMN_QUANTITY = 3;
constexpr int COLUMN_COUNT = 4;

constexpr int MAXIMUM_DECK_SIZE = 30;

class NaturalSortFilterProxy : public QSortFilterProxyModel
{
  using QSortFilterProxyModel::QSortFilterProxyModel;

public:
  void SetFilterText(QString text)
  {
    if (m_search_text == text)
      return;

    m_search_text = std::move(text);
    invalidateFilter();
  }

  void SetShowAllCards(bool show_all_cards)
  {
    if (m_show_all_cards == show_all_cards)
      return;

    m_show_all_cards = show_all_cards;
    invalidateFilter();
  }

protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override
  {
    const auto* model = sourceModel();

    if (!m_show_all_cards && model->data(model->index(row, COLUMN_QUANTITY, parent)).toInt() == 0)
      return false;

    if (m_search_text.isEmpty())
      return true;

    for (int column = 0; column != COLUMN_QUANTITY; ++column)
    {
      const QModelIndex index = model->index(row, column, parent);

      if (model->data(index, Qt::DisplayRole)
              .toString()
              .contains(m_search_text, Qt::CaseInsensitive))
        return true;
    }

    return false;
  }

  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
  {
    static QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    return collator.compare(sourceModel()->data(left, sortRole()).toString(),
                            sourceModel()->data(right, sortRole()).toString()) < 0;
  }

private:
  QString m_search_text;
  bool m_show_all_cards{};
};

class DeckModel : public QAbstractTableModel
{
public:
  explicit DeckModel(QObject* parent = nullptr) : QAbstractTableModel(parent)
  {
    for (auto& [card_number, card_details] : Triforce::LoadCardDatabaseFromFile())
    {
      auto& line = m_data.emplace_back();

      line.number = QString::fromUtf8(card_number);
      line.name_eng = QString::fromUtf8(card_details.name_eng);
      line.name_jpn = QString::fromUtf8(card_details.name_jpn);
    }
  }

  int GetTotalQuantity() const
  {
    int total = 0;
    for (const auto& card : m_data)
      total += card.quantity;
    return total;
  }

  void ClearDeck()
  {
    int row = 0;
    for (auto& card : m_data)
    {
      if (std::exchange(card.quantity, 0) != 0)
      {
        emit dataChanged(index(row, COLUMN_QUANTITY), index(row, COLUMN_QUANTITY),
                         {Qt::DisplayRole});
      }

      ++row;
    }
  }

  int rowCount(const QModelIndex&) const override { return static_cast<int>(m_data.size()); }

  int columnCount(const QModelIndex&) const override { return COLUMN_COUNT; }

  QVariant data(const QModelIndex& index, int role) const override
  {
    if (!index.isValid() || role != Qt::DisplayRole)
      return {};

    const auto& card = m_data[index.row()];

    switch (index.column())
    {
    case COLUMN_NUMBER:
      return card.number;
    case COLUMN_NAME_ENG:
      return card.name_eng;
    case COLUMN_NAME_JPN:
      return card.name_jpn;
    case COLUMN_QUANTITY:
      return card.quantity;
    default:
      return {};
    }
  }

  QVariant headerData(int section, Qt::Orientation orientation, int role) const override
  {
    if (role != Qt::DisplayRole)
      return {};

    if (orientation == Qt::Horizontal)
    {
      switch (section)
      {
      case COLUMN_NUMBER:
        return tr("Number");
      case COLUMN_NAME_ENG:
        return tr("English Name");
      case COLUMN_NAME_JPN:
        return tr("Japanese Name");
      case COLUMN_QUANTITY:
        return tr("Quantity");
      default:
        return {};
      }
    }

    return {};
  }

  Qt::ItemFlags flags(const QModelIndex& index) const override
  {
    if (!index.isValid())
      return Qt::NoItemFlags;

    auto flags = QAbstractTableModel::flags(index);

    if (index.column() == COLUMN_QUANTITY)
      flags |= Qt::ItemIsEditable;

    return flags;
  }

  bool setData(const QModelIndex& index, const QVariant& value, int role) override
  {
    if (!index.isValid() || index.column() != COLUMN_QUANTITY || role != Qt::EditRole)
      return false;

    const auto new_value = value.toInt();

    if (new_value < 0 || new_value > MAXIMUM_DECK_SIZE)
      return false;

    m_data[index.row()].quantity = value.toInt();

    emit dataChanged(index, index, {Qt::DisplayRole});
    return true;
  }

private:
  struct CardData
  {
    QString name_eng;
    QString name_jpn;
    QString number;
    int quantity{};
  };

  std::vector<CardData> m_data;
};

}  // namespace

AvalonDeckManager::AvalonDeckManager(QWidget* parent) : QDialog{parent}
{
  setWindowTitle(tr("The Key of Avalon - Deck Manager"));

  auto* const main_layout = new QVBoxLayout{this};

  auto* const model = new DeckModel{this};

  auto* const proxy = new NaturalSortFilterProxy(this);
  proxy->setSourceModel(model);

  auto* const table_view = new QTableView;
  table_view->setModel(proxy);
  table_view->setSortingEnabled(true);
  table_view->verticalHeader()->hide();
  table_view->setSelectionBehavior(QAbstractItemView::SelectItems);
  table_view->setSelectionMode(QAbstractItemView::SingleSelection);

  table_view->resizeColumnsToContents();

  table_view->sortByColumn(COLUMN_NUMBER, Qt::SortOrder::AscendingOrder);

  auto* const cards_group = new QGroupBox(tr("Cards"));
  auto* const cards_layout = new QVBoxLayout{cards_group};

  main_layout->addWidget(cards_group);

  auto* const show_all_cards = new QCheckBox{tr("Show All Available Cards")};
  cards_layout->addWidget(show_all_cards);

  connect(show_all_cards, &QCheckBox::checkStateChanged, proxy,
          &NaturalSortFilterProxy::SetShowAllCards);

  if (model->GetTotalQuantity() == 0)
    show_all_cards->setChecked(true);

  auto* const search_textbox = new QLineEdit{this};
  search_textbox->setPlaceholderText(tr("Search cards..."));

  connect(search_textbox, &QLineEdit::textChanged, proxy, &NaturalSortFilterProxy::SetFilterText);

  cards_layout->addWidget(search_textbox);
  cards_layout->addWidget(table_view);

  auto* const button_box = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel};

  auto* const clear_button = new QPushButton(tr("Clear"));
  connect(clear_button, &QPushButton::clicked, model, &DeckModel::ClearDeck);

  button_box->addButton(clear_button, QDialogButtonBox::ActionRole);

  connect(button_box, &QDialogButtonBox::accepted, this, &AvalonDeckManager::accept);
  connect(button_box, &QDialogButtonBox::rejected, this, &AvalonDeckManager::reject);

  auto* const deck_size_label = new QLabel;

  const auto update_label_text = [=]() {
    deck_size_label->setText(
        tr("Deck Size: %1 / %2").arg(model->GetTotalQuantity()).arg(MAXIMUM_DECK_SIZE));
  };
  update_label_text();

  connect(model, &QAbstractItemModel::dataChanged, this, update_label_text);

  main_layout->addWidget(deck_size_label);

  main_layout->addWidget(button_box);

  QtUtils::AdjustSizeWithinScreen(this);
}
