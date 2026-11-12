#include "MainWindow.h"

#include "SettingsPage.h"
#include "SpaJson.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
constexpr int IdRole = Qt::UserRole;

// Paths are shown relative to the home directory so they fit and can wrap.
QString shortPath(const QString& path)
{
  const QString home = QDir::homePath();
  return path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path;
}

QString mono(const QString& s)
{
  return "<code>" + shortPath(s).toHtmlEscaped() + "</code>";
}

QString stateText(const QString& state)
{
  if (state == "running")
    return QObject::tr("Playing");
  if (state == "idle")
    return QObject::tr("Idle");
  if (state == "suspended")
    return QObject::tr("Suspended");
  return state;
}

// Graph settings and the "settings" metadata keys that report them live.
QString metadataKeyFor(const QString& key)
{
  if (key.startsWith("default.clock."))
    return key.mid(QStringLiteral("default.").size());
  return {};
}
}  // namespace

MainWindow::MainWindow(const Options& options, QWidget* parent) : QMainWindow(parent), m_options(options), m_paths(options.paths)
{
  setWindowTitle(tr("Pipewire Configurator") + "[*]");
  resize(1100, 820);
  setMinimumSize(900, 560);

  m_live = new LiveState(this);
  if (!options.pwDumpFile.isEmpty())
    m_live->setDumpFile(options.pwDumpFile);
  m_services = new Services(this);

  m_wpVersion = options.wireplumberVersion.isEmpty() ? Services::wireplumberVersion() : options.wireplumberVersion;
  m_wpSupported = m_wpVersion.isEmpty() || Services::versionAtLeast(m_wpVersion, 0, 5);

  // --- Layout ---
  auto* central = new QWidget(this);
  auto* outer = new QVBoxLayout(central);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  m_legacyBanner = new QWidget(central);
  m_legacyBanner->setStyleSheet("background: rgba(229,165,10,0.20);");
  auto* bannerLayout = new QHBoxLayout(m_legacyBanner);
  bannerLayout->setContentsMargins(14, 8, 14, 8);
  m_legacyText = new QLabel(m_legacyBanner);
  m_legacyText->setWordWrap(true);
  bannerLayout->addWidget(m_legacyText, 1);
  auto* importButton = new QPushButton(tr("Import and retire copies…"), m_legacyBanner);
  connect(importButton, &QPushButton::clicked, this, &MainWindow::importLegacy);
  bannerLayout->addWidget(importButton);
  outer->addWidget(m_legacyBanner);

  auto* splitter = new QSplitter(central);
  m_tree = new QTreeWidget(splitter);
  m_tree->setHeaderHidden(true);
  m_tree->setRootIsDecorated(true);
  m_tree->setMinimumWidth(240);
  m_tree->setUniformRowHeights(true);
  m_scroll = new QScrollArea(splitter);
  m_scroll->setWidgetResizable(true);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_scroll->setFrameShape(QFrame::NoFrame);
  splitter->addWidget(m_tree);
  splitter->addWidget(m_scroll);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({270, 830});
  outer->addWidget(splitter, 1);

  auto* footer = new QFrame(central);
  footer->setFrameShape(QFrame::StyledPanel);
  auto* footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(12, 8, 12, 8);
  m_busy = new QProgressBar(footer);
  m_busy->setRange(0, 0);
  m_busy->setMaximumWidth(80);
  m_busy->setTextVisible(false);
  m_busy->hide();
  footerLayout->addWidget(m_busy);
  m_statusLabel = new QLabel(footer);
  m_statusLabel->setWordWrap(true);
  m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  footerLayout->addWidget(m_statusLabel, 1);
  m_revertButton = new QPushButton(tr("Revert"), footer);
  m_revertButton->setToolTip(tr("Discard unsaved changes"));
  m_saveButton = new QPushButton(tr("Save"), footer);
  m_saveButton->setToolTip(tr("Write the configuration files without restarting anything (Ctrl+S)"));
  m_applyButton = new QPushButton(tr("Save and Apply"), footer);
  m_applyButton->setToolTip(tr("Save, then restart the audio services that read the changed files"));
  m_applyButton->setDefault(true);
  footerLayout->addWidget(m_revertButton);
  footerLayout->addWidget(m_saveButton);
  footerLayout->addWidget(m_applyButton);
  outer->addWidget(footer);
  setCentralWidget(central);

  connect(m_revertButton, &QPushButton::clicked, this, &MainWindow::revert);
  connect(m_saveButton, &QPushButton::clicked, this, [this] { save(); });
  connect(m_applyButton, &QPushButton::clicked, this, &MainWindow::apply);
  connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, [this] { save(); });
  connect(new QShortcut(QKeySequence::Refresh, this), &QShortcut::activated, m_live, &LiveState::refresh);

  connect(m_tree, &QTreeWidget::currentItemChanged, this,
          [this](QTreeWidgetItem* item)
          {
            if (item)
              showPage(item->data(0, IdRole).toString());
          });
  connect(m_live, &LiveState::updated, this,
          [this]
          {
            rebuildTree();
            // Rebuild the page for fresh live information, but never while the user is typing in it.
            QWidget* focus = QApplication::focusWidget();
            if (m_page && !(focus && m_page->isAncestorOf(focus)))
              rebuildPage();
          });
  connect(m_services, &Services::busyChanged, this,
          [this]
          {
            m_busy->setVisible(m_services->busy());
            updateButtons();
          });
  connect(m_services, &Services::finished, this,
          [this](bool ok, const QString& message)
          {
            if (!message.isEmpty())
              setStatus(message, !ok);
            QTimer::singleShot(1500, m_live, &LiveState::refresh);
          });

  auto* timer = new QTimer(this);
  timer->setInterval(10000);
  connect(timer, &QTimer::timeout, this,
          [this]
          {
            if (isActiveWindow() && m_options.pwDumpFile.isEmpty())
              m_live->refresh();
          });
  timer->start();

  reloadFromDisk();
  rebuildTree();
  showPage("graph");
  m_live->refresh();
}

