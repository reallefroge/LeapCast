#pragma once
#include "Core.hpp"
#include "LiveServices.hpp"
#include "PlatformServices.hpp"

class AppController final : public QObject {
    Q_OBJECT
public:
    explicit AppController(QObject* parent=nullptr);
    SettingsStore* settings(){return &settings_;}
    void startConfiguredSources();
    void connectSource(const QString& platform,const QString& link);
    void disconnectSource(const QString& platform);
    void authorizeTwitch();
    void configureYouTubeModeration(const QString& token);
    void connectStreamlabs(const QString& token);
    void disconnectStreamlabs();
    void refreshBans(const QString& platform);
    void unbanTwitch(const QString& userId);
    void unbanYouTube(const QString& banId);
    void createTwitchClip();
    // Manual moderation triggered from a chat message's context menu, for the
    // cases AutoMod doesn't catch. seconds=0 means a permanent ban.
    void moderateMessage(const ChatMessage& message, int seconds, const QString& reason);
    void deleteChatMessage(const ChatMessage& message);
    QString blockedWordsPath() const { return automod_.blockedWordsPath(); }
    bool reloadAutoMod() { return automod_.reload(); }
signals:
    void messageReady(const ChatMessage& message);
    // AutoMod held a message back instead of showing it — for a note in the
    // chat views/pop-out only, never the OBS overlay feed.
    void messageModerated(const ChatMessage& message, const QString& reason);
    void eventReady(const StreamEvent& event);
    void sourceStatus(const QString& platform,const QString& state,const QString& detail);
    void viewerCount(const QString& platform,int count);
    void moderationResult(const QString& platform,bool success,const QString& detail);
    void bansUpdated(const QString& platform, const QJsonArray& bans);
    void twitchAppealsUpdated(const QJsonArray& appeals);
    void userChatHistoryReady(const QString& userId, const QJsonArray& messages);
    void twitchClipCreated(const QUrl& editUrl);
    void twitchClipFailed(const QString& detail);
    void twitchAuthorizationUrl(const QUrl& url);
    void twitchAuthorized(const QString& login);
    void twitchAuthorizationFailed(const QString& detail);
private:
    static QString twitchName(const QString& link);
    static QString tiktokName(const QString& link);
    void receive(const ChatMessage& message);
    void autoModerate(const ChatMessage& message,const QString& reason);
public:
    void refreshTwitchAppeals();
    void loadTwitchUserHistory(const QString& userId,const QString& userName);
    SettingsStore settings_;
    AuditStore audit_{&settings_};
    AutoMod automod_{&settings_};
    TwitchChatService twitch_;
    YouTubeChatService youtube_{QStringLiteral("youtube"),false};
    YouTubeChatService shorts_{QStringLiteral("yt_shorts"),true};
    TikTokLiveService tiktok_;
    StreamlabsService streamlabs_;
    TwitchAuthService twitchAuth_;
    TwitchModerationService twitchMod_;
    YouTubeModerationService youtubeMod_;
    QJsonArray youtubeRestrictions_;
    QHash<QString,QString> pendingYouTubeReasons_;
    bool twitchAuthorizationRequested_{};
};
