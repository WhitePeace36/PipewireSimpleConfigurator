#include "SettingsPage.h"

#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SettingsPage::SettingsPage(const QString& title, const QString& subtitle, QWidget* parent) : QWidget(parent)
{
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(20, 16, 20, 20);
  m_layout->setSpacing(14);

  auto* titleLabel = new QLabel(title, this);
  QFont f = titleLabel->font();
  f.setPointSizeF(f.pointSizeF() * 1.45);
  f.setBold(true);
  titleLabel->setFont(f);
  titleLabel->setWordWrap(true);
  titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_layout->addWidget(titleLabel);

  if (!subtitle.isEmpty())
  {
    auto* sub = new QLabel(subtitle, this);
    sub->setWordWrap(true);
    sub->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_layout->addWidget(sub);
  }

  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::RichText);
  m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_status->hide();
  m_layout->addWidget(m_status);

  auto* how = new QLabel(tr("Tick a setting to set it here. Unticked settings show the value that applies without the configurator."), this);
  m_how = how;
  how->setWordWrap(true);
  QPalette dim = how->palette();
  dim.setColor(QPalette::WindowText,
               QColor::fromRgbF(dim.color(QPalette::WindowText).redF(), dim.color(QPalette::WindowText).greenF(), dim.color(QPalette::WindowText).blueF(), 0.7));
  how->setPalette(dim);
  m_layout->addWidget(how);

  m_actions = new QWidget(this);
  auto* actionsLayout = new QHBoxLayout(m_actions);
  actionsLayout->setContentsMargins(0, 0, 0, 0);
  m_layout->addWidget(m_actions);

  auto* notices = new QWidget(this);
  m_notices = new QVBoxLayout(notices);
  m_notices->setContentsMargins(0, 0, 0, 0);
  m_layout->addWidget(notices);
}

void SettingsPage::setStatus(const QString& text)
{
  m_status->setText(text);
  m_status->setVisible(!text.isEmpty());
}

void SettingsPage::addNotice(const QString& text, bool error)
{
  auto* l = new QLabel(text, this);
  l->setWordWrap(true);
  l->setTextFormat(Qt::RichText);
  l->setOpenExternalLinks(false);
  l->setMargin(10);
  l->setStyleSheet(error ? "background: rgba(224,27,36,0.14); border-radius: 6px;" : "background: rgba(229,165,10,0.18); border-radius: 6px;");
  m_notices->addWidget(l);
}