// ---------------------------------------------------------------------------------------------
// State

void MainWindow::reloadFromDisk()
{
  m_loadWarnings.clear();
  m_unreadable.clear();
  m_model = ConfigModel::load(m_paths, &m_loadWarnings, &m_unreadable);
  m_baseline = m_model.render();
  for (ConfigFile f : allConfigFiles())
  {
    m_effective[f] = EffectiveConfig::load(m_paths, f);
    m_loadWarnings += m_effective[f].errors;
  }

  const LegacyImport legacy = findLegacyCopies(m_paths);
  QStringList names;
  for (const QString& f : legacy.files)
    names.append(QFileInfo(f).fileName());
  m_legacyText->setText(tr("<b>Full copies of %1 are in %2.</b> They replace the system files completely, so they hide PipeWire updates "
                           "and settings made here may not combine with them as expected. Import their settings into the configurator and rename the copies to *.bak.")
                          .arg(names.join(", "), m_paths.userDir(ConfigFile::Pipewire).toHtmlEscaped()));
  m_legacyBanner->setVisible(!legacy.files.isEmpty());

  setWindowModified(false);
  updateButtons();
  if (!m_loadWarnings.isEmpty())
    setStatus(m_loadWarnings.join("\n"), true);
}

void MainWindow::onModelChanged()
{
  setWindowModified(isDirty());
  updateButtons();
  if (isDirty())
    setStatus(tr("Unsaved changes."));
}

void MainWindow::updateButtons()
{
  const bool dirty = isDirty();
  m_revertButton->setEnabled(dirty);
  m_saveButton->setEnabled(dirty);
  m_applyButton->setEnabled(!m_services->busy());
}

void MainWindow::setStatus(const QString& text, bool error)
{
  m_statusLabel->setText(text);
  QPalette p = m_statusLabel->palette();
  p.setColor(QPalette::WindowText, error ? QColor("#c01c28") : palette().color(QPalette::WindowText));
  m_statusLabel->setPalette(p);
}

bool MainWindow::save()
{
  m_model.removeEmptyRules();
  const QMap<ConfigFile, QString> files = m_model.render();
  QStringList written, removed, backedUp;
  for (auto it = files.begin(); it != files.end(); ++it)
  {
    const QString path = m_paths.ownFile(it.key());
    const bool unreadable = m_unreadable.contains(it.key()) && QFile::exists(path);
    if (it.value() == m_baseline.value(it.key()) && (unreadable || QFile::exists(path) == !it.value().isEmpty()))
      continue;  // unchanged; a file that did not parse is left exactly as the user wrote it
    if (unreadable)
    {
      // Its settings changed, so it has to be replaced: keep the hand-edited version next to it.
      const QString backup = backupPath(path);
      if (!QFile::rename(path, backup))
      {
        setStatus(tr("Could not rename %1 to keep a copy of it. Nothing more was saved.").arg(path), true);
        return false;
      }
      backedUp.append(backup);
    }
    if (it.value().isEmpty())
    {
      if (QFile::exists(path) && !QFile::remove(path))
      {
        setStatus(tr("Could not remove %1.").arg(path), true);
        return false;
      }
      if (!unreadable)
        removed.append(path);
    }
    else
    {
      QDir().mkpath(QFileInfo(path).absolutePath());
      QSaveFile out(path);
      if (!out.open(QIODevice::WriteOnly | QIODevice::Text) || out.write(it.value().toUtf8()) < 0 || !out.commit())
      {
        setStatus(tr("Could not write %1: %2").arg(path, out.errorString()), true);
        return false;
      }
      written.append(path);
    }
    m_unapplied.insert(it.key());
  }

  const QString current = m_currentId;
  reloadFromDisk();
  rebuildTree();
  showPage(current);

  QStringList parts;
  if (!written.isEmpty())
    parts.append(tr("Saved %1.").arg(written.join(", ")));
  if (!removed.isEmpty())
    parts.append(tr("Removed %1 (nothing left to set).").arg(removed.join(", ")));
  if (!backedUp.isEmpty())
    parts.append(tr("The unreadable files were kept as %1.").arg(backedUp.join(", ")));
  if (parts.isEmpty())
    parts.append(tr("Nothing to save."));
  if (!m_loadWarnings.isEmpty())
    setStatus(m_loadWarnings.join("\n") + "\n" + parts.join(' '), true);
  else
    setStatus(parts.join(' '));
  return true;
}

