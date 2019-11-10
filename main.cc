// This file is part of Projecteur - https://github.com/jahnf/projecteur - See LICENSE.md and README.md
#include "projecteurapp.h"
#include "projecteur-GitVersion.h"

#include "runguard.h"
#include "settings.h"

#include <QCommandLineParser>

#ifndef NDEBUG
#include <QQmlDebuggingEnabler>
#endif

#include <iostream>

namespace {
  class Main : public QObject {};

  std::ostream& operator<<(std::ostream& os, const QString& s) {
    os << s.toStdString();
    return os;
  }

  struct print {
    template<typename T>
    auto& operator<<(const T& a) const { return std::cout << a; }
    ~print() { std::cout << std::endl; }
  };

  struct error {
    template<typename T>
    auto& operator<<(const T& a) const { return std::cerr << a; }
    ~error() { std::cerr << std::endl; }
  };
}

int main(int argc, char *argv[])
{
  QCoreApplication::setApplicationName("Projecteur");
  QCoreApplication::setApplicationVersion(projecteur::version_string());
  ProjecteurApplication::Options options;
  QString ipcCommand;
  {
    QCommandLineParser parser;
    parser.setApplicationDescription(Main::tr("Linux/X11 application for the Logitech Spotlight device."));
    const QCommandLineOption versionOption(QStringList{ "v", "version"}, Main::tr("Print application version."));
    const QCommandLineOption fullVersionOption(QStringList{ "f", "fullversion" });
    const QCommandLineOption helpOption(QStringList{ "h", "help"}, Main::tr("Show command line usage."));
    const QCommandLineOption fullHelpOption(QStringList{ "help-all"}, Main::tr("Show complete command line usage."));
    const QCommandLineOption cfgFileOption(QStringList{ "cfg" }, Main::tr("Set custom config file."), "file");
    const QCommandLineOption commandOption(QStringList{ "c", "command"}, Main::tr("Send command to a running instance."), "cmd");
    parser.addOptions({versionOption, helpOption, fullHelpOption, commandOption, cfgFileOption, fullVersionOption});

    QStringList args;
    for(int i = 0; i < argc; ++i) {
      args.push_back(argv[i]);
    }
    parser.process(args);
    if (parser.isSet(helpOption) || parser.isSet(fullHelpOption))
    {
      print() << QCoreApplication::applicationName() << " "
              << projecteur::version_string() << std::endl;
      print() << "Usage: projecteur [option]" << std::endl;
      print() << "<Options>";
      print() << "  -h, --help             " << helpOption.description();
      print() << "  --help-all             " << fullHelpOption.description();
      print() << "  -v, --version          " << versionOption.description();
      print() << "  --cfg FILE             " << cfgFileOption.description();
      print() << "  -c COMMAND|PROPERTY    " << commandOption.description() << std::endl;
      print() << "<Commands>";
      print() << "  spot=[on|off]          " << Main::tr("Turn spotlight on/off.");
      print() << "  settings=[show|hide]   " << Main::tr("Show/hide preferences dialog.");
      print() << "  quit                   " << Main::tr("Quit the running instance.");

      if (!parser.isSet(fullHelpOption)) return 0;

      print() << "" << std::endl << "<Properties>";
      const auto getValues = [](const Settings::StringProperty& sp) -> QString
      {
        if (sp.type == Settings::StringProperty::Type::Integer
            || sp.type == Settings::StringProperty::Type::Double) {
          return QString("(%1 ... %2)").arg(sp.range[0].toString()).arg(sp.range[1].toString());
        }

        if (sp.type == Settings::StringProperty::Type::StringEnum) {
          QStringList values;
          for (const auto& v : sp.range) {
            values.push_back(v.toString());
          }
          return QString("(%1)").arg(values.join(", "));
        }
        return QString::null;
      };

      Settings settings;
      const auto& sp = settings.stringProperties();
      for (auto it = sp.cbegin(), end = sp.cend(); it != end; ++it)
      {
        print() << "  " << it.key() << "=[" << it.value().typeToString(it.value().type)<< "]"
                << "   " << getValues(*it);
      }
      return 0;
    }
    else if (parser.isSet(versionOption) || parser.isSet(fullVersionOption))
    {
      print() << QCoreApplication::applicationName().toStdString() << " "
              << projecteur::version_string();
      if (parser.isSet(fullVersionOption) ||
          (std::string(projecteur::version_branch()) != "master" && 
           std::string(projecteur::version_branch()) != "not-within-git-repo"))
      { // Not a build from master branch, print out additional information:
        print() << "  - git-branch: " << projecteur::version_branch();
        print() << "  - git-hash: " << projecteur::version_fullhash();
      }
      // Show if we have a build from modified sources
      if (projecteur::version_isdirty())
        print() << "  - dirty-flag: " << projecteur::version_isdirty();
      return 0;
    }
    else if (parser.isSet(commandOption))
    {
      ipcCommand = parser.value(commandOption);
      if (ipcCommand.isEmpty()) {
        error() << Main::tr("Command cannot be an empty string.");
        return 44;
      }
    }
    if (parser.isSet(cfgFileOption)) {
      options.configFile = parser.value(cfgFileOption);
    }
  }

  RunGuard guard(QCoreApplication::applicationName());
  if (!guard.tryToRun())
  {
    if (ipcCommand.size())
    {
      return ProjecteurCommandClientApp(ipcCommand, argc, argv).exec();
    }
    else {
      error() << Main::tr("Another application instance is already running. Exiting.");
      return 42;
    }
  }
  else if (ipcCommand.size())
  {
    // No other application instance running - but command option was used.
    error() << Main::tr("Cannot send command '%1' - no running application instance found.").arg(ipcCommand);
    return 43;
  }

  //QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  ProjecteurApplication app(argc, argv, options);
  return app.exec();
}
