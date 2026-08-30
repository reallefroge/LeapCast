#include "LiveServices.hpp"
#include <algorithm>
#include <functional>

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
    scopes_=QStringLiteral("chat:read chat:edit moderation:read moderator:read:banned_users moderator:manage:banned_users moderator:read:chat_messages moderator:manage:chat_messages moderator:read:unban_requests moderator:manage:unban_requests user:write:chat clips:edit channel:read:redemptions moderator:read:followers channel:read:subscriptions user:manage:blocked_users user:read:blocked_users");
    pollTimer_.setSingleShot(true);
    connect(&pollTimer_,&QTimer::timeout,this,&TwitchAuthService::pollForToken);
    refreshTimer_.setSingleShot(true);
    connect(&refreshTimer_,&QTimer::timeout,this,&TwitchAuthService::refreshToken);
}

void TwitchAuthService::authorize(const QString&clientId){
    clientId_=clientId.trimmed();deviceCode_.clear();accessToken_.clear();refreshToken_.clear();
    pollTimer_.stop();refreshTimer_.stop();
    if(clientId_.isEmpty()){finishWithError(QStringLiteral("No Twitch application Client ID is set. Paste one into \u201cTwitch application Client ID\u201d on this Keys page, then select Connect again."));return;}
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
        QStringList grantedScopes;for(const auto&value:object.value("scopes").toArray())grantedScopes<<value.toString();
        emit scopesValidated(grantedScopes);
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
void TwitchChatService::disconnectChannel(){reconnect_.stop();viewerPoll_.stop();channel_.clear();socket_.close();wasLive_=false;}
QHash<QString,QString>TwitchChatService::parseTags(QStringView s){QHash<QString,QString>o;for(const auto&part:s.split(';')){const auto at=part.indexOf('=');o.insert(part.left(at).toString(),ircUnescape(at<0?QString():part.mid(at+1).toString()));}return o;}
void TwitchChatService::parseLine(const QString&raw){if(raw.startsWith("PING")){socket_.sendTextMessage("PONG :tmi.twitch.tv\r\n");return;}QString line=raw;QHash<QString,QString>tags;if(line.startsWith('@')){const int p=line.indexOf(' ');tags=parseTags(QStringView(line).mid(1,p-1));line=line.mid(p+1);}QString prefix;if(line.startsWith(':')){const int p=line.indexOf(' ');prefix=line.mid(1,p-1);line=line.mid(p+1);}const int trailingAt=line.indexOf(" :");const QString trailing=trailingAt>=0?line.mid(trailingAt+2):QString();const auto fields=line.left(trailingAt<0?line.size():trailingAt).split(' ');if(fields.isEmpty()||fields[0]!="PRIVMSG")return;ChatMessage m;m.user=tags.value("display-name",prefix.section('!',0,0));m.text=trailing;m.platform="twitch";m.userId=tags.value("user-id");m.messageId=tags.value("id");m.color=QColor(tags.value("color"));if(!m.color.isValid())m.color=QColor::fromHsv(qHash(m.user)%360,180,240);for(const auto&badge:tags.value("badges").split(',',Qt::SkipEmptyParts)){const auto name=badge.section('/',0,0);const auto version=badge.section('/',1,1);static const QHash<QString,QString>labels{{"broadcaster","HOST"},{"moderator","MOD"},{"vip","VIP"},{"subscriber","SUB"},{"founder","SUB"},{"premium","PRIME"},{"partner","CHECK"}};if(labels.contains(name)){m.badges<<labels[name];m.badgeIds<<name+"/"+(version.isEmpty()?"1":version);}}m.metadata["room_id"]=tags.value("room-id");m.metadata["emotes"]=tags.value("emotes");m.metadata["badge_info"]=tags.value("badge-info");const QString rewardId=tags.value("custom-reward-id");if(!rewardId.isEmpty())m.metadata["custom_reward_id"]=rewardId;emit messageReceived(m);}
void TwitchChatService::pollViewers(){if(channel_.isEmpty())return;QNetworkRequest req(QUrl("https://gql.twitch.tv/gql"));req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");req.setRawHeader("Client-Id",TwitchGqlClient);QJsonObject body{{"query","query($login:String!){user(login:$login){stream{viewersCount}}}"},{"variables",QJsonObject{{"login",channel_}}}};auto*r=network_.post(req,QJsonDocument(body).toJson(QJsonDocument::Compact));connect(r,&QNetworkReply::finished,this,[this,r]{if(r->error()==QNetworkReply::NoError){auto stream=QJsonDocument::fromJson(r->readAll()).object()["data"].toObject()["user"].toObject()["stream"].toObject();const bool live=stream.contains("viewersCount");if(live)emit viewerCountChanged(stream["viewersCount"].toInt());if(live&&!wasLive_)emit broadcastWentLive();wasLive_=live;}r->deleteLater();});}

TwitchModerationService::TwitchModerationService(QObject*p):QObject(p){}
void TwitchModerationService::configure(QString c,QString t,QString m){clientId_=std::move(c);token_=std::move(t);moderatorId_=std::move(m);autoModDenied_.clear();pinReadDenied_.clear();}
QNetworkRequest TwitchModerationService::request(const QUrl&u)const{QNetworkRequest r(u);r.setRawHeader("Client-Id",clientId_.toUtf8());r.setRawHeader("Authorization",("Bearer "+token_).toUtf8());r.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");return r;}
void TwitchModerationService::watch(QNetworkReply*r,const QString&a){connect(r,&QNetworkReply::finished,this,[this,r,a]{const bool ok=r->error()==QNetworkReply::NoError;emit actionFinished(a,ok,ok?QString():QString::fromUtf8(r->readAll()));r->deleteLater();});}
void TwitchModerationService::resolveBroadcaster(const QString&login){QUrl u("https://api.twitch.tv/helix/users");QUrlQuery q;q.addQueryItem("login",login);u.setQuery(q);auto*r=network_.get(request(u));connect(r,&QNetworkReply::finished,this,[this,r,login]{if(r->error()==QNetworkReply::NoError){auto a=QJsonDocument::fromJson(r->readAll()).object()["data"].toArray();if(!a.isEmpty())emit broadcasterResolved(login,a[0].toObject()["id"].toString());}r->deleteLater();});}
void TwitchModerationService::listBans(const QString&b){QUrl u("https://api.twitch.tv/helix/moderation/banned");QUrlQuery q;q.addQueryItem("broadcaster_id",b);q.addQueryItem("moderator_id",moderatorId_);q.addQueryItem("first","100");u.setQuery(q);auto*r=network_.get(request(u));connect(r,&QNetworkReply::finished,this,[this,r]{if(r->error()==QNetworkReply::NoError)emit bansReceived(QJsonDocument::fromJson(r->readAll()).object()["data"].toArray());else emit actionFinished("list_bans",false,r->errorString());r->deleteLater();});}
void TwitchModerationService::listUnbanRequests(const QString&b){QUrl u("https://api.twitch.tv/helix/moderation/unban_requests");QUrlQuery q;q.addQueryItem("broadcaster_id",b);q.addQueryItem("moderator_id",moderatorId_);q.addQueryItem("status","pending");q.addQueryItem("first","100");u.setQuery(q);auto*r=network_.get(request(u));connect(r,&QNetworkReply::finished,this,[this,r]{const QByteArray body=r->readAll();if(r->error()==QNetworkReply::NoError)emit unbanRequestsReceived(QJsonDocument::fromJson(body).object()["data"].toArray());else emit actionFinished("list_appeals",false,twitchError(body,r->errorString()));r->deleteLater();});}
void TwitchModerationService::resolveUnbanRequest(const QString&b,const QString&id,bool approved,const QString&text){
    QUrl u("https://api.twitch.tv/helix/moderation/unban_requests");QUrlQuery q;
    q.addQueryItem("broadcaster_id",b);q.addQueryItem("moderator_id",moderatorId_);
    q.addQueryItem("unban_request_id",id);q.addQueryItem("status",approved?"approved":"denied");
    if(!text.isEmpty())q.addQueryItem("resolution_text",text.left(500));u.setQuery(q);
    watch(network_.sendCustomRequest(request(u),"PATCH"),approved?"approve_appeal":"deny_appeal");
}
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
void TwitchModerationService::fetchUserCard(const QString&broadcasterId,const QString&userId){
    if(userId.isEmpty()||token_.isEmpty())return;
    pendingCards_[userId]=QJsonObject{{QStringLiteral("user_id"),userId}};
    // Four independent lookups. Every one of them is allowed to fail: a
    // channel's own follower total needs permission on THAT channel, and the
    // follow/subscription checks need scopes the streamer may not have granted
    // yet. The card shows whatever came back.
    pendingCardParts_[userId]=4;
    const auto settle=[this,userId](const QString& key,const QJsonValue& value){
        if(!pendingCards_.contains(userId))return;
        if(!value.isNull()&&!value.isUndefined())pendingCards_[userId].insert(key,value);
        if(--pendingCardParts_[userId]>0)return;
        const QJsonObject card=pendingCards_.take(userId);
        pendingCardParts_.remove(userId);
        emit userCardReady(userId,card);
    };
    const auto call=[this,settle](const QUrl& url,const QString& key,std::function<QJsonValue(const QJsonObject&)> pick){
        auto* reply=network_.get(request(url));
        connect(reply,&QNetworkReply::finished,this,[reply,key,pick,settle]{
            const QByteArray bytes=reply->readAll();
            const bool ok=reply->error()==QNetworkReply::NoError;
            reply->deleteLater();
            settle(key,ok?pick(QJsonDocument::fromJson(bytes).object()):QJsonValue());
        });
    };
    const auto firstEntry=[](const QJsonObject& root){return root.value(QStringLiteral("data")).toArray().isEmpty()?QJsonObject():root.value(QStringLiteral("data")).toArray().first().toObject();};

    QUrl users(QStringLiteral("https://api.twitch.tv/helix/users"));
    {QUrlQuery q;q.addQueryItem(QStringLiteral("id"),userId);users.setQuery(q);}
    call(users,QStringLiteral("account"),[firstEntry](const QJsonObject& root)->QJsonValue{
        const QJsonObject entry=firstEntry(root);return entry.isEmpty()?QJsonValue():QJsonValue(entry);});

    // The chatter's own follower total. Twitch only returns "total" here to the
    // broadcaster or one of their moderators, so for a random chatter this
    // normally comes back refused and the card just omits the line.
    QUrl theirFollowers(QStringLiteral("https://api.twitch.tv/helix/channels/followers"));
    {QUrlQuery q;q.addQueryItem(QStringLiteral("broadcaster_id"),userId);q.addQueryItem(QStringLiteral("first"),QStringLiteral("1"));theirFollowers.setQuery(q);}
    call(theirFollowers,QStringLiteral("followers"),[](const QJsonObject& root)->QJsonValue{
        return root.contains(QStringLiteral("total"))?root.value(QStringLiteral("total")):QJsonValue();});

    if(broadcasterId.isEmpty()){settle(QStringLiteral("following"),QJsonValue());settle(QStringLiteral("subscription"),QJsonValue());return;}

    QUrl following(QStringLiteral("https://api.twitch.tv/helix/channels/followers"));
    {QUrlQuery q;q.addQueryItem(QStringLiteral("broadcaster_id"),broadcasterId);q.addQueryItem(QStringLiteral("user_id"),userId);following.setQuery(q);}
    call(following,QStringLiteral("following"),[firstEntry](const QJsonObject& root)->QJsonValue{
        const QJsonObject entry=firstEntry(root);return entry.isEmpty()?QJsonValue():QJsonValue(entry.value(QStringLiteral("followed_at")));});

    QUrl subscription(QStringLiteral("https://api.twitch.tv/helix/subscriptions"));
    {QUrlQuery q;q.addQueryItem(QStringLiteral("broadcaster_id"),broadcasterId);q.addQueryItem(QStringLiteral("user_id"),userId);subscription.setQuery(q);}
    call(subscription,QStringLiteral("subscription"),[firstEntry](const QJsonObject& root)->QJsonValue{
        const QJsonObject entry=firstEntry(root);return entry.isEmpty()?QJsonValue():QJsonValue(entry);});
}

void TwitchModerationService::setUserBlocked(const QString&userId,bool blocked){
    if(userId.isEmpty()||token_.isEmpty())return;
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/users/blocks"));
    QUrlQuery q;q.addQueryItem(QStringLiteral("target_user_id"),userId);url.setQuery(q);
    watch(blocked?network_.put(request(url),QByteArray()):network_.deleteResource(request(url)),
          blocked?QStringLiteral("block"):QStringLiteral("unblock"));
}

void TwitchModerationService::getPinnedMessage(const QString&b){
    if(b.isEmpty()||moderatorId_.isEmpty()||token_.isEmpty()||pinReadDenied_.contains(b))return;
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/chat/pins"));QUrlQuery q;
    q.addQueryItem(QStringLiteral("broadcaster_id"),b);q.addQueryItem(QStringLiteral("moderator_id"),moderatorId_);url.setQuery(q);
    auto*r=network_.get(request(url));
    connect(r,&QNetworkReply::finished,this,[this,r,b]{
        const QByteArray body=r->readAll();const int status=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if(r->error()!=QNetworkReply::NoError){if(status==401||status==403)pinReadDenied_.insert(b);r->deleteLater();return;}
        const auto data=QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray();
        if(data.isEmpty()){emit pinnedMessageChanged(ChatMessage{},false);r->deleteLater();return;}
        const auto pin=data.first().toObject();ChatMessage m;m.platform=QStringLiteral("twitch");
        m.user=pin.value(QStringLiteral("sender_user_name")).toString(pin.value(QStringLiteral("sender_user_login")).toString(QStringLiteral("viewer")));
        m.userId=pin.value(QStringLiteral("sender_user_id")).toString();m.messageId=pin.value(QStringLiteral("message_id")).toString();
        m.text=pin.value(QStringLiteral("message")).toObject().value(QStringLiteral("text")).toString();m.color=QColor(QStringLiteral("#9146ff"));
        m.metadata={{QStringLiteral("room_id"),b},{QStringLiteral("pinned"),true},{QStringLiteral("pinned_by"),pin.value(QStringLiteral("pinned_by_user_name")).toString()},{QStringLiteral("pinned_starts_at"),pin.value(QStringLiteral("starts_at")).toString()},{QStringLiteral("pinned_ends_at"),pin.value(QStringLiteral("ends_at")).toString()},{QStringLiteral("pinned_updated_at"),pin.value(QStringLiteral("updated_at")).toString()}};
        emit pinnedMessageChanged(m,true);r->deleteLater();
    });
}
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

TwitchEventSubService::TwitchEventSubService(QObject*p):QObject(p){
    reconnect_.setSingleShot(true);
    connect(&reconnect_,&QTimer::timeout,this,[this]{open(reconnectUrl_.isValid()?reconnectUrl_:QUrl(QStringLiteral("wss://eventsub.wss.twitch.tv/ws")));});
    connect(&socket_,&QWebSocket::textMessageReceived,this,&TwitchEventSubService::parseMessage);
    connect(&socket_,&QWebSocket::disconnected,this,[this]{
        if(!broadcasterId_.isEmpty()&&!transferringSession_){emit statusChanged(QStringLiteral("warn"),QStringLiteral("Twitch rewards reconnecting…"));reconnect_.start(3000);}
    });
}
void TwitchEventSubService::connectRedemptions(const QString&clientId,const QString&accessToken,const QString&broadcasterId){
    const bool unchanged=clientId_==clientId.trimmed()&&token_==accessToken.trimmed()&&broadcasterId_==broadcasterId.trimmed()&&socket_.state()!=QAbstractSocket::UnconnectedState;
    if(unchanged)return;
    disconnectService();clientId_=clientId.trimmed();token_=accessToken.trimmed();broadcasterId_=broadcasterId.trimmed();
    if(clientId_.isEmpty()||token_.isEmpty()||broadcasterId_.isEmpty())return;
    emit statusChanged(QStringLiteral("connecting"),QStringLiteral("Connecting Twitch channel-point rewards…"));open();
}
void TwitchEventSubService::disconnectService(){reconnect_.stop();broadcasterId_.clear();reconnectUrl_.clear();transferringSession_=false;socket_.close();}
void TwitchEventSubService::open(const QUrl&url){socket_.open(url);}
void TwitchEventSubService::parseMessage(const QString&message){
    const auto root=QJsonDocument::fromJson(message.toUtf8()).object();const auto metadata=root.value(QStringLiteral("metadata")).toObject();const auto payload=root.value(QStringLiteral("payload")).toObject();const QString type=metadata.value(QStringLiteral("message_type")).toString();
    if(type==QStringLiteral("session_welcome")){
        const QString sessionId=payload.value(QStringLiteral("session")).toObject().value(QStringLiteral("id")).toString();
        if(!transferringSession_)createRedemptionSubscription(sessionId);else{transferringSession_=false;reconnectUrl_.clear();emit statusChanged(QStringLiteral("ok"),QStringLiteral("Twitch channel-point rewards connected"));}
        return;
    }
    if(type==QStringLiteral("session_reconnect")){
        reconnectUrl_=QUrl(payload.value(QStringLiteral("session")).toObject().value(QStringLiteral("reconnect_url")).toString());
        if(reconnectUrl_.isValid()){transferringSession_=true;socket_.close();QTimer::singleShot(0,this,[this]{open(reconnectUrl_);});}return;
    }
    if(type==QStringLiteral("revocation")){emit statusChanged(QStringLiteral("warn"),QStringLiteral("Twitch channel-point permission was revoked. Reauthorize Twitch."));return;}
    if(type!=QStringLiteral("notification"))return;
    const auto subscription=payload.value(QStringLiteral("subscription")).toObject();
    if(subscription.value(QStringLiteral("type")).toString()!=QStringLiteral("channel.channel_points_custom_reward_redemption.add"))return;
    const auto value=payload.value(QStringLiteral("event")).toObject();const auto reward=value.value(QStringLiteral("reward")).toObject();
    StreamEvent event;event.eventId=value.value(QStringLiteral("id")).toString();event.kind=QStringLiteral("twitch_redemption");event.platform=QStringLiteral("twitch");event.user=value.value(QStringLiteral("user_name")).toString(value.value(QStringLiteral("user_login")).toString(QStringLiteral("Someone")));event.amount=reward.value(QStringLiteral("title")).toString(QStringLiteral("Channel Point reward"));event.message=value.value(QStringLiteral("user_input")).toString();event.raw=value;emit eventReceived(event);
}
void TwitchEventSubService::createRedemptionSubscription(const QString&sessionId){
    if(sessionId.isEmpty())return;QNetworkRequest request(QUrl(QStringLiteral("https://api.twitch.tv/helix/eventsub/subscriptions")));request.setRawHeader("Client-Id",clientId_.toUtf8());request.setRawHeader("Authorization",("Bearer "+token_).toUtf8());request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));
    const QJsonObject body{{QStringLiteral("type"),QStringLiteral("channel.channel_points_custom_reward_redemption.add")},{QStringLiteral("version"),QStringLiteral("1")},{QStringLiteral("condition"),QJsonObject{{QStringLiteral("broadcaster_user_id"),broadcasterId_}}},{QStringLiteral("transport"),QJsonObject{{QStringLiteral("method"),QStringLiteral("websocket")},{QStringLiteral("session_id"),sessionId}}}};
    auto*reply=network_.post(request,QJsonDocument(body).toJson(QJsonDocument::Compact));connect(reply,&QNetworkReply::finished,this,[this,reply]{const QByteArray payload=reply->readAll();if(reply->error()==QNetworkReply::NoError)emit statusChanged(QStringLiteral("ok"),QStringLiteral("Twitch channel-point rewards connected"));else{const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();emit statusChanged(QStringLiteral("warn"),status==401||status==403?QStringLiteral("Reauthorize Twitch to enable channel-point rewards."):twitchError(payload,reply->errorString()));}reply->deleteLater();});
}

StreamlabsService::StreamlabsService(QObject*p):QObject(p){connect(&socket_,&QWebSocket::connected,this,[this]{emit statusChanged("ok","Streamlabs connected");});connect(&socket_,&QWebSocket::textMessageReceived,this,&StreamlabsService::parseSocketIoFrame);connect(&socket_,&QWebSocket::disconnected,this,[this]{if(!token_.isEmpty()){emit statusChanged("warn","Streamlabs reconnecting…");reconnect_.start(3000);}});reconnect_.setSingleShot(true);connect(&reconnect_,&QTimer::timeout,this,[this]{connectToken(token_);});}
void StreamlabsService::connectToken(const QString&t){token_=t.trimmed();if(token_.isEmpty())return;QUrl u("wss://sockets.streamlabs.com/socket.io/");QUrlQuery q;q.addQueryItem("token",token_);q.addQueryItem("EIO","3");q.addQueryItem("transport","websocket");u.setQuery(q);emit statusChanged("connecting","Connecting to Streamlabs…");socket_.open(u);}
void StreamlabsService::disconnectService(){token_.clear();reconnect_.stop();socket_.close();}
void StreamlabsService::parseSocketIoFrame(const QString&f){if(f.startsWith('0')){socket_.sendTextMessage("40");return;}if(f=="2"){socket_.sendTextMessage("3");return;}if(!f.startsWith("42"))return;auto doc=QJsonDocument::fromJson(f.mid(2).toUtf8());auto a=doc.array();if(a.size()<2||a[0].toString()!="event")return;for(const auto&e:normalize(a[1].toObject()))emit eventReceived(e);}
QList<StreamEvent> StreamlabsService::normalize(const QJsonObject&p){QList<StreamEvent>out;const QString type=p["type"].toString().toLower(),target=p["for"].toString("streamlabs").toLower();QJsonArray items=p["message"].isArray()?p["message"].toArray():QJsonArray{p["message"]};for(const auto&v:items){const auto i=v.toObject();StreamEvent e;if(type=="donation"){e.kind="donation";e.platform="streamlabs";}else if(target=="twitch_account"&&type=="follow"){e.kind="twitch_follow";e.platform="twitch";}else if(target=="twitch_account"&&type=="subscription"){e.kind="twitch_subscription";e.platform="twitch";}else if(target=="youtube_account"&&type=="follow"){e.kind="youtube_subscription";e.platform="youtube";}else if(target=="youtube_account"&&type=="subscription"){e.kind="youtube_member";e.platform="youtube";}else if(target=="youtube_account"&&type=="superchat"){e.kind="youtube_superchat";e.platform="youtube";}else if(target=="twitch_account"&&(type=="bits"||type=="raid"||type=="host")){e.kind="twitch_"+type;e.platform="twitch";}else continue;e.user=i.value("name").toString(i.value("from").toString("Someone"));e.amount=i.value("formatted_amount").toVariant().toString();if(e.amount.isEmpty())e.amount=i.value("amount").toVariant().toString();e.message=i.value("message").toString(i.value("comment").toString());e.eventId=p.value("event_id").toString(i.value("_id").toString(i.value("id").toString()));e.raw=i;out<<e;}return out;}

TwitchEmoteService::TwitchEmoteService(QObject*p):QObject(p){}

void TwitchEmoteService::loadForChannel(const QString& broadcasterId){
    if(broadcasterId.isEmpty()||broadcasterId==broadcasterId_)return;
    broadcasterId_=broadcasterId;words_.clear();loaded_=false;
    // Globals first so a channel emote of the same name wins by overwriting.
    fetch(QUrl(QStringLiteral("https://api.betterttv.net/3/cached/emotes/global")),QStringLiteral("bttv"));
    fetch(QUrl(QStringLiteral("https://api.betterttv.net/3/cached/users/twitch/%1").arg(broadcasterId)),QStringLiteral("bttv"));
    fetch(QUrl(QStringLiteral("https://api.frankerfacez.com/v1/set/global")),QStringLiteral("ffz"));
    fetch(QUrl(QStringLiteral("https://api.frankerfacez.com/v1/room/id/%1").arg(broadcasterId)),QStringLiteral("ffz"));
    fetch(QUrl(QStringLiteral("https://7tv.io/v3/emote-sets/global")),QStringLiteral("7tv"));
    fetch(QUrl(QStringLiteral("https://7tv.io/v3/users/twitch/%1").arg(broadcasterId)),QStringLiteral("7tv"));
}

void TwitchEmoteService::fetch(const QUrl& url,const QString& source){
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent","LeapcastStudio");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply=network_.get(request);
    connect(reply,&QNetworkReply::finished,this,[this,reply,source]{
        const QByteArray bytes=reply->readAll();
        const bool ok=reply->error()==QNetworkReply::NoError;
        reply->deleteLater();
        // A channel with no BTTV/FFZ/7TV presence 404s, which is normal and
        // must not stop the other providers from loading.
        if(!ok)return;
        const QJsonDocument document=QJsonDocument::fromJson(bytes);
        if(source==QStringLiteral("bttv"))absorbBttv(document);
        else if(source==QStringLiteral("ffz"))absorbFfz(document);
        else absorbSeventv(document);
        loaded_=true;
        emit wordsUpdated(words_.size());
    });
}

void TwitchEmoteService::absorbBttv(const QJsonDocument& document){
    const auto take=[this](const QJsonArray& list){
        for(const auto& value:list){
            const QJsonObject emote=value.toObject();
            const QString code=emote.value(QStringLiteral("code")).toString();
            const QString id=emote.value(QStringLiteral("id")).toString();
            if(code.isEmpty()||id.isEmpty())continue;
            words_.insert(code,QStringLiteral("https://cdn.betterttv.net/emote/%1/2x").arg(id));
        }
    };
    if(document.isArray()){take(document.array());return;}
    const QJsonObject root=document.object();
    take(root.value(QStringLiteral("channelEmotes")).toArray());
    take(root.value(QStringLiteral("sharedEmotes")).toArray());
}

void TwitchEmoteService::absorbFfz(const QJsonDocument& document){
    const QJsonObject sets=document.object().value(QStringLiteral("sets")).toObject();
    for(const QString& key:sets.keys()){
        for(const auto& value:sets.value(key).toObject().value(QStringLiteral("emoticons")).toArray()){
            const QJsonObject emote=value.toObject();
            const QString name=emote.value(QStringLiteral("name")).toString();
            const QJsonObject urls=emote.value(QStringLiteral("urls")).toObject();
            // Prefer 2x, fall back to whatever size the set actually has.
            QString url=urls.value(QStringLiteral("2")).toString();
            if(url.isEmpty())url=urls.value(QStringLiteral("1")).toString();
            if(url.isEmpty())url=urls.value(QStringLiteral("4")).toString();
            if(name.isEmpty()||url.isEmpty())continue;
            if(url.startsWith(QStringLiteral("//")))url.prepend(QStringLiteral("https:"));
            words_.insert(name,url);
        }
    }
}

void TwitchEmoteService::absorbSeventv(const QJsonDocument& document){
    const QJsonObject root=document.object();
    QJsonArray emotes=root.value(QStringLiteral("emotes")).toArray();
    if(emotes.isEmpty())emotes=root.value(QStringLiteral("emote_set")).toObject().value(QStringLiteral("emotes")).toArray();
    for(const auto& value:emotes){
        const QJsonObject emote=value.toObject();
        const QString name=emote.value(QStringLiteral("name")).toString();
        const QJsonObject host=emote.value(QStringLiteral("data")).toObject().value(QStringLiteral("host")).toObject();
        QString base=host.value(QStringLiteral("url")).toString();
        if(name.isEmpty()||base.isEmpty())continue;
        if(base.startsWith(QStringLiteral("//")))base.prepend(QStringLiteral("https:"));
        // Pick a static, widely supported file rather than the animated AVIF
        // the host lists first, since QImage cannot decode AVIF.
        QString file=QStringLiteral("2x.webp");
        for(const auto& candidate:host.value(QStringLiteral("files")).toArray()){
            const QJsonObject entry=candidate.toObject();
            const QString fileName=entry.value(QStringLiteral("name")).toString();
            if(fileName==QStringLiteral("2x.png")){file=fileName;break;}
            if(fileName==QStringLiteral("2x.webp"))file=fileName;
        }
        words_.insert(name,base+QLatin1Char('/')+file);
    }
}

QJsonArray TwitchEmoteService::buildRuns(const QString& text,const QString& emotesTag) const {
    if(text.isEmpty())return {};
    // Twitch indexes its ranges by code point, so work in code points.
    const QList<uint> points=text.toUcs4();
    struct Range { int start; int end; QString id; };
    QList<Range> ranges;
    for(const QString& block:emotesTag.split(QLatin1Char('/'),Qt::SkipEmptyParts)){
        const int colon=block.indexOf(QLatin1Char(':'));
        if(colon<=0)continue;
        const QString id=block.left(colon);
        for(const QString& span:block.mid(colon+1).split(QLatin1Char(','),Qt::SkipEmptyParts)){
            const int dash=span.indexOf(QLatin1Char('-'));
            if(dash<=0)continue;
            bool startOk=false,endOk=false;
            const int start=span.left(dash).toInt(&startOk);
            const int end=span.mid(dash+1).toInt(&endOk);
            if(!startOk||!endOk||start<0||end<start||end>=points.size())continue;
            ranges.append({start,end,id});
        }
    }
    std::sort(ranges.begin(),ranges.end(),[](const Range&a,const Range&b){return a.start<b.start;});

    const auto slice=[&points](int from,int to){
        if(to<=from)return QString();
        return QString::fromUcs4(points.constData()+from,to-from);
    };
    // Third-party emotes are whole words inside the plain stretches only, so a
    // word inside a Twitch emote's range is never re-matched.
    const auto appendPlain=[this](QJsonArray& runs,const QString& plain){
        if(plain.isEmpty())return;
        if(words_.isEmpty()){runs.append(QJsonObject{{QStringLiteral("text"),plain}});return;}
        QString buffer;
        const QStringList pieces=plain.split(QLatin1Char(' '));
        for(int i=0;i<pieces.size();++i){
            const QString& word=pieces.at(i);
            const auto match=words_.constFind(word);
            if(match!=words_.constEnd()){
                if(!buffer.isEmpty()){runs.append(QJsonObject{{QStringLiteral("text"),buffer}});buffer.clear();}
                runs.append(QJsonObject{{QStringLiteral("url"),match.value()},{QStringLiteral("alt"),word}});
                if(i+1<pieces.size())buffer+=QLatin1Char(' ');
                continue;
            }
            buffer+=word;
            if(i+1<pieces.size())buffer+=QLatin1Char(' ');
        }
        if(!buffer.isEmpty())runs.append(QJsonObject{{QStringLiteral("text"),buffer}});
    };

    QJsonArray runs;
    int cursor=0;
    for(const Range& range:ranges){
        if(range.start<cursor)continue;   // overlapping ranges: keep the first
        appendPlain(runs,slice(cursor,range.start));
        runs.append(QJsonObject{{QStringLiteral("url"),QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/%1/default/dark/2.0").arg(range.id)},
                                {QStringLiteral("alt"),slice(range.start,range.end+1)}});
        cursor=range.end+1;
    }
    appendPlain(runs,slice(cursor,points.size()));
    // Nothing to show as a picture: let the caller keep the plain text path.
    bool hasImage=false;
    for(const auto& value:runs)if(!value.toObject().contains(QStringLiteral("text"))){hasImage=true;break;}
    return hasImage?runs:QJsonArray{};
}
