#include "PlatformServices.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>

namespace { const QByteArray UA="Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/125 Safari/537.36";
QNetworkRequest webReq(const QUrl&u){
    QNetworkRequest r(u);
    r.setRawHeader("User-Agent",UA);
    r.setRawHeader("Accept-Language","en-US,en;q=0.9");
    // Without this, YouTube serves a cookie-consent interstitial instead of
    // real page content to some regions, which has no video/live markers for
    // liveIds() to find — the channel then looks permanently offline even
    // while live, until a direct video/live link (which skips this page) is
    // pasted in instead.
    r.setRawHeader("Cookie","CONSENT=YES+1");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    return r;
}
QString capture(const QByteArray&b,const QString&p){auto m=QRegularExpression(p).match(QString::fromUtf8(b));return m.hasMatch()?m.captured(1):QString();}}

YouTubeChatService::YouTubeChatService(QString p,bool v,QObject*o):QObject(o),platform_(std::move(p)),verticalOnly_(v){retry_.setSingleShot(true);connect(&retry_,&QTimer::timeout,this,&YouTubeChatService::resolveTarget);pollTimer_.setSingleShot(true);connect(&pollTimer_,&QTimer::timeout,this,&YouTubeChatService::poll);viewerTimer_.setInterval(12000);connect(&viewerTimer_,&QTimer::timeout,this,&YouTubeChatService::pollViewers);}
void YouTubeChatService::connectTarget(const QString&t){disconnectService();target_=t.trimmed();QRegularExpression re("(?:watch\\?v=|youtu\\.be/|/live/|/shorts/|/embed/)([A-Za-z0-9_-]{11})");auto m=re.match(target_);if(m.hasMatch()){videoId_=m.captured(1);explicitVideo_=true;}else if(QRegularExpression("^[A-Za-z0-9_-]{11}$").match(target_).hasMatch()){videoId_=target_;explicitVideo_=true;}else{explicitVideo_=false;if(target_.startsWith('@'))target_="https://www.youtube.com/"+target_;}emit statusChanged("connecting","looking for stream…");resolveTarget();}
void YouTubeChatService::disconnectService(){retry_.stop();pollTimer_.stop();viewerTimer_.stop();target_.clear();videoId_.clear();liveChatId_.clear();continuation_.clear();seen_.clear();}
QStringList YouTubeChatService::liveIds(const QByteArray&raw){QString body=QString::fromUtf8(raw);QRegularExpression re("\\\"videoId\\\":\\\"([\\w-]{11})\\\"");auto it=re.globalMatch(body);QStringList ids;QList<QPair<QString,int>>hits;while(it.hasNext()){auto m=it.next();hits<<qMakePair(m.captured(1),m.capturedStart());}for(int i=0;i<hits.size();++i){const auto window=body.mid(hits[i].second,(i+1<hits.size()?hits[i+1].second:body.size())-hits[i].second);
    // These two markers were previously searched for with an extra layer of
    // backslash-escaping that only matches quotes escaped a second time (as
    // if this JSON were embedded inside another JSON string). The page's own
    // ytInitialData is plain, unescaped JSON, so neither marker ever matched
    // and only BADGE_STYLE_TYPE_LIVE_NOW could detect a live video — which
    // isn't always present, showing "not live" for channels that actually are.
    if((window.contains("BADGE_STYLE_TYPE_LIVE_NOW")||window.contains("\"isLive\":true")||window.contains("liveBroadcastContent\":\"live"))&&!ids.contains(hits[i].first))ids<<hits[i].first;}return ids;}
