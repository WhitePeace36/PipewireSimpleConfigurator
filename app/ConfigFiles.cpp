#include "ConfigFiles.h"

#include "SpaJson.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

QString configFileName(ConfigFile f)
{
  switch (f)
  {
    case ConfigFile::Pipewire:
      return QStringLiteral("pipewire.conf");
    case ConfigFile::Client:
      return QStringLiteral("client.conf");
    case ConfigFile::ClientRt:
      return QStringLiteral("client-rt.conf");
    case ConfigFile::Pulse:
      return QStringLiteral("pipewire-pulse.conf");
    case ConfigFile::Jack:
      return QStringLiteral("jack.conf");
    case ConfigFile::WirePlumber:
      return QStringLiteral("wireplumber.conf");
  }
  return {};
}

QList<ConfigFile> allConfigFiles()
{
  return {ConfigFile::Pipewire, ConfigFile::Client, ConfigFile::ClientRt, ConfigFile::Pulse, ConfigFile::Jack, ConfigFile::WirePlumber};
}

ConfigPaths ConfigPaths::fromEnvironment()
{
  ConfigPaths p;
  p.userConfigHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  p.systemPipewireDirs = {"/usr/share/pipewire", "/etc/pipewire"};
  p.systemWireplumberDirs = {"/usr/share/wireplumber", "/etc/wireplumber"};
  return p;
}

QString ConfigPaths::userDir(ConfigFile f) const
{
  return userConfigHome + (f == ConfigFile::WirePlumber ? "/wireplumber" : "/pipewire");
}

QStringList ConfigPaths::systemDirs(ConfigFile f) const
{
  return f == ConfigFile::WirePlumber ? systemWireplumberDirs : systemPipewireDirs;
}

QString ConfigPaths::ownFile(ConfigFile f) const
{
  return userDir(f) + "/" + configFileName(f) + ".d/" + ownFileName();
}

QString ConfigPaths::legacyUserCopy(ConfigFile f) const
{
  return userDir(f) + "/" + configFileName(f);
}

bool ConfigPaths::hasClientRt() const
{
  for (const QString& dir : systemPipewireDirs)
  {
    if (QFile::exists(dir + "/client-rt.conf"))
      return true;
  }
  return false;
}

namespace
{
bool readText(const QString& path, QString& out)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return false;
  out = QString::fromUtf8(f.readAll());
  return true;
}

void mergeInto(QMap<QString, QMap<QString, ValueOrigin>>& target, const QVariantMap& doc, const QString& file)
{
  for (auto it = doc.begin(); it != doc.end(); ++it)
  {
    if (it->typeId() != QMetaType::QVariantMap)
      continue;
    const QVariantMap section = it->toMap();
    for (auto kv = section.begin(); kv != section.end(); ++kv)
      target[it.key()][kv.key()] = {kv.value(), file};
  }
}
}  // namespace

EffectiveConfig EffectiveConfig::load(const ConfigPaths& paths, ConfigFile which)
{
  EffectiveConfig cfg;
  const QString name = configFileName(which);
  const QStringList dirs = paths.systemDirs(which) + QStringList{paths.userDir(which)};

  // Main file: the highest-priority location wins (user, then /etc, then /usr/share).
  for (int i = dirs.size() - 1; i >= 0; --i)
  {
    if (QFile::exists(dirs[i] + "/" + name))
    {
      cfg.mainFile = dirs[i] + "/" + name;
      break;
    }
  }

  auto loadInto = [&cfg](QMap<QString, QMap<QString, ValueOrigin>>& target, const QString& path)
  {
    QString text;
    if (!readText(path, text))
      return;
    SpaJson::Error error;
    const QVariantMap doc = SpaJson::parseDocument(text, &error);
    if (error.isError())
      cfg.errors.append(QStringLiteral("%1:%2: %3").arg(path).arg(error.line).arg(error.message));
    else
      mergeInto(target, doc, path);
  };

  if (!cfg.mainFile.isEmpty())
    loadInto(cfg.m_before, cfg.mainFile);

  // Drop-ins: directories in load order; a file name in a later directory replaces the same name in
  // an earlier one. Within a directory, files are loaded alphabetically.
  QMap<QString, int> levelOf;
  QMap<QString, QString> pathOf;
  for (int level = 0; level < dirs.size(); ++level)
  {
    QDir dir(dirs[level] + "/" + name + ".d");
    for (const QString& entry : dir.entryList({"*.conf"}, QDir::Files | QDir::Readable, QDir::Name))
    {
      levelOf[entry] = level;
      pathOf[entry] = dir.filePath(entry);
    }
  }
  const QString own = ConfigPaths::ownFileName();
  const int userLevel = dirs.size() - 1;
  for (int level = 0; level < dirs.size(); ++level)
  {
    for (auto it = levelOf.begin(); it != levelOf.end(); ++it)
    {
      if (it.value() != level)
        continue;
      const QString& entry = it.key();
      if (level == userLevel && entry == own)
        continue;
      const bool after = level == userLevel && entry > own;
      (after ? cfg.dropInsAfter : cfg.dropInsBefore).append(pathOf[entry]);
      loadInto(after ? cfg.m_after : cfg.m_before, pathOf[entry]);
    }
  }
  return cfg;
}

ValueOrigin EffectiveConfig::value(const QString& section, const QString& key) const
{
  const ValueOrigin after = overriddenBy(section, key);
  if (after.isValid())
    return after;
  return m_before.value(section).value(key);
}

ValueOrigin EffectiveConfig::overriddenBy(const QString& section, const QString& key) const
{
  return m_after.value(section).value(key);
}