void MainWindow::apply()
{
  if (isDirty() && !save())
    return;

  QSet<ConfigFile> files = m_unapplied;
  if (files.isEmpty())
    files = {ConfigFile::Pipewire};  // nothing pending: restart everything on request

  QStringList units;
  if (files.contains(ConfigFile::Pipewire))
    units << "pipewire.service" << "pipewire-pulse.service" << "wireplumber.service";
  if (files.contains(ConfigFile::Pulse) && !units.contains("pipewire-pulse.service"))
    units << "pipewire-pulse.service";
  if (files.contains(ConfigFile::WirePlumber) && !units.contains("wireplumber.service"))
    units << "wireplumber.service";

  QString question;
  if (units.isEmpty())
  {
    m_unapplied.clear();
    setStatus(tr("Saved. Native PipeWire and JACK applications use the new settings when you restart them."));
    return;
  }
  question = tr("This restarts %1. Audio stops for a moment and some applications may have to be restarted.").arg(units.join(", "));
  if (files.contains(ConfigFile::Client) || files.contains(ConfigFile::ClientRt) || files.contains(ConfigFile::Jack))
    question += "\n\n" + tr("Native PipeWire and JACK applications pick up the new settings when they are restarted.");
  if (QMessageBox::question(this, tr("Apply settings"), question, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes) != QMessageBox::Yes)
    return;

  m_unapplied.clear();
  setStatus(tr("Restarting %1…").arg(units.join(", ")));
  m_services->restart(units);
}

void MainWindow::revert()
{
  const QString current = m_currentId;
  reloadFromDisk();
  rebuildTree();
  showPage(current);
  setStatus(tr("Changes discarded."));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  if (!isDirty() || m_closeConfirmed)
  {
    event->accept();
    return;
  }
  const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save your changes before closing? Saving does not restart the audio services."),
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save()))
  {
    event->ignore();
    return;
  }
  event->accept();
}

void MainWindow::importLegacy()
{
  ConfigModel candidate = m_model;
  const LegacyImport result = importLegacyCopies(m_paths, candidate);
  QString text = result.imported.isEmpty() ? tr("The copies contain no settings that differ from the system files in a way the configurator manages.")
                                           : tr("These settings will be taken over:") + "\n\n" + result.imported.join('\n');
  text += "\n\n" + tr(
                     "The copies are then renamed to *.bak-<date>, so PipeWire uses the system files again plus the configurator's drop-ins. "
                     "Settings in the copies that the configurator does not manage are not taken over; compare with the .bak files if you edited them by hand.");
  if (isDirty())
    text += "\n\n" + tr("Your other unsaved changes are saved at the same time.");
  if (QMessageBox::question(this, tr("Import settings"), text, QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) != QMessageBox::Ok)
    return;

  // Rename first (all or nothing), and only then take over the settings, so a failure changes nothing:
  // neither the files on disk nor the settings being edited.
  QStringList backups;
  QString error;
  if (!retireLegacyCopies(result.files, &backups, &error))
  {
    setStatus(error, true);
    return;
  }
  m_model = candidate;
  m_unapplied.insert(ConfigFile::Pipewire);  // the main files changed: restart everything
  if (save())
    setStatus(tr("Imported %n setting(s) and renamed the copies. Use Save and Apply to restart the audio services.", nullptr, int(result.imported.size())));
}

// ---------------------------------------------------------------------------------------------
// Navigation

QTreeWidgetItem* MainWindow::addItem(QTreeWidgetItem* parent, const QString& id, const QString& text, const QString& icon)
{
  auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
  item->setText(0, text);
  item->setData(0, IdRole, id);
  item->setIcon(0, QIcon::fromTheme(icon));
  return item;
}

void MainWindow::rebuildTree()
{
  QSignalBlocker blocker(m_tree);
  m_tree->clear();
  const LiveSnapshot& live = m_live->snapshot();

  addItem(nullptr, "graph", tr("Graph"), "media-playlist-shuffle");
  addItem(nullptr, "stream", tr("Streams"), "audio-x-generic");
  addItem(nullptr, "pulse", tr("PulseAudio bridge"), "preferences-desktop-sound");

  auto addDevices = [&](const QString& id, const QString& title, const QString& icon, bool outputs)
  {
    QTreeWidgetItem* header = addItem(nullptr, id, title, icon);
    const QString def = outputs ? live.defaultSink : live.defaultSource;
    for (const QString& name : alsaNodeNames(outputs))
    {
      QTreeWidgetItem* item = addItem(header, "alsa:" + name, alsaLabel(name), icon);
      const LiveNode* node = live.node(name);
      QFont f = item->font(0);
      if (name == def)
      {
        f.setBold(true);
        item->setText(0, item->text(0) + "  " + tr("(default)"));
      }
      if (live.valid && !node)
      {
        f.setItalic(true);
        item->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
        item->setToolTip(0, tr("Not connected. Its settings are kept."));
      }
      else
      {
        item->setToolTip(0, name);
      }
      item->setFont(0, f);
    }
    header->setExpanded(true);
  };
  addDevices("outputs", tr("Output devices"), "audio-speakers", true);
  addDevices("inputs", tr("Input devices"), "audio-input-microphone", false);

  QTreeWidgetItem* bt = addItem(nullptr, "bluetooth", tr("Bluetooth"), "bluetooth");
  for (const QString& addr : btAddresses())
  {
    QTreeWidgetItem* item = addItem(bt, "bt:" + addr, btLabel(addr), "audio-headphones");
    if (live.valid && !live.btDevice(addr.toUpper().replace('_', ':')))
    {
      QFont f = item->font(0);
      f.setItalic(true);
      item->setFont(0, f);
      item->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
      item->setToolTip(0, tr("Not connected. Its settings are kept."));
    }
  }
  bt->setExpanded(true);

  QTreeWidgetItem* apps = addItem(nullptr, "apps", tr("Applications"), "applications-multimedia");
  for (const Rule& r : m_model.appRules)
    addItem(apps, "app:" + r.matchValue, r.matchValue, "application-x-executable");
  apps->setExpanded(true);

  QTreeWidgetItem* files = addItem(nullptr, "files", tr("Config files"), "document-properties");
  if (m_legacyBanner->isVisible() || !m_loadWarnings.isEmpty())
    files->setIcon(0, QIcon::fromTheme("dialog-warning"));

  // Restore the selection.
  for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
  {
    if ((*it)->data(0, IdRole).toString() == m_currentId)
    {
      m_tree->setCurrentItem(*it);
      break;
    }
  }
}

