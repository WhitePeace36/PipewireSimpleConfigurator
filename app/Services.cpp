#include "Services.h"

#include <QProcess>
#include <QRegularExpression>

Services::Services(QObject* parent) : QObject(parent) {}

void Services::restart(const QStringList& units)
{
  if (units.isEmpty())
  {
    emit finished(true, {});
    return;
  }
  run("systemctl", QStringList{"--user", "restart"} + units, tr("Restarted %1.").arg(units.join(", ")));
}

void Services::setLiveSetting(const QString& key, const QString& value)
{
  run("pw-metadata", {"-n", "settings", "0", key, value}, {});
}

void Services::run(const QString& program, const QStringList& args, const QString& successMessage)
{
  m_queue.append({program, args, successMessage});
  if (!m_busy)
    startNext();
}

void Services::startNext()
{
  if (m_queue.isEmpty())
  {
    setBusy(false);
    return;
  }
  setBusy(true);
  const Job job = m_queue.takeFirst();
  auto* proc = new QProcess(this);
  proc->setProcessChannelMode(QProcess::MergedChannels);
  connect(proc, &QProcess::finished, this,
          [this, proc, job](int code, QProcess::ExitStatus status)
          {
            const QString output = QString::fromLocal8Bit(proc->readAll()).trimmed();
            proc->deleteLater();
            const bool ok = status == QProcess::NormalExit && code == 0;
            if (!ok)
              m_queue.clear();  // later steps depend on this one
            emit finished(ok, ok ? job.successMessage : tr("%1 failed: %2").arg(job.program, output.isEmpty() ? tr("exit code %1").arg(code) : output));
            startNext();
          });
  connect(proc, &QProcess::errorOccurred, this,
          [this, proc, job](QProcess::ProcessError e)
          {
            if (e != QProcess::FailedToStart)
              return;
            proc->deleteLater();
            m_queue.clear();
            emit finished(false, tr("Could not run %1. Is it installed?").arg(job.program));
            startNext();
          });
  proc->start(job.program, job.args);
}

void Services::setBusy(bool busy)
{
  if (m_busy == busy)
    return;
  m_busy = busy;
  emit busyChanged();
}

QString Services::wireplumberVersion()
{
  QProcess proc;
  proc.start("wireplumber", {"--version"});
  if (!proc.waitForFinished(3000))
  {
    proc.kill();
    return {};
  }
  static const QRegularExpression re(R"(libwireplumber\s+(\d+\.\d+(?:\.\d+)?))");
  const auto m = re.match(QString::fromLocal8Bit(proc.readAll()));
  return m.hasMatch() ? m.captured(1) : QString();
}

bool Services::versionAtLeast(const QString& version, int major, int minor)
{
  const QStringList parts = version.split('.');
  if (parts.size() < 2)
    return false;
  const int ma = parts[0].toInt(), mi = parts[1].toInt();
  return ma > major || (ma == major && mi >= minor);
}
