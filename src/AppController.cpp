#include "AppController.hpp"
#include "BuildInfo.hpp"
#include <algorithm>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QTimer>

namespace {
QString configuredTwitchClientId(SettingsStore& settings){
    const QString bundled=QString::fromLatin1(leapcast::TwitchClientId).trimmed();
    return bundled.isEmpty()?settings.secret(QStringLiteral("twitch_client_id")):bundled;
}
QString twitchRedemptionKey(const QString&rewardId,const QString&user,const QString&input){return rewardId+QLatin1Char('|')+user.toLower()+QLatin1Char('|')+input;}
}

AppController::AppController(QObject*p):QObject(p){
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
    connect(&tiktok_,&TikTokLiveService::messageReceived,this,&AppController::receive);
    connect(&kick_,&KickLiveService::messageReceived,this,&AppController::receive);
    connect(&rumble_,&RumbleLiveService::messageReceived,this,&AppController::receive);
    connect(&twitch_,&TwitchChatService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("twitch",s,d);});
    connect(&youtube_,&YouTubeChatService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("youtube",s,d);});
    connect(&shorts_,&YouTubeChatService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("yt_shorts",s,d);});
    connect(&tiktok_,&TikTokLiveService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("tiktok",s,d);});
    connect(&kick_,&KickLiveService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("kick",s,d);});
    connect(&rumble_,&RumbleLiveService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("rumble",s,d);});
    connect(&twitch_,&TwitchChatService::broadcastWentLive,this,[this]{twitchAutoModOffenses_.clear();});
    connect(&twitch_,&TwitchChatService::viewerCountChanged,this,[this](int n){emit viewerCount("twitch",n);});
    connect(&youtube_,&YouTubeChatService::viewerCountChanged,this,[this](int n){emit viewerCount("youtube",n);});
    connect(&shorts_,&YouTubeChatService::viewerCountChanged,this,[this](int n){emit viewerCount("yt_shorts",n);});
    connect(&tiktok_,&TikTokLiveService::viewerCountChanged,this,[this](int n){emit viewerCount("tiktok",n);});
    connect(&kick_,&KickLiveService::viewerCountChanged,this,[this](int n){emit viewerCount("kick",n);});
    connect(&rumble_,&RumbleLiveService::viewerCountChanged,this,[this](int n){emit viewerCount("rumble",n);});
    connect(&tiktok_,&TikTokLiveService::activityReceived,this,&AppController::tiktokActivityReady);
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
    connect(&twitchMod_,&TwitchModerationService::broadcasterResolved,this,[this,twitchClientId](const QString&,const QString&id){settings_.setSecret("twitch_broadcaster_id",id);twitchEvents_.connectRedemptions(twitchClientId,settings_.secret(QStringLiteral("twitch_access_token")),id);});
    connect(&youtubeMod_,&YouTubeModerationService::banCreated,this,[this](const QString&id,const QString&user,bool permanent){QJsonObject o{{"id",id},{"user_name",user},{"type",permanent?QStringLiteral("Hide from channel"):QStringLiteral("Timeout (5 min)")},{"reason",pendingYouTubeReasons_.take(user)},{"created_at",QDateTime::currentDateTime().toString(Qt::ISODate)}};youtubeRestrictions_.prepend(o);QVariantList saved;for(const auto&v:youtubeRestrictions_)saved<<v.toObject().toVariantMap();settings_.setPreference("youtube_restrictions",saved);emit bansUpdated("youtube",youtubeRestrictions_);});
    connect(&twitchMod_,&TwitchModerationService::actionFinished,this,[this](const QString&action,bool ok,const QString&d){
        if(action=="clip"){if(!ok)emit twitchClipFailed(d);return;}
        emit moderationResult("twitch",ok,d);
        if(ok&&(action=="approve_appeal"||action=="deny_appeal"))QTimer::singleShot(250,this,&AppController::refreshTwitchAppeals);
    });
    connect(&twitchMod_,&TwitchModerationService::clipCreated,this,[this](const QString&,const QUrl&editUrl){emit twitchClipCreated(editUrl);});
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
}
QString AppController::twitchName(const QString&l){auto m=QRegularExpression("twitch\\.tv/([A-Za-z0-9_]+)").match(l);if(m.hasMatch())return m.captured(1).toLower();return QRegularExpression("^[A-Za-z0-9_]{3,25}$").match(l).hasMatch()?l.toLower():QString();}
QString AppController::tiktokName(const QString&l){auto m=QRegularExpression("tiktok\\.com/@([A-Za-z0-9._]+)").match(l);if(m.hasMatch())return m.captured(1);return l.startsWith('@')?l.mid(1):QString();}
QString AppController::kickName(const QString&l){auto m=QRegularExpression("kick\\.com/([A-Za-z0-9_-]+)").match(l);if(m.hasMatch())return m.captured(1).toLower();return QRegularExpression("^[A-Za-z0-9_-]+$").match(l).hasMatch()?l.toLower():QString();}
void AppController::startConfiguredSources(){for(const auto&p:{"twitch","youtube","yt_shorts","tiktok","kick","rumble"})if(settings_.enabled(p)&&!settings_.link(p).isEmpty())connectSource(p,settings_.link(p));const auto token=settings_.secret("streamlabs_socket_token");if(!token.isEmpty())streamlabs_.connectToken(token);}
void AppController::connectSource(const QString&p,const QString&l){settings_.setLink(p,l);if(p=="twitch"){const QString name=twitchName(l);twitch_.connectChannel(name);if(!settings_.secret("twitch_access_token").isEmpty()){twitchMod_.configure(configuredTwitchClientId(settings_),settings_.secret("twitch_access_token"),settings_.secret("twitch_moderator_id"));twitchMod_.resolveBroadcaster(name);}}else if(p=="youtube")youtube_.connectTarget(l);else if(p=="yt_shorts")shorts_.connectTarget(l);else if(p=="tiktok")tiktok_.connectUser(tiktokName(l));else if(p=="kick")kick_.connectChannel(kickName(l));else if(p=="rumble")rumble_.connectApi(QUrl(settings_.secret("rumble_api_url")));}
void AppController::disconnectSource(const QString&p){
    if(p=="twitch"){twitch_.disconnectChannel();twitchEvents_.disconnectService();}
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
    emit sourceStatus(p,"warn","Disconnected");
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
        coloured.color=QColor(palette.first());coloured.metadata[QStringLiteral("name_color_mode")]=colourMode;coloured.metadata[QStringLiteral("name_color_palette")]=QJsonArray::fromStringList(palette);if(colourMode==QStringLiteral("pattern"))coloured.metadata[QStringLiteral("name_color_pattern")]=settings_.preference(QStringLiteral("chat_name_pattern"),QStringLiteral("repeat")).toString();
    }
    const bool omitBlockedFromLog=coloured.platform==QStringLiteral("kick")||coloured.platform==QStringLiteral("rumble");
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
    }else if(m.platform=="youtube"||m.platform=="yt_shorts"){
        const auto chat=m.metadata["live_chat_id"].toString();
        if(chat.isEmpty()||m.userId.isEmpty())return;
        const int seconds=qBound(300,settings_.preference(QStringLiteral("youtube_automod_timeout_seconds"),300).toInt(),86400);
        youtubeMod_.ban(chat,m.userId,seconds,m.user);
    }
}
void AppController::moderateMessage(const ChatMessage&m,int seconds,const QString&reason){
    if(m.platform=="twitch"){
        const auto broadcaster=m.metadata["room_id"].toString();
        if(!broadcaster.isEmpty()&&!m.userId.isEmpty())twitchMod_.ban(broadcaster,m.userId,seconds,reason);
    }else if(m.platform=="youtube"||m.platform=="yt_shorts"){
        const auto chat=m.metadata["live_chat_id"].toString();
        if(!chat.isEmpty()&&!m.userId.isEmpty()){pendingYouTubeReasons_[m.user]=reason;youtubeMod_.ban(chat,m.userId,seconds,m.user);}
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
