#include "AppController.hpp"
#include "BuildInfo.hpp"
#include <algorithm>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QTimer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace {
QString configuredTwitchClientId(SettingsStore& settings){
    const QString bundled=QString::fromLatin1(leapcast::TwitchClientId).trimmed();
    return bundled.isEmpty()?settings.secret(QStringLiteral("twitch_client_id")):bundled;
}
QString twitchRedemptionKey(const QString&rewardId,const QString&user,const QString&input){return rewardId+QLatin1Char('|')+user.toLower()+QLatin1Char('|')+input;}

// A pasted channel LINK (https://discord.com/channels/<guild>/<channel>) is by
// far the most common thing people put in the Channel ID box. Keep only the
// last run of digits so the REST path is always .../channels/<snowflake>.
QString normalizeDiscordId(const QString& raw){
    QString value=raw.trimmed();
    if(value.contains(QLatin1Char('/')))value=value.section(QLatin1Char('/'),-1);
    value.remove(QRegularExpression(QStringLiteral("[^0-9]")));
    return value;
}

// Every HTTP 403 Discord can return for POST /channels/{id}/messages, mapped to
// the thing the streamer actually has to change. Administrator on the bot's role
// does NOT rescue the 50001 / 200000 / timeout cases, which is why "it has admin
// but still 403" is so common.
QString discordCauseFor(int httpCode,int discordCode){
    switch(discordCode){
    case 50001: return QStringLiteral("Missing Access — the bot is not in the server that owns this channel, or the Channel ID belongs to a different server. Administrator in another server does not help here. Re-invite the bot to THIS server and copy the Channel ID again with Developer Mode on.");
    case 50013: return QStringLiteral("Missing Permissions — the bot's own role does not have Send Messages in this channel. Check that the Administrator role is actually assigned to the bot member in the member list, not just ticked in the invite link.");
    case 50007: return QStringLiteral("Cannot send messages to this user — the Channel ID is a DM channel that is closed to the bot.");
    case 50024: return QStringLiteral("Wrong channel type — this ID is a category, forum or stage channel. Use a normal text channel.");
    case 10003: return QStringLiteral("Unknown Channel — that Channel ID does not exist. Turn on Developer Mode in Discord, right-click the text channel, and choose Copy Channel ID.");
    case 10004: return QStringLiteral("Unknown Guild — the bot is not a member of that server.");
    case 40001: return QStringLiteral("Unauthorized — the bot token was rejected. Paste the BOT token from the Bot tab, not the application Client Secret or a user token.");
    case 40333: return QStringLiteral("Blocked before reaching Discord — Cloudflare rejected the request. A VPN, proxy, corporate DNS filter or security suite on this PC is the usual cause.");
    case 20012: return QStringLiteral("Not authorized for this application — the token belongs to a different application than the bot sitting in your server.");
    case 200000: return QStringLiteral("AutoMod blocked the message — a server AutoMod rule (often a mention-spam or @everyone rule) is blocking the bot. Bots are NOT exempt from AutoMod even with Administrator. Add the bot's role to that rule's exempt roles, or drop the @everyone tick.");
    case 200001: return QStringLiteral("AutoMod blocked the message title.");
    case 160002: return QStringLiteral("The target thread is archived or locked.");
    default: break;
    }
    if(httpCode==401)return QStringLiteral("The bot token itself was rejected. Re-paste the token from the Bot tab of your application.");
    if(httpCode==403)return QStringLiteral("Discord refused the request without a specific error code. That is almost always a network-level block (VPN, proxy, DNS filter or security suite) rather than a Discord permission problem, or the bot member is timed out in the server.");
    if(httpCode==429)return QStringLiteral("Rate limited — slow mode on the channel, or too many sends in a row.");
    if(httpCode==404)return QStringLiteral("Channel not found. Re-copy the Channel ID.");
    return QString();
}

}

