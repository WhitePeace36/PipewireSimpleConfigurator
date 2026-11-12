#include "SettingEditor.h"

#include "Checks.h"
#include "SpaJson.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QSpinBox>
#include <QVBoxLayout>

QString valueText(const QVariant& v)
{
  if (v.typeId() == QMetaType::QStringList || v.typeId() == QMetaType::QVariantList)
    return SpaJson::formatValue(SpaJson::toStringList(v));
  return v.toString();
}

namespace
{
// Greyed-out secondary text that still follows the light or dark theme.
void setDimmed(QWidget* w, bool dim)
{
  QPalette p = w->palette();
  QColor c = QApplication::palette().color(QPalette::WindowText);
  if (dim)
    c.setAlphaF(0.62);
  p.setColor(QPalette::WindowText, c);
  w->setPalette(p);
}

// Compares a configured value with the value PipeWire reports, which may be written differently
// ("[ 44100, 48000 ]" vs "[ 44100 48000 ]", "0" vs "0.0", "True" vs "true").
bool sameValue(const SettingDef& def, const QString& a, const QString& b)
{
  if (def.isListType())
  {
    QStringList la = SpaJson::toStringList(a), lb = SpaJson::toStringList(b);
    la.sort();
    lb.sort();
    return la == lb;
  }
  bool okA = false, okB = false;
  const double da = a.toDouble(&okA), db = b.toDouble(&okB);
  if (okA && okB)
    return qFuzzyCompare(da + 1.0, db + 1.0);
  return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

QString displayList(const SettingDef& def, const QStringList& items)
{
  QStringList labels;
  for (const QString& i : items)
    labels.append(def.type == SettingDef::RateList ? i : def.optionLabel(i));
  return labels.isEmpty() ? QObject::tr("none") : labels.join(", ");
}

// --- Bool -------------------------------------------------------------------------------------
class BoolWidget : public ValueWidget
{
public:
  explicit BoolWidget(QWidget* parent) : ValueWidget(parent)
  {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_box = new QComboBox(this);
    m_box->addItem(tr("Yes"), "true");
    m_box->addItem(tr("No"), "false");
    m_box->setMinimumWidth(120);
    l->addWidget(m_box);
    l->addStretch();
    connect(m_box, &QComboBox::activated, this, &ValueWidget::edited);
  }
  void setValue(const QVariant& v) override
  {
    const QString s = v.toString().trimmed().toLower();
    // Blank when the value is unknown, rather than pretending it is "No".
    m_box->setCurrentIndex(s.isEmpty() ? -1 : (s == "true" || s == "1") ? 0 : 1);
  }
  // Blank (nothing chosen) is not a value; it must not turn into "No".
  QVariant value() const override { return m_box->currentIndex() < 0 ? QString() : m_box->currentData().toString(); }
  bool isAcceptable() const override { return m_box->currentIndex() >= 0; }

private:
  QComboBox* m_box;
};

// --- Int / Float / String ----------------------------------------------------------------------
class TextWidget : public ValueWidget
{
public:
  TextWidget(const SettingDef& def, QWidget* parent) : ValueWidget(parent), m_def(def)
  {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_edit = new QLineEdit(this);
    if (def.type == SettingDef::Int)
    {
      auto* v = new QIntValidator(int(def.min), int(qMin(def.max, 2147483647.0)), m_edit);
      v->setLocale(QLocale::c());
      m_edit->setValidator(v);
      m_edit->setMaximumWidth(200);
    }
    else if (def.type == SettingDef::Float)
    {
      // Always "." as decimal separator: that is what PipeWire's parser expects.
      auto* v = new QDoubleValidator(def.min, def.max, 9, m_edit);
      v->setLocale(QLocale::c());
      v->setNotation(QDoubleValidator::StandardNotation);
      m_edit->setValidator(v);
      m_edit->setMaximumWidth(200);
    }
    l->addWidget(m_edit);
    if (!def.unit.isEmpty())
      l->addWidget(new QLabel(def.unit, this));
    l->addStretch();
    connect(m_edit, &QLineEdit::textEdited, this,
            [this]
            {
              updateColor();
              emit edited();
            });
  }
  void setValue(const QVariant& v) override
  {
    m_edit->setText(v.toString());
    updateColor();
  }
  QVariant value() const override { return m_edit->text().trimmed(); }
  bool isAcceptable() const override { return m_def.type == SettingDef::String || m_edit->hasAcceptableInput(); }

private:
  void updateColor()
  {
    if (isAcceptable())
    {
      m_edit->setPalette(QPalette());  // back to the theme, including the disabled look
      return;
    }
    QPalette p = m_edit->palette();
    p.setColor(QPalette::Active, QPalette::Text, QColor("#c01c28"));
    p.setColor(QPalette::Inactive, QPalette::Text, QColor("#c01c28"));
    m_edit->setPalette(p);
  }
  SettingDef m_def;
  QLineEdit* m_edit;
};

// --- Enum ----------------------------------------------------------------------------------------
class EnumWidget : public ValueWidget
{
public:
  EnumWidget(const SettingDef& def, QWidget* parent) : ValueWidget(parent)
  {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_box = new QComboBox(this);
    for (const SettingOption& o : def.options)
      m_box->addItem(o.label, o.value);
    m_box->setMinimumWidth(200);
    l->addWidget(m_box);
    l->addStretch();
    connect(m_box, &QComboBox::activated, this, &ValueWidget::edited);
  }
  void setValue(const QVariant& v) override
  {
    int idx = m_box->findData(v.toString());
    if (idx < 0 && !v.toString().isEmpty())
    {
      m_box->addItem(v.toString(), v.toString());  // keep unknown values instead of replacing them
      idx = m_box->count() - 1;
    }
    m_box->setCurrentIndex(idx);  // -1 (empty) when nothing is known
  }
  QVariant value() const override { return m_box->currentIndex() < 0 ? QString() : m_box->currentData().toString(); }
  bool isAcceptable() const override { return m_box->currentIndex() >= 0; }

private:
  QComboBox* m_box;
};

// --- Rate / Quantum: editable combo --------------------------------------------------------------
class NumberComboWidget : public ValueWidget
{
public:
  NumberComboWidget(const SettingDef& def, std::function<int()> graphRate, QWidget* parent) : ValueWidget(parent), m_def(def), m_graphRate(graphRate)
  {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_box = new QComboBox(this);
    m_box->setEditable(true);
    m_box->setInsertPolicy(QComboBox::NoInsert);
    if (def.type == SettingDef::Rate)
    {
      for (int r : standardRates())
        m_box->addItem(QString::number(r));
    }
    else
    {
      m_box->addItems(standardQuanta());
    }
    auto* v = new QIntValidator(1, int(qMin(def.max, 2000000.0)), m_box);
    v->setLocale(QLocale::c());
    m_box->setValidator(v);
    m_box->setMinimumWidth(140);
    m_box->setMaximumWidth(200);
    l->addWidget(m_box);
    if (def.type == SettingDef::Rate)
      l->addWidget(new QLabel(tr("Hz"), this));
    l->addStretch();
    connect(m_box, &QComboBox::activated, this, &ValueWidget::edited);
    connect(m_box, &QComboBox::editTextChanged, this, &ValueWidget::edited);
  }
  void setValue(const QVariant& v) override
  {
    QSignalBlocker b(m_box);
    m_box->setEditText(v.toString());
  }
  QVariant value() const override { return m_box->currentText().trimmed(); }
  bool isAcceptable() const override
  {
    const int v = m_box->currentText().toInt();
    return v > 0 && v <= qMin(m_def.max, 2000000.0);
  }
  QString extraInfo() const override
  {
    if (m_def.type != SettingDef::Quantum || !m_graphRate)
      return {};
    const int rate = m_graphRate();
    const double q = m_box->currentText().toDouble();
    if (rate <= 0 || q <= 0)
      return {};
    return QStringLiteral("%1 ms").arg(q / rate * 1000.0, 0, 'f', 2);
  }

private:
  SettingDef m_def;
  std::function<int()> m_graphRate;
  QComboBox* m_box;
};

// --- Fraction: samples / rate --------------------------------------------------------------------
class FractionWidget : public ValueWidget
{
public:
  explicit FractionWidget(QWidget* parent) : ValueWidget(parent)
  {
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    m_num = new QSpinBox(this);
    m_num->setRange(1, 10000000);
    m_num->setMinimumWidth(110);
    m_den = new QComboBox(this);
    m_den->setEditable(true);
    for (int r : standardRates())
      m_den->addItem(QString::number(r));
    auto* v = new QIntValidator(1, 2000000, m_den);
    v->setLocale(QLocale::c());
    m_den->setValidator(v);
    m_den->setMinimumWidth(110);
    l->addWidget(m_num);
    l->addWidget(new QLabel("/", this));
    l->addWidget(m_den);
    l->addStretch();
    connect(m_num, &QSpinBox::valueChanged, this, &ValueWidget::edited);
    connect(m_den, &QComboBox::editTextChanged, this, &ValueWidget::edited);
  }
  void setValue(const QVariant& v) override
  {
    QSignalBlocker b1(m_num), b2(m_den);
    const QStringList parts = v.toString().split('/');
    m_num->setValue(parts.value(0).trimmed().toInt() > 0 ? parts.value(0).trimmed().toInt() : 1024);
    m_den->setEditText(parts.value(1).trimmed().isEmpty() ? QStringLiteral("48000") : parts.value(1).trimmed());
  }
  QVariant value() const override { return QStringLiteral("%1/%2").arg(m_num->value()).arg(m_den->currentText().trimmed()); }
  bool isAcceptable() const override { return m_den->currentText().toInt() > 0; }
  QString extraInfo() const override
  {
    const double ms = fractionToMs(value().toString());
    return ms < 0 ? QString() : QStringLiteral("%1 ms").arg(ms, 0, 'f', 2);
  }

private:
  QSpinBox* m_num;
  QComboBox* m_den;
};

// --- Lists: check boxes ----------------------------------------------------------------------------
class ListWidget : public ValueWidget
{
public:
  ListWidget(const SettingDef& def, QWidget* parent) : ValueWidget(parent), m_def(def)
  {
    // Long labels (e.g. the quirks) get one column so the row stays narrow.
    int longest = 0;
    for (const SettingOption& o : def.options)
      longest = qMax(longest, int(o.label.size()));
    m_columns = def.type == SettingDef::RateList ? 3 : (longest > 18 ? 1 : 2);
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(16);
    m_grid->setVerticalSpacing(2);
    if (def.type == SettingDef::RateList)
    {
      for (int r : standardRates())
        addItem(QString::number(r), QStringLiteral("%1 kHz").arg(r / 1000.0, 0, 'g', 5));
    }
    else
    {
      for (const SettingOption& o : def.options)
        addItem(o.value, o.label);
    }
  }
  void setValue(const QVariant& v) override
  {
    const QStringList items = SpaJson::toStringList(v);
    for (const QString& i : items)
    {
      if (!m_items.contains(i))  // keep values we do not know about
        addItem(i, m_def.type == SettingDef::RateList ? QStringLiteral("%1 Hz").arg(i) : i);
    }
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
    {
      QSignalBlocker b(it.value());
      it.value()->setChecked(items.contains(it.key()));
    }
  }
  QVariant value() const override
  {
    QStringList out;
    for (const QString& key : m_order)
    {
      if (m_items.value(key)->isChecked())
        out.append(key);
    }
    if (m_def.type == SettingDef::RateList)
      std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) { return a.toInt() < b.toInt(); });
    return out;
  }
  bool isAcceptable() const override { return m_def.type != SettingDef::RateList || !value().toStringList().isEmpty(); }

private:
  void addItem(const QString& value, const QString& label)
  {
    auto* box = new QCheckBox(label, this);
    const int n = m_order.size();
    m_grid->addWidget(box, n / m_columns, n % m_columns);
    m_items.insert(value, box);
    m_order.append(value);
    connect(box, &QCheckBox::toggled, this, &ValueWidget::edited);
  }