void MainWindow::showPage(const QString& id)
{
  // An application page needs its (possibly still empty) rule so it shows up in the tree.
  if (id.startsWith("app:") && !ConfigModel::findRule(m_model.appRules, "application.process.binary", id.mid(4)))
  {
    m_model.appRules.append({"application.process.binary", id.mid(4), {}});
    m_currentId = id;
    rebuildTree();
  }
  m_currentId = id;
  rebuildPage();
  m_scroll->verticalScrollBar()->setValue(0);
  for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
  {
    if ((*it)->data(0, IdRole).toString() == id && m_tree->currentItem() != *it)
    {
      QSignalBlocker b(m_tree);
      m_tree->setCurrentItem(*it);
      break;
    }
  }
}

void MainWindow::rebuildPage()
{
  const QString id = m_currentId;
  const int scroll = m_scroll->verticalScrollBar()->value();
  SettingsPage* page = nullptr;
  if (id == "graph")
    page = buildGraphPage();
  else if (id == "stream")
    page = buildStreamPage();
  else if (id == "pulse")
    page = buildPulsePage();
  else if (id == "outputs" || id == "inputs")
    page = buildDeviceListPage(id == "outputs");
  else if (id.startsWith("alsa:"))
    page = buildAlsaPage(id.mid(5));
  else if (id == "bluetooth")
    page = buildBluetoothPage();
  else if (id.startsWith("bt:"))
    page = buildBtDevicePage(id.mid(3));
  else if (id == "apps")
    page = buildAppsPage();
  else if (id.startsWith("app:"))
    page = buildAppPage(id.mid(4));
  else
    page = buildFilesPage();

  page->setGraphRate([this] { return graphRate(); });
  page->setGraphValue([this](const QString& k) { return graphValue(k); });
  page->finish();
  connect(page, &SettingsPage::changed, this, &MainWindow::onModelChanged);
  m_scroll->setWidget(page);  // deletes the previous page
  m_page = page;
  m_scroll->verticalScrollBar()->setValue(scroll);
}

// ---------------------------------------------------------------------------------------------
// Bindings and values

SettingBinding MainWindow::globalBinding(Values ConfigModel::*member, ConfigFile file, const QString& section, const QList<ConfigFile>& alsoCheck)
{
  SettingBinding b;
  b.values = [this, member](bool) { return &(m_model.*member); };
  b.inherited = [this, file, section](const QString& key) { return m_effective.value(file).value(section, key); };
  b.overriddenBy = [this, file, section, alsoCheck](const QString& key)
  {
    ValueOrigin o = m_effective.value(file).overriddenBy(section, key);
    for (ConfigFile f : alsoCheck)
    {
      if (!o.isValid())
        o = m_effective.value(f).overriddenBy(section, key);
    }
    return o;
  };
  return b;
}

SettingBinding MainWindow::ruleBinding(QList<Rule> ConfigModel::*list, const QString& key, const QString& value, std::function<const LiveNode*()> live)
{
  SettingBinding b;
  b.values = [this, list, key, value](bool create) -> Values*
  {
    Rule* r = ConfigModel::findRule(m_model.*list, key, value, create);
    return r ? &r->props : nullptr;
  };
  b.live = [live](const QString& k) -> QString
  {
    const LiveNode* n = live ? live() : nullptr;
    return n ? n->currentValue(k) : QString();
  };
  return b;
}

QString MainWindow::graphValue(const QString& key) const
{
  if (m_model.graph.contains(key))
    return valueText(m_model.graph.value(key));
  const ValueOrigin o = m_effective.value(ConfigFile::Pipewire).value("context.properties", key);
  if (o.isValid())
    return valueText(o.value);
  if (const SettingDef* d = Schema::instance().find(key))
    return d->defaultValue;
  return {};
}

int MainWindow::graphRate() const
{
  const int rate = graphValue("default.clock.rate").toInt();
  return rate > 0 ? rate : 48000;
}

QStringList MainWindow::alsaNodeNames(bool outputs) const
{
  const QString prefix = outputs ? "alsa_output." : "alsa_input.";
  QStringList names;
  for (const LiveNode* n : m_live->snapshot().nodesOf(outputs ? LiveNode::AlsaOutput : LiveNode::AlsaInput))
    names.append(n->name);
  for (const Rule& r : m_model.alsaRules)
  {
    if (r.matchKey == "node.name" && r.matchValue.startsWith(prefix) && !names.contains(r.matchValue))
      names.append(r.matchValue);
  }
  return names;
}

