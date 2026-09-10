// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Settings/AvalonDeckManager.h"

#include <ranges>

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QCollator>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

#include "Core/HW/Triforce/DeckReader.h"

#include "DolphinQt/QtUtils/QtUtils.h"

namespace
{

constexpr int COLUMN_CARD_NUMBER = 0;
constexpr int COLUMN_CARD_NAME_ENG = 1;
constexpr int COLUMN_CARD_NAME_JPN = 2;
constexpr int COLUMN_CARD_ATTRIBUTE = 3;
constexpr int COLUMN_CARD_MOVEMENT = 4;
constexpr int COLUMN_CARD_QUANTITY = 5;
constexpr int COLUMN_COUNT = 6;

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

    if (!m_show_all_cards &&
        model->data(model->index(row, COLUMN_CARD_QUANTITY, parent)).toInt() == 0)
      return false;

    if (m_search_text.isEmpty())
      return true;

    for (int column = 0; column != COLUMN_CARD_QUANTITY; ++column)
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
    const auto left_str = sourceModel()->data(left, sortRole()).toString();
    const auto right_str = sourceModel()->data(right, sortRole()).toString();

    if (left.column() == COLUMN_CARD_MOVEMENT)
      return CompareMovements(left_str, right_str);

    static QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    return collator.compare(left_str, right_str) < 0;
  }

private:
  static bool CompareMovements(const QString& left, const QString& right)
  {
    for (const auto [a, b] : std::views::zip(left, right))
    {
      if (a != b)
        return RankMovementColor(a) < RankMovementColor(b);
    }

    return left.size() < right.size();
  }

  static int RankMovementColor(QChar c)
  {
    // Sort the movement strings in YBRGW order.
    constexpr std::string_view rank_str = "ybrgw";

    // TODO: character conversion is gross
    return int(rank_str.find(c.toLatin1()));
  }

  QString m_search_text;
  bool m_show_all_cards{};
};

class DeckModel : public QAbstractTableModel
{
public:
  explicit DeckModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

  void LoadData()
  {
    static const QHash<char, QString> attribute_names = {
        {'y', tr("Yellow")},
        {'b', tr("Blue")},
        {'r', tr("Red")},
        {'g', tr("Green")},
        // "The Key of Avalon" card attribute name. Original Japanese: マップ上魔法
        {'m', tr("Magic")},
        // "The Key of Avalon" card attribute name. Original Japanese: 戦闘支援
        {'s', tr("Support")},
    };

    const auto card_database = Triforce::LoadCardDatabaseFromFile();
    const auto card_deck = Triforce::LoadCardDeckFromFile(card_database);

    beginResetModel();

    m_data.resize(card_database.size());
    std::size_t line_number = 0;

    for (const auto& [card_number, card_details] : card_database)
    {
      auto& line = m_data[line_number++];

      line.number = QString::fromUtf8(card_number);
      line.name_eng = QString::fromUtf8(card_details.name_eng);
      line.name_jpn = QString::fromUtf8(card_details.name_jpn);

      if (!card_details.attribute.empty())
      {
        line.attribute = attribute_names.value(card_details.attribute.front(),
                                               QString::fromUtf8(card_details.attribute));
      }

      line.movement = QString::fromUtf8(card_details.movement);

      if (card_deck)
        line.quantity = int(std::ranges::count(*card_deck, card_details.card_id));
    }

    endResetModel();
  }

  void SaveDeck()
  {
    std::vector<Triforce::DeckEntry> deck;

    for (auto& card : m_data)
    {
      if (card.quantity != 0)
        deck.emplace_back(card.number.toStdString(), card.quantity);
    }

    const auto card_database = Triforce::LoadCardDatabaseFromFile();
    Triforce::SaveCardDeckToFile(deck, card_database);
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
        emit dataChanged(index(row, COLUMN_CARD_QUANTITY), index(row, COLUMN_CARD_QUANTITY),
                         {Qt::DisplayRole});
      }

      ++row;
    }
  }

  int rowCount(const QModelIndex&) const override { return static_cast<int>(m_data.size()); }

  int columnCount(const QModelIndex&) const override { return COLUMN_COUNT; }

  QVariant data(const QModelIndex& index, int role) const override
  {
    if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::EditRole))
      return {};

    const auto& card = m_data[index.row()];

    switch (index.column())
    {
    case COLUMN_CARD_NUMBER:
      return card.number;
    case COLUMN_CARD_NAME_ENG:
      return card.name_eng;
    case COLUMN_CARD_NAME_JPN:
      return card.name_jpn;
    case COLUMN_CARD_ATTRIBUTE:
      return card.attribute;
    case COLUMN_CARD_MOVEMENT:
      return card.movement;
    case COLUMN_CARD_QUANTITY:
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
      case COLUMN_CARD_NUMBER:
        return tr("Number");
      case COLUMN_CARD_NAME_ENG:
        return tr("English Name");
      case COLUMN_CARD_NAME_JPN:
        return tr("Japanese Name");
      case COLUMN_CARD_ATTRIBUTE:
        return tr("Attribute");
      case COLUMN_CARD_MOVEMENT:
        return tr("Movement");
      case COLUMN_CARD_QUANTITY:
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

    if (index.column() == COLUMN_CARD_QUANTITY)
      flags |= Qt::ItemIsEditable;

    return flags;
  }

  bool setData(const QModelIndex& index, const QVariant& value, int role) override
  {
    if (!index.isValid() || index.column() != COLUMN_CARD_QUANTITY || role != Qt::EditRole)
      return false;

    m_data[index.row()].quantity = value.toInt();

    emit dataChanged(index, index, {Qt::DisplayRole});
    return true;
  }

