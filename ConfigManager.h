#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QStandardPaths>
#include <qprocess.h>

class ConfigManager : public QObject
{
  Q_OBJECT
public:
  explicit ConfigManager(QObject* parent = nullptr) : QObject(parent) {}

  // Search paths in order of priority: User -> System -> Default
  QString getUserConfigDir() { return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/pipewire"; }

  // Resolves the file path based on Pipewire's priority order
  QString getConfPath(const QString& fileName)
  {
    QStringList searchPaths = {getUserConfigDir() + "/" + fileName, /*"/etc/pipewire/" + fileName,*/ "/usr/share/pipewire/" + fileName};

    for (const QString& path : searchPaths)
    {
      if (QFile::exists(path))
        return path;
    }
    return "";  // File not found in any path
  }

  // Parses simple key = value pairs for initial UI filling
  Q_INVOKABLE QVariantMap loadSettings(const QString& fileName)
  {
    QVariantMap data;
    QString path = getConfPath(fileName);
    if (path.isEmpty())
      return data;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
      return data;

    QTextStream in(&file);
    QString currentSection = "";

    QRegularExpression sectionStartRe(R"(^([\w\.]+)\s*=\s*[\{\[])");
    QRegularExpression sectionEndRe(R"(^[\}\]])");
    // Matches "key = value" ignoring comments
    QRegularExpression keyValueRe(R"(^\s*(?!#)\s*([\w\.-]+)\s*=\s*(.+?)\s*(?:#.*)?$)");

    while (!in.atEnd())
    {
      QString line = in.readLine().trimmed();

      // Track entering a section
      auto sectionMatch = sectionStartRe.match(line);
      if (sectionMatch.hasMatch())
      {
        currentSection = sectionMatch.captured(1);
        continue;
      }

      // Track exiting a section
      if (sectionEndRe.match(line).hasMatch())
      {
        currentSection = "";
        continue;
      }

      // Extract key-value pairs if we are inside a section
      auto kvMatch = keyValueRe.match(line);
      if (kvMatch.hasMatch() && !currentSection.isEmpty())
      {
        QString key = kvMatch.captured(1);
        QString value = kvMatch.captured(2);

        // Construct a unique key, e.g., "context.properties|default.clock.rate"
        data.insert(currentSection + "|" + key, value);
      }
    }
    file.close();
    qDebug() << "Loaded" << data.size() << "settings from" << path;
    return data;
  }

  // Section-aware saving that preserves comments and replaces values
  Q_INVOKABLE void saveToUserConfig(const QString& fileName, const QVariantMap& guiSettings)
  {
    QString templatePath = "/usr/share/pipewire/" + fileName;
    QString userPath = getUserConfigDir() + "/" + fileName;

    QFile templateFile(templatePath);
    if (!templateFile.open(QIODevice::ReadOnly | QIODevice::Text))
      return;

    QString outputContent;
    QTextStream in(&templateFile);

    QString currentSection = "";
    int braceDepth = 0;  // NEW: Track nested levels
    QStringList appliedKeys;

    QRegularExpression sectionStartRe(R"(^([\w\.]+)\s*=\s*[\{\[])");

    while (!in.atEnd())
    {
      QString line = in.readLine();
      QString trimmed = line.trimmed();

      // 1. Detect Section Start
      auto sectionMatch = sectionStartRe.match(trimmed);
      if (sectionMatch.hasMatch() && braceDepth == 0)
      {
        currentSection = sectionMatch.captured(1);
      }

      // 2. Update Depth
      if (trimmed.contains('{') || trimmed.contains('['))
        braceDepth++;

      // 3. Process Lines inside a section
      bool lineModified = false;
      if (!currentSection.isEmpty() && braceDepth > 0)
      {
        for (auto it = guiSettings.begin(); it != guiSettings.end(); ++it)
        {
          QString fullKey = it.key();
          QStringList parts = fullKey.split('|');

          if (parts.size() == 2 && parts[0] == currentSection)
          {
            QString shortKey = parts[1];
            // Matches even if commented out with #
            QRegularExpression keyRe("^(\\s*)#?(\\s*)" + QRegularExpression::escape(shortKey) + "(\\s*=).*");
            auto keyMatch = keyRe.match(line);

            if (keyMatch.hasMatch())
            {
              line = QString("%1%2 = %3").arg(keyMatch.captured(1), shortKey, it.value().toString());
              appliedKeys.append(fullKey);
              lineModified = true;
              break;
            }
          }
        }
      }

      // 4. Detect Section End (Only when depth returns to 0)
      if (trimmed.contains('}') || trimmed.contains(']'))
      {
        braceDepth--;

        // If we just closed the main section, inject missing keys
        if (braceDepth == 0 && !currentSection.isEmpty())
        {
          for (auto it = guiSettings.begin(); it != guiSettings.end(); ++it)
          {
            if (it.key().startsWith(currentSection + "|") && !appliedKeys.contains(it.key()))
            {
              outputContent += QString("    %1 = %2\n").arg(it.key().split('|')[1], it.value().toString());
              appliedKeys.append(it.key());
            }
          }
          currentSection = "";  // Reset
        }
      }

      outputContent += line + "\n";
    }
    templateFile.close();

    // Final check: If a section was totally missing from the template, append it
    QMap<QString, QStringList> sectionsToAppend;
    for (auto it = guiSettings.begin(); it != guiSettings.end(); ++it)
    {
      if (!appliedKeys.contains(it.key()))
      {
        sectionsToAppend[it.key().split('|')[0]].append(QString("    %1 = %2").arg(it.key().split('|')[1], it.value().toString()));
      }
    }

    if (!sectionsToAppend.isEmpty())
    {
      for (auto it = sectionsToAppend.begin(); it != sectionsToAppend.end(); ++it)
      {
        outputContent += "\n" + it.key() + " = {\n" + it.value().join("\n") + "\n}\n";
      }
    }

    // Save to file
    QDir().mkpath(getUserConfigDir());
    QFile outFile(userPath);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
      QTextStream(&outFile) << outputContent;
      outFile.close();
    }
  }

  Q_INVOKABLE void restartServices()
  {
    // We use /bin/sh to interpret the full command string
    // Note: 'pipewire.conf' and 'pipewire-pulse.conf' are not valid systemd units.
    // The correct services are pipewire.service and pipewire-pulse.service.
    QString command = "systemctl restart --user pipewire.service pipewire-pulse.service pipewire.socket pipewire-pulse.socket wireplumber.service";

    QProcess::startDetached("/bin/sh", QStringList() << "-c" << command);
    qDebug() << "Restart command sent to systemd.";
  }

  Q_INVOKABLE void resetToDefaults()
  {
    QString userDirPath = getUserConfigDir();
    QStringList filesToRemove = {"pipewire.conf", "pipewire-pulse.conf", "client.conf"};

    bool deletedAny = false;
    for (const QString& fileName : filesToRemove)
    {
      QFile file(userDirPath + "/" + fileName);
      if (file.exists())
      {
        if (file.remove())
        {
          qDebug() << "Removed user config:" << fileName;
          deletedAny = true;
        }
        else
        {
          qWarning() << "Failed to remove:" << fileName;
        }
      }
    }

    if (deletedAny)
    {
      // Restart services to reload the system defaults from /usr/share
      restartServices();
    }
  }
};