QStringList MainWindow::btAddresses() const
{
  QStringList out;
  for (const LiveDevice& d : m_live->snapshot().devices)
  {
    if (d.api == "bluez5" && !d.btAddress.isEmpty())
      out.append(ConfigModel::btId(d.btAddress));
  }
  static const QRegularExpression re(R"(^~?bluez_(?:card|output|input)\.([0-9A-Fa-f_]{17}))");
  for (const Rule& r : m_model.bluezRules)
  {
    const auto m = re.match(r.matchValue);
    if (m.hasMatch() && !out.contains(m.captured(1).toUpper()))
      out.append(m.captured(1).toUpper());
  }
  return out;
}

QString MainWindow::alsaLabel(const QString& nodeName) const
{
  if (const LiveNode* n = m_live->snapshot().node(nodeName))
  {
    if (!n->description.isEmpty())
      return n->description;
  }
  if (const Rule* r = ConfigModel::findRule(m_model.alsaRules, "node.name", nodeName))
  {
    if (r->props.contains("node.description"))
      return r->props.value("node.description").toString();
  }
  return nodeName;
}

QString MainWindow::btLabel(const QString& address) const
{
  if (const LiveDevice* d = m_live->snapshot().btDevice(QString(address).replace('_', ':')))
  {
    if (!d->description.isEmpty())
      return d->description;
  }
  return QString(address).replace('_', ':');
}

void MainWindow::addWirePlumberNotice(SettingsPage* page)
{
  if (m_wpSupported)
    return;
  page->addNotice(tr("Device and Bluetooth settings need WirePlumber 0.5 or newer. You have WirePlumber %1, which uses Lua config files instead, "
                     "so these settings are shown read-only.")
                    .arg(m_wpVersion));
}

// ---------------------------------------------------------------------------------------------
// Pages

SettingsPage* MainWindow::buildGraphPage()
{
  auto* page =
    new SettingsPage(tr("Graph"), tr("The clock all audio runs on, shared by every output, input and application. Saved to %1.").arg(mono(m_paths.ownFile(ConfigFile::Pipewire))));
  const LiveSnapshot& live = m_live->snapshot();
  if (live.valid)
  {
    QStringList now;
    now.append(tr("Running now: %1 Hz, quantum %2").arg(live.settings.value("clock.rate").toString(), live.settings.value("clock.quantum").toString()));
    const QString forcedRate = live.settings.value("clock.force-rate").toString();
    const QString forcedQuantum = live.settings.value("clock.force-quantum").toString();
    if (forcedRate.toInt() > 0 || forcedQuantum.toInt() > 0)
      now.append("<b>" + tr("Being tried: rate %1, quantum %2 (until PipeWire restarts)").arg(forcedRate, forcedQuantum) + "</b>");
    page->setStatus(now.join("<br>"));
  }
  else if (!live.error.isEmpty())
  {
    page->setStatus(live.error.toHtmlEscaped());
  }

  auto* actions = page->actionArea()->layout();
  auto* tryButton = new QPushButton(tr("Try rate and quantum now"), page);
  tryButton->setToolTip(tr("Force the default rate and quantum on the running graph without saving or restarting. Lasts until PipeWire restarts."));
  auto* stopButton = new QPushButton(tr("Stop trying"), page);
  connect(tryButton, &QPushButton::clicked, this,
          [this]
          {
            m_services->setLiveSetting("clock.force-rate", QString::number(graphRate()));
            m_services->setLiveSetting("clock.force-quantum", graphValue("default.clock.quantum"));
            setStatus(tr("Trying %1 Hz with quantum %2 on the running graph.").arg(graphRate()).arg(graphValue("default.clock.quantum")));
          });
  connect(stopButton, &QPushButton::clicked, this,
          [this]
          {
            m_services->setLiveSetting("clock.force-rate", "0");
            m_services->setLiveSetting("clock.force-quantum", "0");
            setStatus(tr("Stopped trying; the graph follows the saved settings again."));
          });
  tryButton->setEnabled(live.valid && m_options.pwDumpFile.isEmpty());
  stopButton->setEnabled(tryButton->isEnabled());
  actions->addWidget(tryButton);
  actions->addWidget(stopButton);
  static_cast<QHBoxLayout*>(actions)->addStretch();

  SettingBinding b = globalBinding(&ConfigModel::graph, ConfigFile::Pipewire, "context.properties");
  // The quantum limit is also written to the client configs; a later drop-in there overrides it too.
  b.overriddenBy = [this](const QString& key)
  {
    QList<ConfigFile> files = {ConfigFile::Pipewire};
    if (ConfigModel::clientContextKeys().contains(key))
      files += {ConfigFile::Client, ConfigFile::ClientRt, ConfigFile::Pulse, ConfigFile::Jack};
    for (ConfigFile f : files)
    {
      const ValueOrigin o = m_effective.value(f).overriddenBy("context.properties", key);
      if (o.isValid())
        return o;
    }
    return ValueOrigin();
  };
  b.live = [this](const QString& key) { return m_live->snapshot().settings.value(metadataKeyFor(key)).toString(); };
  page->addSection(Scope::Graph, b);
  return page;
}

