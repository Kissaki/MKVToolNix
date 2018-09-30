#include "common/common_pch.h"

#include <QRegularExpression>
#include <QThread>

#include "common/qt.h"
#include "common/version.h"
#include "mkvtoolnix-gui/app.h"
#include "mkvtoolnix-gui/main_window/main_window.h"
#include "mkvtoolnix-gui/util/installation_checker.h"
#include "mkvtoolnix-gui/util/process.h"
#include "mkvtoolnix-gui/util/settings.h"

namespace mtx { namespace gui { namespace Util {

InstallationChecker::InstallationChecker(QObject *parent)
  : QObject{parent}
{
}

InstallationChecker::~InstallationChecker() {
}

void
InstallationChecker::runChecks() {
  m_problems.clear();
  auto mkvmergeExe = Util::Settings::get().actualMkvmergeExe();
  auto versionRE   = QRegularExpression{Q("^mkvmerge [[:space:]]+ v ( [[:digit:].]+ )"), QRegularExpression::ExtendedPatternSyntaxOption};
  auto guiVersion  = Q(get_current_version().to_string());

  if (mkvmergeExe.isEmpty() || !QFileInfo{mkvmergeExe}.exists())
    m_problems << Problem{ ProblemType::MkvmergeNotFound, {} };

  else {
    auto process = Process::execute(mkvmergeExe, { Q("--version") });

    if (process->hasError())
      m_problems << Problem{ ProblemType::MkvmergeCannotBeExecuted, {} };

    else {
      // mkvmerge v9.7.1 ('Pandemonium') 64bit
      auto output = process->output().join(QString{});
      auto match  = versionRE.match(output);

      if (!match.hasMatch())
        m_problems << Problem{ ProblemType::MkvmergeVersionNotRecognized, output };

      else {
        m_mkvmergeVersion = match.captured(1);
        if (guiVersion != match.captured(1))
          m_problems << Problem{ ProblemType::MkvmergeVersionDiffers, match.captured(1) };
      }
    }
  }

#if defined(SYS_WINDOWS)
  auto magicFile = App::applicationDirPath() + "/share/misc/magic.mgc";
  if (!QFileInfo{magicFile}.exists())
    m_problems << Problem{ ProblemType::FileNotFound, Q("share\\misc\\magic.mgc") };

#endif  // SYS_WINDOWS

  if (!m_problems.isEmpty())
    emit problemsFound(m_problems);

  emit finished();
}

void
InstallationChecker::checkInstallation() {
  auto thread  = new QThread{};
  auto checker = new InstallationChecker{};

  checker->moveToThread(thread);

  connect(thread,  &QThread::started,                   checker,           &InstallationChecker::runChecks);
  connect(checker, &InstallationChecker::problemsFound, MainWindow::get(), &MainWindow::displayInstallationProblems);
  connect(checker, &InstallationChecker::finished,      checker,           &InstallationChecker::deleteLater);
  connect(thread,  &QThread::finished,                  thread,            &QThread::deleteLater);

  thread->start();
}

QString
InstallationChecker::mkvmergeVersion()
  const {
  return m_mkvmergeVersion;
}

InstallationChecker::Problems
InstallationChecker::problems()
  const {
  return m_problems;
}

}}}
