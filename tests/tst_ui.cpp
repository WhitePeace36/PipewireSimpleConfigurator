// Drives the real window: ticks settings, edits values, uses presets and saves, then checks the files.
#include "MainWindow.h"
#include "SettingEditor.h"
#include "SpaJson.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
const QString kDac = "alsa_output.usb-Test_DAC-00.analog-stereo";

QString readFile(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return {};
  return QString::fromUtf8(f.readAll());
}

SettingEditor* editor(MainWindow& w, const QString& key, int index = 0)
{
  int n = 0;
  for (SettingEditor* e : w.findChildren<SettingEditor*>())
  {
    if (e->def().key == key && n++ == index)
      return e;
  }
  return nullptr;
}

QCheckBox* overrideBox(SettingEditor* e)
{
  return e->findChildren<QCheckBox*>().first();
}

QPushButton* button(QWidget& w, const QString& text)
{
  for (QPushButton* b : w.findChildren<QPushButton*>())
  {
    if (b->text() == text && b->isVisible())
      return b;
  }
  return nullptr;
}

void setText(SettingEditor* e, const QString& text)
{
  if (auto* combo = e->findChild<QComboBox*>(); combo && combo->isEditable())
  {
    combo->setEditText(text);
    return;
  }
  auto* edit = e->findChild<QLineEdit*>();
  QVERIFY(edit);
  edit->selectAll();
  QTest::keyClicks(edit, text);
}

bool anyVisibleLabelContains(QWidget& w, const QString& text)
{
  for (QLabel* l : w.findChildren<QLabel*>())
  {
    if (l->isVisible() && l->text().contains(text))
      return true;
  }
  return false;
}
}  // namespace

class TestUi : public QObject
{
  Q_OBJECT

private:
  QTemporaryDir m_dir;
  MainWindow::Options options() const
  {
    MainWindow::Options o;
    o.pwDumpFile = QStringLiteral(TEST_DATA_DIR "/pw-dump.json");
    o.wireplumberVersion = "0.5.8";
    o.paths.userConfigHome = m_dir.path() + "/config";
    o.paths.systemPipewireDirs = {m_dir.path() + "/sys/pipewire"};
    o.paths.systemWireplumberDirs = {m_dir.path() + "/sys/wireplumber"};
    return o;
  }
  QString ownFile(ConfigFile f) const { return options().paths.ownFile(f); }

private slots:
  void initTestCase()
  {
    QVERIFY(m_dir.isValid());
    QDir().mkpath(m_dir.path() + "/sys/pipewire");
    QFile f(m_dir.path() + "/sys/pipewire/pipewire.conf");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("context.properties = { default.clock.quantum = 1024 }\n");
  }

  void graphSettings()
  {
    MainWindow w(options());
    w.show();
    w.showPage("graph");

    // Inherited from the system file, not set here.
    SettingEditor* quantum = editor(w, "default.clock.quantum");
    QVERIFY(quantum);
    QVERIFY(!quantum->isOverridden());
    QCOMPARE(quantum->effectiveValue(), QString("1024"));

    SettingEditor* rate = editor(w, "default.clock.rate");
    // The DSD-range rates can be picked from the list.
    auto* rateBox = rate->findChild<QComboBox*>();
    QVERIFY(rateBox->findText("1411200") >= 0);
    QVERIFY(rateBox->findText("1536000") >= 0);
    overrideBox(rate)->click();
    setText(rate, "96000");
    QVERIFY(w.isWindowModified());
    // 96000 is not in the allowed rates (48000 from the live graph): a warning appears.
    QVERIFY(anyVisibleLabelContains(w, "not one of the allowed rates"));

    SettingEditor* allowed = editor(w, "default.clock.allowed-rates");
    overrideBox(allowed)->click();
    for (QCheckBox* box : allowed->findChildren<QCheckBox*>())
    {
      if (box->text() == "96 kHz" || box->text() == "44.1 kHz")
        box->click();
    }
    QVERIFY(!anyVisibleLabelContains(w, "not one of the allowed rates"));

    // The quantum limit accepts PipeWire's maximum (65536) but not more.
    SettingEditor* limit = editor(w, "default.clock.quantum-limit");
    overrideBox(limit)->click();
    setText(limit, "70000");
    QVERIFY(!readFile(ownFile(ConfigFile::Pipewire)).contains("70000"));
    setText(limit, "65536");

    button(w, "Save")->click();
    QVERIFY(!w.isWindowModified());
    for (ConfigFile f : {ConfigFile::Client, ConfigFile::Pulse, ConfigFile::Jack})
      QVERIFY2(readFile(ownFile(f)).contains("default.clock.quantum-limit = 65536"), qPrintable(configFileName(f)));
    const QString text = readFile(ownFile(ConfigFile::Pipewire));
    QVERIFY2(text.contains("default.clock.rate = 96000"), qPrintable(text));
    QVERIFY2(text.contains("default.clock.allowed-rates = [ 44100 48000 96000 ]"), qPrintable(text));
    QVERIFY(!text.contains("default.clock.quantum ="));  // inherited, not written
  }

