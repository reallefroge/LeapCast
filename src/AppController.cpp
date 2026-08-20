#include "AppController.hpp"
#include "BuildInfo.hpp"
#include <QRegularExpression>
#include <QTimer>

namespace {
QString configuredTwitchClientId(SettingsStore& settings){
    const QString bundled=QString::fromLatin1(leapcast::TwitchClientId).trimmed();
    return bundled.isEmpty()?settings.secret(QStringLiteral("twitch_client_id")):bundled;
}
}

AppController::AppController(QObject*p):QObject(p){
    for(auto*service:{static_cast<QObject*>(&twitch_),static_cast<QObject*>(&youtube_),static_cast<QObject*>(&shorts_),static_cast<QObject*>(&tiktok_)})Q_UNUSED(service);
    connect(&twitch_,&TwitchChatService::messageReceived,this,&AppController::receive);
    connect(&youtube_,&YouTubeChatService::messageReceived,this,&AppController::receive);
    connect(&shorts_,&YouTubeChatService::messageReceived,this,&AppController::receive);
    connect(&tiktok_,&TikTokLiveService::messageReceived,this,&AppController::receive);
    connect(&twitch_,&TwitchChatService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("twitch",s,d);});
    connect(&youtube_,&YouTubeChatService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("youtube",s,d);});
    connect(&shorts_,&YouTubeChatService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("yt_shorts",s,d);});
    connect(&tiktok_,&TikTokLiveService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("tiktok",s,d);});
    connect(&twitch_,&TwitchChatService::viewerCountChanged,this,[this](int n){emit viewerCount("twitch",n);});
    connect(&youtube_,&YouTubeChatService::viewerCountChanged,this,[this](int n){emit viewerCount("youtube",n);});
    connect(&shorts_,&YouTubeChatService::viewerCountChanged,this,[this](int n){emit viewerCount("yt_shorts",n);});
    connect(&tiktok_,&TikTokLiveService::viewerCountChanged,this,[this](int n){emit viewerCount("tiktok",n);});
    connect(&streamlabs_,&StreamlabsService::eventReceived,this,[this](const StreamEvent&e){audit_.appendEvent(e);emit eventReady(e);});
    connect(&streamlabs_,&StreamlabsService::statusChanged,this,[this](const QString&s,const QString&d){emit sourceStatus("streamlabs",s,d);});
    const QString twitchClientId=configuredTwitchClientId(settings_);
    twitchMod_.configure(twitchClientId,settings_.secret("twitch_access_token"),settings_.secret("twitch_moderator_id"));
    youtubeMod_.setAccessToken(settings_.secret("youtube_access_token"));
    for(const auto& item:settings_.preference("youtube_restrictions").toList())youtubeRestrictions_.append(QJsonObject::fromVariantMap(item.toMap()));
    connect(&twitchMod_,&TwitchModerationService::bansReceived,this,[this](const QJsonArray&a){emit bansUpdated("twitch",a);});
    connect(&twitchMod_,&TwitchModerationService::broadcasterResolved,this,[this](const QString&,const QString&id){settings_.setSecret("twitch_broadcaster_id",id);});
    connect(&youtubeMod_,&YouTubeModerationService::banCreated,this,[this](const QString&id,const QString&user,bool permanent){QJsonObject o{{"id",id},{"user_name",user},{"type",permanent?QStringLiteral("Hide from channel"):QStringLiteral("Timeout (5 min)")},{"created_at",QDateTime::currentDateTime().toString(Qt::ISODate)}};youtubeRestrictions_.prepend(o);QVariantList saved;for(const auto&v:youtubeRestrictions_)saved<<v.toObject().toVariantMap();settings_.setPreference("youtube_restrictions",saved);emit bansUpdated("youtube",youtubeRestrictions_);});
    connect(&twitchMod_,&TwitchModerationService::actionFinished,this,[this](const QString&action,bool ok,const QString&d){
        if(action=="clip"){if(!ok)emit twitchClipFailed(d);return;}
        emit moderationResult("twitch",ok,d);
    });
    connect(&twitchMod_,&TwitchModerationService::clipCreated,this,[this](const QString&,const QUrl&editUrl){emit twitchClipCreated(editUrl);});
    connect(&youtubeMod_,&YouTubeModerationService::actionFinished,this,[this](const QString&,bool ok,const QString&d){emit moderationResult("youtube",ok,d);});
    connect(&twitchAuth_,&TwitchAuthService::browserAuthorizationReady,this,&AppController::twitchAuthorizationUrl);
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
        emit sourceStatus("twitch","ok",QStringLiteral("Authorized as %1").arg(login));
        if(twitchAuthorizationRequested_)emit twitchAuthorized(login);
        twitchAuthorizationRequested_=false;
    });
    if(!twitchClientId.isEmpty()&&!settings_.secret("twitch_access_token").isEmpty())
        twitchAuth_.restore(twitchClientId,settings_.secret("twitch_access_token"),settings_.secret("twitch_refresh_token"));
}
QString AppController::twitchName(const QString&l){auto m=QRegularExpression("twitch\\.tv/([A-Za-z0-9_]+)").match(l);if(m.hasMatch())return m.captured(1).toLower();return QRegularExpression("^[A-Za-z0-9_]{3,25}$").match(l).hasMatch()?l.toLower():QString();}
QString AppController::tiktokName(const QString&l){auto m=QRegularExpression("tiktok\\.com/@([A-Za-z0-9._]+)").match(l);if(m.hasMatch())return m.captured(1);return l.startsWith('@')?l.mid(1):QString();}
void AppController::startConfiguredSources(){for(const auto&p:{"twitch","youtube","yt_shorts","tiktok"})if(settings_.enabled(p)&&!settings_.link(p).isEmpty())connectSource(p,settings_.link(p));const auto token=settings_.secret("streamlabs_socket_token");if(!token.isEmpty())streamlabs_.connectToken(token);}
void AppController::connectSource(const QString&p,const QString&l){settings_.setLink(p,l);if(p=="twitch"){const QString name=twitchName(l);twitch_.connectChannel(name);if(!settings_.secret("twitch_access_token").isEmpty()){twitchMod_.configure(configuredTwitchClientId(settings_),settings_.secret("twitch_access_token"),settings_.secret("twitch_moderator_id"));twitchMod_.resolveBroadcaster(name);}}else if(p=="youtube")youtube_.connectTarget(l);else if(p=="yt_shorts")shorts_.connectTarget(l);else if(p=="tiktok")tiktok_.connectUser(tiktokName(l));}
void AppController::disconnectSource(const QString&p){
    if(p=="twitch")twitch_.disconnectChannel();
    else if(p=="youtube")youtube_.disconnectService();
    else if(p=="yt_shorts")shorts_.disconnectService();
    else if(p=="tiktok")tiktok_.disconnectService();
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
    audit_.appendMessage(m);
    QString reason;
    if(automod_.check(m,&reason)){
        // Hold the message back instead of showing it and moderating after the
        // fact — the chat views, overlay, and pop-out never see the original
        // message. messageModerated lets the chat views (not the overlay) show
        // a note that something was removed.
        autoModerate(m,reason);
        emit messageModerated(m,reason);
        return;
    }
    emit messageReady(m);
}
void AppController::autoModerate(const ChatMessage&m,const QString&r){if(m.platform=="twitch"){const auto broadcaster=m.metadata["room_id"].toString();if(!broadcaster.isEmpty()&&!m.userId.isEmpty())twitchMod_.ban(broadcaster,m.userId,300,r);}else if(m.platform=="youtube"||m.platform=="yt_shorts"){const auto chat=m.metadata["live_chat_id"].toString();if(!chat.isEmpty()&&!m.userId.isEmpty())youtubeMod_.ban(chat,m.userId,300,m.user);}}
void AppController::moderateMessage(const ChatMessage&m,int seconds){
    if(m.platform=="twitch"){
        const auto broadcaster=m.metadata["room_id"].toString();
        if(!broadcaster.isEmpty()&&!m.userId.isEmpty())twitchMod_.ban(broadcaster,m.userId,seconds,QStringLiteral("Manual moderation"));
    }else if(m.platform=="youtube"||m.platform=="yt_shorts"){
        const auto chat=m.metadata["live_chat_id"].toString();
        if(!chat.isEmpty()&&!m.userId.isEmpty())youtubeMod_.ban(chat,m.userId,seconds,m.user);
    }
}
void AppController::deleteChatMessage(const ChatMessage&m){
    if(m.platform=="twitch"){
        const auto broadcaster=m.metadata["room_id"].toString();
        if(!broadcaster.isEmpty()&&!m.messageId.isEmpty())twitchMod_.deleteMessage(broadcaster,m.messageId);
    }else if(m.platform=="youtube"||m.platform=="yt_shorts"){
        if(!m.messageId.isEmpty())youtubeMod_.deleteMessage(m.messageId);
    }
}