AppController::AppController(QObject*p):QObject(p){
    discordLiveDelay_.setSingleShot(true);discordLiveDelay_.setInterval(5000);
    connect(&discordLiveDelay_,&QTimer::timeout,this,[this]{const auto platforms=discordPendingPlatforms_;discordPendingPlatforms_.clear();sendDiscordLiveNotification(platforms);});
    discordLiveReset_.setSingleShot(true);discordLiveReset_.setInterval(90000);connect(&discordLiveReset_,&QTimer::timeout,this,[this]{discordBroadcastNotificationSent_=false;discordPendingPlatforms_.clear();});
    connect(&discordHeartbeat_,&QTimer::timeout,this,&AppController::sendDiscordHeartbeat);
    connect(&discordGateway_,&QWebSocket::textMessageReceived,this,[this](const QString& text){
        const auto payload=QJsonDocument::fromJson(text.toUtf8()).object();const int op=payload.value(QStringLiteral("op")).toInt(-1);if(!payload.value(QStringLiteral("s")).isNull()&&!payload.value(QStringLiteral("s")).isUndefined()){discordSequence_=payload.value(QStringLiteral("s")).toVariant().toLongLong();discordSequenceKnown_=true;}
        if(op==10){
            const int interval=payload.value(QStringLiteral("d")).toObject().value(QStringLiteral("heartbeat_interval")).toInt();
            if(interval<=0){emit discordBotStatus(false,QStringLiteral("Discord returned an invalid heartbeat interval."));discordGateway_.close();return;}
            discordHeartbeat_.setInterval(interval);discordHeartbeatAcknowledged_=true;
            QTimer::singleShot(QRandomGenerator::global()->bounded(qMax(1,interval)),this,[this]{if(discordGateway_.state()==QAbstractSocket::ConnectedState){sendDiscordHeartbeat();discordHeartbeat_.start();}});
            const QJsonObject properties{{QStringLiteral("os"),QStringLiteral("windows")},{QStringLiteral("browser"),QStringLiteral("leapcast-studio")},{QStringLiteral("device"),QStringLiteral("leapcast-studio")}};
            const QJsonObject presence{{QStringLiteral("since"),QJsonValue(QJsonValue::Null)},{QStringLiteral("activities"),QJsonArray{}},{QStringLiteral("status"),QStringLiteral("online")},{QStringLiteral("afk"),false}};
            const QJsonObject identify{{QStringLiteral("token"),settings_.secret(QStringLiteral("discord_bot_token"))},{QStringLiteral("intents"),1},{QStringLiteral("properties"),properties},{QStringLiteral("presence"),presence}};
            const QJsonObject packet{{QStringLiteral("op"),2},{QStringLiteral("d"),identify}};
            discordGateway_.sendTextMessage(QString::fromUtf8(QJsonDocument(packet).toJson(QJsonDocument::Compact)));
        }
        else if(op==11)discordHeartbeatAcknowledged_=true;
        else if(op==0&&payload.value(QStringLiteral("t")).toString()==QStringLiteral("READY"))emit discordBotStatus(true,QStringLiteral("Bot is online while Leapcast Studio is running."));
        else if(op==7){emit discordBotStatus(false,QStringLiteral("Discord requested a reconnect. Select Run Bot again."));discordGateway_.close();}
        else if(op==9){emit discordBotStatus(false,QStringLiteral("Discord rejected the Gateway session. Check the bot token, then try Run Bot again."));discordGateway_.close();}
    });
    connect(&discordGateway_,&QWebSocket::disconnected,this,[this]{discordHeartbeat_.stop();discordSequenceKnown_=false;if(discordBotRequested_)emit discordBotStatus(false,QStringLiteral("Bot is offline. Select Run Bot to reconnect."));else emit discordBotStatus(false,QStringLiteral("Bot stopped."));});
    connect(&discordGateway_,&QWebSocket::errorOccurred,this,[this](QAbstractSocket::SocketError){emit discordBotStatus(false,QStringLiteral("Discord Gateway connection failed: %1").arg(discordGateway_.errorString()));});
    for(auto*service:{static_cast<QObject*>(&twitch_),static_cast<QObject*>(&youtube_),static_cast<QObject*>(&shorts_),static_cast<QObject*>(&tiktok_),static_cast<QObject*>(&kick_),static_cast<QObject*>(&rumble_)})Q_UNUSED(service);
    connect(&twitch_,&TwitchChatService::messageReceived,this,[this](const ChatMessage&message){
        const QString rewardId=message.metadata.value(QStringLiteral("custom_reward_id")).toString();
        if(rewardId.isEmpty()){receive(message);return;}
        const QString key=twitchRedemptionKey(rewardId,message.user,message.text);
        if(recentTwitchRedemptions_.contains(key))return;
        pendingTwitchRedemptions_.insert(key,message);
        QTimer::singleShot(3000,this,[this,key]{
            if(!pendingTwitchRedemptions_.contains(key))return;
            ChatMessage fallback=pendingTwitchRedemptions_.take(key);fallback.badges<<QStringLiteral("MONEY");
            fallback.metadata[QStringLiteral("channel_point_redemption")]=true;fallback.metadata[QStringLiteral("event_kind")]=QStringLiteral("twitch_redemption");
            fallback.text=QStringLiteral("redeemed a Channel Point reward")+(fallback.text.isEmpty()?QString():QStringLiteral(": ")+fallback.text);
            receive(fallback);
        });
    });
    connect(&youtube_,&YouTubeChatService::messageReceived,this,&AppController::receive);
    connect(&shorts_,&YouTubeChatService::messageReceived,this,&AppController::receive);
    const auto acceptYouTubePin=[this](const QString&platform,const ChatMessage&message,bool active){if(active)currentPinnedMessages_[platform]=message;else currentPinnedMessages_.remove(platform);if(pinnedMessagesEnabled_)emit pinnedMessageChanged(platform,message,active);};
    connect(&youtube_,&YouTubeChatService::pinnedMessageChanged,this,[acceptYouTubePin](const ChatMessage&m,bool active){acceptYouTubePin(QStringLiteral("youtube"),m,active);});
    connect(&shorts_,&YouTubeChatService::pinnedMessageChanged,this,[acceptYouTubePin](const ChatMessage&m,bool active){acceptYouTubePin(QStringLiteral("yt_shorts"),m,active);});
    connect(&tiktok_,&TikTokLiveService::messageReceived,this,&AppController::receive);
    connect(&kick_,&KickLiveService::messageReceived,this,&AppController::receive);
    connect(&rumble_,&RumbleLiveService::messageReceived,this,&AppController::receive);
    connect(&twitch_,&TwitchChatService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("twitch",s,d);});
    connect(&youtube_,&YouTubeChatService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("youtube",s,d);});
    connect(&shorts_,&YouTubeChatService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("yt_shorts",s,d);});
    connect(&tiktok_,&TikTokLiveService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("tiktok",s,d);});
    connect(&kick_,&KickLiveService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("kick",s,d);});
    connect(&rumble_,&RumbleLiveService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("rumble",s,d);});
    connect(&twitch_,&TwitchChatService::broadcastWentLive,this,[this]{twitchAutoModOffenses_.clear();regenerateNameColours();discordBroadcastNotificationSent_=false;queueDiscordLivePlatform(QStringLiteral("twitch"));});
    const auto viewers=[this](const QString& platform,int n){emit viewerCount(platform,n);queueDiscordLivePlatform(platform);};
    connect(&twitch_,&TwitchChatService::viewerCountChanged,this,[viewers](int n){viewers(QStringLiteral("twitch"),n);});
    connect(&youtube_,&YouTubeChatService::viewerCountChanged,this,[viewers](int n){viewers(QStringLiteral("youtube"),n);});
    connect(&shorts_,&YouTubeChatService::viewerCountChanged,this,[viewers](int n){viewers(QStringLiteral("yt_shorts"),n);});
    connect(&tiktok_,&TikTokLiveService::viewerCountChanged,this,[viewers](int n){viewers(QStringLiteral("tiktok"),n);});
    connect(&kick_,&KickLiveService::viewerCountChanged,this,[viewers](int n){viewers(QStringLiteral("kick"),n);});
    connect(&rumble_,&RumbleLiveService::viewerCountChanged,this,[viewers](int n){viewers(QStringLiteral("rumble"),n);});
    connect(&tiktok_,&TikTokLiveService::activityReceived,this,&AppController::tiktokActivityReady);
    connect(&tiktok_,&TikTokLiveService::diagnostic,this,&AppController::tiktokDiagnostic);
    connect(&tiktok_,&TikTokLiveService::collectorVisibilityChanged,this,&AppController::tiktokCollectorVisibilityChanged);
    connect(&rumble_,&RumbleLiveService::eventReceived,this,[this](const StreamEvent&e){audit_.appendEvent(e);emit eventReady(e);});
    connect(&streamlabs_,&StreamlabsService::eventReceived,this,[this](const StreamEvent&e){audit_.appendEvent(e);emit eventReady(e);});
    connect(&twitchEvents_,&TwitchEventSubService::eventReceived,this,[this](const StreamEvent&e){const QString key=twitchRedemptionKey(e.raw.value(QStringLiteral("reward")).toObject().value(QStringLiteral("id")).toString(),e.user,e.message);pendingTwitchRedemptions_.remove(key);recentTwitchRedemptions_.insert(key);QTimer::singleShot(10000,this,[this,key]{recentTwitchRedemptions_.remove(key);});audit_.appendEvent(e);ChatMessage message;message.platform=QStringLiteral("twitch");message.user=e.user;message.text=QStringLiteral("redeemed %1").arg(e.amount.isEmpty()?QStringLiteral("a Channel Point reward"):e.amount);if(!e.message.isEmpty())message.text+=QStringLiteral(": ")+e.message;message.messageId=e.eventId;message.badges<<QStringLiteral("MONEY");message.metadata={{QStringLiteral("event_kind"),e.kind},{QStringLiteral("channel_point_redemption"),true}};receive(message);emit eventReady(e);});
    connect(&twitchEvents_,&TwitchEventSubService::statusChanged,this,[this](const QString&state,const QString&detail){emit sourceStatus(QStringLiteral("twitch"),state,detail);});
    connect(&streamlabs_,&StreamlabsService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("streamlabs",s,d);});
    const QString twitchClientId=configuredTwitchClientId(settings_);
    twitchMod_.configure(twitchClientId,settings_.secret("twitch_access_token"),settings_.secret("twitch_moderator_id"));
    youtubeMod_.setAccessToken(settings_.secret("youtube_access_token"));
    for(const auto& item:settings_.preference("youtube_restrictions").toList())youtubeRestrictions_.append(QJsonObject::fromVariantMap(item.toMap()));
    connect(&twitchMod_,&TwitchModerationService::bansReceived,this,[this](const QJsonArray&a){emit bansUpdated("twitch",a);});
    connect(&twitchMod_,&TwitchModerationService::unbanRequestsReceived,this,&AppController::twitchAppealsUpdated);
    connect(&twitchMod_,&TwitchModerationService::broadcasterResolved,this,[this,twitchClientId](const QString&,const QString&id){settings_.setSecret("twitch_broadcaster_id",id);twitchEvents_.connectRedemptions(twitchClientId,settings_.secret(QStringLiteral("twitch_access_token")),id);if(pinnedMessagesEnabled_)twitchMod_.getPinnedMessage(id);});
    connect(&youtubeMod_,&YouTubeModerationService::banCreated,this,[this](const QString&id,const QString&user,bool permanent){const int seconds=pendingYouTubeSeconds_.take(user);const QString duration=seconds>=3600?QStringLiteral("%1 hr").arg(seconds/3600.0,0,'g',3):QStringLiteral("%1 min").arg(qMax(1,seconds/60));QJsonObject o{{"id",id},{"user_name",user},{"type",permanent?QStringLiteral("Hide from channel"):QStringLiteral("Timeout (%1)").arg(duration)},{"duration_seconds",seconds},{"reason",pendingYouTubeReasons_.take(user)},{"created_at",QDateTime::currentDateTime().toString(Qt::ISODate)}};youtubeRestrictions_.prepend(o);QVariantList saved;for(const auto&v:youtubeRestrictions_)saved<<v.toObject().toVariantMap();settings_.setPreference("youtube_restrictions",saved);emit bansUpdated("youtube",youtubeRestrictions_);});
    connect(&twitchMod_,&TwitchModerationService::actionFinished,this,[this](const QString&action,bool ok,const QString&d){
        if(action=="clip"){if(!ok)emit twitchClipFailed(d);return;}
        emit moderationResult("twitch",ok,d);
        if(ok&&action=="ban")QTimer::singleShot(500,this,[this]{refreshBans(QStringLiteral("twitch"));});
        if(ok&&(action=="approve_appeal"||action=="deny_appeal"))QTimer::singleShot(250,this,&AppController::refreshTwitchAppeals);
    });
    connect(&twitchMod_,&TwitchModerationService::clipCreated,this,[this](const QString&,const QUrl&editUrl){emit twitchClipCreated(editUrl);});
    connect(&twitchMod_,&TwitchModerationService::pinnedMessageChanged,this,[this](const ChatMessage&m,bool active){if(active)currentPinnedMessages_[QStringLiteral("twitch")]=m;else currentPinnedMessages_.remove(QStringLiteral("twitch"));if(pinnedMessagesEnabled_)emit pinnedMessageChanged(QStringLiteral("twitch"),m,active);});
    twitchPinnedTimer_.setInterval(5000);connect(&twitchPinnedTimer_,&QTimer::timeout,this,[this]{if(!pinnedMessagesEnabled_||twitch_.channel().isEmpty())return;const QString broadcaster=settings_.secret(QStringLiteral("twitch_broadcaster_id"));if(!broadcaster.isEmpty())twitchMod_.getPinnedMessage(broadcaster);});
    connect(&youtubeMod_,&YouTubeModerationService::actionFinished,this,[this](const QString&,bool ok,const QString&d){emit moderationResult("youtube",ok,d);});
    connect(&twitchAuth_,&TwitchAuthService::browserAuthorizationReady,this,&AppController::twitchAuthorizationUrl);
    connect(&twitchAuth_,&TwitchAuthService::scopesValidated,this,[this](const QStringList&scopes){settings_.setPreference(QStringLiteral("twitch_authorized_scopes"),scopes);if(!scopes.contains(QStringLiteral("channel:read:redemptions")))emit sourceStatus(QStringLiteral("twitch"),QStringLiteral("warn"),QStringLiteral("Reconnect Twitch to enable Channel Point redemptions."));});
    connect(&twitchAuth_,&TwitchAuthService::authorizationPending,this,[this]{emit sourceStatus("twitch","connecting","Waiting for Twitch approval…");});
    connect(&twitchAuth_,&TwitchAuthService::authorizationFailed,this,[this](const QString&detail){
        emit sourceStatus("twitch","error",detail);
        if(twitchAuthorizationRequested_)emit twitchAuthorizationFailed(detail);
        twitchAuthorizationRequested_=false;
    });
    connect(&twitchAuth_,&TwitchAuthService::authorized,this,[this,twitchClientId](const QString&token,const QString&refresh,const QString&userId,const QString&login,int){
        settings_.setSecret("twitch_access_token",token);
        settings_.setSecret("twitch_refresh_token",refresh);
        settings_.setSecret("twitch_moderator_id",userId);
        twitchMod_.configure(twitchClientId,token,userId);
        const QString channel=twitchName(settings_.link("twitch"));
        if(!channel.isEmpty())twitchMod_.resolveBroadcaster(channel);
        const QString broadcaster=settings_.secret(QStringLiteral("twitch_broadcaster_id"));if(!broadcaster.isEmpty())twitchEvents_.connectRedemptions(twitchClientId,token,broadcaster);
        emit sourceStatus("twitch","ok",QStringLiteral("Authorized as %1").arg(login));
        if(twitchAuthorizationRequested_)emit twitchAuthorized(login);
        twitchAuthorizationRequested_=false;
    });
    if(!twitchClientId.isEmpty()&&!settings_.secret("twitch_access_token").isEmpty())
        twitchAuth_.restore(twitchClientId,settings_.secret("twitch_access_token"),settings_.secret("twitch_refresh_token"));
    if(!twitchClientId.isEmpty()&&!settings_.secret(QStringLiteral("twitch_access_token")).isEmpty()&&!settings_.secret(QStringLiteral("twitch_broadcaster_id")).isEmpty())twitchEvents_.connectRedemptions(twitchClientId,settings_.secret(QStringLiteral("twitch_access_token")),settings_.secret(QStringLiteral("twitch_broadcaster_id")));
    pinnedMessagesEnabled_=settings_.preference(QStringLiteral("show_pinned_messages"),true).toBool();if(pinnedMessagesEnabled_)twitchPinnedTimer_.start();
}
QString AppController::twitchName(const QString&l){auto m=QRegularExpression("twitch\\.tv/([A-Za-z0-9_]+)").match(l);if(m.hasMatch())return m.captured(1).toLower();return QRegularExpression("^[A-Za-z0-9_]{3,25}$").match(l).hasMatch()?l.toLower():QString();}
QString AppController::tiktokName(const QString&l){auto m=QRegularExpression("tiktok\\.com/@([A-Za-z0-9._]+)").match(l);if(m.hasMatch())return m.captured(1);return l.startsWith('@')?l.mid(1):QString();}
QString AppController::kickName(const QString&l){auto m=QRegularExpression("kick\\.com/([A-Za-z0-9_-]+)").match(l);if(m.hasMatch())return m.captured(1).toLower();return QRegularExpression("^[A-Za-z0-9_-]+$").match(l).hasMatch()?l.toLower():QString();}
void AppController::startConfiguredSources(){for(const auto&p:{"twitch","youtube","yt_shorts","tiktok","kick","rumble"})if(settings_.enabled(p)&&!settings_.link(p).isEmpty())connectSource(p,settings_.link(p));const auto token=settings_.secret("streamlabs_socket_token");if(!token.isEmpty())streamlabs_.connectToken(token);}
void AppController::connectSource(const QString&p,const QString&l){settings_.setLink(p,l);if(p=="twitch"){const QString name=twitchName(l);twitch_.connectChannel(name);if(!settings_.secret("twitch_access_token").isEmpty()){twitchMod_.configure(configuredTwitchClientId(settings_),settings_.secret("twitch_access_token"),settings_.secret("twitch_moderator_id"));twitchMod_.resolveBroadcaster(name);}}else if(p=="youtube")youtube_.connectTarget(l);else if(p=="yt_shorts")shorts_.connectTarget(l);else if(p=="tiktok")tiktok_.connectUser(tiktokName(l));else if(p=="kick")kick_.connectChannel(kickName(l));else if(p=="rumble")rumble_.connectApi(QUrl(settings_.secret("rumble_api_url")));}
void AppController::disconnectSource(const QString&p){
    if(p=="twitch"){twitch_.disconnectChannel();twitchEvents_.disconnectService();currentPinnedMessages_.remove(QStringLiteral("twitch"));if(pinnedMessagesEnabled_)emit pinnedMessageChanged(QStringLiteral("twitch"),ChatMessage{},false);}
    else if(p=="youtube")youtube_.disconnectService();
    else if(p=="yt_shorts")shorts_.disconnectService();
    else if(p=="tiktok")tiktok_.disconnectService();
    else if(p=="kick")kick_.disconnectService();
    else if(p=="rumble")rumble_.disconnectService();
    else return;
    // The underlying services don't reliably emit a status change when torn
    // down manually (e.g. Twitch only reports "reconnecting" on an
    // *unexpected* drop, not a deliberate one), so Disconnect looked like it
    // did nothing. Tell the UI directly instead.
    discordPendingPlatforms_.remove(p);if(discordPendingPlatforms_.isEmpty()&&!discordLiveDelay_.isActive())discordBroadcastNotificationSent_=false;
    emit sourceStatus(p,"warn","Disconnected");
}