void SettingsPage::addSection(const QString& scope, const SettingBinding& binding, const QString& heading, const QString& headingNote)
{
  Section section;
  section.scope = scope;

  if (!heading.isEmpty())
  {
    auto* h = new QLabel(heading, this);
    QFont f = h->font();
    f.setPointSizeF(f.pointSizeF() * 1.2);
    f.setBold(true);
    h->setFont(f);
    m_layout->addSpacing(8);
    m_layout->addWidget(h);
    if (!headingNote.isEmpty())
    {
      auto* n = new QLabel(headingNote, this);
      n->setWordWrap(true);
      n->setTextFormat(Qt::RichText);
      m_layout->addWidget(n);
    }
  }

  section.warnings = new QLabel(this);
  section.warnings->setWordWrap(true);
  section.warnings->setMargin(10);
  section.warnings->setStyleSheet("background: rgba(229,165,10,0.18); border-radius: 6px;");
  section.warnings->hide();
  m_layout->addWidget(section.warnings);

  for (const GroupDef& group : Schema::instance().groupsFor(scope))
  {
    auto* box = new QGroupBox(group.title, this);
    auto* boxLayout = new QVBoxLayout(box);
    boxLayout->setSpacing(4);
    if (!group.description.isEmpty())
    {
      auto* d = new QLabel(group.description, box);
      d->setWordWrap(true);
      boxLayout->addWidget(d);
    }

    const QList<PresetDef> presets = group.presetsFor(scope);
    if (!presets.isEmpty())
    {
      auto* row = new QHBoxLayout();
      row->addWidget(new QLabel(tr("Presets:"), box));
      for (const PresetDef& preset : presets)
      {
        auto* b = new QPushButton(preset.label, box);
        QStringList tip;
        for (auto it = preset.values.begin(); it != preset.values.end(); ++it)
          tip.append(it.key() + " = " + it.value());
        if (!preset.clear.isEmpty() && preset.values.isEmpty())
          tip.append(tr("Removes these settings from the configurator's file, so PipeWire's defaults apply again."));
        b->setToolTip(tip.join('\n'));
        connect(b, &QPushButton::clicked, this,
                [this, preset, binding]
                {
                  Values* v = binding.values(!preset.values.isEmpty());
                  if (v)
                  {
                    for (const QString& k : preset.clear)
                      v->remove(k);
                    for (auto it = preset.values.begin(); it != preset.values.end(); ++it)
                      v->insert(it.key(), it.value());
                  }
                  refresh();
                  emit changed();
                });
        row->addWidget(b);
      }
      row->addStretch();
      boxLayout->addLayout(row);
    }

    const QList<SettingDef> settings = group.settingsFor(scope);
    for (int i = 0; i < settings.size(); ++i)
    {
      if (i > 0)
      {
        auto* line = new QFrame(box);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        line->setProperty("separatorFor", settings[i].key);
        boxLayout->addWidget(line);
      }
      auto* editor = new SettingEditor(settings[i], binding, [this] { return m_graphRate ? m_graphRate() : 48000; }, box);
      connect(editor, &SettingEditor::changed, this,
              [this]
              {
                updateDerived();
                emit changed();
              });
      boxLayout->addWidget(editor);
      section.editors.append(editor);
    }
    m_layout->addWidget(box);
  }
  m_sections.append(section);
}

void SettingsPage::addWidget(QWidget* w)
{
  m_layout->addWidget(w);
}

void SettingsPage::finish()
{
  m_how->setVisible(!m_sections.isEmpty());
  m_layout->addStretch(1);
  updateDerived();
}

void SettingsPage::refresh()
{
  for (const Section& s : m_sections)
  {
    for (SettingEditor* e : s.editors)
      e->refresh();
  }
  updateDerived();
}

QString SettingsPage::valueIn(const Section& s, const QString& key) const
{
  for (SettingEditor* e : s.editors)
  {
    if (e->def().key == key)
      return e->effectiveValue();
  }
  return {};
}

void SettingsPage::updateDerived()
{
  for (const Section& s : m_sections)
  {
    // Show settings that only apply to a certain choice (e.g. Kaiser parameters).
    for (SettingEditor* e : s.editors)
    {
      if (e->def().showIfKey.isEmpty())
        continue;
      const QString v = valueIn(s, e->def().showIfKey);
      const bool visible = e->def().showIfValues.contains(v) || e->isOverridden();
      e->setVisible(visible);
      for (QFrame* line : e->parentWidget()->findChildren<QFrame*>())
      {
        if (line->property("separatorFor").toString() == e->def().key)
          line->setVisible(visible);
      }
    }
    // Ms hints depend on other values (graph rate), so refresh them too.
    for (SettingEditor* e : s.editors)
    {
      if (e->def().type == SettingDef::Quantum)
        e->refreshHints();
    }

    const QStringList warnings = runChecks(
      s.scope, [this, &s](const QString& k) { return valueIn(s, k); },
      [this, &s](const QString& k)
      {
        if (s.scope == Scope::Graph)
          return valueIn(s, k);
        return m_graphValue ? m_graphValue(k) : QString();
      });
    QStringList items;
    for (const QString& w : warnings)
      items.append("⚠ " + w.toHtmlEscaped());
    s.warnings->setText(items.join("<br>"));
    s.warnings->setVisible(!items.isEmpty());
  }
}
