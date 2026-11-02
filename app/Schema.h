#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

// Description of every setting the configurator offers, loaded from resources/schema.json.
struct SettingOption
{
  QString value;
  QString label;
};

struct SettingDef
{
  enum Type
  {
    Bool,
    Int,
    Float,
    Enum,
    Rate,
    RateList,
    Quantum,
    Fraction,
    String,
    List,
  };

  QString key;
  QString label;
  QString help;
  QString warning;  // shown while the setting is overridden
  QString unit;
  Type type = String;
  QList<SettingOption> options;
  double min = 0;
  double max = 1e9;
  QString defaultValue;  // documented default, empty when it depends on the version or device
  QStringList scopes;
  QString showIfKey;
  QStringList showIfValues;

  bool isListType() const { return type == RateList || type == List; }
  QString optionLabel(const QString& value) const;
};

struct PresetDef
{
  QString label;
  QStringList scopes;
  QMap<QString, QString> values;
  QStringList clear;
};

struct GroupDef
{
  QString id;
  QString title;
  QString description;
  QStringList scopes;
  QList<SettingDef> settings;
  QList<PresetDef> presets;

  QList<SettingDef> settingsFor(const QString& scope) const;
  QList<PresetDef> presetsFor(const QString& scope) const;
};

class Schema
{
public:
  // Loads the built-in schema. |error| is set when the resource is broken.
  static const Schema& instance();
  static Schema load(const QByteArray& json, QString* error);

  QList<GroupDef> groupsFor(const QString& scope) const;
  const SettingDef* find(const QString& key) const;
  const QList<GroupDef>& groups() const { return m_groups; }
  QString loadError() const { return m_error; }

private:
  QList<GroupDef> m_groups;
  QString m_error;
};

// Common sample rates offered in rate pickers.
const QList<int>& standardRates();
const QStringList& standardQuanta();

// Scope names used in the schema.
namespace Scope
{
inline const QString Graph = QStringLiteral("graph");
inline const QString Stream = QStringLiteral("stream");
inline const QString Pulse = QStringLiteral("pulse");
inline const QString AlsaNode = QStringLiteral("alsa-node");
inline const QString BtNode = QStringLiteral("bt-node");
inline const QString BtGlobal = QStringLiteral("bt-global");
inline const QString WpSettings = QStringLiteral("wp-settings");
inline const QString BtDevice = QStringLiteral("bt-device");
inline const QString App = QStringLiteral("app");
}  // namespace Scope
