#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "ConfigManager.h"

int main(int argc, char* argv[])
{
  QGuiApplication app(argc, argv);

  QQmlApplicationEngine engine;
  ConfigManager configMgr;
  engine.rootContext()->setContextProperty("backend", &configMgr);

  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.loadFromModule("PipewireConfigurator", "Main");

  return QCoreApplication::exec();
}
