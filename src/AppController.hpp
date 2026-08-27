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
    void resolveTwitchAppeal(const QString& requestId, bool approved, const QString& resolutionText);
    void createTwitchClip();
    void setPinnedMessagesEnabled(bool enabled);
    void testDiscordLiveNotification();
    void startDiscordBot();
    void stopDiscordBot();
    void regenerateNameColours();
    // Manual moderation triggered from a chat message's context menu, for the
    // cases AutoMod doesn't catch. seconds=0 means a permanent ban.
    void moderateMessage(const ChatMessage& message, int seconds, const QString& reason);
    void deleteChatMessage(const ChatMessage& message);
    QString blockedWordsPath() const { return automod_.blockedWordsPath(); }
    QString whitelistedWordsPath() const { return automod_.whitelistedWordsPath(); }
    bool reloadAutoMod() { return automod_.reload(); }
signals:
    void messageReady(const ChatMessage& message);
    // AutoMod held a message back instead of showing it — for a note in the
    // chat views/pop-out only, never the OBS overlay feed.
    void messageModerated(const ChatMessage& message, const QString& reason);
    void eventReady(const StreamEvent& event);
    void tiktokActivityReady(const StreamEvent& event);
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
    void pinnedMessageChanged(const QString& platform, const ChatMessage& message, bool active);
    void discordNotificationResult(bool success, const QString& detail);
    void discordBotStatus(bool online, const QString& detail);
private:
    static QString twitchName(const QString& link);
    static QString tiktokName(const QString& link);
    static QString kickName(const QString& link);
    void receive(const ChatMessage& message);
    void autoModerate(const ChatMessage& message,const QString& reason);
    void queueDiscordLivePlatform(const QString& platform);
    void sendDiscordLiveNotification(const QSet<QString>& platforms, bool test = false);
    void sendDiscordHeartbeat();
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
    KickLiveService kick_;
    RumbleLiveService rumble_;
    StreamlabsService streamlabs_;
    TwitchAuthService twitchAuth_;
    TwitchModerationService twitchMod_;
    TwitchEventSubService twitchEvents_;
    YouTubeModerationService youtubeMod_;
    QJsonArray youtubeRestrictions_;
    QHash<QString,QString> pendingYouTubeReasons_;
    QHash<QString,int> pendingYouTubeSeconds_;
    // AutoMod's Twitch timeout escalation, keyed by Twitch user id: offenses
    // 1-5 use the configured base timeout, offense 6 doubles it, and offense
    // 7+ is a permanent ban. Reset when the channel
    // transitions from offline to live (see TwitchChatService::broadcastWentLive) —
    // AutoMod itself keeps moderating while offline, only the ladder resets.
    QHash<QString,int> twitchAutoModOffenses_;
    QHash<QString,ChatMessage> pendingTwitchRedemptions_;
    QSet<QString> recentTwitchRedemptions_;
    // Per-broadcast name colours. Assigned on a chatter's first message and
    // reused for every later message from that same platform account.
    QHash<QString,QColor> broadcastUserColours_;
    QHash<QString,int> broadcastPaletteVariations_;
    QHash<QString,ChatMessage> currentPinnedMessages_;
    QTimer twitchPinnedTimer_;
    bool pinnedMessagesEnabled_{true};
    int nextBroadcastColour_{};
    int broadcastColourOffset_{-1};
    int nextPaletteVariation_{};
    int broadcastPaletteOffset_{-1};
    bool twitchAuthorizationRequested_{};
    QNetworkAccessManager discordNetwork_;
    QWebSocket discordGateway_;
    QTimer discordHeartbeat_;
    QTimer discordLiveDelay_;
    QTimer discordLiveReset_;
    QSet<QString> discordPendingPlatforms_;
    bool discordBroadcastNotificationSent_{};
    bool discordBotRequested_{};
    bool discordHeartbeatAcknowledged_{true};
    bool discordSequenceKnown_{};
    qint64 discordSequence_{};
};
