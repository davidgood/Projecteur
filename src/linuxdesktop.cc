// This file is part of Projecteur - https://github.com/jahnf/projecteur
// - See LICENSE.md and README.md

#include "linuxdesktop.h"

#include "logging.h"

#include <QApplication>
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
  #include <QDesktopWidget>
#endif
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QScreen>

#if HAS_Qt_DBus
#include <QDBusInterface>
#include <QDBusReply>
#endif

LOGGING_CATEGORY(desktop, "desktop")

namespace {
#if HAS_Qt_DBus
  // -----------------------------------------------------------------------------------------------
  QPixmap grabScreenDBusGnome()
  {
    const auto filepath = QDir::temp().absoluteFilePath("000_projecteur_zoom_screenshot.png");
    QDBusInterface interface(QStringLiteral("org.gnome.Shell"),
                             QStringLiteral("/org/gnome/Shell/Screenshot"),
                             QStringLiteral("org.gnome.Shell.Screenshot"));
    QDBusReply<bool> reply = interface.call(QStringLiteral("Screenshot"), false, false, filepath);

    if (reply.value())
    {
      QPixmap pm(filepath);
      QFile::remove(filepath);
      return pm;
    }
    logError(desktop) << LinuxDesktop::tr("Screenshot via GNOME DBus interface failed.");
    return QPixmap();
  }

  // -----------------------------------------------------------------------------------------------
  QPixmap grabScreenDBusKde()
  {
    // Create a temporary file to receive the screenshot data
    QTemporaryFile tempFile;
    if (!tempFile.open()) {
      qDebug() << "Failed to create temporary file";
      return QPixmap();
    }

    QDBusUnixFileDescriptor pipe(tempFile.handle());

    QVariantMap options;
    options["include-cursor"] = false;  // Include mouse cursor in screenshot
    options["include-decoration"] = false;  // Include window decorations
    options["native-resolution"] = true;  // Use native resolution

    // Create DBus interface
    QDBusInterface interface(
        QStringLiteral("org.kde.KWin.ScreenShot2"),
        QStringLiteral("/org/kde/KWin/ScreenShot2"),
        QStringLiteral("org.kde.KWin.ScreenShot2")
    );

    QList<QVariant> args;
    args << QVariant::fromValue(options)
         << QVariant::fromValue(pipe);

    QDBusReply<QVariantMap> reply = interface.callWithArgumentList(
        QDBus::Block,
        QStringLiteral("CaptureActiveScreen"),
        args
    );

    if (!reply.isValid()) {
      qDebug() << "Screenshot failed:" << reply.error().message();
      return QPixmap();
    }

    QVariantMap results = reply.value();

    if (results["status"].toString() != "ok") {
      qDebug() << "Screenshot failed with status:" << results["status"].toString();
      qDebug() << "Error message:" << results["error"].toString();
      return QPixmap();
    }

    tempFile.seek(0);

    QImage screenshot;
    screenshot.load(&tempFile, "PNG");  // Format should match what KWin writes

    return QPixmap::fromImage(screenshot);
  }
#endif // HAS_Qt_DBus

  // -----------------------------------------------------------------------------------------------
  QPixmap grabScreenVirtualDesktop(QScreen* screen)
  {
    QRect g;
    for (const auto s : QGuiApplication::screens()) {
      g = g.united(s->geometry());
    }

    #if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPixmap pm(QApplication::primaryScreen()->grabWindow(
                 QApplication::desktop()->winId(), g.x(), g.y(), g.width(), g.height()));
    #else
    QPixmap pm(QApplication::primaryScreen()->grabWindow(0, g.x(), g.y(), g.width(), g.height()));
    #endif

    if (!pm.isNull())
    {
      pm.setDevicePixelRatio(screen->devicePixelRatio());
      return pm.copy(screen->geometry());
    }

    return pm;
  }
} // end anonymous namespace

LinuxDesktop::LinuxDesktop(QObject* parent)
  : QObject(parent)
{
  const auto env = QProcessEnvironment::systemEnvironment();
  { // check for Kde and Gnome
    const auto kdeFullSession = env.value(QStringLiteral("KDE_FULL_SESSION"));
    const auto gnomeSessionId = env.value(QStringLiteral("GNOME_DESKTOP_SESSION_ID"));
    const auto desktopSession = env.value(QStringLiteral("DESKTOP_SESSION"));
    const auto xdgCurrentDesktop = env.value(QStringLiteral("XDG_CURRENT_DESKTOP"));
    if (gnomeSessionId.size() || xdgCurrentDesktop.contains("Gnome", Qt::CaseInsensitive)) {
      m_type = LinuxDesktop::Type::Gnome;
    }
    else if (kdeFullSession.size() || desktopSession == "kde-plasma") {
      m_type = LinuxDesktop::Type::KDE;
    }
  }

  { // check for wayland session
    const auto waylandDisplay = env.value(QStringLiteral("WAYLAND_DISPLAY"));
    const auto xdgSessionType = env.value(QStringLiteral("XDG_SESSION_TYPE"));
    m_wayland = (xdgSessionType == "wayland")
                || waylandDisplay.contains("wayland", Qt::CaseInsensitive);
  }
}

QPixmap LinuxDesktop::grabScreen(QScreen* screen) const
{
  if (screen == nullptr) {
    return QPixmap();
  }

  if (isWayland()) {
    return grabScreenWayland(screen);
  }

  #if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
    const bool isVirtualDesktop = QApplication::primaryScreen()->virtualSiblings().size() > 1;
  #else
    const bool isVirtualDesktop = QApplication::desktop()->isVirtualDesktop();
  #endif

  if (isVirtualDesktop) {
    return grabScreenVirtualDesktop(screen);
  }

  // everything else.. usually X11
  return screen->grabWindow(0);
}

QPixmap LinuxDesktop::grabScreenWayland(QScreen* screen) const
{
#if HAS_Qt_DBus
  QPixmap pm;
  switch (type())
  {
  case LinuxDesktop::Type::Gnome:
    pm = grabScreenDBusGnome();
    break;
  case LinuxDesktop::Type::KDE:
    pm = grabScreenDBusKde();
    break;
  default:
    logWarning(desktop) << tr("Currently zoom on Wayland is only supported via DBus on KDE and GNOME.");
  }
  return pm.isNull() ? pm : pm.copy(screen->geometry());
#else
  Q_UNUSED(screen);
  logWarning(desktop) << tr("Projecteur was compiled without Qt DBus. Currently zoom on Wayland is "
                            "only supported via DBus on KDE and GNOME.");
  return QPixmap();
#endif
}
