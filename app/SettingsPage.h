#pragma once

#include "Checks.h"
#include "SettingEditor.h"

#include <QWidget>

class QLabel;
class QVBoxLayout;

// A scrollable page made of setting groups from the schema. A page can contain several sections,
// e.g. a Bluetooth device page has one section for the codec and one per node.
class SettingsPage : public QWidget
{
  Q_OBJECT
public:
  SettingsPage(const QString& title, const QString& subtitle, QWidget* parent = nullptr);

  // Status lines below the title (live information).
  void setStatus(const QString& text);
  // Area for page-level buttons below the title.
  QWidget* actionArea() const { return m_actions; }
  // A coloured notice at the top (e.g. "WirePlumber 0.5 required").
  void addNotice(const QString& text, bool error = false);

  // Adds all groups of |scope|. |heading| starts a new section when not empty.
  void addSection(const QString& scope, const SettingBinding& binding, const QString& heading = {}, const QString& headingNote = {});
  // Adds a custom widget at the end of the page.
  void addWidget(QWidget* w);
  void finish();

  void setGraphRate(std::function<int()> f) { m_graphRate = std::move(f); }
  void setGraphValue(ValueGetter f) { m_graphValue = std::move(f); }

  // Re-reads every row from the model and updates warnings and visibility.
  void refresh();

signals:
  void changed();

private:
  struct Section
  {
    QString scope;
    QList<SettingEditor*> editors;
    QLabel* warnings = nullptr;
  };

  void updateDerived();
  QString valueIn(const Section& s, const QString& key) const;

  QVBoxLayout* m_layout;
  QLabel* m_status;
  QLabel* m_how;
  QWidget* m_actions;
  QVBoxLayout* m_notices;
  QList<Section> m_sections;
  std::function<int()> m_graphRate;
  ValueGetter m_graphValue;
};
