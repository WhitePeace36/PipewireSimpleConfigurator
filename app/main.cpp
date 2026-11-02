#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  QApplication::setApplicationName("SimplePipewireConfigurator");
  QApplication::setApplicationDisplayName(QObject::tr("Pipewire Configurator"));
  QApplication::setApplicationVersion(APP_VERSION);
  QApplication::setDesktopFileName("SimplePipewireConfigurator");
  // The installed theme icon if present, otherwise the copy built into the program.
  QApplication::setWindowIcon(QIcon::fromTheme("SimplePipewireConfigurator", QIcon(":/icons/SimplePipewireConfigurator.png")));

  QCommandLineParser parser;
  parser.setApplicationDescription(QObject::tr("Configure PipeWire and WirePlumber."));
  parser.addHelpOption();
  parser.addVersionOption();
  QCommandLineOption dump("pw-dump", QObject::tr("Read devices from a saved pw-dump output instead of the running PipeWire (for testing)."), "file");
  QCommandLineOption wp("wireplumber-version", QObject::tr("Assume this WirePlumber version (for testing)."), "version");
  QCommandLineOption page("page", QObject::tr("Page to open, e.g. graph, stream, bluetooth, files."), "id");
  QCommandLineOption shot("screenshot", QObject::tr("Save a screenshot of the window to this file and quit (for testing)."), "file");
  parser.addOptions({dump, wp, page, shot});
  parser.process(app);

  MainWindow::Options options;
  options.pwDumpFile = parser.value(dump);
  options.wireplumberVersion = parser.value(wp);
  MainWindow window(options);
  if (parser.isSet(page))
    window.showPage(parser.value(page));
  window.show();

  if (parser.isSet(shot))
  {
    QTimer::singleShot(600, &window,
                       [&window, &parser, &shot]
                       {
                         window.grab().save(parser.value(shot));
                         QApplication::quit();
                       });
  }
  return QApplication::exec();
}