  void savedValuesLoadBack()
  {
    MainWindow w(options());
    w.showPage("graph");
    QVERIFY(editor(w, "default.clock.rate")->isOverridden());
    QCOMPARE(editor(w, "default.clock.rate")->effectiveValue(), QString("96000"));
  }

  void streamPresetsKeepFilterGraphSeparate()
  {
    MainWindow w(options());
    w.show();
    w.showPage("stream");
    button(w, "Disable upmixing")->click();
    QVERIFY(editor(w, "channelmix.upmix")->isOverridden());
    QVERIFY(editor(w, "channelmix.stereo-widen")->isOverridden());  // pinned even though it equals the default
    QVERIFY(!editor(w, "audioconvert.filter-graph.disable")->isOverridden());

    // Kaiser parameters only show for the Kaiser window.
    QVERIFY(!editor(w, "resample.param.kaiser.stopband-attenuation")->isVisible());
    button(w, "Best quality")->click();
    QVERIFY(editor(w, "resample.param.kaiser.stopband-attenuation")->isVisible());

    button(w, "Save")->click();
    for (ConfigFile f : {ConfigFile::Client, ConfigFile::Pulse})
    {
      const QString text = readFile(ownFile(f));
      QVERIFY2(text.contains("channelmix.upmix = false"), qPrintable(text));
      QVERIFY(text.contains("channelmix.stereo-widen = 0.0"));
      QVERIFY(text.contains("resample.window = kaiser"));
      QVERIFY(!text.contains("filter-graph"));
    }
  }

  void deviceRule()
  {
    MainWindow w(options());
    w.show();
    w.showPage("alsa:" + kDac);
    SettingEditor* suspend = editor(w, "session.suspend-timeout-seconds");
    overrideBox(suspend)->click();
    setText(suspend, "0");
    // The live format is shown for unset settings.
    QCOMPARE(editor(w, "audio.format")->effectiveValue(), QString("S32LE"));
    button(w, "Save")->click();

    const QString text = readFile(ownFile(ConfigFile::WirePlumber));
    QVERIFY2(text.contains("node.name = \"" + kDac + "\""), qPrintable(text));
    QVERIFY(text.contains("session.suspend-timeout-seconds = 0"));
    QVERIFY(button(w, "Remove all settings for this device"));
  }

  void bluetooth()
  {
    MainWindow w(options());
    w.show();
    w.showPage("bluetooth");
    SettingEditor* codecs = editor(w, "bluez5.codecs");
    overrideBox(codecs)->click();
    for (QCheckBox* box : codecs->findChildren<QCheckBox*>())
    {
      if (box->text() == "SBC" && box->isChecked())
        box->click();
      if (box->text() == "LDAC" && !box->isChecked())
        box->click();
    }
    QVERIFY(anyVisibleLabelContains(w, "SBC is not allowed"));

    w.showPage("bt:AC_80_0A_12_34_56");
    SettingEditor* ldac = editor(w, "bluez5.a2dp.ldac.quality");
    overrideBox(ldac)->click();
    auto* combo = ldac->findChild<QComboBox*>();
    combo->setCurrentIndex(combo->findData("hq"));
    emit combo->activated(combo->currentIndex());
    // Playback and microphone sections each have their own settings.
    SettingEditor* micSuspend = editor(w, "session.suspend-timeout-seconds", 1);
    QVERIFY(micSuspend);
    button(w, "Save")->click();

    const QString text = readFile(ownFile(ConfigFile::WirePlumber));
    QVERIFY2(text.contains("device.name = \"bluez_card.AC_80_0A_12_34_56\""), qPrintable(text));
    QVERIFY(text.contains("bluez5.a2dp.ldac.quality = hq"));
    QVERIFY(text.contains("monitor.bluez.properties"));
    const QVariantMap doc = SpaJson::parseDocument(text);
    QVERIFY(!SpaJson::toStringList(doc.value("monitor.bluez.properties").toMap().value("bluez5.codecs")).contains("sbc"));
    // The device rule from the previous test is still there.
    QVERIFY(text.contains(kDac));
  }

