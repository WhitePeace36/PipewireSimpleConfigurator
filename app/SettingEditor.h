#pragma once

#include "ConfigFiles.h"
#include "ConfigModel.h"
#include "Schema.h"

#include <QWidget>
#include <functional>

class QCheckBox;
class QLabel;

// Where a setting row reads and writes its value.
struct SettingBinding
{
  // Returns the values the row writes to; |create| creates the rule when it does not exist yet.
  std::function<Values*(bool create)> values;
  // Value from the other config files (not ours), with the file it comes from.
  std::function<ValueOrigin(const QString& key)> inherited;
  // Value set by a drop-in that loads after ours and therefore wins.
  std::function<ValueOrigin(const QString& key)> overriddenBy;
  // Value currently in effect on the running device, or empty.
  std::function<QString(const QString& key)> live;
  // Label for inherited values without a file, e.g. "Uses the global stream setting".
  QString inheritedLabel;
};

// Editor widget for one value type. Values are QString for scalars and QStringList for lists.
class ValueWidget : public QWidget
{
  Q_OBJECT
public:
  using QWidget::QWidget;
  virtual void setValue(const QVariant& value) = 0;
  virtual QVariant value() const = 0;
  virtual bool isAcceptable() const { return true; }
  // Extra text after the editor, e.g. "21.33 ms".
  virtual QString extraInfo() const { return {}; }

  static ValueWidget* create(const SettingDef& def, std::function<int()> graphRate, QWidget* parent);

signals:
  void edited();
};

// One row: [override] label/help  editor  where-the-value-comes-from.
class SettingEditor : public QWidget
{
  Q_OBJECT
public:
  SettingEditor(const SettingDef& def, const SettingBinding& binding, std::function<int()> graphRate, QWidget* parent = nullptr);

  const SettingDef& def() const { return m_def; }
  bool isOverridden() const;
  // Override value, else inherited, else live, else documented default.
  QString effectiveValue() const;
  // Re-reads the model (e.g. after a preset or reload).
  void refresh();
  // Updates only the hint texts (e.g. latency in ms after the graph rate changed).
  void refreshHints() { updateHints(); }

signals:
  void changed();

private:
  void onOverrideToggled(bool on);
  // The editor holds something that can be written (valid, not blank, not an empty list).
  bool editorHasValue() const;
  void onEdited();
  void updateHints();

  SettingDef m_def;
  SettingBinding m_binding;
  QCheckBox* m_override = nullptr;
  ValueWidget* m_editor = nullptr;
  QLabel* m_source = nullptr;
  QLabel* m_extra = nullptr;
  QLabel* m_warning = nullptr;
  bool m_updating = false;
  bool m_pending = false;  // ticked, but no value entered yet: nothing is written
};

// Converts a value to the text used for comparisons and display ("[ 44100 48000 ]" for lists).
QString valueText(const QVariant& v);
