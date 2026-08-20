#pragma once

#include "Core.hpp"

#include <QNetworkAccessManager>
#include <QTimer>
#include <QWebSocket>

class QNetworkReply;

class TwitchChatService final : public QObject {
    Q_OBJECT
public:
    explicit TwitchChatService(QObject* parent = nullptr);
    void connectChannel(const QString& channel);
    void disconnectChannel();
    QString channel() const { return channel_; }
signals:
    void messageReceived(const ChatMessage& message);
    void statusChanged(const QString& state, const QString& detail);
    void viewerCountChanged(int viewers);
private:
    void parseLine(const QString& line);
    void pollViewers();
    static QHash<QString,QString> parseTags(QStringView input);
    QWebSocket socket_;
    QNetworkAccessManager network_;
    QTimer reconnect_;
    QTimer viewerPoll_;
    QString channel_;
    int backoffMs_{2000};
};

class TwitchModerationService final : public QObject {
    Q_OBJECT
public:
    explicit TwitchModerationService(QObject* parent = nullptr);
    void configure(QString clientId, QString accessToken, QString moderatorId);
    void resolveBroadcaster(const QString& login);
    void ban(const QString& broadcasterId, const QString& userId,
             int seconds, const QString& reason);
    void unban(const QString& broadcasterId, const QString& userId);
    void deleteMessage(const QString& broadcasterId, const QString& messageId);
    void sendMessage(const QString& broadcasterId, const QString& text);
signals:
    void broadcasterResolved(const QString& login, const QString& id);
    void actionFinished(const QString& action, bool success, const QString& detail);
private:
    QNetworkRequest request(const QUrl& url) const;
    void watch(QNetworkReply* reply, const QString& action);
    QNetworkAccessManager network_;
    QString clientId_, token_, moderatorId_;
};

class StreamlabsService final : public QObject {
    Q_OBJECT
public:
    explicit StreamlabsService(QObject* parent = nullptr);
    void connectToken(const QString& token);
    void disconnectService();
    static QList<StreamEvent> normalize(const QJsonObject& payload);
signals:
    void eventReceived(const StreamEvent& event);
    void statusChanged(const QString& state, const QString& detail);
private:
    void parseSocketIoFrame(const QString& frame);
    QWebSocket socket_;
    QTimer reconnect_;
    QString token_;
};
