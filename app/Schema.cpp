#include "Schema.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace
{
SettingDef::Type parseType(const QString& t, bool* ok)
{
  static const QMap<QString, SettingDef::Type> types = {
    {"bool", SettingDef::Bool},         {"int", SettingDef::Int},         {"float", SettingDef::Float},       {"enum", SettingDef::Enum},     {"rate", SettingDef::Rate},
    {"ratelist", SettingDef::RateList}, {"quantum", SettingDef::Quantum}, {"fraction", SettingDef::Fraction}, {"string", SettingDef::String}, {"list", SettingDef::List},
  };
  *ok = types.contains(t);
  return types.value(t, SettingDef::String);
}

QStringList toStrings(const QJsonValue& v)
{
  QStringList out;
  for (const QJsonValue& e : v.toArray())
    out.append(e.toString());
  return out;
}
}  // namespace

QString SettingDef::optionLabel(const QString& value) const
{
  for (const SettingOption& o : options)
  {
    if (o.value == value)
      return o.label;
  }
  return value;
}

QList<SettingDef> GroupDef::settingsFor(const QString& scope) const
{
  QList<SettingDef> out;
  for (const SettingDef& s : settings)
  {
    if (s.scopes.contains(scope))
      out.append(s);
  }
  return out;
}

QList<PresetDef> GroupDef::presetsFor(const QString& scope) const
{
  QList<PresetDef> out;
  for (const PresetDef& p : presets)
  {
    if (p.scopes.isEmpty() || p.scopes.contains(scope))
      out.append(p);
  }
  return out;
}

Schema Schema::load(const QByteArray& json, QString* error)
{
  Schema schema;
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
  if (doc.isNull())
  {
    schema.m_error = parseError.errorString();
    if (error)
      *error = schema.m_error;
    return schema;
  }

  QStringList problems;
  QSet<QString> seenInScope;
  for (const QJsonValue& gv : doc.object().value("groups").toArray())
  {
    const QJsonObject g = gv.toObject();
    GroupDef group;
    group.id = g.value("id").toString();
    group.title = g.value("title").toString();
    group.description = g.value("description").toString();
    group.scopes = toStrings(g.value("scopes"));

    for (const QJsonValue& sv : g.value("settings").toArray())
    {
      const QJsonObject s = sv.toObject();
      SettingDef def;
      bool typeOk = false;
      def.key = s.value("key").toString();
      def.label = s.value("label").toString();
      def.help = s.value("help").toString();
      def.warning = s.value("warning").toString();
      def.unit = s.value("unit").toString();
      def.type = parseType(s.value("type").toString(), &typeOk);
      def.min = s.value("min").toDouble(0);
      def.max = s.value("max").toDouble(1e9);
      def.defaultValue = s.value("default").toString();
      def.scopes = s.contains("scopes") ? toStrings(s.value("scopes")) : group.scopes;
      const QJsonObject showIf = s.value("showIf").toObject();
      def.showIfKey = showIf.value("key").toString();
      def.showIfValues = toStrings(showIf.value("values"));
      for (const QJsonValue& ov : s.value("options").toArray())
        def.options.append({ov.toObject().value("value").toString(), ov.toObject().value("label").toString()});

      if (def.key.isEmpty() || def.label.isEmpty())
        problems.append(QStringLiteral("setting without key or label in group %1").arg(group.id));
      if (!typeOk)
        problems.append(QStringLiteral("%1: unknown type").arg(def.key));
      if ((def.type == SettingDef::Enum || def.type == SettingDef::List) && def.options.isEmpty())
        problems.append(QStringLiteral("%1: enum/list without options").arg(def.key));
      for (const QString& scope : def.scopes)
      {
        if (!group.scopes.contains(scope))
          problems.append(QStringLiteral("%1: scope %2 is not a scope of group %3").arg(def.key, scope, group.id));
        const QString id = scope + "|" + def.key;
        if (seenInScope.contains(id))
          problems.append(QStringLiteral("%1 appears twice in scope %2").arg(def.key, scope));
        seenInScope.insert(id);
      }
      group.settings.append(def);
    }

    for (const QJsonValue& pv : g.value("presets").toArray())
    {
      const QJsonObject p = pv.toObject();
      PresetDef preset;
      preset.label = p.value("label").toString();
      preset.scopes = toStrings(p.value("scopes"));
      const QJsonObject values = p.value("values").toObject();
      for (auto it = values.begin(); it != values.end(); ++it)
        preset.values.insert(it.key(), it.value().toString());
      preset.clear = toStrings(p.value("clear"));
      group.presets.append(preset);
    }
    schema.m_groups.append(group);
  }

  // Presets may only touch settings of their own group.
  for (const GroupDef& group : schema.m_groups)
  {
    for (const PresetDef& p : group.presets)
    {
      QStringList keys = p.values.keys() + p.clear;
      for (const QString& k : keys)
      {
        bool found = false;
        for (const SettingDef& s : group.settings)
          found |= s.key == k;
        if (!found)
          problems.append(QStringLiteral("preset %1 in %2 uses unknown key %3").arg(p.label, group.id, k));
      }
    }
  }

  schema.m_error = problems.join("; ");
  if (error)
    *error = schema.m_error;
  return schema;
}

const Schema& Schema::instance()
{
  static const Schema schema = []
  {
    QFile f(":/schema.json");
    f.open(QIODevice::ReadOnly);
    QString error;
    Schema s = Schema::load(f.readAll(), &error);
    if (!error.isEmpty())
      qWarning("Schema problems: %s", qPrintable(error));
    return s;
  }();
  return schema;
}

QList<GroupDef> Schema::groupsFor(const QString& scope) const
{
  QList<GroupDef> out;
  for (const GroupDef& g : m_groups)
  {
    if (g.scopes.contains(scope) && !g.settingsFor(scope).isEmpty())
      out.append(g);
  }
  return out;
}

const SettingDef* Schema::find(const QString& key) const
{
  for (const GroupDef& g : m_groups)
  {
    for (const SettingDef& s : g.settings)
    {
      if (s.key == key)
        return &s;
    }
  }
  return nullptr;
}

const QList<int>& standardRates()
{
  static const QList<int> rates = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, 705600, 768000, 1411200, 1536000};
  return rates;
}

const QStringList& standardQuanta()
{
  static const QStringList q = {"4", "8", "16", "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "16384", "32768", "65536"};
  return q;
}