void AppController::testDiscordLiveNotification(){
    QSet<QString> configured;for(const auto& platform:{QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("tiktok"),QStringLiteral("kick"),QStringLiteral("rumble")})if(!settings_.link(platform).isEmpty())configured.insert(platform);
    if(configured.isEmpty())configured.insert(QStringLiteral("twitch"));sendDiscordLiveNotification(configured,true);
}

void AppController::startDiscordBot(){
    const QString token=settings_.secret(QStringLiteral("discord_bot_token"));if(token.isEmpty()){emit discordBotStatus(false,QStringLiteral("Save a bot token before selecting Run Bot."));return;}
    if(discordGateway_.state()==QAbstractSocket::ConnectedState||discordGateway_.state()==QAbstractSocket::ConnectingState)return;discordBotRequested_=true;discordHeartbeatAcknowledged_=true;discordSequenceKnown_=false;emit discordBotStatus(false,QStringLiteral("Connecting bot to Discord…"));discordGateway_.open(QUrl(QStringLiteral("wss://gateway.discord.gg/?v=10&encoding=json")));
}

void AppController::setTikTokCollectorVisible(bool visible){tiktok_.setCollectorVisible(visible);}
void AppController::reloadTikTokCollector(){tiktok_.reloadCollector();}

void AppController::stopDiscordBot(){discordBotRequested_=false;discordHeartbeat_.stop();discordGateway_.close(QWebSocketProtocol::CloseCodeNormal,QStringLiteral("Stopped by user"));}

