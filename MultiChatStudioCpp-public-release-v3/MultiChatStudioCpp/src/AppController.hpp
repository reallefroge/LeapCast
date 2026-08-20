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
signals:
    void messageReady(const ChatMessage& message);
    void eventReady(const StreamEvent& event);
    void sourceStatus(const QString& platform,const QString& state,const QString& detail);
    void viewerCount(const QString& platform,int count);
    void moderationResult(const QString& platform,bool success,const QString& detail);
private:
    static QString twitchName(const QString& link);
    static QString tiktokName(const QString& link);
    void receive(const ChatMessage& message);
    void autoModerate(const ChatMessage& message,const QString& reason);
    SettingsStore settings_;
    AuditStore audit_{&settings_};
    AutoMod automod_{&settings_};
    TwitchChatService twitch_;
    YouTubeChatService youtube_{QStringLiteral("youtube"),false};
    YouTubeChatService shorts_{QStringLiteral("yt_shorts"),true};
    TikTokLiveService tiktok_;
    StreamlabsService streamlabs_;
    TwitchModerationService twitchMod_;
    YouTubeModerationService youtubeMod_;
    TikTokModerationService tiktokMod_;
};