SettingsPage* MainWindow::buildStreamPage()
{
  auto* page =
    new SettingsPage(tr("Streams"), tr("Defaults for every application stream. Saved to %1 (native PipeWire applications) and %2 (applications using the PulseAudio API).")
                                      .arg(mono(m_paths.ownFile(ConfigFile::Client)), mono(m_paths.ownFile(ConfigFile::Pulse))));
  page->addNotice(tr("Native PipeWire applications read these settings when they start, so restart them after applying."));
  if (m_paths.hasClientRt())
    page->addNotice(tr("Your PipeWire still has client-rt.conf (before PipeWire 1.4), which realtime applications read instead of client.conf, "
                       "so these settings are written to %1 too.")
                      .arg(mono(m_paths.ownFile(ConfigFile::ClientRt))));
  page->addSection(Scope::Stream, globalBinding(&ConfigModel::stream, ConfigFile::Client, "stream.properties", {ConfigFile::ClientRt, ConfigFile::Pulse}));
  return page;
}

SettingsPage* MainWindow::buildPulsePage()
{
  auto* page = new SettingsPage(
    tr("PulseAudio bridge"), tr("Settings of pipewire-pulse, which serves applications that use the PulseAudio API. Saved to %1.").arg(mono(m_paths.ownFile(ConfigFile::Pulse))));
  page->addSection(Scope::Pulse, globalBinding(&ConfigModel::pulse, ConfigFile::Pulse, "pulse.properties"));
  return page;
}

SettingsPage* MainWindow::buildDeviceListPage(bool outputs)
{
  auto* page = new SettingsPage(outputs ? tr("Output devices") : tr("Input devices"),
                                tr("Settings for a single sound card output or input. They are stored as WirePlumber rules that match the device's node name, so they stay "
                                   "with the device even when it is unplugged. Bluetooth devices are under Bluetooth."));
  addWirePlumberNotice(page);
  const LiveSnapshot& live = m_live->snapshot();
  if (!live.valid && !live.error.isEmpty())
    page->addNotice(live.error.toHtmlEscaped(), true);

  const QStringList names = alsaNodeNames(outputs);
  if (names.isEmpty())
    page->addWidget(new QLabel(tr("No devices found."), page));
  for (const QString& name : names)
  {
    auto* button = new QPushButton(page);
    const LiveNode* n = live.node(name);
    QString text = alsaLabel(name);
    if (name == (outputs ? live.defaultSink : live.defaultSource))
      text += "  " + tr("(default)");
    text += "\n" + (n ? stateText(n->state) + (n->formatSummary().isEmpty() ? QString() : " · " + n->formatSummary()) : tr("Not connected"));
    if (ConfigModel::findRule(m_model.alsaRules, "node.name", name))
      text += " · " + tr("has settings");
    button->setText(text);
    button->setStyleSheet("text-align: left; padding: 8px;");
    connect(button, &QPushButton::clicked, this, [this, name] { showPage("alsa:" + name); });
    page->addWidget(button);
  }
  return page;
}

SettingsPage* MainWindow::buildAlsaPage(const QString& nodeName)
{
  const LiveSnapshot& live = m_live->snapshot();
  const LiveNode* node = live.node(nodeName);
  const bool output = nodeName.startsWith("alsa_output.");
  auto* page = new SettingsPage(alsaLabel(nodeName), mono(nodeName));

  QStringList status;
  if (node)
  {
    QString line = stateText(node->state);
    if (!node->formatSummary().isEmpty())
      line += " · " + node->formatSummary();
    if (nodeName == (output ? live.defaultSink : live.defaultSource))
      line += " · <b>" + (output ? tr("default output") : tr("default input")) + "</b>";
    status.append(line);
  }
  else if (live.valid)
  {
    status.append(tr("Not connected. The settings below apply when it is plugged in again."));
  }
  status.append(tr("Saved as a WirePlumber rule in %1.").arg(mono(m_paths.ownFile(ConfigFile::WirePlumber))));
  page->setStatus(status.join("<br>"));
  addWirePlumberNotice(page);

  if (ConfigModel::findRule(m_model.alsaRules, "node.name", nodeName))
  {
    auto* remove = new QPushButton(QIcon::fromTheme("edit-clear"), tr("Remove all settings for this device"), page);
    connect(remove, &QPushButton::clicked, this,
            [this, nodeName]
            {
              m_model.alsaRules.removeIf([&](const Rule& r) { return r.matchKey == "node.name" && r.matchValue == nodeName; });
              onModelChanged();
              rebuildTree();
              showPage(m_live->snapshot().node(nodeName) ? "alsa:" + nodeName : QString(nodeName.startsWith("alsa_output.") ? "outputs" : "inputs"));
            });
    page->actionArea()->layout()->addWidget(remove);
    static_cast<QHBoxLayout*>(page->actionArea()->layout())->addStretch();
  }

  page->addSection(Scope::AlsaNode, ruleBinding(&ConfigModel::alsaRules, "node.name", nodeName, [this, nodeName] { return m_live->snapshot().node(nodeName); }));
  if (!m_wpSupported)
  {
    for (QWidget* w : page->findChildren<SettingEditor*>())
      w->setEnabled(false);
  }
  return page;
}