QNetworkRequest AppController::discordRequest(const QString& path) const {
    QNetworkRequest request(QUrl(QStringLiteral("https://discord.com/api/v10")+path));
    request.setRawHeader("Authorization",QByteArray("Bot ")+settings_.secret(QStringLiteral("discord_bot_token")).toUtf8());
    // Discord's edge drops requests without a bot-shaped User-Agent with a bare
    // HTTP 403 and an HTML body, which reads exactly like a permission problem.
    request.setRawHeader("User-Agent",QStringLiteral("DiscordBot (https://github.com/reallefroge/LeapCast, %1)").arg(QString::fromLatin1(leapcast::Version)).toUtf8());
    return request;
}

// Discord permission bits used by the diagnosis below.
namespace {
constexpr quint64 kPermAdministrator = 0x8ULL;
constexpr quint64 kPermViewChannel   = 0x400ULL;
constexpr quint64 kPermSendMessages  = 0x800ULL;
constexpr quint64 kPermMentionEveryone = 0x20000ULL;
quint64 permissionBits(const QJsonValue& value){return value.toString().toULongLong();}
}

// Step 1: is the token usable, and which bot does it belong to? A bot token
// cannot use the "@me" member alias, so the snowflake returned here is what the
// guild calls below are keyed on.
void AppController::diagnoseDiscord(){
    const QString token=settings_.secret(QStringLiteral("discord_bot_token"));
    const QString channel=normalizeDiscordId(settings_.secret(QStringLiteral("discord_channel_id")));
    if(token.isEmpty()){emit discordNotificationResult(false,QStringLiteral("No bot token saved. Paste the token from the Bot tab of your Discord application, then Save."));return;}
    if(channel.isEmpty()){emit discordNotificationResult(false,QStringLiteral("No Channel ID saved. Turn on Developer Mode in Discord, right-click the text channel, choose Copy Channel ID, then Save."));return;}
    discordDiagnosis_=DiscordDiagnosis{};
    emit discordDiagnostic(QStringLiteral("--- Discord diagnosis started ---\nChannel ID in use: %1").arg(channel));
    auto* reply=discordNetwork_.get(discordRequest(QStringLiteral("/users/@me")));
    connect(reply,&QNetworkReply::finished,this,[this,reply,channel]{
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body=reply->readAll();reply->deleteLater();
        const QJsonObject json=QJsonDocument::fromJson(body).object();
        emit discordDiagnostic(QStringLiteral("GET /users/@me -> HTTP %1\n%2").arg(code).arg(QString::fromUtf8(body.left(600))));
        if(code!=200){
            const QString cause=discordCauseFor(code,json.value(QStringLiteral("code")).toInt());
            emit discordNotificationResult(false,QStringLiteral("Step 1 failed: Discord would not accept the bot token (HTTP %1). %2").arg(code).arg(cause));
            return;
        }
        discordDiagnosis_.botId=json.value(QStringLiteral("id")).toString();
        emit discordDiagnostic(QStringLiteral("Step 1 OK: token belongs to bot %1 (id %2).")
                                   .arg(json.value(QStringLiteral("username")).toString(),discordDiagnosis_.botId));
        diagnoseDiscordChannel(channel);
    });
}

