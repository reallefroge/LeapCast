#pragma once
#include <QNetworkAccessManager>
#include <QObject>
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
    void updateAvailable(const QString& version, const QString& notes,
                         const QUrl& assetUrl, const QString& assetName,
                         const QString& digest);
    void upToDate();
    void progress(qint64 received, qint64 total);
    void failed(const QString& detail);
private:
    QNetworkAccessManager network_;
    bool userInitiated_{};
};

