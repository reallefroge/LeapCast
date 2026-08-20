#include "LiveServices.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrlQuery>

namespace {
constexpr auto TwitchGqlClient = "kimne78kx3ncx6brgo4mv6wki5h1ko";
QString ircUnescape(QString s){return s.replace("\\s"," ").replace("\\:",";").replace("\\r","\r").replace("\\n","\n").replace("\\\\","\\");}
QByteArray formBody(const QList<QPair<QString,QString>>& fields){
    QUrlQuery form;
    for(const auto& field:fields)form.addQueryItem(field.first,field.second);
    return form.query(QUrl::FullyEncoded).toUtf8();
}
QString twitchError(const QByteArray& payload,const QString& fallback){
    const auto object=QJsonDocument::fromJson(payload).object();
    return object.value("message").toString(object.value("error").toString(fallback));
}
}

TwitchAuthService::TwitchAuthService(QObject*p):QObject(p){
    scopes_=QStringLiteral("chat:read chat:edit moderation:read moderator:read:banned_users moderator:manage:banned_users moderator:read:chat_messages moderator:manage:chat_messages moderator:read:unban_requests user:write:chat clips:edit");
    pollTimer_.setSingleShot(true);
    connect(&pollTimer_,&QTimer::timeout,this,&TwitchAuthService::pollForToken);
    refreshTimer_.setSingleShot(true);
    connect(&refreshTimer_,&QTimer::timeout,this,&TwitchAuthService::refreshToken);
}

void TwitchAuthService::authorize(const QString&clientId){
    clientId_=clientId.trimmed();deviceCode_.clear();accessToken_.clear();refreshToken_.clear();
    pollTimer_.stop();refreshTimer_.stop();
    if(clientId_.isEmpty()){finishWithError(QStringLiteral("This build is missing Leapcast Studio's Twitch application ID."));return;}
    requestDeviceCode();
}

void TwitchAuthService::restore(const QString&clientId,const QString&accessToken,const QString&refreshTokenValue){
    clientId_=clientId.trimmed();accessToken_=accessToken.trimmed();refreshToken_=refreshTokenValue.trimmed();
    if(clientId_.isEmpty()||accessToken_.isEmpty())return;
    validateToken(accessToken_,refreshToken_);
}

void TwitchAuthService::requestDeviceCode(){
    QNetworkRequest request(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/device")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/x-www-form-urlencoded"));
    auto* reply=network_.post(request,formBody({{"client_id",clientId_},{"scopes",scopes_}}));
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        const QByteArray payload=reply->readAll();
        if(reply->error()!=QNetworkReply::NoError){finishWithError(twitchError(payload,reply->errorString()));reply->deleteLater();return;}
        const auto object=QJsonDocument::fromJson(payload).object();
        deviceCode_=object.value("device_code").toString();
        const QUrl verificationUrl(object.value("verification_uri").toString());
        const int interval=qMax(5,object.value("interval").toInt(5));
        if(deviceCode_.isEmpty()||!verificationUrl.isValid()){finishWithError(QStringLiteral("Twitch returned an incomplete authorization response."));reply->deleteLater();return;}
        pollTimer_.setInterval(interval*1000);
        emit browserAuthorizationReady(verificationUrl);
        emit authorizationPending();
        pollTimer_.start();
        reply->deleteLater();
    });
}

