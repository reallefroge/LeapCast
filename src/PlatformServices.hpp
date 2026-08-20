#pragma once
#include "Core.hpp"
#include <QNetworkAccessManager>
#include <QSet>
#include <QTimer>
#include <QWebEnginePage>
#include <QUrlQuery>

class QNetworkReply;

class YouTubeChatService final : public QObject {
    Q_OBJECT
public:
    explicit YouTubeChatService(QString platform, bool verticalOnly, QObject* parent=nullptr);
    void connectTarget(const QString& target);
    void disconnectService();
    QString videoId() const{return videoId_;}
    QString liveChatId() const{return liveChatId_;}
signals:
    void messageReceived(const ChatMessage& message);
    void statusChanged(const QString& state,const QString& detail);
    void viewerCountChanged(int viewers);
private:
    void resolveTarget(); void bootstrap(const QString& videoId); void poll(); void pollViewers();
    void resolveViaLiveRedirect(const QString& base); void resolveViaStreamsScrape(const QString& base);
    static QStringList liveIds(const QByteArray& html);
    static QString runsText(const QJsonObject& message);
    QNetworkAccessManager network_; QTimer retry_,pollTimer_,viewerTimer_;
    QString platform_,target_,videoId_,liveChatId_,apiKey_,clientVersion_,continuation_;
    bool verticalOnly_{}; bool explicitVideo_{}; QSet<QString> seen_;
};

class YouTubeModerationService final : public QObject {
    Q_OBJECT
public:
    explicit YouTubeModerationService(QObject* parent=nullptr);
    void setAccessToken(QString token){token_=std::move(token);autoModDenied_.clear();}
    void deleteMessage(const QString& messageId);
    void ban(const QString& liveChatId,const QString& channelId,int seconds,const QString& user);
    void removeBan(const QString& banId);
signals:
    void actionFinished(const QString& action,bool success,const QString& detail);
    // permanent distinguishes a "Hide from channel" ban from a timed timeout,
    // so callers can record/display which one actually happened.
    void banCreated(const QString& id,const QString& user,bool permanent);
private:
    QNetworkRequest request(const QUrl& url)const; void watch(QNetworkReply*,const QString&);
    QNetworkAccessManager network_; QString token_;
    // Live chats we've already been told (via HTTP 401/403) we can't moderate —
    // see TwitchModerationService::autoModDenied_ for why this matters.
    QSet<QString> autoModDenied_;
};

class TikTokPage final : public QWebEnginePage {
    Q_OBJECT
public: using QWebEnginePage::QWebEnginePage;
signals: void bridgeMessage(const QJsonObject& message);
protected: void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel,const QString&,int,const QString&) override;
};

class TikTokLiveService final : public QObject {
    Q_OBJECT
public:
    explicit TikTokLiveService(QObject* parent=nullptr);
    void connectUser(const QString& username); void disconnectService();
    QWebEnginePage* page(){return &page_;}
signals:
    void messageReceived(const ChatMessage& message);
    void statusChanged(const QString& state,const QString& detail);
    void viewerCountChanged(int viewers);
private:
    void installBridge(); TikTokPage page_; QString username_; QSet<QString> seen_;
};

class TikTokModerationService final : public QObject {
    Q_OBJECT
public:
    explicit TikTokModerationService(QObject* parent=nullptr);
    void setToken(QString token){token_=std::move(token);}
    void mute(const QString& room,const QString& user,int seconds,const QString& comment={});
    void unmute(const QString& room,const QString& user);
    void ban(const QString& room,const QString& user,const QString& comment={});
    void unban(const QString& room,const QString& user);
signals:void actionFinished(const QString& action,bool success,const QString& detail);
private:void send(const QString&method,const QString&path,const QUrlQuery&query,const QString&action);
    QNetworkAccessManager network_;QString token_;
};