SettingsPage* MainWindow::buildBluetoothPage()
{
  auto* page = new SettingsPage(
    tr("Bluetooth"),
    tr("Settings for all Bluetooth audio devices. Saved to %1. Settings for a single headphone are on its own page below.").arg(mono(m_paths.ownFile(ConfigFile::WirePlumber))));
  addWirePlumberNotice(page);
  page->addSection(Scope::BtGlobal, globalBinding(&ConfigModel::bluez, ConfigFile::WirePlumber, "monitor.bluez.properties"));
  page->addSection(Scope::WpSettings, globalBinding(&ConfigModel::wpSettings, ConfigFile::WirePlumber, "wireplumber.settings"));
  page->addNotice(tr("Changes to codecs and profiles take effect when the headphones reconnect after applying."));
  if (!m_wpSupported)
  {
    for (QWidget* w : page->findChildren<SettingEditor*>())
      w->setEnabled(false);
  }
  return page;
}

SettingsPage* MainWindow::buildBtDevicePage(const QString& address)
{
  const LiveSnapshot& live = m_live->snapshot();
  const QString colon = QString(address).replace('_', ':');
  const LiveDevice* device = live.btDevice(colon);
  const LiveNode* out = live.btNode(colon, true);
  auto* page = new SettingsPage(btLabel(address), mono(colon));

  QStringList status;
  if (device)
  {
    QString line = tr("Connected");
    if (!device->profile.isEmpty())
      line += " · " + device->profile.toHtmlEscaped();
    if (out && !out->formatSummary().isEmpty())
      line += " · " + out->formatSummary();
    status.append(line);
  }
  else if (live.valid)
  {
    status.append(tr("Not connected. The settings below apply when it connects again."));
  }
  status.append(tr("Saved as WirePlumber rules in %1.").arg(mono(m_paths.ownFile(ConfigFile::WirePlumber))));
  page->setStatus(status.join("<br>"));
  addWirePlumberNotice(page);

  const QString deviceName = ConfigModel::btDeviceName(address);
  const QString outPattern = ConfigModel::btOutputPattern(address);
  const QString inPattern = ConfigModel::btInputPattern(address);
  const bool hasRules = ConfigModel::findRule(m_model.bluezRules, "device.name", deviceName) || ConfigModel::findRule(m_model.bluezRules, "node.name", outPattern) ||
                        ConfigModel::findRule(m_model.bluezRules, "node.name", inPattern);
  if (hasRules)
  {
    auto* remove = new QPushButton(QIcon::fromTheme("edit-clear"), tr("Remove all settings for this device"), page);
    connect(remove, &QPushButton::clicked, this,
            [this, deviceName, outPattern, inPattern, address]
            {
              m_model.bluezRules.removeIf([&](const Rule& r) { return r.matchValue == deviceName || r.matchValue == outPattern || r.matchValue == inPattern; });
              onModelChanged();
              rebuildTree();
              showPage(btAddresses().contains(address) ? "bt:" + address : QStringLiteral("bluetooth"));
            });
    page->actionArea()->layout()->addWidget(remove);
    static_cast<QHBoxLayout*>(page->actionArea()->layout())->addStretch();
  }

  page->addSection(Scope::BtDevice, ruleBinding(&ConfigModel::bluezRules, "device.name", deviceName, nullptr));
  page->addSection(Scope::BtNode, ruleBinding(&ConfigModel::bluezRules, "node.name", outPattern, [this, colon] { return m_live->snapshot().btNode(colon, true); }), tr("Playback"),
                   tr("The headphones as an output, in any profile."));
  page->addSection(Scope::BtNode, ruleBinding(&ConfigModel::bluezRules, "node.name", inPattern, [this, colon] { return m_live->snapshot().btNode(colon, false); }),
                   tr("Microphone"), tr("The headset microphone, available in the headset profile."));
  if (!m_wpSupported)
  {
    for (QWidget* w : page->findChildren<SettingEditor*>())
      w->setEnabled(false);
  }
  return page;
}

SettingsPage* MainWindow::buildAppsPage()
{
  auto* page =
    new SettingsPage(tr("Applications"), tr("Settings for one application, for example a lower latency for a game or blocking an app from changing your microphone volume. "
                                            "They apply to applications that use the PulseAudio API, which includes most desktop applications such as Firefox, Chrome, "
                                            "Discord and Spotify. Saved to %1.")
                                           .arg(mono(m_paths.ownFile(ConfigFile::Pulse))));

  auto* row = new QWidget(page);
  auto* l = new QHBoxLayout(row);
  l->setContentsMargins(0, 0, 0, 0);
  l->addWidget(new QLabel(tr("Program:"), row));
  auto* combo = new QComboBox(row);
  combo->setEditable(true);
  combo->setMinimumWidth(260);
  combo->lineEdit()->setPlaceholderText(tr("process name, e.g. firefox"));
  QStringList running;
  for (const LiveClient& c : m_live->snapshot().pulseClients)
  {
    if (!running.contains(c.binary))
    {
      running.append(c.binary);
      combo->addItem(c.appName.isEmpty() || c.appName == c.binary ? c.binary : QStringLiteral("%1 (%2)").arg(c.binary, c.appName), c.binary);
    }
  }
  combo->setCurrentIndex(-1);
  l->addWidget(combo);
  auto* add = new QPushButton(QIcon::fromTheme("list-add"), tr("Add"), row);
  l->addWidget(add);
  l->addStretch();
  connect(add, &QPushButton::clicked, this,
          [this, combo]
          {
            QString binary = combo->currentData().toString();
            if (binary.isEmpty() || combo->currentText() != combo->itemText(combo->currentIndex()))
              binary = combo->currentText().trimmed();
            if (binary.isEmpty())
              return;
            // The rule is created with the first setting; until then the page just shows the program.
            if (!ConfigModel::findRule(m_model.appRules, "application.process.binary", binary))
              m_model.appRules.append({"application.process.binary", binary, {}});
            rebuildTree();
            showPage("app:" + binary);
          });
  page->addWidget(row);
  if (!running.isEmpty())
  {
    auto* hint = new QLabel(tr("The list shows programs that are connected right now."), page);
    page->addWidget(hint);
  }
  for (const Rule& r : m_model.appRules)
  {
    auto* b = new QPushButton(r.matchValue, page);
    b->setStyleSheet("text-align: left; padding: 8px;");
    const QString id = "app:" + r.matchValue;
    connect(b, &QPushButton::clicked, this, [this, id] { showPage(id); });
    page->addWidget(b);
  }
  return page;
}

