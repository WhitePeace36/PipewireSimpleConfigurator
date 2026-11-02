#pragma once

#include <QObject>
#include <QStringList>

// Runs the external commands: restarting the user services and changing live settings.
class Services : public QObject
{
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
  explicit Services(QObject* parent = nullptr);

  bool busy() const { return m_busy; }

  // systemctl --user restart <units>
  void restart(const QStringList& units);

  // pw-metadata -n settings 0 <key> <value>. Takes effect immediately, until PipeWire restarts.
  void setLiveSetting(const QString& key, const QString& value);

  // Version of the installed WirePlumber ("0.5.8"), or empty when it cannot be determined.
  static QString wireplumberVersion();
  static bool versionAtLeast(const QString& version, int major, int minor);

signals:
  void finished(bool ok, const QString& message);
  void busyChanged();

private:
  struct Job
  {
    QString program;
    QStringList args;
    QString successMessage;
  };
  void run(const QString& program, const QStringList& args, const QString& successMessage);
  void startNext();
  void setBusy(bool busy);

  bool m_busy = false;
  QList<Job> m_queue;
};
