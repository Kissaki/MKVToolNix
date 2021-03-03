#include "common/common_pch.h"

#include <Qt>
#include <QDir>
#include <QOperatingSystemVersion>
#include <QScreen>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QSysInfo>

#include "common/fs_sys_helpers.h"
#include "common/version.h"
#include "mkvtoolnix-gui/app.h"
#include "mkvtoolnix-gui/util/installation_checker.h"
#include "mkvtoolnix-gui/util/settings.h"
#include "mkvtoolnix-gui/util/system_information.h"

namespace mtx::gui::Util {

using namespace mtx::gui;

namespace {

void
gatherGeneralInfo(QStringList &info) {
  InstallationChecker checker;
  checker.runChecks();

  info << Q("# MKVToolNix") << Q("");
  info << Q("## General") << Q("");

  info << Q("* MKVToolNix GUI version: %1").arg(Q(get_current_version().to_string()));
  info << Q("* mkvmerge version: %1").arg(checker.mkvmergeVersion().isEmpty() ? Q("unknown") : checker.mkvmergeVersion());
#if defined(SYS_WINDOWS)
  info << Q("* Installation type: %1").arg(App::isInstalled() ? Q("installed") : Q("portable"));
#endif
  info << Q("* Installation path: %1").arg(QDir::toNativeSeparators(App::applicationDirPath()));
  info << Q("* INI file location: %1").arg(QDir::toNativeSeparators(Util::Settings::iniFileName()));

  info << Q("") << Q("## Installation problems") << Q("");

  auto problems = checker.problems();
  if (problems.isEmpty()) {
    info << Q("No problems were found with the installation.");
    return;
  }

  for (auto const &problem : problems) {
    auto name = problem.first == InstallationChecker::ProblemType::FileNotFound                 ? Q("FileNotFound")
              : problem.first == InstallationChecker::ProblemType::MkvmergeNotFound             ? Q("MkvmergeNotFound")
              : problem.first == InstallationChecker::ProblemType::MkvmergeCannotBeExecuted     ? Q("MkvmergeCannotBeExecuted")
              : problem.first == InstallationChecker::ProblemType::MkvmergeVersionNotRecognized ? Q("MkvmergeVersionNotRecognized")
              : problem.first == InstallationChecker::ProblemType::MkvmergeVersionDiffers       ? Q("MkvmergeVersionDiffers")
              :                                                                                   Q("unknown");
    info << Q("* Type: %1, info: %2").arg(name).arg(problem.second.isEmpty() ? Q("—") : problem.second);
  }
}

void
gatherOperatingSystemInfo(QStringList &info) {
  QString osName, osVersion;
  auto versionInfo = QOperatingSystemVersion::current();

  if (versionInfo.type() != QOperatingSystemVersion::Unknown) {
    osName = versionInfo.name();
  } else {
    osName = QSysInfo::productType();
  }

  if (versionInfo.majorVersion() != -1) {
    osVersion += QString::number(versionInfo.majorVersion());
    if (versionInfo.minorVersion() != -1) {
      osVersion += QLatin1Char('.');
      osVersion += QString::number(versionInfo.minorVersion());
      if (versionInfo.microVersion() != -1) {
        osVersion += QLatin1Char('.');
        osVersion += QString::number(versionInfo.microVersion());
      }
    }
  } else {
    osVersion = QSysInfo::productVersion();
  }

  info << Q("") << Q("## Operating system");
  info << Q("* Name: %1").arg(osName);
  info << Q("* Version: %1").arg(osVersion);
  info << Q("* Pretty name: %1").arg(QSysInfo::prettyProductName());
}

void
gatherScreenInfo(QStringList &info) {
  info << Q("") << Q("## Screens");

  int idx = 0;
  for (auto const &screen : App::screens()) {
    info << Q("") << Q("### Screen %1").arg(idx++) << Q("");
    info << Q("* Device pixel ratio: %1").arg(screen->devicePixelRatio());
    info << Q("* Logical DPI: %1x%2").arg(screen->logicalDotsPerInchX()).arg(screen->logicalDotsPerInchX());
    info << Q("* Physical DPI: %1x%2").arg(screen->physicalDotsPerInchX()).arg(screen->physicalDotsPerInchX());
    info << Q("* Physical size: %1x%2").arg(screen->physicalSize().width()).arg(screen->physicalSize().height());
    info << Q("* Virtual size: %1x%2").arg(screen->virtualSize().width()).arg(screen->virtualSize().height());
    info << Q("* Geometry: %1x%2@%3x%4").arg(screen->geometry().width()).arg(screen->geometry().height()).arg(screen->geometry().x()).arg(screen->geometry().y());
  }

#if defined(SYS_WINDOWS)
  QSettings reg{Q("HKEY_CURRENT_USER\\Control Panel\\Desktop"), QSettings::NativeFormat};

  info << Q("") << Q("### Desktop scaling settings") << Q("");

  info << Q("* Scaling mode (`Win8DpiScaling`): %1").arg(reg.value("Win8DpiScaling", "not set").toString());
  info << Q("* Scaling override (`DesktopDPIOverride`): %1").arg(reg.value("DesktopDPIOverride", "not set").toString());
  info << Q("* System-wide scale factor (`LogPixels`): %1").arg(reg.value("LogPixels", "not set").toString());
#endif
}

void
gatherEnvironmentVariables(QStringList &info) {
  info << Q("") << Q("## Environment variables") << Q("");

  auto keys = QStringList{} << Q("QT_AUTO_SCREEN_SCALE_FACTOR") << Q("QT_SCALE_FACTOR") << Q("QT_SCREEN_SCALE_FACTORS") << Q("QT_DEVICE_PIXEL_RATIO")
                            << Q("MTX_LOGGER")                  << Q("MTX_DEBUG")       << Q("MKVTOOLNIX_DEBUG")        << Q("MKVMERGE_DEBUG");
  keys.sort();

  for (auto const &name : keys)
    info << Q("* `%1=%2`").arg(name).arg(Q(mtx::sys::get_environment_variable(to_utf8(name))));
}

void
gatherQtInfo(QStringList &info) {
  info << Q("") << Q("# Qt") << Q("");

  info << Q("* Version: %1.%2.%3").arg((QT_VERSION >> 16) & 0xff).arg((QT_VERSION >> 8) & 0xff).arg(QT_VERSION & 0xff);
  info << Q("* Build ABI: %1").arg(QSysInfo::buildAbi());

#if defined(__clang__)
  info << Q("* Compiler: clang++ %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(__GNUC__)
  info << Q("* Compiler: g++ %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
  info << Q("* Compiler: unknown");
#endif

}

} // anonymous namespace

QString
gatherSystemInformation() {
  QStringList info;

  gatherGeneralInfo(info);

  info << Q("") << Q("# System");

  gatherOperatingSystemInfo(info);
  gatherScreenInfo(info);
  gatherEnvironmentVariables(info);

  gatherQtInfo(info);

  return info.join(Q("\n"));
}

}