void TwitchAuthService::pollForToken(){
    if(deviceCode_.isEmpty())return;
    QNetworkRequest request(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/x-www-form-urlencoded"));
    auto* reply=network_.post(request,formBody({{"client_id",clientId_},{"scopes",scopes_},{"device_code",deviceCode_},{"grant_type",QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")}}));
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        const QByteArray payload=reply->readAll();
        const auto object=QJsonDocument::fromJson(payload).object();
        const QString message=object.value("message").toString();
        if(message==QStringLiteral("authorization_pending")){pollTimer_.start();reply->deleteLater();return;}
        if(message==QStringLiteral("slow_down")){pollTimer_.setInterval(pollTimer_.interval()+5000);pollTimer_.start();reply->deleteLater();return;}
        if(reply->error()!=QNetworkReply::NoError){finishWithError(twitchError(payload,reply->errorString()));reply->deleteLater();return;}
        accessToken_=object.value("access_token").toString();
        refreshToken_=object.value("refresh_token").toString();
        deviceCode_.clear();
        if(accessToken_.isEmpty()){finishWithError(QStringLiteral("Twitch did not return an access token."));reply->deleteLater();return;}
        validateToken(accessToken_,refreshToken_);
        reply->deleteLater();
    });
}

void TwitchAuthService::validateToken(const QString&accessToken,const QString&refreshTokenValue){
    QNetworkRequest request(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/validate")));
    request.setRawHeader("Authorization",("OAuth "+accessToken).toUtf8());
    auto* reply=network_.get(request);
    connect(reply,&QNetworkReply::finished,this,[this,reply,accessToken,refreshTokenValue]{
        const QByteArray payload=reply->readAll();
        if(reply->error()!=QNetworkReply::NoError){
            reply->deleteLater();
            if(!refreshTokenValue.isEmpty()){accessToken_=accessToken;refreshToken_=refreshTokenValue;refreshToken();}
            else finishWithError(QStringLiteral("Twitch authorization expired. Select Authorize Twitch to reconnect."));
            return;
        }
        const auto object=QJsonDocument::fromJson(payload).object();
        const QString userId=object.value("user_id").toString();
        if(userId.isEmpty()){finishWithError(QStringLiteral("Twitch did not identify the authorized account."));reply->deleteLater();return;}
        accessToken_=accessToken;refreshToken_=refreshTokenValue;
        const int expiresIn=object.value("expires_in").toInt();
        if(!refreshToken_.isEmpty()&&expiresIn>0)
            refreshTimer_.start(qMax(60,expiresIn-300)*1000);
        emit authorized(accessToken_,refreshToken_,userId,object.value("login").toString(),expiresIn);
        reply->deleteLater();
    });
}

void TwitchAuthService::refreshToken(){
    QNetworkRequest request(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/x-www-form-urlencoded"));
    auto* reply=network_.post(request,formBody({{"client_id",clientId_},{"grant_type",QStringLiteral("refresh_token")},{"refresh_token",refreshToken_}}));
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        const QByteArray payload=reply->readAll();
        if(reply->error()!=QNetworkReply::NoError){finishWithError(QStringLiteral("Twitch authorization expired. Select Authorize Twitch to reconnect."));reply->deleteLater();return;}
        const auto object=QJsonDocument::fromJson(payload).object();
        const QString token=object.value("access_token").toString();
        const QString refresh=object.value("refresh_token").toString(refreshToken_);
        if(token.isEmpty()){finishWithError(twitchError(payload,QStringLiteral("Twitch token refresh failed.")));reply->deleteLater();return;}
        validateToken(token,refresh);
        reply->deleteLater();
    });
}

void TwitchAuthService::finishWithError(const QString&detail){pollTimer_.stop();refreshTimer_.stop();deviceCode_.clear();emit authorizationFailed(detail);}

TwitchChatService::TwitchChatService(QObject*p):QObject(p){
    connect(&socket_,&QWebSocket::connected,this,[this]{
        backoffMs_=2000; const auto nick=QStringLiteral("justinfan%1").arg(QRandomGenerator::global()->bounded(10000,99999));
        socket_.sendTextMessage("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n");
        socket_.sendTextMessage("PASS SCHMOOPIIE\r\n"); socket_.sendTextMessage("NICK "+nick+"\r\n");
        socket_.sendTextMessage("JOIN #"+channel_+"\r\n"); emit statusChanged("ok","#"+channel_);
    });
    connect(&socket_,&QWebSocket::textMessageReceived,this,[this](const QString&payload){for(const auto&line:payload.split("\r\n",Qt::SkipEmptyParts))parseLine(line);});
    connect(&socket_,&QWebSocket::disconnected,this,[this]{if(!channel_.isEmpty()){emit statusChanged("warn","reconnecting");reconnect_.start(backoffMs_);backoffMs_=qMin(backoffMs_*2,30000);}});
    reconnect_.setSingleShot(true);connect(&reconnect_,&QTimer::timeout,this,[this]{socket_.open(QUrl("wss://irc-ws.chat.twitch.tv:443"));});
    viewerPoll_.setInterval(20000);connect(&viewerPoll_,&QTimer::timeout,this,&TwitchChatService::pollViewers);
}
void TwitchChatService::connectChannel(const QString&c){disconnectChannel();channel_=c.trimmed().toLower();if(channel_.isEmpty())return;emit statusChanged("connecting","connecting…");socket_.open(QUrl("wss://irc-ws.chat.twitch.tv:443"));viewerPoll_.start();pollViewers();}
void TwitchChatService::disconnectChannel(){reconnect_.stop();viewerPoll_.stop();channel_.clear();socket_.close();}
QHash<QString,QString>TwitchChatService::parseTags(QStringView s){QHash<QString,QString>o;for(const auto&part:s.split(';')){const auto at=part.indexOf('=');o.insert(part.left(at).toString(),ircUnescape(at<0?QString():part.mid(at+1).toString()));}return o;}
void TwitchChatService::parseLine(const QString&raw){if(raw.startsWith("PING")){socket_.sendTextMessage("PONG :tmi.twitch.tv\r\n");return;}QString line=raw;QHash<QString,QString>tags;if(line.startsWith('@')){const int p=line.indexOf(' ');tags=parseTags(QStringView(line).mid(1,p-1));line=line.mid(p+1);}QString prefix;if(line.startsWith(':')){const int p=line.indexOf(' ');prefix=line.mid(1,p-1);line=line.mid(p+1);}const int trailingAt=line.indexOf(" :");const QString trailing=trailingAt>=0?line.mid(trailingAt+2):QString();const auto fields=line.left(trailingAt<0?line.size():trailingAt).split(' ');if(fields.isEmpty()||fields[0]!="PRIVMSG")return;ChatMessage m;m.user=tags.value("display-name",prefix.section('!',0,0));m.text=trailing;m.platform="twitch";m.userId=tags.value("user-id");m.messageId=tags.value("id");m.color=QColor(tags.value("color"));if(!m.color.isValid())m.color=QColor::fromHsv(qHash(m.user)%360,180,240);for(const auto&badge:tags.value("badges").split(',',Qt::SkipEmptyParts)){const auto name=badge.section('/',0,0);const auto version=badge.section('/',1,1);static const QHash<QString,QString>labels{{"broadcaster","HOST"},{"moderator","MOD"},{"vip","VIP"},{"subscriber","SUB"},{"founder","SUB"},{"premium","PRIME"},{"partner","CHECK"}};if(labels.contains(name)){m.badges<<labels[name];m.badgeIds<<name+"/"+(version.isEmpty()?"1":version);}}m.metadata["room_id"]=tags.value("room-id");emit messageReceived(m);}
void TwitchChatService::pollViewers(){if(channel_.isEmpty())return;QNetworkRequest req(QUrl("https://gql.twitch.tv/gql"));req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");req.setRawHeader("Client-Id",TwitchGqlClient);QJsonObject body{{"query","query($login:String!){user(login:$login){stream{viewersCount}}}"},{"variables",QJsonObject{{"login",channel_}}}};auto*r=network_.post(req,QJsonDocument(body).toJson(QJsonDocument::Compact));connect(r,&QNetworkReply::finished,this,[this,r]{if(r->error()==QNetworkReply::NoError){auto stream=QJsonDocument::fromJson(r->readAll()).object()["data"].toObject()["user"].toObject()["stream"].toObject();if(stream.contains("viewersCount"))emit viewerCountChanged(stream["viewersCount"].toInt());}r->deleteLater();});}

TwitchModerationService::TwitchModerationService(QObject*p):QObject(p){}
void TwitchModerationService::configure(QString c,QString t,QString m){clientId_=std::move(c);token_=std::move(t);moderatorId_=std::move(m);autoModDenied_.clear();}
QNetworkRequest TwitchModerationService::request(const QUrl&u)const{QNetworkRequest r(u);r.setRawHeader("Client-Id",clientId_.toUtf8());r.setRawHeader("Authorization",("Bearer "+token_).toUtf8());r.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");return r;}
void TwitchModerationService::watch(QNetworkReply*r,const QString&a){connect(r,&QNetworkReply::finished,this,[this,r,a]{const bool ok=r->error()==QNetworkReply::NoError;emit actionFinished(a,ok,ok?QString():QString::fromUtf8(r->readAll()));r->deleteLater();});}
void TwitchModerationService::resolveBroadcaster(const QString&login){QUrl u("https://api.twitch.tv/helix/users");QUrlQuery q;q.addQueryItem("login",login);u.setQuery(q);auto*r=network_.get(request(u));connect(r,&QNetworkReply::finished,this,[this,r,login]{if(r->error()==QNetworkReply::NoError){auto a=QJsonDocument::fromJson(r->readAll()).object()["data"].toArray();if(!a.isEmpty())emit broadcasterResolved(login,a[0].toObject()["id"].toString());}r->deleteLater();});}
void TwitchModerationService::listBans(const QString&b){QUrl u("https://api.twitch.tv/helix/moderation/banned");QUrlQuery q;q.addQueryItem("broadcaster_id",b);q.addQueryItem("moderator_id",moderatorId_);q.addQueryItem("first","100");u.setQuery(q);auto*r=network_.get(request(u));connect(r,&QNetworkReply::finished,this,[this,r]{if(r->error()==QNetworkReply::NoError)emit bansReceived(QJsonDocument::fromJson(r->readAll()).object()["data"].toArray());else emit actionFinished("list_bans",false,r->errorString());r->deleteLater();});}
void TwitchModerationService::listUnbanRequests(const QString&b){QUrl u("https://api.twitch.tv/helix/moderation/unban_requests");QUrlQuery q;q.addQueryItem("broadcaster_id",b);q.addQueryItem("moderator_id",moderatorId_);q.addQueryItem("status","pending");q.addQueryItem("first","100");u.setQuery(q);auto*r=network_.get(request(u));connect(r,&QNetworkReply::finished,this,[this,r]{const QByteArray body=r->readAll();if(r->error()==QNetworkReply::NoError)emit unbanRequestsReceived(QJsonDocument::fromJson(body).object()["data"].toArray());else emit actionFinished("list_appeals",false,twitchError(body,r->errorString()));r->deleteLater();});}
void TwitchModerationService::ban(const QString&b,const QString&u,int seconds,const QString&reason){
    // Watching (or testing chat on) a channel you don't moderate is expected —
    // don't hammer Twitch, and don't keep re-warning the user, once we already
    // know a broadcaster has rejected our moderator status.
    if(autoModDenied_.contains(b))return;
    QUrl url("https://api.twitch.tv/helix/moderation/bans");QUrlQuery q;q.addQueryItem("broadcaster_id",b);q.addQueryItem("moderator_id",moderatorId_);url.setQuery(q);
    QJsonObject data{{"user_id",u},{"reason",reason.left(500)}};if(seconds>0)data["duration"]=seconds;
    auto*r=network_.post(request(url),QJsonDocument(QJsonObject{{"data",data}}).toJson(QJsonDocument::Compact));
    connect(r,&QNetworkReply::finished,this,[this,r,b]{
        const bool ok=r->error()==QNetworkReply::NoError;
        const int status=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString detail=ok?QString():QString::fromUtf8(r->readAll());
        if(!ok&&(status==401||status==403))autoModDenied_.insert(b);
        emit actionFinished("ban",ok,detail);
        r->deleteLater();
    });
}
void TwitchModerationService::unban(const QString&b,const QString&u){QUrl url("https://api.twitch.tv/helix/moderation/bans");QUrlQuery q;q.addQueryItem("broadcaster_id",b);q.addQueryItem("moderator_id",moderatorId_);q.addQueryItem("user_id",u);url.setQuery(q);watch(network_.deleteResource(request(url)),"unban");}
void TwitchModerationService::deleteMessage(const QString&b,const QString&m){QUrl url("https://api.twitch.tv/helix/moderation/chat");QUrlQuery q;q.addQueryItem("broadcaster_id",b);q.addQueryItem("moderator_id",moderatorId_);q.addQueryItem("message_id",m);url.setQuery(q);watch(network_.deleteResource(request(url)),"delete");}
void TwitchModerationService::sendMessage(const QString&b,const QString&t){QUrl url("https://api.twitch.tv/helix/chat/messages");watch(network_.post(request(url),QJsonDocument(QJsonObject{{"broadcaster_id",b},{"sender_id",moderatorId_},{"message",t.left(500)}}).toJson(QJsonDocument::Compact)),"send");}
void TwitchModerationService::createClip(const QString&b){
    QUrl url("https://api.twitch.tv/helix/clips");QUrlQuery q;q.addQueryItem("broadcaster_id",b);url.setQuery(q);
    auto*r=network_.post(request(url),QByteArray());
    connect(r,&QNetworkReply::finished,this,[this,r]{
        const bool ok=r->error()==QNetworkReply::NoError;
        const QByteArray body=r->readAll();
        if(ok){
            const auto data=QJsonDocument::fromJson(body).object()["data"].toArray();
            if(!data.isEmpty()){
                const auto clip=data[0].toObject();
                emit clipCreated(clip["id"].toString(),QUrl(clip["edit_url"].toString()));
                emit actionFinished("clip",true,QString());
                r->deleteLater();
                return;
            }
        }
        emit actionFinished("clip",false,ok?QStringLiteral("Twitch didn't return a clip. Make sure the channel is live."):twitchError(body,r->errorString()));
        r->deleteLater();
    });
}

StreamlabsService::StreamlabsService(QObject*p):QObject(p){connect(&socket_,&QWebSocket::connected,this,[this]{emit statusChanged("ok","Streamlabs connected");});connect(&socket_,&QWebSocket::textMessageReceived,this,&StreamlabsService::parseSocketIoFrame);connect(&socket_,&QWebSocket::disconnected,this,[this]{if(!token_.isEmpty()){emit statusChanged("warn","Streamlabs reconnecting…");reconnect_.start(3000);}});reconnect_.setSingleShot(true);connect(&reconnect_,&QTimer::timeout,this,[this]{connectToken(token_);});}
void StreamlabsService::connectToken(const QString&t){token_=t.trimmed();if(token_.isEmpty())return;QUrl u("wss://sockets.streamlabs.com/socket.io/");QUrlQuery q;q.addQueryItem("token",token_);q.addQueryItem("EIO","3");q.addQueryItem("transport","websocket");u.setQuery(q);emit statusChanged("connecting","Connecting to Streamlabs…");socket_.open(u);}
void StreamlabsService::disconnectService(){token_.clear();reconnect_.stop();socket_.close();}
void StreamlabsService::parseSocketIoFrame(const QString&f){if(f.startsWith('0')){socket_.sendTextMessage("40");return;}if(f=="2"){socket_.sendTextMessage("3");return;}if(!f.startsWith("42"))return;auto doc=QJsonDocument::fromJson(f.mid(2).toUtf8());auto a=doc.array();if(a.size()<2||a[0].toString()!="event")return;for(const auto&e:normalize(a[1].toObject()))emit eventReceived(e);}
QList<StreamEvent> StreamlabsService::normalize(const QJsonObject&p){QList<StreamEvent>out;const QString type=p["type"].toString().toLower(),target=p["for"].toString("streamlabs").toLower();QJsonArray items=p["message"].isArray()?p["message"].toArray():QJsonArray{p["message"]};for(const auto&v:items){const auto i=v.toObject();StreamEvent e;if(type=="donation"){e.kind="donation";e.platform="streamlabs";}else if(target=="twitch_account"&&type=="follow"){e.kind="twitch_follow";e.platform="twitch";}else if(target=="twitch_account"&&type=="subscription"){e.kind="twitch_subscription";e.platform="twitch";}else if(target=="youtube_account"&&type=="follow"){e.kind="youtube_subscription";e.platform="youtube";}else if(target=="youtube_account"&&type=="subscription"){e.kind="youtube_member";e.platform="youtube";}else if(target=="youtube_account"&&type=="superchat"){e.kind="youtube_superchat";e.platform="youtube";}else if(target=="twitch_account"&&(type=="bits"||type=="raid"||type=="host")){e.kind="twitch_"+type;e.platform="twitch";}else continue;e.user=i.value("name").toString(i.value("from").toString("Someone"));e.amount=i.value("formatted_amount").toVariant().toString();if(e.amount.isEmpty())e.amount=i.value("amount").toVariant().toString();e.message=i.value("message").toString(i.value("comment").toString());e.eventId=p.value("event_id").toString(i.value("_id").toString(i.value("id").toString()));e.raw=i;out<<e;}return out;}
