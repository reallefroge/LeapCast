#include "UpdateService.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QVersionNumber>
#include <QDir>

namespace {
constexpr auto OfficialReleaseApi =
    "https://api.github.com/repos/reallefroge/MultiStreamChat/releases/latest";
}

UpdateService::UpdateService(QObject* parent) : QObject(parent) {}

void UpdateService::check(bool userInitiated) {
    userInitiated_ = userInitiated;
    const QUrl url(QString::fromLatin1(OfficialReleaseApi));
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "LeapcastStudio-Updater");
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            if (userInitiated_) emit failed(reply->errorString());
            reply->deleteLater();
            return;
        }
        const QJsonObject release = QJsonDocument::fromJson(bytes).object();
        QString tag = release.value(QStringLiteral("tag_name")).toString();
        if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) tag.remove(0, 1);
        const auto remote = QVersionNumber::fromString(tag);
        const auto current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
        // Leapcast Studio moved from an inaccurate legacy build sequence to
        // the public 2.x version line. Let older installations cross that
        // reset once; normal semantic comparisons apply after the update.
        const bool crossesVersionReset = !remote.isNull() && !current.isNull()
            && current.majorVersion() > remote.majorVersion() && remote.majorVersion() == 2
            && QVersionNumber::compare(remote, QVersionNumber(2, 0, 3)) >= 0;
        if (remote.isNull() || (!crossesVersionReset && QVersionNumber::compare(remote, current) <= 0)) {
            if (userInitiated_) emit upToDate();
            reply->deleteLater();
            return;
        }
        for (const auto& value : release.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (!name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) continue;
            emit updateAvailable(tag, release.value(QStringLiteral("body")).toString(),
                                 QUrl(asset.value(QStringLiteral("browser_download_url")).toString()),
                                 name, asset.value(QStringLiteral("digest")).toString());
            reply->deleteLater();
            return;
        }
        if (userInitiated_) emit failed(QStringLiteral("The latest release has no Windows installer asset."));
        reply->deleteLater();
    });
}

void UpdateService::downloadAndInstall(const QUrl& url, const QString& name,
                                       const QString& expectedDigest) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "LeapcastStudio-Updater");
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateService::progress);
    connect(reply, &QNetworkReply::finished, this, [this, reply, name, expectedDigest] {
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString()); reply->deleteLater(); return;
        }
        if (expectedDigest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
            const QString actual = QString::fromLatin1(
                QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
            if (actual.compare(expectedDigest.mid(7), Qt::CaseInsensitive) != 0) {
                emit failed(QStringLiteral("The downloaded installer's SHA-256 digest did not match GitHub."));
                reply->deleteLater(); return;
            }
        }
        const QString safeName = name.isEmpty() ? QStringLiteral("LeapcastStudio-Update.exe")
                                                : QFileInfo(name).fileName();
        QDir tempDirectory(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
        for (const QString& stale : tempDirectory.entryList(
                 {QStringLiteral("LeapcastStudio-Setup-*.exe"),
                  QStringLiteral("LeapcastStudio-Update.exe")}, QDir::Files)) {
            tempDirectory.remove(stale);
        }
        const QString path = tempDirectory.filePath(safeName);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
            emit failed(QStringLiteral("Could not save the downloaded installer."));
            reply->deleteLater(); return;
        }
        file.close();
        const QString logPath = tempDirectory.filePath(QStringLiteral("LeapcastStudio-Update.log"));
        const QStringList arguments{
            QStringLiteral("/VERYSILENT"),
            QStringLiteral("/SUPPRESSMSGBOXES"),
            QStringLiteral("/NORESTART"),
            QStringLiteral("/CLOSEAPPLICATIONS"),
            QStringLiteral("/SP-"),
            QStringLiteral("/UPDATE"),
            QStringLiteral("/LOG=%1").arg(QDir::toNativeSeparators(logPath))
        };
        if (!QProcess::startDetached(path, arguments)) {
            emit failed(QStringLiteral("Could not launch the downloaded installer."));
            reply->deleteLater(); return;
        }
        reply->deleteLater();
        QCoreApplication::quit();
    });
}
