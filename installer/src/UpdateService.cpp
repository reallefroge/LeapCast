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
#include <QStringList>
#include <QVersionNumber>
#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
constexpr auto OfficialReleaseApi =
    "https://api.github.com/repos/reallefroge/LeapCast/releases/latest";

QString quoteInstallerArgument(QString value){
    value.replace(QLatin1Char('"'),QStringLiteral("\\\""));
    return QStringLiteral("\"")+value+QStringLiteral("\"");
}

QString quotePowerShellLiteral(QString value){
    value.replace(QLatin1Char('\''),QStringLiteral("''"));
    return QStringLiteral("'")+value+QStringLiteral("'");
}

#ifdef Q_OS_WIN
bool launchInstallerElevated(const QString&path,const QStringList&arguments,DWORD*outPid=nullptr){
    const QString parameters=[&]{QStringList quoted;for(const auto&arg:arguments)quoted<<quoteInstallerArgument(arg);return quoted.join(QLatin1Char(' '));}();
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb=L"runas";const std::wstring file=QDir::toNativeSeparators(path).toStdWString();const std::wstring params=parameters.toStdWString();
    info.lpFile=file.c_str();info.lpParameters=params.c_str();info.nShow=SW_SHOWNORMAL;
    const bool launched=ShellExecuteExW(&info)!=FALSE;
    if(launched&&outPid)*outPid=GetProcessId(info.hProcess);
    if(info.hProcess)CloseHandle(info.hProcess);
    return launched;
}
#else
bool launchInstallerElevated(const QString&path,const QStringList&arguments){
    return QProcess::startDetached(path,arguments);
}
#endif
}

UpdateService::UpdateService(QObject* parent) : QObject(parent) {
    downloadTimeout_.setSingleShot(true);
    downloadTimeout_.setInterval(180000);
    connect(&downloadTimeout_,&QTimer::timeout,this,[this]{
        if(activeDownload_)activeDownload_->abort();
    });
}

void UpdateService::check(bool userInitiated) {
    if (checkInProgress_) {
        if (userInitiated) emit failed(QStringLiteral("An update check is already running."));
        return;
    }
    checkInProgress_ = true;
    const QUrl url(QString::fromLatin1(OfficialReleaseApi));
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "LeapcastStudio-Updater");
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        checkInProgress_ = false;
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            reply->deleteLater();
            return;
        }
        const QJsonObject release = QJsonDocument::fromJson(bytes).object();
        QString tag = release.value(QStringLiteral("tag_name")).toString();
        if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) tag.remove(0, 1);
        const auto remote = QVersionNumber::fromString(tag);
        const auto current = QVersionNumber::fromString(QCoreApplication::applicationVersion());
        // Only a strictly newer semantic version is an update. In particular,
        // never offer an older major release (for example v2.x to v3.x users).
        if (remote.isNull() || current.isNull() || QVersionNumber::compare(remote, current) <= 0) {
            emit upToDate();
            reply->deleteLater();
            return;
        }
        const QString expectedInstaller=QStringLiteral("LeapcastStudio-Setup-%1.exe").arg(tag);
        for (const auto& value : release.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
            if (name.compare(expectedInstaller,Qt::CaseInsensitive)!=0) continue;
            emit updateAvailable(tag, release.value(QStringLiteral("body")).toString(),
                                 QUrl(asset.value(QStringLiteral("browser_download_url")).toString()),
                                 name, asset.value(QStringLiteral("digest")).toString());
            reply->deleteLater();
            return;
        }
        emit failed(QStringLiteral("Release v%1 does not contain its matching installer (%2). The update was not installed.").arg(tag,expectedInstaller));
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
    activeDownload_=reply;
    downloadTimeout_.start();
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateService::progress);
    connect(reply, &QNetworkReply::finished, this, [this, reply, name, expectedDigest] {
        const bool timedOut=!downloadTimeout_.isActive();
        downloadTimeout_.stop();
        if(activeDownload_==reply)activeDownload_=nullptr;
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(timedOut?QStringLiteral("The update download timed out. Leapcast Studio will open normally so you can try again."):reply->errorString()); reply->deleteLater(); return;
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
            QStringLiteral("/DIR=%1").arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath())),
            QStringLiteral("/LOG=%1").arg(QDir::toNativeSeparators(logPath))
        };
        // Keep a recovery copy of account settings outside the install folder.
        // Normal upgrades never touch AppData, but this also protects against a
        // damaged installer or an over-aggressive older uninstaller.
        const QString settingsPath=QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("settings.json"));
        const QString settingsBackup=settingsPath+QStringLiteral(".update-backup");
        if(QFile::exists(settingsPath)){QFile::remove(settingsBackup);QFile::copy(settingsPath,settingsBackup);}
        // Launch the verified installer directly. This avoids the old .cmd
        // handoff, whose quoting/UAC boundary could leave the splash screen at
        // 100% or close the app without ever running Setup. Inno Setup's own
        // [Run] entry (runasoriginaluser) is meant to relaunch Leapcast Studio
        // afterward, but that depends on Windows correctly recovering "the
        // original user" from our elevated installer process, which isn't
        // always reliable — when it silently fails, nothing reopens after a
        // silent auto-update.
#ifdef Q_OS_WIN
        DWORD installerPid=0;
        const bool launched=launchInstallerElevated(path,arguments,&installerPid);
        if (!launched) {
            emit failed(QStringLiteral("Windows could not launch the downloaded installer. Leapcast Studio will remain open."));
            reply->deleteLater(); return;
        }
        // Belt-and-suspenders fallback: wait for the installer process to
        // exit, then relaunch the app ourselves from a detached, unelevated
        // helper if Inno's own relaunch didn't already do it. This runs as
        // the current (non-elevated) user, same as this process.
        if (installerPid!=0) {
            const QString exePath=quotePowerShellLiteral(
                QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
            QProcess::startDetached(QStringLiteral("powershell.exe"),{
                QStringLiteral("-NoProfile"),QStringLiteral("-WindowStyle"),QStringLiteral("Hidden"),
                QStringLiteral("-Command"),
                QStringLiteral(
                    "$installer = Get-Process -Id %1 -ErrorAction SilentlyContinue; "
                    "if ($installer) { $installer.WaitForExit(); "
                    "if ($installer.ExitCode -eq 0) { Start-Sleep -Seconds 1; "
                    "if (-not (Get-Process -Name LeapcastStudio -ErrorAction SilentlyContinue)) "
                    "{ Start-Process -FilePath %2 } } }"
                ).arg(installerPid).arg(exePath)});
        }
#else
        if (!launchInstallerElevated(path,arguments)) {
            emit failed(QStringLiteral("Windows could not launch the downloaded installer. Leapcast Studio will remain open."));
            reply->deleteLater(); return;
        }
#endif
        reply->deleteLater();
        QCoreApplication::quit();
    });
}