// Step 2: can this bot see the channel, and what overwrites does it carry?
void AppController::diagnoseDiscordChannel(const QString& channelId){
    auto* reply=discordNetwork_.get(discordRequest(QStringLiteral("/channels/%1").arg(channelId)));
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body=reply->readAll();reply->deleteLater();
        const QJsonObject json=QJsonDocument::fromJson(body).object();
        emit discordDiagnostic(QStringLiteral("GET /channels/{id} -> HTTP %1\n%2").arg(code).arg(QString::fromUtf8(body.left(800))));
        if(code!=200){
            const QString cause=discordCauseFor(code,json.value(QStringLiteral("code")).toInt());
            emit discordNotificationResult(false,QStringLiteral("Step 2 failed: the bot cannot see that channel (HTTP %1). %2").arg(code).arg(cause));
            return;
        }
        const int type=json.value(QStringLiteral("type")).toInt(-1);
        discordDiagnosis_.guildId=json.value(QStringLiteral("guild_id")).toString();
        discordDiagnosis_.channelName=json.value(QStringLiteral("name")).toString();
        discordDiagnosis_.overwrites=json.value(QStringLiteral("permission_overwrites")).toArray();
        if(type==4||type==15||type==16){
            emit discordNotificationResult(false,QStringLiteral("Step 2 failed: that ID is a %1, which cannot receive a plain message. Copy the ID of a normal text channel instead.")
                                                     .arg(type==4?QStringLiteral("category"):QStringLiteral("forum channel")));
            return;
        }
        emit discordDiagnostic(QStringLiteral("Step 2 OK: channel #%1 (type %2) in server %3.")
                                   .arg(discordDiagnosis_.channelName).arg(type).arg(discordDiagnosis_.guildId));
        if(discordDiagnosis_.guildId.isEmpty()){
            emit discordNotificationResult(true,QStringLiteral("Token and channel both check out. Press Send test notification for the final check."));
            return;
        }
        diagnoseDiscordMember();
    });
}

// Step 3: is the bot a member, is it timed out, and which roles does it hold?
void AppController::diagnoseDiscordMember(){
    auto* reply=discordNetwork_.get(discordRequest(QStringLiteral("/guilds/%1/members/%2")
                                                       .arg(discordDiagnosis_.guildId,discordDiagnosis_.botId)));
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body=reply->readAll();reply->deleteLater();
        const QJsonObject json=QJsonDocument::fromJson(body).object();
        emit discordDiagnostic(QStringLiteral("GET /guilds/%1/members/%2 -> HTTP %3\n%4")
                                   .arg(discordDiagnosis_.guildId,discordDiagnosis_.botId).arg(code).arg(QString::fromUtf8(body.left(800))));
        if(code!=200){
            const QString cause=discordCauseFor(code,json.value(QStringLiteral("code")).toInt());
            emit discordNotificationResult(false,QStringLiteral("Step 3 failed: the bot is not a member of that server (HTTP %1). %2").arg(code).arg(cause));
            return;
        }
        const QString until=json.value(QStringLiteral("communication_disabled_until")).toString();
        if(!until.isEmpty()&&QDateTime::fromString(until,Qt::ISODateWithMs)>QDateTime::currentDateTimeUtc()){
            emit discordNotificationResult(false,QStringLiteral("Step 3 failed: the bot member is timed out in that server until %1. A timed-out member gets HTTP 403 even with Administrator. Remove the timeout in the member list.").arg(until));
            return;
        }
        discordDiagnosis_.roleIds.clear();
        const QJsonArray roles=json.value(QStringLiteral("roles")).toArray();
        for(const auto& role:roles)discordDiagnosis_.roleIds<<role.toString();
        emit discordDiagnostic(QStringLiteral("Step 3 OK: bot is a member holding %1 role(s).").arg(discordDiagnosis_.roleIds.size()));
        diagnoseDiscordRoles();
    });
}

// Step 4: read the guild roles so the effective channel permissions can be
// resolved the same way Discord resolves them.
void AppController::diagnoseDiscordRoles(){
    auto* reply=discordNetwork_.get(discordRequest(QStringLiteral("/guilds/%1/roles").arg(discordDiagnosis_.guildId)));
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body=reply->readAll();reply->deleteLater();
        emit discordDiagnostic(QStringLiteral("GET /guilds/{id}/roles -> HTTP %1 (%2 bytes)").arg(code).arg(body.size()));
        if(code!=200){
            emit discordNotificationResult(true,QStringLiteral("Token, channel and membership all check out. The role list could not be read (HTTP %1), so exact permissions were not calculated. Press Send test notification.").arg(code));
            return;
        }
        discordDiagnosis_.guildRoles=QJsonDocument::fromJson(body).array();
        reportDiscordPermissions();
    });
}

