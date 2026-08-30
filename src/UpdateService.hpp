#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

class UpdateService final : public QObject {
    Q_OBJECT
public:
    explicit UpdateService(QObject* parent = nullptr);
    void check(bool userInitiated = false);
    void downloadAndInstall(const QUrl& assetUrl, const QString& assetName,
                            const QString& expectedDigest = {});
signals:
    // Keep this signature synchronized with the five values emitted by
    // UpdateService::check() and consumed by MainWindow.
    void updateAvailable(const QString& version, const QString& notes,
                         const QUrl& assetUrl, const QString& assetName,
                         const QString& digest);
    void upToDate();
    void progress(qint64 received, qint64 total);
    void failed(const QString& detail);
private:
    QNetworkAccessManager network_;
    QNetworkReply* activeDownload_{};
    QTimer downloadTimeout_;
    bool checkInProgress_{};
};
