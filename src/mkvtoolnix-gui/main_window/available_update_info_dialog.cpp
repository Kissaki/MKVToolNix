#include "common/common_pch.h"

#if defined(HAVE_UPDATE_CHECK)

#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>

#include "common/markdown.h"
#include "common/qt.h"
#include "common/regex.h"
#include "mkvtoolnix-gui/forms/main_window/available_update_info_dialog.h"
#include "mkvtoolnix-gui/main_window/available_update_info_dialog.h"

namespace mtx::gui {

AvailableUpdateInfoDialog::AvailableUpdateInfoDialog(QWidget *parent)
  : QDialog{parent, Qt::Dialog | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint}
  , ui{new Ui::AvailableUpdateInfoDialog}
  , m_statusRetrieved{}
  , m_status{UpdateCheckStatus::Failed}
{
  // Setup UI controls.
  ui->setupUi(this);

  setWindowTitle(QY("Online check for updates"));
  updateDisplay();

  connect(ui->close,    &QPushButton::clicked, this, &AvailableUpdateInfoDialog::accept);
  connect(ui->download, &QPushButton::clicked, this, &AvailableUpdateInfoDialog::visitDownloadLocation);

  auto checker = new UpdateChecker{parent};

  connect(checker, &UpdateChecker::checkFinished,               this, &AvailableUpdateInfoDialog::updateCheckFinished);
  connect(checker, &UpdateChecker::releaseInformationRetrieved, this, &AvailableUpdateInfoDialog::setReleaseInformation);

  checker->setRetrieveReleasesInfo(true).start();
}

AvailableUpdateInfoDialog::~AvailableUpdateInfoDialog() {
}

void
AvailableUpdateInfoDialog::setReleaseInformation(std::shared_ptr<pugi::xml_document> releasesInfo) {
  m_releasesInfo = releasesInfo;

  updateDisplay();
}

void
AvailableUpdateInfoDialog::setChangeLogContent(QString const &content) {
  auto html = QStringList{};
  html << Q("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\" \"http://www.w3.org/TR/REC-html40/strict.dtd\">"
            "<html><head><meta name=\"qrichtext\" content=\"1\" />"
            "<style type=\"text/css\">"
            "p, li { white-space: pre-wrap; }\n"
            "</style>"
            "</head><body>");

  html << Q("<h1>%1 [<a href=\"%2\">%3</a>]</h1>")
    .arg(QY("MKVToolNix news & changes").toHtmlEscaped())
    .arg(UpdateChecker::newsURL().toHtmlEscaped())
    .arg(QY("Read full NEWS.md file online").toHtmlEscaped());

  html << content;

  html << Q("</body></html>");

  ui->changeLog->setHtml(html.join(Q("")));
}

void
AvailableUpdateInfoDialog::updateCheckFinished(UpdateCheckStatus status,
                                               mtx_release_version_t releaseVersion) {
  m_statusRetrieved = true;
  m_status          = status;
  m_releaseVersion  = releaseVersion;

  updateDisplay();
}

void
AvailableUpdateInfoDialog::updateDisplay() {
  updateStatusDisplay();
  updateReleasesInfoDisplay();
}

void
AvailableUpdateInfoDialog::updateStatusDisplay() {
  auto statusStr = !m_statusRetrieved                                   ? QY("Downloading release information")
                 : UpdateCheckStatus::NoNewReleaseAvailable == m_status ? QY("Currently no newer version is available online.")
                 : UpdateCheckStatus::NewReleaseAvailable   == m_status ? QY("There is a new version available online.")
                 :                                                        QY("There was an error querying the update status.");
  ui->status->setText(statusStr);

  if (!m_statusRetrieved || (UpdateCheckStatus::Failed == m_status))
    return;

  ui->currentVersion->setText(to_qs(m_releaseVersion.current_version.to_string()));
  ui->availableVersion->setText(to_qs(m_releaseVersion.latest_source.to_string()));
  ui->close->setText(QY("&Close"));

  auto url = m_releaseVersion.urls.find("general");
  if ((url != m_releaseVersion.urls.end()) && !url->second.empty()) {
    m_downloadURL = to_qs(url->second);
    ui->downloadURL->setText(Q("<html><body><a href=\"%1\">%1</a></body></html>").arg(m_downloadURL.toHtmlEscaped()));
    ui->download->setEnabled(true);
  }
}

QString
AvailableUpdateInfoDialog::formattedVersionHeading(pugi::xpath_node const &release) {
  auto versionStrQ = Q(std::string{release.node().attribute("version").value()}).toHtmlEscaped();
  auto artist      = Q(release.node().attribute("codename-artist").value());
  auto codename    = Q(release.node().attribute("codename").value());

  if (codename.isEmpty() || artist.isEmpty())
    return QY("Version %1").arg(versionStrQ);

  auto codenameQ = codename.toHtmlEscaped();
  auto url       = QUrl{Q("https://www.youtube.com/results")};
  auto query     = QUrlQuery{};

  query.addQueryItem(Q("search_query"), Q("%1 %2").arg(artist).arg(codename));
  url.setQuery(query);

  return Q("%1 [<a href=\"%2\">%3</a>]")
    .arg(QY("Version %1 \"%2\"").arg(versionStrQ).arg(codenameQ))
    .arg(url.toString(QUrl::FullyEncoded))
    .arg(QY("Listen on YouTube").toHtmlEscaped());
}

void
AvailableUpdateInfoDialog::updateReleasesInfoDisplay() {
  if (!m_releasesInfo) {
    setChangeLogContent(Q(""));
    return;
  }

  if (!m_statusRetrieved || (UpdateCheckStatus::Failed == m_status))
    return;

  auto html              = QStringList{};
  auto numReleasesOutput = 0;
  auto releases          = m_releasesInfo->select_nodes("/mkvtoolnix-releases/release[not(@version='HEAD')]");
  auto reReleased        = mtx::regex::jp::Regex{"^released\\s+v?[\\d\\.]+", "iS"};
  auto reBug             = mtx::regex::jp::Regex{"(#\\d+)",                  "iS"};
  auto reNewlines        = mtx::regex::jp::Regex{"\r?\n",                    "iS"};
  auto bugFormatter      = [](mtx::regex::jp::NumSub const &matches) -> std::string {
    auto number_str = matches[1].substr(1);
    return fmt::format("<a href=\"https://gitlab.com/mbunkus/mkvtoolnix/issues/{0}\">#{0}</a>", number_str);
  };

  releases.sort();

  for (auto &release : releases) {
    auto versionStr  = std::string{release.node().attribute("version").value()};
    auto headingQ     = formattedVersionHeading(release);
    auto currentTypeQ = QString{};
    auto inList       = false;

    html << Q("<h2>%1</h2>").arg(headingQ);

    for (auto change = release.node().child("changes").first_child() ; change ; change = change.next_sibling()) {
      if (   (std::string{change.name()} != "change")
          || mtx::regex::match(change.child_value(), reReleased))
        continue;

      auto typeQ = Q(change.attribute("type").value()).toHtmlEscaped();
      if (typeQ != currentTypeQ) {
        currentTypeQ = typeQ;

        if (inList) {
          inList = false;
          html << Q("</ul></p>");
        }

        html << Q("<h3>%1</h3>").arg(typeQ);
      }

      if (!inList) {
        inList = true;
        html << Q("<p><ul>");
      }

      auto text = mtx::regex::replace(mtx::regex::replace(mtx::markdown::to_html(change.child_value()), reBug, "g",  bugFormatter), reNewlines, "g", " ");
      html     << Q("<li>%1</li>").arg(Q(text));
    }

    if (inList)
      html << Q("</ul></p>");

    numReleasesOutput++;
    if ((10 < numReleasesOutput) && (version_number_t{versionStr} < m_releaseVersion.current_version))
      break;
  }

  html << Q("<p><a href=\"%1\">%2</a></h1>").arg(UpdateChecker::newsURL()).arg(QY("Read full NEWS.md file online"));

  setChangeLogContent(html.join(Q("\n")));
}

void
AvailableUpdateInfoDialog::visitDownloadLocation() {
  QDesktopServices::openUrl(m_downloadURL);
}

}

#endif  // HAVE_UPDATE_CHECK