  SettingDef m_def;
  int m_columns = 2;
  QGridLayout* m_grid;
  QMap<QString, QCheckBox*> m_items;
  QStringList m_order;
};
}  // namespace

ValueWidget* ValueWidget::create(const SettingDef& def, std::function<int()> graphRate, QWidget* parent)
{
  switch (def.type)
  {
    case SettingDef::Bool:
      return new BoolWidget(parent);
    case SettingDef::Int:
    case SettingDef::Float:
    case SettingDef::String:
      return new TextWidget(def, parent);
    case SettingDef::Enum:
      return new EnumWidget(def, parent);
    case SettingDef::Rate:
    case SettingDef::Quantum:
      return new NumberComboWidget(def, graphRate, parent);
    case SettingDef::Fraction:
      return new FractionWidget(parent);
    case SettingDef::RateList:
    case SettingDef::List:
      return new ListWidget(def, parent);
  }
  return new TextWidget(def, parent);
}

// ---------------------------------------------------------------------------------------------

SettingEditor::SettingEditor(const SettingDef& def, const SettingBinding& binding, std::function<int()> graphRate, QWidget* parent)
  : QWidget(parent), m_def(def), m_binding(binding)
{
  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(0, 4, 0, 4);
  row->setSpacing(12);

  m_override = new QCheckBox(this);
  m_override->setToolTip(tr("Set this value in the configurator's config file. When off, the value comes from your other config files or PipeWire's defaults."));
  m_override->setAccessibleName(tr("Set %1").arg(def.label));
  row->addWidget(m_override, 0, Qt::AlignTop);

  auto* text = new QVBoxLayout();
  text->setSpacing(1);
  auto* title = new QLabel("<b>" + def.label.toHtmlEscaped() + "</b>", this);
  title->setWordWrap(true);
  title->setBuddy(m_override);
  auto* key = new QLabel(def.key, this);
  QFont mono("monospace");
  mono.setStyleHint(QFont::Monospace);
  mono.setPointSizeF(font().pointSizeF() * 0.88);
  key->setFont(mono);
  key->setTextInteractionFlags(Qt::TextSelectableByMouse);
  setDimmed(key, true);
  text->addWidget(title);
  text->addWidget(key);
  if (!def.help.isEmpty())
  {
    auto* help = new QLabel(def.help, this);
    help->setWordWrap(true);
    QFont small = help->font();
    small.setPointSizeF(small.pointSizeF() * 0.92);
    help->setFont(small);
    text->addWidget(help);
  }
  m_warning = new QLabel(this);
  m_warning->setWordWrap(true);
  m_warning->setStyleSheet("color: #b5650d;");
  m_warning->hide();
  text->addWidget(m_warning);
  auto* textHolder = new QWidget(this);
  textHolder->setLayout(text);
  textHolder->setMinimumWidth(220);
  textHolder->setMaximumWidth(330);
  row->addWidget(textHolder, 2, Qt::AlignTop);

  auto* right = new QVBoxLayout();
  right->setSpacing(3);
  auto* editorRow = new QHBoxLayout();
  m_editor = ValueWidget::create(def, graphRate, this);
  editorRow->addWidget(m_editor, 1);
  m_extra = new QLabel(this);
  setDimmed(m_extra, true);
  editorRow->addWidget(m_extra);
  right->addLayout(editorRow);
  m_source = new QLabel(this);
  m_source->setWordWrap(true);
  QFont small = m_source->font();
  small.setPointSizeF(small.pointSizeF() * 0.9);
  m_source->setFont(small);
  m_source->setTextInteractionFlags(Qt::TextSelectableByMouse);
  right->addWidget(m_source);
  row->addLayout(right, 3);

  connect(m_override, &QCheckBox::toggled, this, &SettingEditor::onOverrideToggled);
  connect(m_editor, &ValueWidget::edited, this, &SettingEditor::onEdited);
  refresh();
}