  void applicationRule()
  {
    MainWindow w(options());
    w.show();
    w.showPage("app:Discord");
    SettingEditor* quirks = editor(w, "quirks");
    overrideBox(quirks)->click();
    for (QCheckBox* box : quirks->findChildren<QCheckBox*>())
    {
      if (box->text() == "Block microphone volume changes")
        box->click();
    }
    button(w, "Save")->click();
    const QString text = readFile(ownFile(ConfigFile::Pulse));
    QVERIFY2(text.contains("application.process.binary = \"Discord\""), qPrintable(text));
    QVERIFY(text.contains("quirks = [ block-source-volume ]"));
  }

  void untickRemovesSettingsAndFiles()
  {
    MainWindow w(options());
    w.show();
    w.showPage("graph");
    overrideBox(editor(w, "default.clock.rate"))->click();
    overrideBox(editor(w, "default.clock.allowed-rates"))->click();
    QVERIFY(w.isWindowModified());
    button(w, "Revert")->click();
    QVERIFY(!w.isWindowModified());
    QVERIFY(editor(w, "default.clock.rate")->isOverridden());

    overrideBox(editor(w, "default.clock.rate"))->click();
    overrideBox(editor(w, "default.clock.allowed-rates"))->click();
    overrideBox(editor(w, "default.clock.quantum-limit"))->click();
    button(w, "Save")->click();
    QVERIFY(!QFile::exists(ownFile(ConfigFile::Pipewire)));  // nothing left to set
    QVERIFY(!QFile::exists(ownFile(ConfigFile::Jack)));
    QVERIFY(!readFile(ownFile(ConfigFile::Client)).contains("quantum-limit"));
  }

  // Nothing on any page may stick out past the right edge (the page cannot scroll sideways),
  // at the default size and at the minimum window size.
  void noHorizontalOverflow_data()
  {
    QTest::addColumn<int>("width");
    QTest::newRow("default") << 1100;
    QTest::newRow("minimum") << 900;
  }
  void noHorizontalOverflow()
  {
    MainWindow w(options());
    QFETCH(int, width);
    w.resize(width, 820);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    const QStringList pages = {"graph", "stream", "pulse", "outputs", "inputs", "alsa:" + kDac, "bluetooth", "bt:AC_80_0A_12_34_56", "apps", "app:Discord", "files"};
    for (const QString& id : pages)
    {
      w.showPage(id);
      QTest::qWait(150);
      auto* area = w.findChild<QScrollArea*>();
      QWidget* page = area->widget();
      QVERIFY(page->findChildren<SettingEditor*>().size() > 0 || id == "files" || id == "apps" || id == "outputs" || id == "inputs");
      const int limit = area->viewport()->width();
      QStringList offenders;
      for (QWidget* c : page->findChildren<QWidget*>())
      {
        if (!c->isVisible())
          continue;
        const int right = c->mapTo(page, QPoint(c->width(), 0)).x();
        if (right > limit)
          offenders.append(QStringLiteral("%1 right=%2 minHint=%3 hint=%4 text=%5")
                             .arg(c->metaObject()->className())
                             .arg(right)
                             .arg(c->minimumSizeHint().width())
                             .arg(c->sizeHint().width())
                             .arg(qobject_cast<QLabel*>(c) ? qobject_cast<QLabel*>(c)->text().left(40) : QString()));
      }
      if (!offenders.isEmpty())
      {
        QStringList info = {QStringLiteral("page minHint %1").arg(page->minimumSizeHint().width())};
        for (SettingEditor* e : page->findChildren<SettingEditor*>())
          info.append(QStringLiteral("  %1 minHint=%2 visible=%3").arg(e->def().key).arg(e->minimumSizeHint().width()).arg(e->isVisible()));
        qWarning("%s", qPrintable(info.join('\n')));
      }
      QVERIFY2(offenders.isEmpty(), qPrintable(id + " (viewport " + QString::number(limit) + "):\n" + offenders.mid(0, 8).join('\n')));
    }
  }

  // Lets an integration script feed the generated files to a real PipeWire.
  void cleanupTestCase()
  {
    const QString out = qEnvironmentVariable("PSC_TEST_OUTPUT");
    if (out.isEmpty())
      return;
    for (ConfigFile f : allConfigFiles())
    {
      if (!QFile::exists(ownFile(f)))
        continue;
      const QString target = out + "/" + QFileInfo(ownFile(f)).dir().dirName() + "/" + QFileInfo(ownFile(f)).fileName();
      QDir().mkpath(QFileInfo(target).absolutePath());
      QFile::remove(target);
      QFile::copy(ownFile(f), target);
    }
  }

