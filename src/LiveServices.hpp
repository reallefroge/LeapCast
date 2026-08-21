#pragma once

#include "Core.hpp"

#include <QNetworkAccessManager>
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <QWebSocket>

class QNetworkReply;

class TwitchAuthService final : public QObject {
    Q_OBJECT
public:
    explicit TwitchAuthService(QObject* parent = nullptr);
    void authorize(const QString& clientId);
    void restore(const QString& clientId, const QString& accessToken,
                 const QString& refreshToken);
signals:
    void browserAuthorizationReady(const QUrl& url);
    void authorizationPending();
    void authorized(const QString& accessToken, const QString& refreshToken,
                    const QString& userId, const QString& login, int expiresIn);
    void authorizationFailed(const QString& detail);
private:
    void requestDeviceCode();
    void pollForToken();
    void validateToken(const QString& accessToken, const QString& refreshToken);
    void refreshToken();
    void finishWithError(const QString& detail);
    QNetworkAccessManager network_;
    QTimer pollTimer_;
    QTimer refreshTimer_;
    QString clientId_;
    QString deviceCode_;
    QString accessToken_;
    QString refreshToken_;
    QString scopes_;
};

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
    void listBans(const QString& broadcasterId);
    void listUnbanRequests(const QString& broadcasterId);
    void resolveUnbanRequest(const QString& broadcasterId, const QString& requestId,
                             bool approved, const QString& resolutionText);
    void ban(const QString& broadcasterId, const QString& userId,
             int seconds, const QString& reason);
    void unban(const QString& broadcasterId, const QString& userId);
    void deleteMessage(const QString& broadcasterId, const QString& messageId);
    void sendMessage(const QString& broadcasterId, const QString& text);
    // Creates a Twitch clip of the broadcaster's current stream, mirroring the
    // "Clip" button on twitch.tv. Only requires the clips:edit scope, not
    // moderator status, so it works while watching any live channel.
    void createClip(const QString& broadcasterId);
signals:
    void broadcasterResolved(const QString& login, const QString& id);
    void actionFinished(const QString& action, bool success, const QString& detail);
    void bansReceived(const QJsonArray& bans);
    void unbanRequestsReceived(const QJsonArray& requests);
    void clipCreated(const QString& id, const QUrl& editUrl);
private:
    QNetworkRequest request(const QUrl& url) const;
    void watch(QNetworkReply* reply, const QString& action);
    QNetworkAccessManager network_;
    QString clientId_, token_, moderatorId_;
    // Broadcasters we've already been told (via HTTP 401/403) we can't moderate.
    // Prevents autoModerate() from retrying — and re-warning the user about —
    // the same permission failure on every subsequent chat message.
    QSet<QString> autoModDenied_;
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