bool SettingEditor::editorHasValue() const
{
  if (!m_editor->isAcceptable())
    return false;
  const QVariant v = m_editor->value();
  return m_def.isListType() ? !SpaJson::toStringList(v).isEmpty() : !v.toString().trimmed().isEmpty();
}

bool SettingEditor::isOverridden() const
{
  const Values* v = m_binding.values(false);
  return v && v->contains(m_def.key);
}

QString SettingEditor::effectiveValue() const
{
  const Values* v = m_binding.values(false);
  if (v && v->contains(m_def.key))
    return valueText(v->value(m_def.key));
  const ValueOrigin inherited = m_binding.inherited ? m_binding.inherited(m_def.key) : ValueOrigin();
  if (inherited.isValid())
    return valueText(inherited.value);
  const QString live = m_binding.live ? m_binding.live(m_def.key) : QString();
  if (!live.isEmpty())
    return live;
  return m_def.defaultValue;
}

void SettingEditor::refresh()
{
  m_updating = true;
  const bool on = isOverridden();
  if (on)
    m_pending = false;
  m_override->setChecked(on || m_pending);
  if (on)
  {
    m_editor->setValue(m_binding.values(false)->value(m_def.key));
  }
  else if (!m_pending)  // keep what the user has entered so far
  {
    const QString shown = effectiveValue();
    m_editor->setValue(m_def.isListType() ? QVariant(SpaJson::toStringList(shown)) : QVariant(shown));
  }
  m_editor->setEnabled(on || m_pending);
  m_updating = false;
  updateHints();
}