void YouTubeChatService::resolveTarget(){
    if(target_.isEmpty())return;
    if(explicitVideo_){bootstrap(videoId_);return;}
    QString base=target_;
    if(!base.startsWith("http")){emit statusChanged("error","invalid YouTube channel");return;}
    resolveViaLiveRedirect(base);
}
void YouTubeChatService::resolveViaLiveRedirect(const QString&base){
    // youtube.com/<channel>/live redirects straight to the live watch page
    // when the channel is live, and otherwise lands back on a channel page
    // with no video id in the URL. This is far more reliable than scraping
    // the Streams tab for "is this live" markers, whose page layout changes
    // often enough that a live channel could still show "not live".
    auto*r=network_.get(webReq(QUrl(base+"/live")));
    connect(r,&QNetworkReply::finished,this,[this,r,base]{
        const QUrl finalUrl=r->url();
        r->deleteLater();
        const auto m=QRegularExpression("(?:watch\\?v=|/live/)([A-Za-z0-9_-]{11})").match(finalUrl.toString());
        if(m.hasMatch()){videoId_=m.captured(1);bootstrap(videoId_);return;}
        resolveViaStreamsScrape(base);
    });
}
void YouTubeChatService::resolveViaStreamsScrape(const QString&base){
    auto*r=network_.get(webReq(QUrl(base+"/streams")));
    connect(r,&QNetworkReply::finished,this,[this,r]{
        auto bytes=r->readAll();
        if(r->error()!=QNetworkReply::NoError){emit statusChanged("error",r->errorString());retry_.start(10000);r->deleteLater();return;}
        auto ids=liveIds(bytes);
        if(ids.isEmpty()){emit statusChanged("warn",verticalOnly_?"no vertical / Shorts live found":"not live");retry_.start(30000);r->deleteLater();return;}
        videoId_=ids.first();bootstrap(videoId_);r->deleteLater();
    });
}
void YouTubeChatService::bootstrap(const QString&vid){auto*r=network_.get(webReq(QUrl("https://www.youtube.com/live_chat?is_popout=1&v="+vid)));connect(r,&QNetworkReply::finished,this,[this,r,vid]{auto b=r->readAll();r->deleteLater();apiKey_=capture(b,"\\\"INNERTUBE_API_KEY\\\":\\\"([^\\\"]+)");clientVersion_=capture(b,"\\\"clientVersion\\\":\\\"([\\d.]+)");continuation_=capture(b,"\\\"continuation\\\":\\\"([^\\\"]+)");liveChatId_=capture(b,"\\\"liveChatId\\\":\\\"([^\\\"]+)");if(apiKey_.isEmpty()||continuation_.isEmpty()){emit statusChanged("warn","chat unavailable");retry_.start(20000);return;}if(clientVersion_.isEmpty())clientVersion_="2.20240101.00.00";emit statusChanged("ok",QStringLiteral("LIVE — %1").arg(vid));viewerTimer_.start();poll();pollViewers();});}
QString YouTubeChatService::runsText(const QJsonObject&m){QString out;for(const auto&v:m["runs"].toArray()){auto r=v.toObject();if(r.contains("text"))out+=r["text"].toString();else{auto e=r["emoji"].toObject();auto s=e["shortcuts"].toArray();out+=s.isEmpty()?e["emojiId"].toString():s[0].toString();}}return out;}
void YouTubeChatService::poll(){if(continuation_.isEmpty())return;QUrl u("https://www.youtube.com/youtubei/v1/live_chat/get_live_chat");QUrlQuery q;q.addQueryItem("key",apiKey_);q.addQueryItem("prettyPrint","false");u.setQuery(q);QJsonObject client{{"clientName","WEB"},{"clientVersion",clientVersion_}};QJsonObject body{{"context",QJsonObject{{"client",client}}},{"continuation",continuation_}};auto req=webReq(u);req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");auto*r=network_.post(req,QJsonDocument(body).toJson(QJsonDocument::Compact));connect(r,&QNetworkReply::finished,this,[this,r]{if(r->error()!=QNetworkReply::NoError){emit statusChanged("warn",r->errorString());retry_.start(5000);r->deleteLater();return;}auto lcc=QJsonDocument::fromJson(r->readAll()).object()["continuationContents"].toObject()["liveChatContinuation"].toObject();r->deleteLater();for(const auto&av:lcc["actions"].toArray()){auto item=av.toObject()["addChatItemAction"].toObject()["item"].toObject();auto rend=item["liveChatTextMessageRenderer"].toObject();bool paid=false;if(rend.isEmpty()){rend=item["liveChatPaidMessageRenderer"].toObject();paid=!rend.isEmpty();}const QString id=rend["id"].toString();if(rend.isEmpty()||seen_.contains(id))continue;seen_.insert(id);ChatMessage m;m.platform=platform_;m.user=rend["authorName"].toObject()["simpleText"].toString("viewer");m.text=runsText(rend["message"].toObject());m.messageId=id;m.userId=rend["authorExternalChannelId"].toString();m.metadata={{"video_id",videoId_},{"live_chat_id",liveChatId_},{"vertical",verticalOnly_}};for(const auto&bv:rend["authorBadges"].toArray()){const QString tip=bv.toObject()["liveChatAuthorBadgeRenderer"].toObject()["tooltip"].toString().toLower();if(tip.contains("owner"))m.badges<<"HOST";else if(tip.contains("moderator"))m.badges<<"MOD";else if(tip.contains("member"))m.badges<<"SUB";else if(tip.contains("verified"))m.badges<<"CHECK";}if(paid)m.badges<<"MONEY";if(!m.text.isEmpty())emit messageReceived(m);}auto cs=lcc["continuations"].toArray();if(cs.isEmpty()){emit statusChanged("warn","chat ended");retry_.start(10000);return;}auto c=cs[0].toObject();QJsonObject n;for(const auto&k:{"invalidationContinuationData","timedContinuationData","reloadContinuationData","liveChatReplayContinuationData"})if(c.contains(k)){n=c[k].toObject();break;}continuation_=n["continuation"].toString();pollTimer_.start(qBound(800,n["timeoutMs"].toInt(1500),1500));});}
void YouTubeChatService::pollViewers(){if(videoId_.isEmpty())return;QUrl u("https://www.youtube.com/youtubei/v1/updated_metadata");QUrlQuery q;q.addQueryItem("key",apiKey_);q.addQueryItem("prettyPrint","false");u.setQuery(q);QJsonObject body{{"context",QJsonObject{{"client",QJsonObject{{"clientName","WEB"},{"clientVersion",clientVersion_}}}}},{"videoId",videoId_}};auto req=webReq(u);req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");auto*r=network_.post(req,QJsonDocument(body).toJson(QJsonDocument::Compact));connect(r,&QNetworkReply::finished,this,[this,r]{for(const auto&av:QJsonDocument::fromJson(r->readAll()).object()["actions"].toArray()){auto vc=av.toObject()["updateViewershipAction"].toObject()["viewCount"].toObject()["videoViewCountRenderer"].toObject();QString raw=vc["originalViewCount"].toString();raw.remove(QRegularExpression("[^0-9]"));if(!raw.isEmpty()){emit viewerCountChanged(raw.toInt());break;}}r->deleteLater();});}

YouTubeModerationService::YouTubeModerationService(QObject*p):QObject(p){}
QNetworkRequest YouTubeModerationService::request(const QUrl&u)const{QNetworkRequest r(u);r.setRawHeader("Authorization",("Bearer "+token_).toUtf8());r.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");return r;}
void YouTubeModerationService::watch(QNetworkReply*r,const QString&a){connect(r,&QNetworkReply::finished,this,[this,r,a]{bool ok=r->error()==QNetworkReply::NoError;emit actionFinished(a,ok,ok?QString():QString::fromUtf8(r->readAll()));r->deleteLater();});}
void YouTubeModerationService::deleteMessage(const QString&id){QUrl u("https://www.googleapis.com/youtube/v3/liveChat/messages");QUrlQuery q;q.addQueryItem("id",id);u.setQuery(q);watch(network_.deleteResource(request(u)),"delete");}
void YouTubeModerationService::ban(const QString&chat,const QString&channel,int seconds,const QString&user){
    if(autoModDenied_.contains(chat))return;
    QUrl u("https://www.googleapis.com/youtube/v3/liveChat/bans");QUrlQuery q;q.addQueryItem("part","snippet");u.setQuery(q);QJsonObject sn{{"liveChatId",chat},{"type",seconds>0?"temporary":"permanent"},{"bannedUserDetails",QJsonObject{{"channelId",channel}}}};if(seconds>0)sn["banDurationSeconds"]=seconds;
    auto*r=network_.post(request(u),QJsonDocument(QJsonObject{{"snippet",sn}}).toJson(QJsonDocument::Compact));
    connect(r,&QNetworkReply::finished,this,[this,r,user,chat,seconds]{
        const bool ok=r->error()==QNetworkReply::NoError;
        const int status=r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto data=QJsonDocument::fromJson(r->readAll()).object();
        if(ok){emit banCreated(data["id"].toString(),user,seconds<=0);emit actionFinished("ban",true,QString());r->deleteLater();return;}
        // Only a hard 403 (not a moderator/owner of this broadcast) should stop
        // retrying for this chat, matching Twitch's behavior. A 401 usually just
        // means the pasted access token expired — unlike Twitch, YouTube here
        // has no refresh flow, so treating it the same as 403 silently and
        // permanently disabled AutoMod for the rest of the stream. Surfacing the
        // real Google API error (instead of a generic Qt error string) also
        // makes it clear this is a token problem, not a permissions one.
        if(status==403)autoModDenied_.insert(chat);
        QString detail=data.value("error").toObject().value("message").toString();
        if(detail.isEmpty())detail=r->errorString();
        if(status==401)detail=QStringLiteral("YouTube access token expired or invalid. Reconnect YouTube under Moderation. (%1)").arg(detail);
        emit actionFinished("ban",false,detail);
        r->deleteLater();
    });
}
void YouTubeModerationService::removeBan(const QString&id){QUrl u("https://www.googleapis.com/youtube/v3/liveChat/bans");QUrlQuery q;q.addQueryItem("id",id);u.setQuery(q);watch(network_.deleteResource(request(u)),"unban");}

void TikTokPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel l,const QString&m,int line,const QString&s){Q_UNUSED(l);Q_UNUSED(line);Q_UNUSED(s);if(!m.startsWith("LEFROGE_CHAT:"))return;auto d=QJsonDocument::fromJson(m.mid(13).toUtf8());if(d.isObject())emit bridgeMessage(d.object());}
TikTokLiveService::TikTokLiveService(QObject*p):QObject(p),page_(this){connect(&page_,&QWebEnginePage::loadFinished,this,[this](bool ok){if(ok){installBridge();emit statusChanged("ok","@"+username_);}else emit statusChanged("error","TikTok page failed to load");});connect(&page_,&TikTokPage::bridgeMessage,this,[this](const QJsonObject&o){const QString type=o["type"].toString();if(type=="viewers"){emit viewerCountChanged(o["count"].toInt());return;}const QString id=o["id"].toString();if(type=="join"||type=="follow"||type=="like"){if(!id.isEmpty()&&activitySeen_.contains(id))return;if(!id.isEmpty())activitySeen_.insert(id);StreamEvent e;e.eventId=id;e.kind=QStringLiteral("tiktok_")+type;e.platform=QStringLiteral("tiktok");e.user=o["user"].toString("Someone");e.amount=o["amount"].toString();e.message=o["text"].toString();e.raw=o;emit activityReceived(e);return;}if(type!="comment")return;if(!id.isEmpty()&&seen_.contains(id))return;if(!id.isEmpty())seen_.insert(id);ChatMessage m;m.platform="tiktok";m.user=o["user"].toString("viewer");m.text=o["text"].toString();m.userId=o["userId"].toString();m.messageId=id;m.metadata={{"room_id",o["roomId"]},{"unique_id",o["uniqueId"]},{"comment_msg_id",id}};if(!m.text.isEmpty())emit messageReceived(m);});}
void TikTokLiveService::connectUser(const QString&u){username_=u;username_.remove('@');seen_.clear();activitySeen_.clear();emit statusChanged("connecting","opening TikTok LIVE…");page_.load(QUrl("https://www.tiktok.com/@"+username_+"/live"));}
void TikTokLiveService::disconnectService(){page_.setUrl(QUrl("about:blank"));username_.clear();seen_.clear();activitySeen_.clear();}
void TikTokLiveService::installBridge(){
    // Leapcast never displays this page — it only reads chat text and the
    // viewer count from the DOM — so keep the live video paused/muted here.
    // Decoding a live video stream nobody watches was by far the heaviest
    // CPU/GPU cost of having a TikTok source connected. TikTok's own script
    // can still call .play() on it (e.g. after an ad or a reconnect), so a
    // 'play' listener re-pauses it immediately rather than polling.
    page_.runJavaScript(QStringLiteral(R"JS((()=>{if(window.__leapcastTikTokBridgeV3)return;window.__leapcastTikTokBridgeV3=true;let seq=0,activityReady=false;const send=o=>console.log('LEFROGE_CHAT:'+JSON.stringify(o));const text=n=>(n?.innerText||n?.textContent||'').trim();const all=(root,selector)=>{const out=[];if(root?.matches?.(selector))out.push(root);root?.querySelectorAll?.(selector).forEach(n=>out.push(n));return out};const compact=s=>{const m=String(s||'').replace(/,/g,'').match(/([0-9]+(?:\.[0-9]+)?)\s*([KMB])?/i);if(!m)return NaN;return Math.round(parseFloat(m[1])*({K:1e3,M:1e6,B:1e9}[m[2]?.toUpperCase()]||1))};const guard=v=>{if(v.dataset.leapcastGuarded)return;v.dataset.leapcastGuarded='1';v.muted=true;v.pause();v.addEventListener('play',()=>v.pause())};const itemSelector='[data-e2e="chat-message"],[data-e2e="chat-room-message"],[data-e2e*="chat-message"],[data-e2e="comment-item"],[class*="DivChatMessage"],[class*="ChatMessage"],[class*="CommentItem"]';const activitySelector='[data-e2e*="join"],[data-e2e*="follow"],[data-e2e*="like"],[class*="JoinMessage"],[class*="FollowMessage"],[class*="LikeMessage"],[class*="SocialMessage"]';const process=(n,allowActivity)=>{if(n.dataset.leapcastSeen)return;const whole=text(n);if(!whole)return;const structure=((n.getAttribute('data-e2e')||'')+' '+(n.className||'')).toLowerCase(),lower=whole.toLowerCase();const userNode=n.querySelector('[data-e2e="message-owner-name"],[data-e2e="chat-message-user"],[data-e2e*="user-name"],[class*="SpanUserName"],[class*="UserName"],a[href*="/@"]');let user=text(userNode)||whole.split(/\s+(?:joined|followed|liked|sent)\b/i)[0]||'Someone';const id=n.getAttribute('data-id')||n.getAttribute('data-message-id')||n.id||String(Date.now())+'-'+(++seq);let type='';if(/join/.test(structure)||/\bjoined(?:\s+the\s+live)?[.!]?$/i.test(lower))type='join';else if(/follow/.test(structure)||/\bfollowed(?:\s+the\s+host)?[.!]?$/i.test(lower))type='follow';else if(/like/.test(structure)||/\bsent\s+(?:[0-9,.kmb]+\s+)?likes?[.!]?$/i.test(lower))type='like';if(type){n.dataset.leapcastSeen='1';if(allowActivity)send({type,id,user,text:whole,amount:type==='like'?String(compact(whole)||''):''});return}const body=n.querySelector('[data-e2e="message-text"],[data-e2e="chat-message-comment"],[data-e2e="comment-level-1"],[data-e2e*="message-text"],[class*="DivComment"],[class*="CommentText"],[class*="MessageText"]');const message=text(body)||whole.replace(text(userNode),'').trim();if(message){n.dataset.leapcastSeen='1';send({type:'comment',id,user,text:message,userId:userNode?.getAttribute('data-user-id')||'',roomId:location.pathname,uniqueId:userNode?.getAttribute('data-unique-id')||''})}};const scan=(root,allowActivity=true)=>{all(root,'video').forEach(guard);all(root,itemSelector).forEach(n=>process(n,allowActivity));all(root,activitySelector).forEach(n=>process(n,allowActivity));const viewers=document.querySelector('[data-e2e="live-room-viewer-count"],[data-e2e="live-viewer-count"],[class*="ViewerCount"]');if(viewers){const count=compact(text(viewers));if(Number.isFinite(count))send({type:'viewers',count})}};scan(document,false);activityReady=true;new MutationObserver(ms=>ms.forEach(m=>m.addedNodes.forEach(n=>{if(n.nodeType===1)scan(n,activityReady)}))).observe(document.documentElement,{childList:true,subtree:true});setInterval(()=>scan(document,activityReady),3000)})())JS"));
}

KickLiveService::KickLiveService(QObject*p):QObject(p),page_(this){
    connect(&page_,&QWebEnginePage::loadFinished,this,[this](bool ok){if(ok){installBridge();emit statusChanged("ok",channel_);}else emit statusChanged("error","Kick page failed to load");});
    connect(&page_,&TikTokPage::bridgeMessage,this,[this](const QJsonObject&o){if(o["platform"]!="kick")return;if(o["type"]=="viewers"){emit viewerCountChanged(o["count"].toInt());return;}const QString id=o["id"].toString();if(!id.isEmpty()&&seen_.contains(id))return;if(!id.isEmpty())seen_.insert(id);ChatMessage m;m.platform="kick";m.user=o["user"].toString("viewer");m.text=o["text"].toString();m.userId=o["userId"].toString();m.messageId=id;m.metadata={{"channel",channel_}};if(!m.text.isEmpty())emit messageReceived(m);});
}
void KickLiveService::connectChannel(const QString&channel){disconnectService();channel_=channel.trimmed();channel_.remove('@');if(channel_.isEmpty()){emit statusChanged("error","invalid Kick channel");return;}emit statusChanged("connecting","opening Kick chat…");page_.load(QUrl("https://kick.com/"+channel_));}
void KickLiveService::disconnectService(){page_.setUrl(QUrl("about:blank"));channel_.clear();seen_.clear();}
void KickLiveService::installBridge(){page_.runJavaScript(QStringLiteral(R"JS((()=>{if(window.__leapcastKick)return;window.__leapcastKick=true;let seq=0;const send=o=>console.log('LEFROGE_CHAT:'+JSON.stringify({...o,platform:'kick'}));const txt=n=>(n?.innerText||n?.textContent||'').trim();const scan=root=>{root.querySelectorAll?.('[data-testid="chat-entry"],[data-chat-entry],[class*="chat-entry"],[class*="ChatMessage"]').forEach(n=>{if(n.dataset.leapcastSeen)return;n.dataset.leapcastSeen='1';const user=n.querySelector('[data-testid="chat-entry-username"],[class*="username"],[class*="Username"]');const body=n.querySelector('[data-testid="chat-entry-content"],[class*="message-content"],[class*="MessageContent"]');const message=txt(body)||txt(n).replace(txt(user),'').trim();if(message)send({type:'comment',id:n.getAttribute('data-id')||String(Date.now())+'-'+(++seq),user:txt(user)||'viewer',text:message,userId:user?.getAttribute('data-user-id')||''});});const viewers=document.querySelector('[data-testid="viewers-count"],[class*="viewer-count"]');if(viewers){const count=parseInt(txt(viewers).replace(/[^0-9]/g,''));if(Number.isFinite(count))send({type:'viewers',count});}};new MutationObserver(ms=>ms.forEach(m=>m.addedNodes.forEach(n=>{if(n.nodeType===1)scan(n)}))).observe(document.body,{childList:true,subtree:true});scan(document);})())JS"));}

RumbleLiveService::RumbleLiveService(QObject*p):QObject(p){timer_.setInterval(2500);connect(&timer_,&QTimer::timeout,this,&RumbleLiveService::poll);}
void RumbleLiveService::connectApi(const QUrl&url){disconnectService();const bool hostOk=url.host()==QStringLiteral("rumble.com")||url.host().endsWith(QStringLiteral(".rumble.com"));if(!url.isValid()||!hostOk){emit statusChanged("error","Add the private Rumble Live Stream API URL under Keys first.");return;}apiUrl_=url;emit statusChanged("connecting","connecting to Rumble Live Stream API…");poll();timer_.start();}
void RumbleLiveService::disconnectService(){timer_.stop();apiUrl_.clear();seen_.clear();eventSeen_.clear();eventsBaselined_=false;}
void RumbleLiveService::poll(){
    if(apiUrl_.isEmpty())return;
    auto*r=network_.get(webReq(apiUrl_));
    connect(r,&QNetworkReply::finished,this,[this,r]{
        const QByteArray bytes=r->readAll();
        if(r->error()!=QNetworkReply::NoError){emit statusChanged("warn",r->errorString());r->deleteLater();return;}
        const auto root=QJsonDocument::fromJson(bytes).object();
        const auto acceptEvent=[this](const QString&kind,const QString&user,const QString&idSource,const QString&amount,const QJsonObject&raw){
            const QString id=kind+QLatin1Char('|')+idSource;
            if(eventSeen_.contains(id))return;
            eventSeen_.insert(id);
            if(!eventsBaselined_)return;
            StreamEvent event;event.eventId=id;event.kind=kind;event.user=user.isEmpty()?QStringLiteral("Someone"):user;event.amount=amount;event.platform=QStringLiteral("rumble");event.raw=raw;emit eventReceived(event);
        };
        for(const auto&v:root["followers"].toObject()["recent_followers"].toArray()){const auto o=v.toObject();acceptEvent(QStringLiteral("follow"),o["username"].toString(),o["username"].toString()+QLatin1Char('|')+o["followed_on"].toString(),QString(),o);}
        for(const auto&v:root["subscribers"].toObject()["recent_subscribers"].toArray()){const auto o=v.toObject();const QString user=o["username"].toString(o["user"].toString());acceptEvent(QStringLiteral("subscription"),user,user+QLatin1Char('|')+o["subscribed_on"].toString(),QStringLiteral("$%1").arg(o["amount_dollars"].toVariant().toString()),o);}
        for(const auto&v:root["gifted_subs"].toObject()["recent_gifted_subs"].toArray()){const auto o=v.toObject();const QString user=o["purchased_by"].toString();const QString total=QString::number(o["total_gifts"].toInt());acceptEvent(QStringLiteral("gifted_sub"),user,user+QLatin1Char('|')+o["video_id"].toVariant().toString()+QLatin1Char('|')+total,total,o);}
        eventsBaselined_=true;
        while(eventSeen_.size()>500)eventSeen_.erase(eventSeen_.begin());
        const auto streams=root["livestreams"].toArray();bool live=false;
        for(const auto&value:streams){const auto stream=value.toObject();if(!stream["is_live"].toBool())continue;live=true;emit viewerCountChanged(stream["watching_now"].toInt());const auto messages=stream["chat"].toObject()["recent_messages"].toArray();for(const auto&mv:messages){const auto o=mv.toObject();const QString created=o["created_on"].toString();const QString user=o["username"].toString("viewer");const QString text=o["text"].toString();const QString id=QString::number(qHash(user+QLatin1Char('|')+created+QLatin1Char('|')+text));if(seen_.contains(id))continue;seen_.insert(id);ChatMessage m;m.platform="rumble";m.user=user;m.text=text;m.messageId=id;m.metadata={{"stream_id",stream["id"]},{"created_on",created}};for(const auto&badge:o["badges"].toArray()){const QString b=badge.toString().toLower();if(b=="admin")m.badges<<"MOD";else if(b.contains("premium"))m.badges<<"SUB";}if(!text.isEmpty())emit messageReceived(m);}while(seen_.size()>500)seen_.erase(seen_.begin());}
        emit statusChanged(live?"ok":"warn",live?QStringLiteral("Rumble LIVE"):QStringLiteral("Rumble is not live"));r->deleteLater();
    });
}

TikTokModerationService::TikTokModerationService(QObject*p):QObject(p){}
void TikTokModerationService::send(const QString&m,const QString&p,const QUrlQuery&q,const QString&a){QUrl u("https://tiktok.eulerstream.com"+p);u.setQuery(q);QNetworkRequest req(u);req.setRawHeader("x-oauth-token",token_.toUtf8());req.setRawHeader("Accept","application/json");QNetworkReply*r=nullptr;if(m=="PUT")r=network_.put(req,QByteArray());else r=network_.deleteResource(req);connect(r,&QNetworkReply::finished,this,[this,r,a]{bool ok=r->error()==QNetworkReply::NoError;auto body=r->readAll();auto o=QJsonDocument::fromJson(body).object();if(ok&&o.contains("code"))ok=o["code"].toVariant().toInt()==0;emit actionFinished(a,ok,ok?QString():QString::fromUtf8(body));r->deleteLater();});}
void TikTokModerationService::mute(const QString&r,const QString&u,int s,const QString&c){QUrlQuery q;q.addQueryItem("user_id",u);q.addQueryItem("duration",QString::number(s));if(!c.isEmpty())q.addQueryItem("comment_msg_id",c);send("PUT","/webcast/rooms/"+r+"/moderation/mutes",q,"mute");}
void TikTokModerationService::unmute(const QString&r,const QString&u){QUrlQuery q;q.addQueryItem("user_id",u);send("DELETE","/webcast/rooms/"+r+"/moderation/mutes",q,"unmute");}
void TikTokModerationService::ban(const QString&r,const QString&u,const QString&c){QUrlQuery q;q.addQueryItem("tiktok_user_id",u);if(!c.isEmpty())q.addQueryItem("comment_msg_id",c);send("PUT","/webcast/rooms/"+r+"/moderation/bans",q,"ban");}
void TikTokModerationService::unban(const QString&r,const QString&u){QUrlQuery q;q.addQueryItem("tiktok_user_id",u);send("DELETE","/webcast/rooms/"+r+"/moderation/bans",q,"unban");}