// Discord's own resolution order: base permissions from @everyone plus every
// role the member holds; Administrator short-circuits everything; otherwise the
// @everyone overwrite, then role overwrites, then the member overwrite.
void AppController::reportDiscordPermissions(){
    quint64 base=0;
    for(const auto& value:discordDiagnosis_.guildRoles){
        const QJsonObject role=value.toObject();
        const QString id=role.value(QStringLiteral("id")).toString();
        if(id==discordDiagnosis_.guildId||discordDiagnosis_.roleIds.contains(id))
            base|=permissionBits(role.value(QStringLiteral("permissions")));
    }
    const bool administrator=(base&kPermAdministrator)!=0;
    quint64 effective=base;
    if(!administrator){
        quint64 memberAllow=0,memberDeny=0,roleAllow=0,roleDeny=0;
        for(const auto& value:discordDiagnosis_.overwrites){
            const QJsonObject overwrite=value.toObject();
            const QString id=overwrite.value(QStringLiteral("id")).toString();
            const quint64 allow=permissionBits(overwrite.value(QStringLiteral("allow")));
            const quint64 deny=permissionBits(overwrite.value(QStringLiteral("deny")));
            if(id==discordDiagnosis_.guildId)effective=(effective&~deny)|allow;
            else if(discordDiagnosis_.roleIds.contains(id)){roleAllow|=allow;roleDeny|=deny;}
            else if(id==discordDiagnosis_.botId){memberAllow=allow;memberDeny=deny;}
        }
        effective=(effective&~roleDeny)|roleAllow;
        effective=(effective&~memberDeny)|memberAllow;
    }
    const bool canView=administrator||(effective&kPermViewChannel)!=0;
    const bool canSend=administrator||(effective&kPermSendMessages)!=0;
    const bool canMention=administrator||(effective&kPermMentionEveryone)!=0;
    emit discordDiagnostic(QStringLiteral("Effective in #%1 - Administrator:%2 View:%3 Send:%4 MentionEveryone:%5")
                               .arg(discordDiagnosis_.channelName,
                                    administrator?QStringLiteral("yes"):QStringLiteral("no"),
                                    canView?QStringLiteral("yes"):QStringLiteral("no"),
                                    canSend?QStringLiteral("yes"):QStringLiteral("no"),
                                    canMention?QStringLiteral("yes"):QStringLiteral("no")));
    if(!canSend){
        emit discordNotificationResult(false,QStringLiteral("Found it: the bot cannot Send Messages in #%1. Open that channel's Edit Channel -> Permissions, add the bot's role, and allow Send Messages.").arg(discordDiagnosis_.channelName));
        return;
    }
    const bool wantsMention=settings_.preference(QStringLiteral("discord_mention_everyone"),true).toBool();
    if(wantsMention&&!canMention){
        emit discordNotificationResult(false,QStringLiteral("The bot can post in #%1 but cannot mention @everyone there. Either allow Mention @everyone for its role in that channel, or untick the @everyone option above.").arg(discordDiagnosis_.channelName));
        return;
    }
    emit discordNotificationResult(true,QStringLiteral("All checks pass: the bot can post%1 in #%2. If Send test notification still returns 403, the cause is server-side filtering - an AutoMod rule (error 200000) or a network block between this PC and Discord. The raw response is in the log.")
                                            .arg(canMention?QStringLiteral(" and mention @everyone"):QString(),discordDiagnosis_.channelName));
}

void AppController::sendDiscordHeartbeat(){
    if(discordGateway_.state()!=QAbstractSocket::ConnectedState)return;if(!discordHeartbeatAcknowledged_){discordGateway_.close(QWebSocketProtocol::CloseCodeGoingAway,QStringLiteral("Heartbeat not acknowledged"));emit discordBotStatus(false,QStringLiteral("Discord stopped acknowledging the bot. Select Run Bot to reconnect."));return;}discordHeartbeatAcknowledged_=false;const QJsonValue sequence=discordSequenceKnown_?QJsonValue(discordSequence_):QJsonValue(QJsonValue::Null);discordGateway_.sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("op"),1},{QStringLiteral("d"),sequence}}).toJson(QJsonDocument::Compact)));
}

void AppController::queueDiscordLivePlatform(const QString& platform){
    static const QSet<QString> supported{QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("yt_shorts"),QStringLiteral("tiktok"),QStringLiteral("kick"),QStringLiteral("rumble")};
    if(!supported.contains(platform)||!settings_.preference(QStringLiteral("discord_live_notifications"),false).toBool())return;discordLiveReset_.start();if(discordBroadcastNotificationSent_)return;
    discordPendingPlatforms_.insert(platform==QStringLiteral("yt_shorts")?QStringLiteral("youtube"):platform);if(!discordLiveDelay_.isActive())discordLiveDelay_.start();
}

