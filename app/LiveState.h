#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

class QProcess;

// What is running right now, read from `pw-dump`.
struct LiveNode
{
  enum Kind
  {
    AlsaOutput,
    AlsaInput,
    BtOutput,
    BtInput,
    Other,
  };

  int id = -1;
  Kind kind = Other;
  QString name;
  QString description;
  QString mediaClass;
  QString state;  // running, idle, suspended, ...
  QString btAddress;
  QString codec;  // Bluetooth codec
  QString format;
  int rate = 0;
  int channels = 0;
  QVariantMap props;          // node properties
  QVariantMap adapterValues;  // current audioconvert values (resample.quality, channelmix.* ...)

  bool isOutput() const { return kind == AlsaOutput || kind == BtOutput; }
  // Value of a node or adapter property as currently in effect.
  QString currentValue(const QString& key) const;
  QString formatSummary() const;
};

struct LiveDevice
{
  int id = -1;
  QString name;
  QString description;
  QString api;
  QString btAddress;
  QString profile;  // active profile description
};

struct LiveClient
{
  QString binary;
  QString appName;
};

struct LiveSnapshot
{
  bool valid = false;
  QString error;
  QString pipewireVersion;
  QList<LiveNode> nodes;
  QList<LiveDevice> devices;
  QList<LiveClient> pulseClients;
  QString defaultSink;    // node.name of the current default output
  QString defaultSource;  // node.name of the current default input
  QVariantMap settings;   // "settings" metadata: clock.rate, clock.quantum, clock.force-rate, ...

  static LiveSnapshot parse(const QByteArray& pwDumpJson);
  const LiveNode* node(const QString& name) const;
  QList<const LiveNode*> nodesOf(LiveNode::Kind kind) const;
  const LiveDevice* btDevice(const QString& address) const;
  const LiveNode* btNode(const QString& address, bool output) const;
};

class LiveState : public QObject
{
  Q_OBJECT
public:
  explicit LiveState(QObject* parent = nullptr);

  // Use a saved pw-dump output instead of running pw-dump (for testing and screenshots).
  void setDumpFile(const QString& path) { m_dumpFile = path; }

  void refresh();
  const LiveSnapshot& snapshot() const { return m_snapshot; }

signals:
  void updated();

private:
  void finish(const QByteArray& data, const QString& error);

  LiveSnapshot m_snapshot;
  QByteArray m_lastData;  // to skip updates when nothing changed
  QString m_dumpFile;
  QProcess* m_process = nullptr;
};