SettingsPage* MainWindow::buildAppPage(const QString& binary)
{
  auto* page = new SettingsPage(binary, tr("Applies when a program with the process name %1 connects through the PulseAudio API.").arg(mono(binary)));
  auto* remove = new QPushButton(QIcon::fromTheme("edit-delete"), tr("Remove this application"), page);
  connect(remove, &QPushButton::clicked, this,
          [this, binary]
          {
            m_model.appRules.removeIf([&](const Rule& r) { return r.matchValue == binary; });
            onModelChanged();
            rebuildTree();
            showPage("apps");
          });
  page->actionArea()->layout()->addWidget(remove);
  static_cast<QHBoxLayout*>(page->actionArea()->layout())->addStretch();

  SettingBinding b = ruleBinding(&ConfigModel::appRules, "application.process.binary", binary, nullptr);
  b.inherited = [this](const QString& key) -> ValueOrigin
  {
    if (m_model.stream.contains(key))
      return {m_model.stream.value(key), tr("the Streams page")};
    if (m_model.pulse.contains(key))
      return {m_model.pulse.value(key), tr("the PulseAudio bridge page")};
    ValueOrigin o = m_effective.value(ConfigFile::Pulse).value("stream.properties", key);
    return o.isValid() ? o : m_effective.value(ConfigFile::Pulse).value("pulse.properties", key);
  };
  b.inheritedLabel = tr("Not set; the application uses the global setting.");
  page->addSection(Scope::App, b);
  return page;
}

SettingsPage* MainWindow::buildFilesPage()
{
  auto* page = new SettingsPage(tr("Config files"), tr("The configurator only writes its own files. Your other config files are never changed."));

  QStringList own;
  for (ConfigFile f : allConfigFiles())
  {
    const QString path = m_paths.ownFile(f);
    if (f == ConfigFile::ClientRt && !m_paths.hasClientRt())
      continue;
    own.append(mono(path) + " — " + (QFile::exists(path) ? tr("in use") : tr("not needed (nothing set)")));
  }
  page->addNotice("<b>" + tr("Files written by the configurator") + "</b><br>" + own.join("<br>"));

  for (const QString& w : m_loadWarnings)
    page->addNotice(w.toHtmlEscaped(), true);

  QStringList later;
  for (ConfigFile f : allConfigFiles())
  {
    for (const QString& p : m_effective.value(f).dropInsAfter)
      later.append(mono(p));
  }
  if (!later.isEmpty())
    page->addNotice("<b>" + tr("These files load after the configurator's files and win where they set the same thing:") + "</b><br>" + later.join("<br>"));

  QStringList other;
  for (ConfigFile f : allConfigFiles())
  {
    const EffectiveConfig& e = m_effective.value(f);
    if (!e.mainFile.isEmpty())
      other.append(mono(e.mainFile));
    for (const QString& p : e.dropInsBefore)
      other.append(mono(p));
  }
  auto* otherLabel = new QLabel("<b>" + tr("Other files that are read (settings shown as \"From …\" come from here)") + "</b><br>" + other.join("<br>"), page);
  otherLabel->setWordWrap(true);
  otherLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  page->addWidget(otherLabel);

  QStringList versions;
  versions.append(tr("PipeWire: %1").arg(m_live->snapshot().pipewireVersion.isEmpty() ? tr("unknown") : m_live->snapshot().pipewireVersion));
  versions.append(tr("WirePlumber: %1").arg(m_wpVersion.isEmpty() ? tr("unknown") : m_wpVersion));
  auto* versionLabel = new QLabel(versions.join("<br>"), page);
  page->addWidget(versionLabel);

  auto* reset = new QPushButton(QIcon::fromTheme("edit-clear-all"), tr("Remove all configurator settings…"), page);
  connect(
    reset, &QPushButton::clicked, this,
    [this]
    {
      if (QMessageBox::question(this, tr("Remove all settings"),
                                tr("This removes every setting made in the configurator. After Save and Apply, PipeWire runs with your other config files and its defaults again."),
                                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
        return;
      m_model = ConfigModel();
      m_model.includeClientRt = m_paths.hasClientRt();
      onModelChanged();
      rebuildTree();
      showPage("files");
    });
  page->actionArea()->layout()->addWidget(reset);
  static_cast<QHBoxLayout*>(page->actionArea()->layout())->addStretch();
  return page;
}