private:
  struct CardData
  {
    QString number;
    QString name_eng;
    QString name_jpn;
    QString attribute;
    QString movement;
    int quantity{};
  };

  std::vector<CardData> m_data;
};

class MovementPips : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    QColor colorless = option.palette.color(QPalette::Base);
    colorless.setHslF(colorless.hslHueF(), colorless.hslSaturationF(),
                      1.f - colorless.lightnessF());

    static const QHash<char, QColor> colors = {
        {'y', Qt::yellow}, {'b', Qt::blue}, {'r', Qt::red}, {'g', Qt::green}, {'w', colorless},
    };

    const QString text = index.data(Qt::DisplayRole).toString();

    painter->save();

    painter->setRenderHint(QPainter::Antialiasing, true);

    const QFontMetrics font_metrics(option.font);

    const int diameter = font_metrics.height() * 3 / 4;
    const int radius = diameter / 2;
    const int margin = (option.rect.height() - diameter) / 2;
    const int spacing = margin / 2;

    int x = option.rect.x() + margin + radius;
    const int y = option.rect.center().y();

    painter->setPen(Qt::NoPen);

    for (const QChar ch : text)
    {
      // TODO: character conversion is gross..
      painter->setBrush(colors.value(ch.toLatin1(), Qt::gray));

      painter->drawEllipse(QPoint(x, y), radius, radius);

      x += diameter + spacing;
    }

    painter->restore();
  }
};

class CardQuantityEditor : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                        const QModelIndex& index) const override
  {
    auto* const editor = QStyledItemDelegate::createEditor(parent, option, index);

    if (auto* const spin_box = qobject_cast<QSpinBox*>(editor))
    {
      spin_box->setMinimum(0);
      spin_box->setMaximum(MAXIMUM_DECK_SIZE);
    }

    return editor;
  }
};

}  // namespace

AvalonDeckManager::AvalonDeckManager(QWidget* parent) : QDialog{parent}
{
  setWindowTitle(tr("The Key of Avalon - Deck Manager"));

  auto* const main_layout = new QVBoxLayout{this};

  auto* const model = new DeckModel{this};

  auto* const proxy = new NaturalSortFilterProxy{this};
  proxy->setSourceModel(model);

  auto* const table_view = new QTableView;
  table_view->setModel(proxy);
  table_view->setSortingEnabled(true);
  table_view->verticalHeader()->hide();
  table_view->setSelectionBehavior(QAbstractItemView::SelectItems);
  table_view->setSelectionMode(QAbstractItemView::SingleSelection);

  // table_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  // table_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

  auto* const header = table_view->horizontalHeader();
  header->setSectionResizeMode(COLUMN_CARD_NUMBER, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(COLUMN_CARD_NAME_ENG, QHeaderView::Stretch);
  header->setSectionResizeMode(COLUMN_CARD_NAME_JPN, QHeaderView::Stretch);
  header->setSectionResizeMode(COLUMN_CARD_ATTRIBUTE, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(COLUMN_CARD_MOVEMENT, QHeaderView::ResizeToContents);
  header->setSectionResizeMode(COLUMN_CARD_QUANTITY, QHeaderView::ResizeToContents);

  // table_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

  table_view->sortByColumn(COLUMN_CARD_NUMBER, Qt::SortOrder::AscendingOrder);

  table_view->setItemDelegateForColumn(COLUMN_CARD_MOVEMENT, new MovementPips(table_view));
  table_view->setItemDelegateForColumn(COLUMN_CARD_QUANTITY, new CardQuantityEditor(table_view));

  auto* const cards_group = new QGroupBox(tr("Cards"));
  auto* const cards_layout = new QVBoxLayout{cards_group};

  main_layout->addWidget(cards_group);

  auto* const show_all_cards = new QCheckBox{tr("Show All Available Cards")};
  cards_layout->addWidget(show_all_cards);

  connect(show_all_cards, &QCheckBox::toggled, proxy, &NaturalSortFilterProxy::SetShowAllCards);

  auto* const search_textbox = new QLineEdit;
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

  connect(this, &AvalonDeckManager::accepted, model, &DeckModel::SaveDeck);

  auto* const deck_size_label = new QLabel;

  const auto update_label_text = [=]() {
    deck_size_label->setText(
        tr("Deck Size: %1 / %2").arg(model->GetTotalQuantity()).arg(MAXIMUM_DECK_SIZE));
  };

  connect(model, &QAbstractItemModel::dataChanged, this, update_label_text);

  main_layout->addWidget(deck_size_label);

  main_layout->addWidget(button_box);

  model->LoadData();

  // table_view->resizeColumnsToContents();

  update_label_text();

  show_all_cards->setChecked(model->GetTotalQuantity() == 0);

  QtUtils::AdjustSizeWithinScreen(this);
}