void AppController::sendDiscordLiveNotification(const QSet<QString>& platforms,bool test){
    if(!settings_.preference(QStringLiteral("discord_live_notifications"),false).toBool()&&!test)return;
    const QString token=settings_.secret(QStringLiteral("discord_bot_token"));
    const QString channel=normalizeDiscordId(settings_.secret(QStringLiteral("discord_channel_id")));
    if(token.isEmpty()||channel.isEmpty()){if(test)emit discordNotificationResult(false,QStringLiteral("Enter a bot token and channel ID first."));return;}
    if(platforms.isEmpty()||(!test&&discordBroadcastNotificationSent_))return;
    const QStringList order{QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("rumble"),QStringLiteral("kick"),QStringLiteral("tiktok")};
    const QHash<QString,QString> labels{{QStringLiteral("twitch"),QStringLiteral("Twitch")},{QStringLiteral("youtube"),QStringLiteral("YouTube")},{QStringLiteral("rumble"),QStringLiteral("Rumble")},{QStringLiteral("kick"),QStringLiteral("Kick")},{QStringLiteral("tiktok"),QStringLiteral("TikTok LIVE")}};
    QStringList platformNames,links;for(const auto& key:order)if(platforms.contains(key)){platformNames<<labels.value(key);const QString link=settings_.link(key);if(!link.isEmpty())links<<QStringLiteral("%1: %2").arg(labels.value(key),link);}
    QString joined;if(platformNames.size()==1)joined=platformNames.first();else if(platformNames.size()==2)joined=platformNames.join(QStringLiteral(" & "));else{joined=platformNames.mid(0,platformNames.size()-1).join(QStringLiteral(", "))+QStringLiteral(" & ")+platformNames.last();}
    QString user=settings_.preference(QStringLiteral("discord_streamer_name"),QStringLiteral("Streamer")).toString().trimmed();if(user.isEmpty())user=QStringLiteral("Streamer");
    const bool mention=settings_.preference(QStringLiteral("discord_mention_everyone"),true).toBool();
    QString content=settings_.preference(QStringLiteral("discord_live_message"),QStringLiteral("@everyone {user} is live on {Platforms}")).toString();if(content.trimmed().isEmpty())content=QStringLiteral("@everyone {user} is live on {Platforms}");
    content.replace(QStringLiteral("{user}"),user,Qt::CaseInsensitive);content.replace(QStringLiteral("{Platforms}"),joined,Qt::CaseInsensitive);if(!mention)content.replace(QRegularExpression(QStringLiteral("@everyone\\s*"),QRegularExpression::CaseInsensitiveOption),QString());
    if(test)content=QStringLiteral("TEST • ")+content;if(!links.isEmpty())content+=QStringLiteral("\n")+links.join(QLatin1Char('\n'));
    QNetworkRequest request(QUrl(QStringLiteral("https://discord.com/api/v10/channels/%1/messages").arg(channel)));
    request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));
    request.setRawHeader("Authorization",QByteArray("Bot ")+token.toUtf8());
    request.setRawHeader("User-Agent",QStringLiteral("DiscordBot (https://github.com/reallefroge/LeapCast, %1)").arg(QString::fromLatin1(leapcast::Version)).toUtf8());
    QJsonObject allowed{{QStringLiteral("parse"),mention?QJsonArray{QStringLiteral("everyone")}:QJsonArray{}}};
    if(!test)discordBroadcastNotificationSent_=true;
    auto* reply=discordNetwork_.post(request,QJsonDocument(QJsonObject{{QStringLiteral("content"),content},{QStringLiteral("allowed_mentions"),allowed}}).toJson(QJsonDocument::Compact));
    connect(reply,&QNetworkReply::finished,this,[this,reply,test]{
        const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const bool ok=reply->error()==QNetworkReply::NoError&&code>=200&&code<300;
        if(!ok&&!test)discordBroadcastNotificationSent_=false;
        QString detail;
        if(ok)detail=test?QStringLiteral("Test notification sent."):QStringLiteral("Combined Discord live notification sent.");
        else{
            const QByteArray body=reply->readAll();const QJsonObject error=QJsonDocument::fromJson(body).object();const int discordCode=error.value(QStringLiteral("code")).toInt();QString message=error.value(QStringLiteral("message")).toString().trimmed();if(message.isEmpty())message=reply->errorString();
            detail=discordCode>0?QStringLiteral("Discord error %1 (HTTP %2): %3").arg(discordCode).arg(code).arg(message):QStringLiteral("Discord rejected the notification (HTTP %1): %2").arg(code).arg(message);
            const QString cause=discordCauseFor(code,discordCode);
            if(!cause.isEmpty())detail+=QStringLiteral("\n\nLikely cause: ")+cause;
            // The raw body is the only thing that distinguishes a Discord
            // rejection from a Cloudflare/HTML interception, so surface it.
            emit discordDiagnostic(QStringLiteral("POST /channels/%1/messages -> HTTP %2\n%3")
                                       .arg(normalizeDiscordId(settings_.secret(QStringLiteral("discord_channel_id"))))
                                       .arg(code)
                                       .arg(QString::fromUtf8(body.left(1200))));
            if(test)detail+=QStringLiteral("\n\nPress Diagnose for a step-by-step check.");
        }
        emit discordNotificationResult(ok,detail);reply->deleteLater();
    });
}
void AppController::setPinnedMessagesEnabled(bool enabled){
    pinnedMessagesEnabled_=enabled;settings_.setPreference(QStringLiteral("show_pinned_messages"),enabled);
    if(!enabled){twitchPinnedTimer_.stop();for(const auto&platform:{QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("yt_shorts")})emit pinnedMessageChanged(platform,ChatMessage{},false);return;}
    twitchPinnedTimer_.start();for(auto it=currentPinnedMessages_.cbegin();it!=currentPinnedMessages_.cend();++it)emit pinnedMessageChanged(it.key(),it.value(),true);
    const QString broadcaster=settings_.secret(QStringLiteral("twitch_broadcaster_id"));if(!twitch_.channel().isEmpty()&&!broadcaster.isEmpty())twitchMod_.getPinnedMessage(broadcaster);
}
void AppController::authorizeTwitch(){twitchAuthorizationRequested_=true;twitchAuth_.authorize(configuredTwitchClientId(settings_));}
void AppController::configureYouTubeModeration(const QString&t){settings_.setSecret("youtube_access_token",t);youtubeMod_.setAccessToken(t);emit sourceStatus("youtube","ok","moderation configured");}
void AppController::connectStreamlabs(const QString&t){settings_.setSecret("streamlabs_socket_token",t);streamlabs_.connectToken(t);}
void AppController::disconnectStreamlabs(){streamlabs_.disconnectService();settings_.setSecret("streamlabs_socket_token",QString());}
void AppController::refreshBans(const QString&p){if(p=="twitch"){const QString id=settings_.secret("twitch_broadcaster_id");if(id.isEmpty()){emit moderationResult("twitch",false,"Connect Twitch and configure moderation first.");return;}twitchMod_.listBans(id);}else if(p=="youtube")emit bansUpdated("youtube",youtubeRestrictions_);}
void AppController::unbanTwitch(const QString&u){const QString id=settings_.secret("twitch_broadcaster_id");if(!id.isEmpty()&&!u.isEmpty()){twitchMod_.unban(id,u);QTimer::singleShot(800,this,[this]{refreshBans("twitch");});}}
void AppController::resolveTwitchAppeal(const QString&requestId,bool approved,const QString&resolutionText){
    const QStringList scopes=settings_.preference(QStringLiteral("twitch_authorized_scopes")).toStringList();
    if(!scopes.contains(QStringLiteral("moderator:manage:unban_requests"))){
        emit moderationResult(QStringLiteral("twitch"),false,QStringLiteral("TWITCH_SCOPE_UPGRADE_REQUIRED"));
        return;
    }
    const QString broadcaster=settings_.secret("twitch_broadcaster_id");
    if(!broadcaster.isEmpty()&&!requestId.isEmpty())twitchMod_.resolveUnbanRequest(broadcaster,requestId,approved,resolutionText);
}
void AppController::regenerateNameColours(){
    broadcastUserColours_.clear();
    broadcastPaletteVariations_.clear();
    nextBroadcastColour_=0;
    nextPaletteVariation_=0;
    broadcastColourOffset_=QRandomGenerator::global()->bounded(360);
    broadcastPaletteOffset_=QRandomGenerator::global()->bounded(360);
}
void AppController::createTwitchClip(){
    const QString broadcaster=settings_.secret("twitch_broadcaster_id");
    if(settings_.secret("twitch_access_token").isEmpty()||broadcaster.isEmpty()){
        emit twitchClipFailed(QStringLiteral("Connect Twitch under Moderation, then pick a live Twitch channel, before creating a clip."));
        return;
    }
    twitchMod_.createClip(broadcaster);
}
void AppController::unbanYouTube(const QString&id){if(id.isEmpty())return;youtubeMod_.removeBan(id);for(int i=youtubeRestrictions_.size()-1;i>=0;--i)if(youtubeRestrictions_[i].toObject()["id"].toString()==id)youtubeRestrictions_.removeAt(i);QVariantList saved;for(const auto&v:youtubeRestrictions_)saved<<v.toObject().toVariantMap();settings_.setPreference("youtube_restrictions",saved);emit bansUpdated("youtube",youtubeRestrictions_);}
void AppController::receive(const ChatMessage&m){
    ChatMessage coloured=m;
    // Assign every chatter a vivid, stable pseudo-random name colour. Using
    // the platform plus account id avoids collisions between services, while
    // falling back to the displayed name still covers guests without an id.
    const QString identity=coloured.platform+QLatin1Char('|')+
        (coloured.userId.isEmpty()?coloured.user.toLower():coloured.userId);
    if(!broadcastUserColours_.contains(identity)){
        if(broadcastColourOffset_<0)broadcastColourOffset_=QRandomGenerator::global()->bounded(360);
        // A golden-angle step spreads consecutive users around the full hue
        // wheel instead of clustering similar colours together.
        const int index=nextBroadcastColour_++;
        const int hue=(broadcastColourOffset_+index*137)%360;
        const int saturation=175+(index*23)%46;
        broadcastUserColours_.insert(identity,QColor::fromHsv(hue,saturation,245));
    }
    const QString colourMode=settings_.preference(QStringLiteral("chat_colour_mode"),QStringLiteral("random")).toString();
    QColor selected(settings_.preference(QStringLiteral("chat_name_colour"),QStringLiteral("#53cdf3")).toString());
    if(!selected.isValid())selected=QColor(QStringLiteral("#53cdf3"));
    coloured.color=colourMode==QStringLiteral("single")?selected:broadcastUserColours_.value(identity);
    if(colourMode==QStringLiteral("gradient")||colourMode==QStringLiteral("pattern")){
        QStringList palette=settings_.preference(QStringLiteral("chat_name_palette"),QStringList{QStringLiteral("#6c4cff"),QStringLiteral("#18dfd1"),QStringLiteral("#ff5ca8"),QStringLiteral("#ffd166")}).toStringList();
        palette.erase(std::remove_if(palette.begin(),palette.end(),[](const QString&value){return !QColor(value).isValid();}),palette.end());
        if(palette.size()<2)palette={QStringLiteral("#6c4cff"),QStringLiteral("#18dfd1")};
        const bool randomized=settings_.preference(QStringLiteral("chat_palette_randomize"),false).toBool();
        if(randomized){
            if(broadcastPaletteOffset_<0)broadcastPaletteOffset_=QRandomGenerator::global()->bounded(360);
            if(!broadcastPaletteVariations_.contains(identity))broadcastPaletteVariations_.insert(identity,nextPaletteVariation_++);
            const int variant=broadcastPaletteVariations_.value(identity);
            const int rotation=variant%palette.size();
            const int hueShift=((broadcastPaletteOffset_+variant*47)%73)-36;
            QStringList varied;varied.reserve(palette.size());
            for(int i=0;i<palette.size();++i){
                QColor base(palette.at((i+rotation)%palette.size()));
                int h=base.hsvHue();int sat=base.hsvSaturation();int val=base.value();
                if(h<0)h=(broadcastPaletteOffset_+variant*29+i*17)%360;else h=(h+hueShift+i*3+360)%360;
                sat=qBound(120,sat+((variant*19+i*11)%31)-15,255);
                val=qBound(185,val+((variant*13+i*7)%25)-12,255);
                varied<<QColor::fromHsv(h,sat,val).name();
            }
            palette=varied;
            coloured.metadata[QStringLiteral("name_color_variant")]=variant;
        }
        coloured.color=QColor(palette.first());coloured.metadata[QStringLiteral("name_color_mode")]=colourMode;coloured.metadata[QStringLiteral("name_color_palette")]=QJsonArray::fromStringList(palette);if(colourMode==QStringLiteral("pattern"))coloured.metadata[QStringLiteral("name_color_pattern")]=settings_.preference(QStringLiteral("chat_name_pattern"),QStringLiteral("repeat")).toString();
    }
    const bool omitBlockedFromLog=coloured.platform==QStringLiteral("kick")||coloured.platform==QStringLiteral("rumble");
    const QString iconKey=QStringLiteral("custom_chatter_icons_")+coloured.platform;
    const QStringList customIcons=settings_.preference(iconKey,QStringList{}).toStringList();
    if(!customIcons.isEmpty())coloured.metadata[QStringLiteral("custom_chatter_icons")]=QJsonArray::fromStringList(customIcons.mid(0,3));
    if(!omitBlockedFromLog)audit_.appendMessage(coloured);
    QString reason;
    if(automod_.check(coloured,&reason)){
        // Hold the message back instead of showing it and moderating after the
        // fact — the chat views, overlay, and pop-out never see the original
        // message. messageModerated lets the chat views (not the overlay) show
        // a note that something was removed.
        autoModerate(coloured,reason);
        emit messageModerated(coloured,reason);
        return;
    }
    if(omitBlockedFromLog)audit_.appendMessage(coloured);
    emit messageReady(coloured);
}
void AppController::autoModerate(const ChatMessage&m,const QString&r){
    if(m.platform=="twitch"){
        const auto broadcaster=m.metadata["room_id"].toString();
        if(broadcaster.isEmpty()||m.userId.isEmpty())return;
        // Escalating penalty for repeat offenders within one broadcast:
        // offenses 1-5 use the configured base timeout, offense 6 doubles it,
        // and offense 7+ is a permanent ban. The count resets when
        // the channel goes live again (see the broadcastWentLive connection
        // in the constructor); AutoMod otherwise keeps moderating even while
        // the channel is offline.
        const int offense=++twitchAutoModOffenses_[m.userId];
        const int base=qBound(30,settings_.preference(QStringLiteral("twitch_automod_timeout_seconds"),300).toInt(),86400);
        const int seconds=offense<=5?base:offense==6?qMin(base*2,86400):0;
        twitchMod_.ban(broadcaster,m.userId,seconds,r);
        if(!m.messageId.isEmpty())twitchMod_.deleteMessage(broadcaster,m.messageId);
    }else if(m.platform=="youtube"||m.platform=="yt_shorts"){
        const auto chat=m.metadata["live_chat_id"].toString();
        if(chat.isEmpty()||m.userId.isEmpty())return;
        const int seconds=qBound(300,settings_.preference(QStringLiteral("youtube_automod_timeout_seconds"),300).toInt(),86400);
        pendingYouTubeReasons_[m.user]=r;pendingYouTubeSeconds_[m.user]=seconds;
        youtubeMod_.ban(chat,m.userId,seconds,m.user);
        if(!m.messageId.isEmpty())youtubeMod_.deleteMessage(m.messageId);
    }
}
void AppController::moderateMessage(const ChatMessage&m,int seconds,const QString&reason){
    if(m.platform=="twitch"){
        const auto broadcaster=m.metadata["room_id"].toString();
        if(!broadcaster.isEmpty()&&!m.userId.isEmpty())twitchMod_.ban(broadcaster,m.userId,seconds,reason);
    }else if(m.platform=="youtube"||m.platform=="yt_shorts"){
        const auto chat=m.metadata["live_chat_id"].toString();
        if(!chat.isEmpty()&&!m.userId.isEmpty()){pendingYouTubeReasons_[m.user]=reason;pendingYouTubeSeconds_[m.user]=seconds;youtubeMod_.ban(chat,m.userId,seconds,m.user);}
    }
}
void AppController::refreshTwitchAppeals(){const QString id=settings_.secret("twitch_broadcaster_id");if(id.isEmpty()){emit moderationResult("twitch",false,"Connect Twitch and choose a channel first.");return;}twitchMod_.listUnbanRequests(id);}
void AppController::loadTwitchUserHistory(const QString&userId,const QString&userName){emit userChatHistoryReady(userId,audit_.messagesForUser("twitch",userId,userName));}
void AppController::deleteChatMessage(const ChatMessage&m){
    if(m.platform=="twitch"){
        const auto broadcaster=m.metadata["room_id"].toString();
        if(!broadcaster.isEmpty()&&!m.messageId.isEmpty())twitchMod_.deleteMessage(broadcaster,m.messageId);
    }else if(m.platform=="youtube"||m.platform=="yt_shorts"){
        if(!m.messageId.isEmpty())youtubeMod_.deleteMessage(m.messageId);
    }
}
