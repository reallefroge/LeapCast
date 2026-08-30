#pragma once

#include "Core.hpp"

#include <QNetworkAccessManager>
#include <QJsonArray>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QStringList>

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
    void scopesValidated(const QStringList& scopes);
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
    // Fires once per transition from offline (or not-yet-known) to live, as
    // detected by the same viewer-count poll — used to reset AutoMod's
    // per-broadcast Twitch timeout escalation at the start of each stream.
    void broadcastWentLive();
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
    bool wasLive_{false};
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
    // Reads Twitch's current mod-pinned chat message. This uses Twitch's
    // official /helix/chat/pins endpoint (GA as of May 2026) rather than
    // guessing from IRC tags, which do not identify the active pin.
    void getPinnedMessage(const QString& broadcasterId);
    // Creates a Twitch clip of the broadcaster's current stream, mirroring the
    // "Clip" button on twitch.tv. Only requires the clips:edit scope, not
    // moderator status, so it works while watching any live channel.
    void createClip(const QString& broadcasterId);
    // Gathers everything the user card shows about one chatter: account age,
    // whether they follow this channel and since when, and their subscription
    // tier. Each piece is a separate Helix call and any of them may be refused
    // (Twitch no longer exposes another channel's follower total publicly), so
    // the parts that succeed are emitted and the rest simply stay absent.
    void fetchUserCard(const QString& broadcasterId, const QString& userId);
    // Twitch account-level block list, which is what the card's Block box sets.
    void setUserBlocked(const QString& userId, bool blocked);
signals:
    void broadcasterResolved(const QString& login, const QString& id);
    void userCardReady(const QString& userId, const QJsonObject& card);
    void actionFinished(const QString& action, bool success, const QString& detail);
    void bansReceived(const QJsonArray& bans);
    void unbanRequestsReceived(const QJsonArray& requests);
    void clipCreated(const QString& id, const QUrl& editUrl);
    void pinnedMessageChanged(const ChatMessage& message, bool active);
private:
    QNetworkRequest request(const QUrl& url) const;
    void watch(QNetworkReply* reply, const QString& action);
    QNetworkAccessManager network_;
    QString clientId_, token_, moderatorId_;
    // One card is assembled from several replies that land in any order, so the
    // partial result is held here until the last of them settles.
    QHash<QString,QJsonObject> pendingCards_;
    QHash<QString,int> pendingCardParts_;
    // Broadcasters we've already been told (via HTTP 401/403) we can't moderate.
    // Prevents autoModerate() from retrying — and re-warning the user about —
    // the same permission failure on every subsequent chat message.
    QSet<QString> autoModDenied_;
    // A channel that rejects the pin read (normally because the connected
    // account is not a moderator there) is not hammered every few seconds.
    QSet<QString> pinReadDenied_;
};

class TwitchEventSubService final : public QObject {
    Q_OBJECT
public:
    explicit TwitchEventSubService(QObject* parent = nullptr);
    void connectRedemptions(const QString& clientId, const QString& accessToken,
                            const QString& broadcasterId);
    void disconnectService();
signals:
    void eventReceived(const StreamEvent& event);
    void statusChanged(const QString& state, const QString& detail);
private:
    void open(const QUrl& url = QUrl(QStringLiteral("wss://eventsub.wss.twitch.tv/ws")));
    void parseMessage(const QString& message);
    void createRedemptionSubscription(const QString& sessionId);
    QWebSocket socket_;
    QNetworkAccessManager network_;
    QTimer reconnect_;
    QString clientId_, token_, broadcasterId_;
    QUrl reconnectUrl_;
    bool transferringSession_{};
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

// Resolves a Twitch chat line into text + emote-image runs.
//
// Twitch's own emotes arrive in the IRC "emotes" tag as id:start-end ranges,
// indexed in CODE POINTS, so a message containing an astral emoji shifts every
// later range if you index UTF-16 units instead. Third-party emotes (BTTV,
// FrankerFaceZ, 7TV) have no tag at all - they are plain words that have to be
// matched against a per-channel word list fetched from each provider.
class TwitchEmoteService final : public QObject {
    Q_OBJECT
public:
    explicit TwitchEmoteService(QObject* parent=nullptr);
    // Broadcaster id, which the IRC "room-id" tag already carries, so no extra
    // Twitch API call is needed to start loading.
    void loadForChannel(const QString& broadcasterId);
    bool ready() const { return loaded_; }
    // Returns a runs array: {"text":...} and {"url":...,"alt":...} pieces.
    QJsonArray buildRuns(const QString& text,const QString& emotesTag) const;
    int emoteWordCount() const { return words_.size(); }
    // Pure parsers over each provider's response shape, public so they can be
    // exercised against captured fixtures without touching the network.
    void absorbBttv(const QJsonDocument& document);
    void absorbFfz(const QJsonDocument& document);
    void absorbSeventv(const QJsonDocument& document);
signals:
    void wordsUpdated(int count);
private:
    void fetch(const QUrl& url,const QString& source);
    QNetworkAccessManager network_;
    QHash<QString,QString> words_;   // emote code -> image URL
    QString broadcasterId_;
    bool loaded_{};
};
