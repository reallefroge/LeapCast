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
        // Do not let the installer race the still-running application. A tiny
        // detached handoff waits for this exact process to exit, installs in
        // place, then explicitly relaunches the installed executable. The
        // fallback relaunch also runs when Setup returns a non-zero code, so a
        // failed update does not leave the streamer wondering where the app went.
        const qint64 pid=QCoreApplication::applicationPid();
        const QString installedExe=QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const QString helperPath=tempDirectory.filePath(QStringLiteral("LeapcastStudio-Update.cmd"));
        // Keep a recovery copy of account settings outside the install folder.
        // Normal upgrades never touch AppData, but this also protects against a
        // damaged installer or an over-aggressive older uninstaller.
        const QString settingsPath=QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("settings.json"));
        const QString settingsBackup=settingsPath+QStringLiteral(".update-backup");
        if(QFile::exists(settingsPath)){QFile::remove(settingsBackup);QFile::copy(settingsPath,settingsBackup);}
        QFile helper(helperPath);
        if(!helper.open(QIODevice::WriteOnly|QIODevice::Truncate|QIODevice::Text)){
            emit failed(QStringLiteral("Could not create the update handoff."));reply->deleteLater();return;
        }
        auto quote=[](QString value){value.replace(QLatin1Char('"'),QStringLiteral("\"\""));return QStringLiteral("\"")+value+QStringLiteral("\"");};
        QStringList quotedArgs;for(const auto&argument:arguments)quotedArgs<<quote(argument);
        const QString script=QStringLiteral(
            "@echo off\r\n"
            "setlocal\r\n"
            ":wait_for_app\r\n"
            "tasklist /FI \"PID eq %1\" /NH 2>NUL | find \"%1\" >NUL\r\n"
            "if not errorlevel 1 (ping 127.0.0.1 -n 2 >NUL & goto wait_for_app)\r\n"
            "start \"\" /wait %2 %3\r\n"
            "if exist %4 start \"\" %4\r\n"
            "del \"%%~f0\"\r\n")
            .arg(pid).arg(quote(QDir::toNativeSeparators(path)),quotedArgs.join(QLatin1Char(' ')),quote(installedExe));
        helper.write(script.toLocal8Bit());helper.close();
        if (!QProcess::startDetached(QStringLiteral("cmd.exe"),
                {QStringLiteral("/D"),QStringLiteral("/S"),QStringLiteral("/C"),QDir::toNativeSeparators(helperPath)})) {
            emit failed(QStringLiteral("Could not launch the update handoff."));
            reply->deleteLater(); return;
        }
        reply->deleteLater();
        QCoreApplication::quit();
    });
}