void SettingEditor::onOverrideToggled(bool on)
{
  if (m_updating)
    return;
  m_pending = false;
  if (on)
  {
    // Pin the value shown. When nothing is known (no default, no other config file, not running),
    // wait for the user to enter one instead of writing an empty or made-up value.
    if (editorHasValue())
      m_binding.values(true)->insert(m_def.key, m_editor->value());
    else
      m_pending = true;
  }
  else if (Values* v = m_binding.values(false))
  {
    v->remove(m_def.key);
  }
  refresh();
  if (on)
    m_editor->setFocus();
  emit changed();
}

void SettingEditor::onEdited()
{
  if (m_updating || !m_override->isChecked())
    return;
  if (editorHasValue())
  {
    m_binding.values(true)->insert(m_def.key, m_editor->value());
    m_pending = false;
  }
  else if (m_def.isListType())
  {
    // All boxes cleared: nothing to write, but the row stays ticked for a new choice.
    if (Values* v = m_binding.values(false))
      v->remove(m_def.key);
    m_pending = true;
  }
  updateHints();
  emit changed();
}

void SettingEditor::updateHints()
{
  const bool on = isOverridden();
  m_extra->setText(m_editor->extraInfo());

  QStringList lines;
  const ValueOrigin inherited = m_binding.inherited ? m_binding.inherited(m_def.key) : ValueOrigin();
  const QString live = m_binding.live ? m_binding.live(m_def.key) : QString();
  auto show = [this](const QString& v) { return m_def.isListType() ? displayList(m_def, SpaJson::toStringList(v)) : (m_def.type == SettingDef::Enum ? m_def.optionLabel(v) : v); };

  if (m_pending && !on)
  {
    const bool choice = m_def.type == SettingDef::Bool || m_def.type == SettingDef::Enum || m_def.isListType();
    lines.append(choice ? tr("Choose a value to set it here.") : tr("Enter a value to set it here."));
  }
  else if (on)
  {
    QString line = tr("Set here.");
    if (inherited.isValid())
      line += " " + tr("Without it: %1 (from %2)").arg(show(valueText(inherited.value)), QFileInfo(inherited.file).fileName());
    else if (!m_def.defaultValue.isEmpty())
      line += " " + tr("Default: %1").arg(show(m_def.defaultValue));
    lines.append(line);
  }
  else if (inherited.isValid())
  {
    lines.append(tr("From %1").arg(inherited.file));
  }
  else if (!m_binding.inheritedLabel.isEmpty())
  {
    lines.append(m_binding.inheritedLabel);
  }
  else if (!live.isEmpty())
  {
    lines.append(tr("Not set; this is what is in use now."));
  }
  else if (!m_def.defaultValue.isEmpty())
  {
    lines.append(tr("Not set; the default is %1.").arg(show(m_def.defaultValue)));
  }
  else
  {
    lines.append(tr("Not set; PipeWire decides."));
  }
  if (!live.isEmpty() && on && !sameValue(m_def, valueText(m_binding.values(false)->value(m_def.key)), live))
    lines.append(tr("Currently in use: %1 (applies after Save and Apply)").arg(show(live)));

  const ValueOrigin later = m_binding.overriddenBy ? m_binding.overriddenBy(m_def.key) : ValueOrigin();
  QString warning;
  if (on && later.isValid() && !later.value.isValid())
    warning = tr("⚠ %1 loads after this tool's file and replaces this whole section, so your value has no effect.").arg(later.file);
  else if (on && later.isValid())
    warning = tr("⚠ %1 loads after this tool's file and sets this to %2, so your value has no effect.").arg(later.file, show(valueText(later.value)));
  else if (on && !m_def.warning.isEmpty())
    warning = "⚠ " + m_def.warning;
  m_warning->setText(warning);
  m_warning->setVisible(!warning.isEmpty());

  m_source->setText(lines.join('\n'));
  setDimmed(m_source, !on && !m_pending);
}
