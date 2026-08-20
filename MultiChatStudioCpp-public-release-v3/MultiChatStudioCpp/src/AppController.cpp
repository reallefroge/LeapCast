#include "AppController.hpp"
#include <QRegularExpression>

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
    twitchMod_.configure(settings_.secret("twitch_client_id"),settings_.secret("twitch_access_token"),settings_.secret("twitch_moderator_id"));
    youtubeMod_.setAccessToken(settings_.secret("youtube_access_token"));tiktokMod_.setToken(settings_.secret("tiktok_mod_token"));
}
QString AppController::twitchName(const QString&l){auto m=QRegularExpression("twitch\\.tv/([A-Za-z0-9_]+)").match(l);if(m.hasMatch())return m.captured(1).toLower();return QRegularExpression("^[A-Za-z0-9_]{3,25}$").match(l).hasMatch()?l.toLower():QString();}
QString AppController::tiktokName(const QString&l){auto m=QRegularExpression("tiktok\\.com/@([A-Za-z0-9._]+)").match(l);if(m.hasMatch())return m.captured(1);return l.startsWith('@')?l.mid(1):QString();}
void AppController::startConfiguredSources(){for(const auto&p:{"twitch","youtube","yt_shorts","tiktok"})if(settings_.enabled(p)&&!settings_.link(p).isEmpty())connectSource(p,settings_.link(p));const auto token=settings_.secret("streamlabs_socket_token");if(!token.isEmpty())streamlabs_.connectToken(token);}
void AppController::connectSource(const QString&p,const QString&l){settings_.setLink(p,l);if(p=="twitch")twitch_.connectChannel(twitchName(l));else if(p=="youtube")youtube_.connectTarget(l);else if(p=="yt_shorts")shorts_.connectTarget(l);else if(p=="tiktok")tiktok_.connectUser(tiktokName(l));}
void AppController::disconnectSource(const QString&p){if(p=="twitch")twitch_.disconnectChannel();else if(p=="youtube")youtube_.disconnectService();else if(p=="yt_shorts")shorts_.disconnectService();else if(p=="tiktok")tiktok_.disconnectService();}
void AppController::receive(const ChatMessage&m){audit_.appendMessage(m);QString reason;if(automod_.check(m,&reason))autoModerate(m,reason);emit messageReady(m);}
void AppController::autoModerate(const ChatMessage&m,const QString&r){if(m.platform=="twitch"){const auto broadcaster=m.metadata["room_id"].toString();if(!broadcaster.isEmpty()&&!m.userId.isEmpty())twitchMod_.ban(broadcaster,m.userId,300,r);}else if(m.platform=="youtube"||m.platform=="yt_shorts"){const auto chat=m.metadata["live_chat_id"].toString();if(!chat.isEmpty()&&!m.userId.isEmpty())youtubeMod_.ban(chat,m.userId,300,m.user);}else if(m.platform=="tiktok"){const auto room=m.metadata["room_id"].toString();if(!room.isEmpty()&&!m.userId.isEmpty())tiktokMod_.mute(room,m.userId,300,m.messageId);}}