  void iconIsBuiltIn()
  {
    QIcon icon(":/icons/SimplePipewireConfigurator.png");
    QVERIFY(!icon.isNull());
    QCOMPARE(icon.pixmap(256).size(), QSize(256, 256));
  }

  void legacyCopyIsReported()
  {
    QFile f(options().paths.legacyUserCopy(ConfigFile::Client));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("stream.properties = { node.force-rate = 192000 }\n");
    f.close();
    MainWindow w(options());
    w.show();
    QVERIFY(anyVisibleLabelContains(w, "Full copies of client.conf"));
    QVERIFY(button(w, "Import and retire copies…"));
  }

  // Ticking a setting without a known value must not write an empty or made-up value.
  void tickWithoutValueWaitsForInput()
  {
    QTemporaryDir dir;
    MainWindow::Options o = options();
    o.paths.userConfigHome = dir.path() + "/config";
    MainWindow w(o);
    w.show();
    w.showPage("stream");

    SettingEditor* rate = editor(w, "node.force-rate");
    overrideBox(rate)->click();
    QVERIFY(overrideBox(rate)->isChecked());  // ticked and waiting for a value
    QVERIFY(!w.isWindowModified());
    QVERIFY(anyVisibleLabelContains(w, "Enter a value"));
    setText(rate, "96000");
    QVERIFY(w.isWindowModified());

    SettingEditor* upmix = editor(w, "channelmix.upmix");
    QCOMPARE(upmix->effectiveValue(), QString());  // no default, nothing inherited
    overrideBox(upmix)->click();
    QVERIFY(!upmix->isOverridden());  // not silently "No"
    auto* combo = upmix->findChild<QComboBox*>();
    combo->setCurrentIndex(combo->findData("true"));
    emit combo->activated(combo->currentIndex());
    QVERIFY(upmix->isOverridden());

    button(w, "Save")->click();
    const QString text = readFile(o.paths.ownFile(ConfigFile::Client));
    QVERIFY2(text.contains("node.force-rate = 96000"), qPrintable(text));
    QVERIFY(text.contains("channelmix.upmix = true"));
    QVERIFY(!text.contains("\"\""));

    // Unticking a pending setting leaves nothing behind.
    w.showPage("alsa:" + kDac);
    SettingEditor* prio = editor(w, "priority.session");
    QCOMPARE(prio->effectiveValue(), QString());
    overrideBox(prio)->click();
    overrideBox(prio)->click();
    QVERIFY(!overrideBox(prio)->isChecked());
    QVERIFY(!w.isWindowModified());
  }

  // A hand-edited drop-in that does not parse is left alone by saves that do not touch it, and backed
  // up (not lost) when it has to be replaced.
  void brokenOwnFileIsKept()
  {
    QTemporaryDir dir;
    MainWindow::Options o = options();
    o.paths.userConfigHome = dir.path() + "/config";
    const QString wpFile = o.paths.ownFile(ConfigFile::WirePlumber);
    const QByteArray broken = "monitor.alsa.rules = [ { matches = [ \n";
    QDir().mkpath(QFileInfo(wpFile).absolutePath());
    {
      QFile f(wpFile);
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(broken);
    }

    MainWindow w(o);
    w.show();
    w.showPage("graph");
    overrideBox(editor(w, "clock.power-of-two-quantum"))->click();
    button(w, "Save")->click();
    QCOMPARE(readFile(wpFile).toUtf8(), broken);  // untouched

    w.showPage("bluetooth");
    SettingEditor* sbcXq = editor(w, "bluez5.enable-sbc-xq");
    overrideBox(sbcXq)->click();
    auto* combo = sbcXq->findChild<QComboBox*>();
    combo->setCurrentIndex(combo->findData("true"));
    emit combo->activated(combo->currentIndex());
    button(w, "Save")->click();
    QVERIFY(readFile(wpFile).contains("bluez5.enable-sbc-xq = true"));
    const QStringList backups = QFileInfo(wpFile).dir().entryList({"*.bak-*"}, QDir::Files);
    QCOMPARE(backups.size(), 1);
    QCOMPARE(readFile(QFileInfo(wpFile).dir().filePath(backups.first())).toUtf8(), broken);
  }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
