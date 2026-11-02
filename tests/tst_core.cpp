#include "Checks.h"
#include "ConfigFiles.h"
#include "ConfigModel.h"
#include "LiveState.h"
#include "Schema.h"
#include "SpaJson.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
void writeFile(const QString& path, const QString& text)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  f.write(text.toUtf8());
}

QString readFile(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return {};
  return QString::fromUtf8(f.readAll());
}

ConfigPaths tempPaths(const QTemporaryDir& dir)
{
  ConfigPaths p;
  p.userConfigHome = dir.path() + "/home";
  p.systemPipewireDirs = {dir.path() + "/usr/share/pipewire", dir.path() + "/etc/pipewire"};
  p.systemWireplumberDirs = {dir.path() + "/usr/share/wireplumber", dir.path() + "/etc/wireplumber"};
  return p;
}

void saveModel(const ConfigModel& m, const ConfigPaths& p)
{
  const auto files = m.render();
  for (auto it = files.begin(); it != files.end(); ++it)
  {
    if (it.value().isEmpty())
      QFile::remove(p.ownFile(it.key()));
    else
      writeFile(p.ownFile(it.key()), it.value());
  }
}
}  // namespace

class TestCore : public QObject
{
  Q_OBJECT

private slots:
  void schemaIsValid()
  {
    QFile f(":/schema.json");
    QVERIFY(f.open(QIODevice::ReadOnly));
    QString error;
    const Schema s = Schema::load(f.readAll(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(s.groups().size() > 10);
    QVERIFY(s.find("default.clock.rate"));
    QVERIFY(!s.groupsFor(Scope::App).isEmpty());
  }

  void parseSyntax()
  {
    // Built from separate lines: moc misreads "#" at the start of a line inside raw strings.
    const QString text =
      QStringList{
        "# comment",
        "context.properties = {",
        "    default.clock.rate = 48000   # trailing comment",
        "    \"quoted.key\": \"a \\\"quoted\\\" value\"",
        "    list = [ 44100, 48000 96000 ]",
        "    nested = { a = 1 b = { c = [ x y ] } }",
        "    fraction = 512/48000",
        "}",
        "rules = [ { matches = [ { node.name = \"~alsa_output.*\" } ] actions = { update-props = { x = 1 } } } ]",
        "rules = [ { matches = [ { node.name = second } ] } ]",
      }
        .join('\n');
    SpaJson::Error error;
    const QVariantMap doc = SpaJson::parseDocument(text, &error);
    QVERIFY2(!error.isError(), qPrintable(error.message));
    const QVariantMap ctx = doc.value("context.properties").toMap();
    QCOMPARE(ctx.value("default.clock.rate").toString(), QString("48000"));
    QCOMPARE(ctx.value("quoted.key").toString(), QString("a \"quoted\" value"));
    QCOMPARE(SpaJson::toStringList(ctx.value("list")), QStringList({"44100", "48000", "96000"}));
    QCOMPARE(ctx.value("fraction").toString(), QString("512/48000"));
    QCOMPARE(ctx.value("nested").toMap().value("b").toMap().value("c").toList().size(), 2);
    QCOMPARE(doc.value("rules").toList().size(), 2);  // repeated arrays are appended

    SpaJson::Error bad;
    SpaJson::parseDocument("a = { b = 1", &bad);
    QVERIFY(bad.isError());
    QCOMPARE(bad.line, 1);
  }

  void formatRoundTrip()
  {
    QCOMPARE(SpaJson::formatScalar("48000"), QString("48000"));
    QCOMPARE(SpaJson::formatScalar("My DAC"), QString("\"My DAC\""));
    QCOMPARE(SpaJson::formatScalar(""), QString("\"\""));
    QCOMPARE(SpaJson::formatValue(QStringList{"44100", "48000"}), QString("[ 44100 48000 ]"));
    const QString weird = "a \"b\" \\ c # d";
    QCOMPARE(SpaJson::parseValue(SpaJson::formatScalar(weird)).toString(), weird);
  }

  // Every real config file must parse (system files, plus extra directories from PSC_TEST_CONF_DIRS).
  void parseRealConfigs()
  {
    QStringList dirs = {"/usr/share/pipewire", "/usr/share/wireplumber", "/etc/pipewire", "/etc/wireplumber"};
    dirs += qEnvironmentVariable("PSC_TEST_CONF_DIRS").split(':', Qt::SkipEmptyParts);
    int parsed = 0;
    for (const QString& d : dirs)
    {
      QDirIterator it(d, {"*.conf"}, QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext())
      {
        const QString path = it.next();
        SpaJson::Error error;
        SpaJson::parseDocument(readFile(path), &error);
        QVERIFY2(!error.isError(), qPrintable(QString("%1:%2: %3").arg(path).arg(error.line).arg(error.message)));
        ++parsed;
      }
    }
    if (parsed == 0)
      QSKIP("No PipeWire config files installed");
    qInfo() << "parsed" << parsed << "real config files";
  }

  void modelRoundTrip()
  {
    QTemporaryDir dir;
    const ConfigPaths p = tempPaths(dir);

    ConfigModel m;
    m.graph["default.clock.rate"] = "96000";
    m.graph["default.clock.allowed-rates"] = QStringList{"44100", "48000", "96000"};
    m.stream["resample.quality"] = "10";
    m.stream["channelmix.upmix"] = "false";
    m.pulse["pulse.min.req"] = "512/48000";
    m.bluez["bluez5.codecs"] = QStringList{"sbc", "aac", "ldac"};
    m.wpSettings["bluetooth.autoswitch-to-headset-profile"] = "false";
    ConfigModel::findRule(m.alsaRules, "node.name", "alsa_output.usb-Foo.analog-stereo", true)->props["audio.rate"] = "96000";
    ConfigModel::findRule(m.alsaRules, "node.name", "alsa_output.usb-Foo.analog-stereo", true)->props["node.description"] = "My \"DAC\"";
    ConfigModel::findRule(m.bluezRules, "device.name", ConfigModel::btDeviceName("AA_BB_CC_DD_EE_FF"), true)->props["bluez5.a2dp.ldac.quality"] = "hq";
    ConfigModel::findRule(m.bluezRules, "node.name", ConfigModel::btOutputPattern("AA_BB_CC_DD_EE_FF"), true)->props["session.suspend-timeout-seconds"] = "0";
    Rule* app = ConfigModel::findRule(m.appRules, "application.process.binary", "Discord", true);
    app->props["quirks"] = QStringList{"block-source-volume"};
    app->props["node.latency"] = "1024/48000";
    ConfigModel::findRule(m.appRules, "application.process.binary", "empty", true);  // no props: not written

    saveModel(m, p);
    QStringList warnings;
    ConfigModel loaded = ConfigModel::load(p, &warnings);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    m.removeEmptyRules();
    QVERIFY(loaded == m);
    QCOMPARE(loaded.alsaRules.first().props.value("node.description").toString(), QString("My \"DAC\""));
    QCOMPARE(loaded.appRules.size(), 1);
    QCOMPARE(loaded.appRules.first().props.value("quirks").toStringList(), QStringList{"block-source-volume"});

    const QString pulse = readFile(p.ownFile(ConfigFile::Pulse));
    QVERIFY(pulse.contains("quirks = [ block-source-volume ]"));
    // Stream properties of an application go to stream.rules, pulse.* settings and quirks to pulse.rules.
    const QVariantMap pulseDoc = SpaJson::parseDocument(pulse);
    const QVariantMap streamRule = pulseDoc.value("stream.rules").toList().value(0).toMap();
    QCOMPARE(streamRule.value("actions").toMap().value("update-props").toMap().value("node.latency").toString(), QString("1024/48000"));
    QVERIFY(!pulseDoc.value("pulse.rules").toList().value(0).toMap().value("actions").toMap().contains("update-props"));
    QVERIFY(pulse.contains("stream.properties"));
    QVERIFY(readFile(p.ownFile(ConfigFile::Client)).contains("resample.quality = 10"));

    // Saving the loaded model again gives identical files.
    const auto first = m.render();
    QCOMPARE(loaded.render(), first);

    // An empty model removes everything.
    saveModel(ConfigModel(), p);
    for (ConfigFile f : allConfigFiles())
      QVERIFY(!QFile::exists(p.ownFile(f)));
  }

  void clientRtOnOldPipewire()
  {
    QTemporaryDir dir;
    const ConfigPaths p = tempPaths(dir);
    ConfigModel m = ConfigModel::load(p);
    QVERIFY(!m.includeClientRt);
    m.stream["resample.quality"] = "10";
    QVERIFY(m.render().value(ConfigFile::ClientRt).isEmpty());

    writeFile(dir.path() + "/usr/share/pipewire/client-rt.conf", "stream.properties = { }");
    ConfigModel old = ConfigModel::load(p);
    QVERIFY(old.includeClientRt);
    old.stream["resample.quality"] = "10";
    QCOMPARE(old.render().value(ConfigFile::ClientRt), old.render().value(ConfigFile::Client));
    QVERIFY(p.ownFile(ConfigFile::ClientRt).endsWith("/pipewire/client-rt.conf.d/60-simple-configurator.conf"));
  }

  void quantumLimitReachesClients()
  {
    ConfigModel m;
    m.stream["resample.quality"] = "10";
    QVERIFY(!m.render().value(ConfigFile::Client).contains("context.properties"));
    QVERIFY(m.render().value(ConfigFile::Jack).isEmpty());

    m.graph["default.clock.quantum-limit"] = "65536";
    m.graph["default.clock.quantum"] = "2048";
    const auto files = m.render();
    for (ConfigFile f : {ConfigFile::Pipewire, ConfigFile::Client, ConfigFile::Pulse, ConfigFile::Jack})
    {
      const QVariantMap doc = SpaJson::parseDocument(files.value(f));
      QCOMPARE(doc.value("context.properties").toMap().value("default.clock.quantum-limit").toString(), QString("65536"));
    }
    // Only the limit is mirrored; the other graph settings stay in pipewire.conf.
    QVERIFY(!SpaJson::parseDocument(files.value(ConfigFile::Client)).value("context.properties").toMap().contains("default.clock.quantum"));

    // Loading the files back does not turn the mirrored copies into anything else.
    QTemporaryDir dir;
    const ConfigPaths p = tempPaths(dir);
    saveModel(m, p);
    QVERIFY(ConfigModel::load(p) == m);
  }

  void brokenOwnFileIsReported()
  {
    QTemporaryDir dir;
    const ConfigPaths p = tempPaths(dir);
    writeFile(p.ownFile(ConfigFile::Pipewire), "context.properties = { default.clock.rate = 48000 ");
    QStringList warnings;
    const ConfigModel m = ConfigModel::load(p, &warnings);
    QCOMPARE(warnings.size(), 1);
    QVERIFY(m.graph.isEmpty());
  }

  void effectiveConfigOrder()
  {
    QTemporaryDir dir;
    const ConfigPaths p = tempPaths(dir);
    const QString sys = dir.path() + "/usr/share/pipewire";
    writeFile(sys + "/pipewire.conf", "context.properties = { default.clock.rate = 48000 default.clock.quantum = 1024 }");
    writeFile(sys + "/pipewire.conf.d/10-distro.conf", "context.properties = { default.clock.quantum = 512 }");
    writeFile(dir.path() + "/etc/pipewire/pipewire.conf.d/10-distro.conf", "context.properties = { default.clock.min-quantum = 64 }");
    writeFile(p.userDir(ConfigFile::Pipewire) + "/pipewire.conf.d/10-mine.conf", "context.properties = { default.clock.max-quantum = 4096 }");
    writeFile(p.ownFile(ConfigFile::Pipewire), "context.properties = { default.clock.rate = 96000 }");
    writeFile(p.userDir(ConfigFile::Pipewire) + "/pipewire.conf.d/99-late.conf", "context.properties = { default.clock.rate = 44100 }");

    const EffectiveConfig cfg = EffectiveConfig::load(p, ConfigFile::Pipewire);
    QVERIFY(cfg.errors.isEmpty());
    QCOMPARE(cfg.mainFile, sys + "/pipewire.conf");
    // The /etc file with the same name replaces the /usr/share one.
    QCOMPARE(cfg.value("context.properties", "default.clock.quantum").value.toString(), QString("1024"));
    QCOMPARE(cfg.value("context.properties", "default.clock.min-quantum").value.toString(), QString("64"));
    QCOMPARE(cfg.value("context.properties", "default.clock.max-quantum").value.toString(), QString("4096"));
    // Our own file is excluded, and the later user drop-in is reported as overriding.
    QCOMPARE(cfg.value("context.properties", "default.clock.rate").value.toString(), QString("44100"));
    QVERIFY(cfg.overriddenBy("context.properties", "default.clock.rate").file.endsWith("99-late.conf"));
    QVERIFY(!cfg.overriddenBy("context.properties", "default.clock.max-quantum").isValid());
    QCOMPARE(cfg.dropInsAfter.size(), 1);
    QCOMPARE(cfg.dropInsBefore.size(), 2);
  }

  void legacyImport()
  {
    QTemporaryDir dir;
    const ConfigPaths p = tempPaths(dir);
    const QString sys = dir.path() + "/usr/share/pipewire";
    writeFile(sys + "/pipewire.conf", "context.properties = { core.daemon = true default.clock.quantum = 1024 }");
    writeFile(sys + "/client.conf", "stream.properties = { }");
    writeFile(p.legacyUserCopy(ConfigFile::Pipewire), "context.properties = { core.daemon = true default.clock.quantum = 1024 default.clock.rate = 192000 resample.quality = 14 }");
    writeFile(p.legacyUserCopy(ConfigFile::Client), "stream.properties = { node.force-rate = 192000 channelmix.upmix = false }");

    ConfigModel m;
    const LegacyImport r = importLegacyCopies(p, m);
    QCOMPARE(r.files.size(), 2);
    QCOMPARE(m.graph.value("default.clock.rate").toString(), QString("192000"));
    QVERIFY(!m.graph.contains("default.clock.quantum"));  // same as the system file
    QVERIFY(!m.graph.contains("core.daemon"));            // not managed by the configurator
    QVERIFY(!m.graph.contains("resample.quality"));       // ignored by PipeWire in pipewire.conf
    QCOMPARE(m.stream.value("node.force-rate").toString(), QString("192000"));
    QCOMPARE(m.stream.value("channelmix.upmix").toString(), QString("false"));
  }

  void liveSnapshot()
  {
    const QString path = QStringLiteral(TEST_DATA_DIR "/pw-dump.json");
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const LiveSnapshot s = LiveSnapshot::parse(f.readAll());
    QVERIFY2(s.valid, qPrintable(s.error));
    QVERIFY(!s.pipewireVersion.isEmpty());
    QCOMPARE(s.settings.value("clock.rate").toString(), QString("48000"));
    QVERIFY(!s.defaultSink.isEmpty());
    QVERIFY(s.node(s.defaultSink));
    QVERIFY(!s.nodesOf(LiveNode::AlsaOutput).isEmpty());
    QVERIFY(!s.nodesOf(LiveNode::AlsaInput).isEmpty());
    const LiveNode* out = s.nodesOf(LiveNode::AlsaOutput).first();
    QCOMPARE(out->currentValue("resample.quality"), QString("4"));
    const QList<const LiveNode*> bt = s.nodesOf(LiveNode::BtOutput);
    QVERIFY(!bt.isEmpty());
    QVERIFY(!bt.first()->codec.isEmpty());
    QVERIFY(s.btDevice(bt.first()->btAddress));
    QVERIFY(!s.pulseClients.isEmpty());
  }

  void checks()
  {
    QMap<QString, QString> v = {
      {"default.clock.rate", "96000"}, {"default.clock.allowed-rates", "[ 44100 48000 ]"}, {"default.clock.min-quantum", "2048"}, {"default.clock.quantum", "1024"}};
    auto get = [&v](const QString& k) { return v.value(k); };
    const QStringList w = runChecks(Scope::Graph, get, get);
    QCOMPARE(w.size(), 2);

    // Power-of-two rounding and the scaled maximum hitting the limit at 192 kHz.
    QMap<QString, QString> g = {{"default.clock.rate", "48000"},
                                {"default.clock.allowed-rates", "[ 48000 192000 ]"},
                                {"default.clock.quantum", "960"},
                                {"default.clock.max-quantum", "2048"},
                                {"default.clock.quantum-limit", "8192"}};
    auto getG = [&g](const QString& k) { return g.value(k); };
    QStringList gw = runChecks(Scope::Graph, getG, getG);
    QCOMPARE(gw.size(), 1);
    QVERIFY(gw.join(' ').contains("960 → 512"));
    QVERIFY(gw.join(' ').contains("scales to 8192") == false);  // 2048 * 4 = 8192 is not above the limit
    g["default.clock.max-quantum"] = "4096";
    QVERIFY(runChecks(Scope::Graph, getG, getG).join(' ').contains("scales to 16384"));
    g["clock.power-of-two-quantum"] = "false";
    g["default.clock.quantum-limit"] = "65536";
    gw = runChecks(Scope::Graph, getG, getG);
    QCOMPARE(gw.size(), 1);  // only the JACK note remains
    QVERIFY(gw.first().contains("JACK"));

    QMap<QString, QString> bt = {{"bluez5.codecs", "[ aac ldac ]"}};
    QCOMPARE(runChecks(Scope::BtGlobal, [&bt](const QString& k) { return bt.value(k); }, get).size(), 1);

    QCOMPARE(fractionToMs("480/48000"), 10.0);
    QCOMPARE(fractionToMs("bad"), -1.0);
    QCOMPARE(parseIntList("[ 44100 48000 ]"), QList<int>({44100, 48000}));
  }
};

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
