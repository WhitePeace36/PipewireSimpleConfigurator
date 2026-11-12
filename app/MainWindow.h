#pragma once

#include "ConfigFiles.h"
#include "ConfigModel.h"
#include "LiveState.h"
#include "Services.h"
#include "SettingEditor.h"

#include <QMainWindow>
#include <QSet>

class QLabel;
class QPushButton;
class QProgressBar;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;
class SettingsPage;

class MainWindow : public QMainWindow
{
  Q_OBJECT
public:
  struct Options
  {
    QString pwDumpFile;          // read devices from this file instead of running pw-dump
    QString wireplumberVersion;  // pretend this WirePlumber version is installed
    ConfigPaths paths = ConfigPaths::fromEnvironment();
  };

  explicit MainWindow(const Options& options, QWidget* parent = nullptr);

  // Page ids: graph, stream, pulse, outputs, inputs, alsa:<node.name>, bluetooth, bt:<address>,
  // apps, app:<binary>, files.
  void showPage(const QString& id);
  QString currentPage() const { return m_currentId; }
  LiveState* liveState() const { return m_live; }

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  // State
  void reloadFromDisk();
  bool isDirty() const { return m_model.render() != m_baseline; }
  void onModelChanged();
  void updateButtons();
  void setStatus(const QString& text, bool error = false);
  bool save();
  void apply();
  void revert();
  void importLegacy();

  // Navigation
  void rebuildTree();
  void rebuildPage();
  QTreeWidgetItem* addItem(QTreeWidgetItem* parent, const QString& id, const QString& text, const QString& icon);

  // Pages
  SettingsPage* buildGraphPage();
  SettingsPage* buildStreamPage();
  SettingsPage* buildPulsePage();
  SettingsPage* buildDeviceListPage(bool outputs);
  SettingsPage* buildAlsaPage(const QString& nodeName);
  SettingsPage* buildBluetoothPage();
  SettingsPage* buildBtDevicePage(const QString& address);
  SettingsPage* buildAppsPage();
  SettingsPage* buildAppPage(const QString& binary);
  SettingsPage* buildFilesPage();
  void addWirePlumberNotice(SettingsPage* page);

  // Bindings
  SettingBinding globalBinding(Values ConfigModel::*member, ConfigFile file, const QString& section, const QList<ConfigFile>& alsoCheck = {});
  SettingBinding ruleBinding(QList<Rule> ConfigModel::*list, const QString& key, const QString& value, std::function<const LiveNode*()> live);
  int graphRate() const;
  QString graphValue(const QString& key) const;

  // Device lists (live devices plus devices that only exist in the config)
  QStringList alsaNodeNames(bool outputs) const;
  QStringList btAddresses() const;
  QString alsaLabel(const QString& nodeName) const;
  QString btLabel(const QString& address) const;

  Options m_options;
  ConfigPaths m_paths;
  ConfigModel m_model;
  QMap<ConfigFile, QString> m_baseline;  // render() of the model as last loaded or saved
  QMap<ConfigFile, EffectiveConfig> m_effective;
  QStringList m_loadWarnings;
  QSet<ConfigFile> m_unreadable;  // own files that did not parse; left alone unless their settings change
  QSet<ConfigFile> m_unapplied;  // saved but services not restarted yet
  QString m_wpVersion;
  bool m_wpSupported = true;

  LiveState* m_live;
  Services* m_services;

  QTreeWidget* m_tree;
  QScrollArea* m_scroll;
  SettingsPage* m_page = nullptr;
  QString m_currentId;
  QWidget* m_legacyBanner;
  QLabel* m_legacyText;
  QLabel* m_statusLabel;
  QProgressBar* m_busy;
  QPushButton* m_revertButton;
  QPushButton* m_saveButton;
  QPushButton* m_applyButton;
  bool m_closeConfirmed = false;
};
