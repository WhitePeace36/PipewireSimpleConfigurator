#pragma once

#include "ConfigFiles.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

// key -> value. Scalars are QString, lists are QStringList.
using Values = QMap<QString, QVariant>;

// One "matches = [ { key = value } ] actions = { update-props = { ... } }" rule.
// For application rules the special key "quirks" is written to actions.quirks instead.
struct Rule
{
  QString matchKey;
  QString matchValue;
  Values props;

  bool operator==(const Rule& o) const { return matchKey == o.matchKey && matchValue == o.matchValue && props == o.props; }
};

// Everything this tool writes. It lives entirely in four drop-in files that the tool owns, so the
// files themselves are the saved state and nothing else in the user's configuration is touched.
class ConfigModel
{
public:
  Values graph;                  // pipewire.conf.d          context.properties
  Values stream;                 // client.conf.d + pipewire-pulse.conf.d   stream.properties
  Values pulse;                  // pipewire-pulse.conf.d    pulse.properties
  Values bluez;                  // wireplumber.conf.d       monitor.bluez.properties
  Values wpSettings;             // wireplumber.conf.d       wireplumber.settings
  QList<Rule> alsaRules;         // monitor.alsa.rules  (node.name)
  QList<Rule> bluezRules;        // monitor.bluez.rules (device.name / node.name)
  QList<Rule> appRules;          // pulse.rules + stream.rules in pipewire-pulse.conf.d (application.process.binary)
  bool includeClientRt = false;  // also write stream settings to client-rt.conf.d (PipeWire < 1.4)

  // Reads the tool's own drop-ins. Problems (e.g. hand edits that do not parse) go to |warnings|.
  static ConfigModel load(const ConfigPaths& paths, QStringList* warnings = nullptr);

  // Graph settings that clients read from their own context.properties as well. Clients size some
  // buffers from their own config (e.g. pw_filter, port defaults), so a raised limit must reach them too.
  static const QStringList& clientContextKeys();

  // File contents for every drop-in; an empty string means the file should not exist.
  QMap<ConfigFile, QString> render() const;

  bool operator==(const ConfigModel& o) const;
  bool operator!=(const ConfigModel& o) const { return !(*this == o); }

  // Finds the rule matching key = value, optionally creating it.
  static Rule* findRule(QList<Rule>& rules, const QString& key, const QString& value, bool create);
  static const Rule* findRule(const QList<Rule>& rules, const QString& key, const QString& value);
  void removeEmptyRules();

  // Rule match values used for devices.
  // |address| may be written with ':' (as BlueZ reports it) or '_' (as used in node names).
  static QString btId(const QString& address) { return QString(address).replace(':', '_').toUpper(); }
  static QString btDeviceName(const QString& address) { return "bluez_card." + btId(address); }
  static QString btOutputPattern(const QString& address) { return "~bluez_output." + btId(address) + ".*"; }
  static QString btInputPattern(const QString& address) { return "~bluez_input." + btId(address) + ".*"; }
};

// Imports managed keys from full copies of the main config files that older versions of this tool
// (or the user) placed in ~/.config/pipewire. Only keys known to the schema and differing from the
// system configuration are taken over.
struct LegacyImport
{
  QStringList files;     // legacy copies found
  QStringList imported;  // "file: key = value" descriptions
};
LegacyImport findLegacyCopies(const ConfigPaths& paths);
LegacyImport importLegacyCopies(const ConfigPaths& paths, ConfigModel& model);
