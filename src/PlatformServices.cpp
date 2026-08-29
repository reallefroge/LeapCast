#include "PlatformServices.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QEvent>
#include <QTimer>
#include <QWebEngineProfile>
#include <QWebEngineView>

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
void YouTubeChatService::disconnectService(){retry_.stop();pollTimer_.stop();viewerTimer_.stop();target_.clear();videoId_.clear();liveChatId_.clear();continuation_.clear();seen_.clear();recentMessages_.clear();if(!pinnedMessageId_.isEmpty())emit pinnedMessageChanged(ChatMessage{},false);pinnedMessageId_.clear();}
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
QJsonArray YouTubeChatService::runsMetadata(const QJsonObject&m){
    QJsonArray out;
    for(const auto&v:m.value(QStringLiteral("runs")).toArray()){
        const auto r=v.toObject();
        if(r.contains(QStringLiteral("text"))){out.append(QJsonObject{{QStringLiteral("text"),r.value(QStringLiteral("text")).toString()}});continue;}
        const auto e=r.value(QStringLiteral("emoji")).toObject();const auto shortcuts=e.value(QStringLiteral("shortcuts")).toArray();
        const QString alt=shortcuts.isEmpty()?e.value(QStringLiteral("emojiId")).toString():shortcuts.first().toString();
        QString url;const auto thumbs=e.value(QStringLiteral("image")).toObject().value(QStringLiteral("thumbnails")).toArray();
        if(!thumbs.isEmpty())url=thumbs.last().toObject().value(QStringLiteral("url")).toString();if(url.startsWith(QStringLiteral("//")))url.prepend(QStringLiteral("https:"));
        QJsonObject item{{QStringLiteral("alt"),alt}};if(!url.isEmpty())item[QStringLiteral("url")]=url;out.append(item);
    }
    return out;
}
void YouTubeChatService::poll(){
    if(continuation_.isEmpty())return;
    QUrl u(QStringLiteral("https://www.youtube.com/youtubei/v1/live_chat/get_live_chat"));QUrlQuery q;q.addQueryItem(QStringLiteral("key"),apiKey_);q.addQueryItem(QStringLiteral("prettyPrint"),QStringLiteral("false"));u.setQuery(q);
    QJsonObject client{{QStringLiteral("clientName"),QStringLiteral("WEB")},{QStringLiteral("clientVersion"),clientVersion_}};
    QJsonObject body{{QStringLiteral("context"),QJsonObject{{QStringLiteral("client"),client}}},{QStringLiteral("continuation"),continuation_}};
    auto req=webReq(u);req.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));auto*r=network_.post(req,QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(r,&QNetworkReply::finished,this,[this,r]{
        if(r->error()!=QNetworkReply::NoError){emit statusChanged(QStringLiteral("warn"),r->errorString());retry_.start(5000);r->deleteLater();return;}
        const auto lcc=QJsonDocument::fromJson(r->readAll()).object().value(QStringLiteral("continuationContents")).toObject().value(QStringLiteral("liveChatContinuation")).toObject();r->deleteLater();
        const auto messageFromRenderer=[this](const QJsonObject&rend,bool paid=false){
            ChatMessage m;m.platform=platform_;m.user=rend.value(QStringLiteral("authorName")).toObject().value(QStringLiteral("simpleText")).toString(QStringLiteral("viewer"));
            const QJsonObject messageObject=rend.value(QStringLiteral("message")).toObject();m.text=runsText(messageObject);m.messageId=rend.value(QStringLiteral("id")).toString();m.userId=rend.value(QStringLiteral("authorExternalChannelId")).toString();
            m.metadata={{QStringLiteral("video_id"),videoId_},{QStringLiteral("live_chat_id"),liveChatId_},{QStringLiteral("vertical"),verticalOnly_},{QStringLiteral("youtube_runs"),runsMetadata(messageObject)}};
            for(const auto&bv:rend.value(QStringLiteral("authorBadges")).toArray()){const QString tip=bv.toObject().value(QStringLiteral("liveChatAuthorBadgeRenderer")).toObject().value(QStringLiteral("tooltip")).toString().toLower();if(tip.contains(QStringLiteral("owner")))m.badges<<QStringLiteral("HOST");else if(tip.contains(QStringLiteral("moderator")))m.badges<<QStringLiteral("MOD");else if(tip.contains(QStringLiteral("member")))m.badges<<QStringLiteral("SUB");else if(tip.contains(QStringLiteral("verified")))m.badges<<QStringLiteral("CHECK");}
            if(paid)m.badges<<QStringLiteral("MONEY");return m;
        };
        const auto findRenderer=[](const QJsonValue&rootValue){
            QJsonObject found;QList<QJsonValue> pending{rootValue};
            while(!pending.isEmpty()&&found.isEmpty()){
                const QJsonValue value=pending.takeLast();if(value.isArray()){for(const auto&v:value.toArray())pending<<v;continue;}if(!value.isObject())continue;
                const auto object=value.toObject();for(const auto&key:{QStringLiteral("liveChatTextMessageRenderer"),QStringLiteral("liveChatPaidMessageRenderer")})if(object.value(key).isObject()){found=object.value(key).toObject();break;}
                if(!found.isEmpty())break;for(auto it=object.begin();it!=object.end();++it)if(it.value().isObject()||it.value().isArray())pending<<it.value();
            }
            return found;
        };
        for(const auto&av:lcc.value(QStringLiteral("actions")).toArray()){
            const auto action=av.toObject();
            const auto addBanner=action.value(QStringLiteral("addBannerToLiveChatCommand")).toObject();
            if(!addBanner.isEmpty()){
                const QString targetId=addBanner.value(QStringLiteral("targetId")).toString();ChatMessage pinned;
                if(!targetId.isEmpty()&&recentMessages_.contains(targetId))pinned=recentMessages_.value(targetId);
                if(pinned.messageId.isEmpty()){
                    const auto renderer=findRenderer(addBanner);if(!renderer.isEmpty())pinned=messageFromRenderer(renderer,addBanner.contains(QStringLiteral("liveChatPaidMessageRenderer")));
                }
                if(!targetId.isEmpty()&&pinned.messageId.isEmpty())pinned.messageId=targetId;
                if(!pinned.text.isEmpty()||!pinned.messageId.isEmpty()){
                    pinned.metadata[QStringLiteral("pinned")]=true;pinned.metadata[QStringLiteral("pin_target_id")]=targetId;
                    const QString id=pinned.messageId.isEmpty()?targetId:pinned.messageId;if(id!=pinnedMessageId_){pinnedMessageId_=id;emit pinnedMessageChanged(pinned,true);}
                }
            }
            const auto removeBanner=action.value(QStringLiteral("removeBannerForLiveChatCommand")).toObject();
            if(!removeBanner.isEmpty()&&!pinnedMessageId_.isEmpty()){
                const QString target=removeBanner.value(QStringLiteral("targetActionId")).toString();Q_UNUSED(target)
                pinnedMessageId_.clear();emit pinnedMessageChanged(ChatMessage{},false);
            }
            auto item=action.value(QStringLiteral("addChatItemAction")).toObject().value(QStringLiteral("item")).toObject();auto rend=item.value(QStringLiteral("liveChatTextMessageRenderer")).toObject();bool paid=false;
            if(rend.isEmpty()){rend=item.value(QStringLiteral("liveChatPaidMessageRenderer")).toObject();paid=!rend.isEmpty();}
            const QString id=rend.value(QStringLiteral("id")).toString();if(rend.isEmpty()||seen_.contains(id))continue;seen_.insert(id);ChatMessage m=messageFromRenderer(rend,paid);recentMessages_.insert(id,m);while(recentMessages_.size()>400)recentMessages_.erase(recentMessages_.begin());if(!m.text.isEmpty())emit messageReceived(m);
        }
        const auto cs=lcc.value(QStringLiteral("continuations")).toArray();if(cs.isEmpty()){emit statusChanged(QStringLiteral("warn"),QStringLiteral("chat ended"));retry_.start(10000);return;}auto c=cs[0].toObject();QJsonObject n;for(const auto&k:{"invalidationContinuationData","timedContinuationData","reloadContinuationData","liveChatReplayContinuationData"})if(c.contains(k)){n=c[k].toObject();break;}continuation_=n.value(QStringLiteral("continuation")).toString();pollTimer_.start(qBound(800,n.value(QStringLiteral("timeoutMs")).toInt(1500),1500));
    });
}
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
TikTokLiveService::TikTokLiveService(QObject*p):QObject(p){
    // A NAMED profile keeps cookies on disk, so a TikTok sign-in done once in
    // the collector window survives restarts. The old off-the-record default
    // profile meant every launch was signed out, and a signed-out LIVE page
    // serves a trimmed chat panel (often none at all).
    profile_=new QWebEngineProfile(QStringLiteral("LeapcastTikTok"),this);
    profile_->setHttpUserAgent(QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/151.0.0.0 Safari/537.36"));
    profile_->setHttpAcceptLanguage(QStringLiteral("en-US,en;q=0.9"));
    profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    page_=new TikTokPage(profile_,this);

    // Off-screen but really laid out: WA_DontShowOnScreen gives the page a
    // genuine 1280x900 viewport (so the virtualised chat list renders rows)
    // without putting a window on the streamer's desktop.
    view_=new QWebEngineView;
    view_->setWindowTitle(QStringLiteral("Leapcast — TikTok collector"));
    view_->setAttribute(Qt::WA_DontShowOnScreen,true);
    view_->setAttribute(Qt::WA_QuitOnClose,false);
    view_->setPage(page_);
    view_->resize(1280,900);
    view_->installEventFilter(this);
    view_->show();

    connect(page_,&QWebEnginePage::loadFinished,this,[this](bool ok){
        if(ok){installBridge();emit statusChanged("ok","@"+username_);emit diagnostic(QStringLiteral("Page loaded: %1").arg(page_->url().toString()));}
        else {emit statusChanged("error","TikTok page failed to load");emit diagnostic(QStringLiteral("Page FAILED to load: %1").arg(page_->url().toString()));}
    });
    connect(page_,&TikTokPage::bridgeMessage,this,[this](const QJsonObject&o){
        const QString type=o["type"].toString();
        if(type=="discovery"){
            emit diagnostic(QStringLiteral("Chat list found by change-detection: \"%1\" (%2 rows). Send me this line and I can target it directly.")
                                .arg(o["label"].toString()).arg(o["children"].toInt()));
            return;
        }
        if(type=="snapshot"){
            // One line that answers "is this even a live page?" before any
            // question about selectors is worth asking.
            emit diagnostic(QStringLiteral("PAGE: rows:%1 nameNodes:%2 textNodes:%3 video:%4 iframes:%5 liveRoom:%6 LIVEbadge:%7 offlineText:%8 loginWall:%9 chars:%10\n  title: %11\n  body: %12")
                                .arg(o["items"].toInt()).arg(o["users"].toInt()).arg(o["bodies"].toInt())
                                .arg(o["videos"].toInt()).arg(o["iframes"].toInt())
                                .arg(o["room"].toBool()?QStringLiteral("yes"):QStringLiteral("NO"),
                                     o["liveBadge"].toBool()?QStringLiteral("yes"):QStringLiteral("no"),
                                     o["offline"].toBool()?QStringLiteral("YES"):QStringLiteral("no"),
                                     o["loginWall"].toBool()?QStringLiteral("YES"):QStringLiteral("no"))
                                .arg(o["bodyChars"].toInt())
                                .arg(o["title"].toString(),o["bodySample"].toString()));
            if(o["loginWall"].toBool())emit statusChanged("warn","TikTok is asking to log in — open the TikTok collector and sign in");
            else if(o["offline"].toBool())emit statusChanged("warn","TikTok says this stream is not live right now");
            else if(o["bodyChars"].toInt()<200)emit statusChanged("warn","TikTok returned a nearly empty page — open the collector to see it");
            else if(o["items"].toInt()==0&&o["users"].toInt()==0)emit statusChanged("warn","TikTok page loaded but no chat rows found");
            return;
        }
        if(type=="viewers"){emit diagnostic(QStringLiteral("viewers=%1 (from %2)").arg(o["count"].toInt()).arg(o["source"].toString()));emit viewerCountChanged(o["count"].toInt());return;}
        const QString id=o["id"].toString();
        if(type=="activity"||type=="join"){
            if(!id.isEmpty()&&activitySeen_.contains(id))return;
            if(!id.isEmpty()){activitySeen_.insert(id);if(activitySeen_.size()>4000)activitySeen_.clear();}
            // join / like / follow / gift / share, all from the same feed.
            const QString kind=o["kind"].toString(QStringLiteral("join"));
            StreamEvent e;e.eventId=id;e.kind=QStringLiteral("tiktok_")+kind;e.platform=QStringLiteral("tiktok");
            e.user=o["user"].toString("Someone");e.message=o["text"].toString();e.raw=o;
            emit diagnostic(QStringLiteral("%1: %2").arg(kind,e.user));
            emit activityReceived(e);return;
        }
        if(type!="comment")return;
        if(!id.isEmpty()&&seen_.contains(id))return;
        if(!id.isEmpty()){seen_.insert(id);if(seen_.size()>4000)seen_.clear();}
        ChatMessage m;m.platform="tiktok";m.user=o["user"].toString("viewer");m.text=o["text"].toString();m.userId=o["userId"].toString();m.messageId=id;
        m.metadata={{"room_id",o["roomId"]},{"unique_id",o["uniqueId"]},{"comment_msg_id",id}};
        if(!m.text.isEmpty()){emit diagnostic(QStringLiteral("chat: %1: %2").arg(m.user,m.text));emit messageReceived(m);}
    });
}
bool TikTokLiveService::eventFilter(QObject* watched,QEvent* event){
    // Closing the collector window must put the page back off-screen rather
    // than destroy it, otherwise chat collection stops until the next reconnect.
    if(watched==view_&&event->type()==QEvent::Close){
        event->ignore();
        QTimer::singleShot(0,this,[this]{setCollectorVisible(false);});
        return true;
    }
    return QObject::eventFilter(watched,event);
}
TikTokLiveService::~TikTokLiveService(){if(view_){view_->setPage(nullptr);delete view_;view_=nullptr;}}
void TikTokLiveService::connectUser(const QString&u){username_=u;username_.remove('@');seen_.clear();activitySeen_.clear();emit statusChanged("connecting","opening TikTok LIVE…");page_->load(QUrl("https://www.tiktok.com/@"+username_+"/live"));}
void TikTokLiveService::disconnectService(){page_->setUrl(QUrl("about:blank"));username_.clear();seen_.clear();activitySeen_.clear();}
void TikTokLiveService::reloadCollector(){if(!username_.isEmpty())connectUser(username_);}
bool TikTokLiveService::collectorVisible() const{return view_&&!view_->testAttribute(Qt::WA_DontShowOnScreen);}
void TikTokLiveService::setCollectorVisible(bool visible){
    if(!view_||collectorVisible()==visible)return;
    // WA_DontShowOnScreen can only be changed while the widget is hidden, and
    // a hide/show cycle does not reload the page or drop the bridge.
    view_->hide();
    view_->setAttribute(Qt::WA_DontShowOnScreen,!visible);
    view_->show();
    if(visible){view_->raise();view_->activateWindow();}
    emit collectorVisibilityChanged(visible);
}
void TikTokLiveService::installBridge(){
    // TikTok changes its LIVE DOM frequently. V7 fixes duplicate posting: rank
    // and level chips ("No. 1", "Lv 18") are their own leaf elements that come
    // and go as TikTok re-renders, so the same message kept producing two
    // different texts and posted twice. Rows are now read from a cleaned copy,
    // de-duplicated per row by content, and the profile/discovery passes only
    // run when the selector pass found nothing, instead of alongside it. V6
    // added two things for when the class names change again: a page snapshot in the log
    // (so an empty result can be told apart from an offline page), and a
    // fallback that finds the chat list by watching which container's children
    // actually keep changing. V5's fixes were:
    //   * dedupe is keyed on user+text, not a dataset flag on the DOM node.
    //     TikTok recycles chat rows, so a flagged node silently swallowed
    //     every later message that reused it.
    //   * the viewer count no longer falls back to a whole-page text regex or
    //     to the last "user_count" found anywhere in the page scripts. Both of
    //     those happily matched recommended-stream cards in the sidebar, which
    //     is where the random numbers came from. It now only reads a counter
    //     inside the live room element, or the current room's own serialized
    //     stats as a one-time seed.
    page_->runJavaScript(QStringLiteral(R"JS((()=>{
if(window.__leapcastTikTokBridgeV8)return;window.__leapcastTikTokBridgeV8=true;
let seq=0,activityReady=false,lastViewer=-1,seedUsed=false,debugTick=0,discovered=null,discoveredLabel='';
const send=o=>console.log('LEFROGE_CHAT:'+JSON.stringify(o));
const text=n=>(n?.innerText||n?.textContent||'').trim();
const all=(root,selector)=>{const out=[];try{if(root?.matches?.(selector))out.push(root);root?.querySelectorAll?.(selector).forEach(n=>out.push(n))}catch(e){}return out};
const sig=n=>((n?.getAttribute?.('data-e2e')||'')+' '+(typeof n?.className==='string'?n.className:'')).toLowerCase();
const compact=s=>{const raw=String(s||'').replace(/ /g,' ').trim();const m=raw.match(/^([0-9][0-9.,\s]*)\s*([KMB])?/i);if(!m)return NaN;const suffix=(m[2]||'').toUpperCase();let num=m[1].replace(/\s/g,'');if(suffix)return Math.round(parseFloat(num.replace(/,/g,''))*({K:1e3,M:1e6,B:1e9}[suffix]||1));const v=parseInt(num.replace(/[.,]/g,''),10);return Number.isFinite(v)?v:NaN};
const guard=v=>{if(v.dataset.leapcastGuarded)return;v.dataset.leapcastGuarded='1';v.muted=true;v.pause();v.addEventListener('play',()=>v.pause())};
const userSel='[data-e2e="message-owner-name"],[data-e2e="chat-message-user"],[data-e2e*="user-name"],[data-e2e*="username"],[class*="SpanUserName"],[class*="UserName"],[class*="user-name"],[class*="NickName"],[class*="nickname"],a[href*="/@"]';
const bodySel='[data-e2e="message-text"],[data-e2e="chat-message-comment"],[data-e2e="comment-level-1"],[data-e2e*="message-text"],[data-e2e*="comment-text"],[class*="DivComment"],[class*="CommentText"],[class*="MessageText"],[class*="message-text"],[class*="DivChatContent"],[class*="ChatContent"]';
const itemSel='[data-e2e="chat-message"],[data-e2e="chat-room-message"],[data-e2e="live-chat-message"],[data-e2e*="chat-message"],[data-e2e*="comment-item"],[class*="DivChatMessage"],[class*="ChatMessage"],[class*="chat-message"],[class*="CommentItem"],[class*="comment-item"],[class*="DivChatItem"],[class*="ChatItem"]';
const activitySel='[data-e2e*="join"],[class*="JoinMessage"],[class*="DivSocialMessage"],[class*="SocialMessage"],[class*="GiftMessage"],[class*="DivGift"]';
// A TikTok display name is full of astral emoji. An UNPAIRED surrogate is what
// turns into U+FFFD by the time it reaches the app, so drop only those and
// leave every properly paired emoji intact.
const surrogateSafe=s=>String(s||'')
  .replace(/[\uD800-\uDBFF](?![\uDC00-\uDFFF])/g,'')
  .replace(/(^|[^\uD800-\uDBFF])([\uDC00-\uDFFF])/g,'$1');
// Rank and level chips ("No. 1", "TOP 3", "Lv 18") are separate elements that
// come and go as TikTok re-renders. Trim them off the ends of the strings for
// display, and ignore them entirely when building the de-duplication key, so a
// chip appearing does not make an old message look new.
const chipRun='(?:no\\.?\\s*\\d+|top\\s*\\d+|lv\\.?\\s*\\d+|level\\s*\\d+|#\\d+)';
const trimChips=s=>String(s||'')
  .replace(new RegExp('^(?:\\s*'+chipRun+'\\b)+\\s*','i'),'')
  .replace(new RegExp('(?:\\s*'+chipRun+'\\b)+\\s*$','i'),'')
  .replace(/\s+/g,' ').trim();
const clean=s=>trimChips(surrogateSafe(s));
const normalize=s=>surrogateSafe(s).toLowerCase()
  .replace(new RegExp(chipRun,'gi'),' ')
  .replace(/[^\p{L}\p{N}]+/gu,' ').trim();
// An emoji-only message normalises to nothing, so fall back to the cleaned
// text itself rather than collapsing every such message onto one key.
const keyPart=s=>normalize(s)||trimChips(surrogateSafe(s));
const keyFor=(a,b)=>{const k=keyPart(a)+'|'+keyPart(b);let h=0;for(let i=0;i<k.length;i++){h=(h*31+k.charCodeAt(i))|0}return 'k'+(h>>>0)};
// Two layers: the row remembers the content it was last posted with (TikTok
// recycles rows, so a plain "seen" flag was wrong), and a short global window
// catches the same content arriving through two different elements in one
// burst. Short on purpose, so a genuine repeat still shows.
const recent=new Map();
const burstFresh=key=>{const now=Date.now();
  if(recent.size>800){for(const [k,t] of recent)if(now-t>15000)recent.delete(k)}
  const prior=recent.get(key);if(prior&&now-prior<15000)return false;recent.set(key,now);return true};
const claim=(row,key)=>{
  if(row&&row.dataset&&row.dataset.leapcastKey===key)return false;
  if(!burstFresh(key))return false;
  if(row&&row.dataset)row.dataset.leapcastKey=key;
  return true};
// Activity rows carry no comment body, just a sentence about what happened.
const activityKind=(lower,structure)=>{
  if(/\bjoined\b/.test(lower)||/join/.test(structure))return 'join';
  if(/\bliked\b|\bsent\s+likes?\b|\blikes?\s+the\s+live\b/.test(lower))return 'like';
  if(/\bfollowed\b|\bstarted\s+following\b|\bis\s+now\s+following\b/.test(lower))return 'follow';
  if(/\bsent\b.*\b(rose|roses|gift|gifts|coin|coins|heart|hearts)\b/.test(lower)||/gift/.test(structure))return 'gift';
  if(/\bshared\b.*\blive\b/.test(lower))return 'share';
  return ''};
const activityUser=(row,raw)=>{
  const node=row.querySelector?.(userSel);
  const name=clean(text(node));
  if(name)return name;
  return clean(raw.split(/\s+(?:joined|liked|followed|sent|shared|started)\b/i)[0])||'Someone'};
const emitActivity=(row,kind,user,raw)=>{
  const key=keyFor(kind+'|'+user,raw);
  if(!claim(row,key))return false;
  if(!activityReady)return true;
  send({type:'activity',kind,id:key,user,text:clean(raw)});
  return true};
const emitComment=(row,user,message,userNode,source)=>{
  user=clean(user);message=clean(message);
  if(!user||!message||message===user||message.length>600)return false;
  if(/^(?:0|[0-9,.]+[kmb]?)\s+likes?$/i.test(message)||/^follow(?:ing)?$/i.test(message))return false;
  const key=keyFor(user,message);
  if(!claim(row,key))return false;
  const href=userNode?.closest?.('a[href*="/@"]')?.getAttribute('href')||userNode?.getAttribute?.('href')||'';
  const unique=(href.match(/\/@([^/?]+)/)||[])[1]||userNode?.getAttribute?.('data-unique-id')||'';
  send({type:'comment',id:key,user,text:message,userId:userNode?.getAttribute?.('data-user-id')||'',roomId:location.pathname,uniqueId:unique,source});
  return true};
// Read from the LIVE node. An earlier version read from a detached clone to
// strip badges, but innerText on a detached node collapses to textContent and
// lost the separation between the name and the message, which stopped chat
// working entirely. Badge trimming is done on the strings instead.
const process=(n,source)=>{
  if(!n||n.nodeType!==1)return false;
  const raw=text(n);if(!raw||raw.length>1200)return false;
  const structure=sig(n),lower=raw.toLowerCase();
  const bodyNode=n.querySelector?.(bodySel);
  const userNode=n.querySelector?.(userSel);
  if(!bodyNode){
    const kind=activityKind(lower,structure);
    if(kind)return emitActivity(n,kind,activityUser(n,raw),raw);
  }
  let user=text(userNode);
  let message=text(bodyNode);
  if(!message&&user){message=raw.startsWith(user)?raw.slice(user.length).replace(/^\s*[:：·-]?\s*/,'').trim():raw.replace(user,'').trim()}
  if(!user||!message)return false;
  return emitComment(n,user,message,userNode,source||'selector')};
const profileFallback=root=>{all(root,'a[href*="/@"]').forEach(a=>{let n=a;for(let i=0;i<5&&n;i++,n=n.parentElement){if(/chat|comment|message/.test(sig(n))&&text(n).length<1200){process(n,'profile');break}}})};
const churn=new Map();
const noteChurn=target=>{if(!target||target.nodeType!==1)return;const s=sig(target);
  if(/video|player|progress|seek|caption|toolbar/.test(s))return;
  churn.set(target,(churn.get(target)||0)+1)};
const pickDiscovered=()=>{
  let best=null,bestScore=0;
  for(const [node,score] of churn){
    if(!node.isConnected){churn.delete(node);continue}
    const kids=node.children?node.children.length:0;
    if(kids<3||score<3)continue;
    const sample=text(node);
    if(!sample||sample.length<10||sample.length>20000)continue;
    if(score>bestScore){bestScore=score;best=node}}
  return best};
const scanDiscovered=()=>{
  if(!discovered||!discovered.isConnected){discovered=pickDiscovered();
    if(discovered){discoveredLabel=(discovered.getAttribute('data-e2e')||'')+' '+(typeof discovered.className==='string'?discovered.className:'');
      send({type:'discovery',label:discoveredLabel.trim().slice(0,300),children:discovered.children.length})}}
  if(!discovered)return 0;
  let handled=0;
  for(const row of Array.from(discovered.children)){
    if(process(row,'discovered')){handled++;continue}
    const raw=text(row);
    if(!raw||raw.length>600)continue;
    const kind=activityKind(raw.toLowerCase(),sig(row));
    if(kind){if(emitActivity(row,kind,activityUser(row,raw),raw))handled++;continue}
    let user='',message='';
    const colon=raw.match(/^([^\n:：]{1,40})[:：]\s*(.+)$/s);
    if(colon){user=colon[1].trim();message=colon[2].trim()}
    else if(row.children&&row.children.length>=2){user=text(row.children[0]);message=raw.startsWith(user)?raw.slice(user.length).trim():text(row.children[1])}
    if(user&&message&&emitComment(row,user,message,null,'discovered'))handled++;
  }
  return handled};
const sendViewer=(count,source)=>{if(Number.isFinite(count)&&count>=0&&count!==lastViewer){lastViewer=count;send({type:'viewers',count,source})}};
const roomRoot=()=>document.querySelector('[data-e2e="live-room"],[class*="DivLiveRoomContainer"],[class*="LiveRoomContainer"],[class*="live-room"]')||document.querySelector('main')||null;
const scanViewers=()=>{
  const root=roomRoot();
  if(root){
    for(const n of root.querySelectorAll('[data-e2e="live-room-viewer-count"],[data-e2e="live-viewer-count"],[data-e2e*="viewer-count"],[class*="ViewerCount"],[class*="viewer-count"],[class*="DivWatchingCount"]')){
      const c=compact(text(n));if(Number.isFinite(c)){sendViewer(c,'dom');return 'dom'}}}
  if(!seedUsed){
    try{
      for(const id of ['SIGI_STATE','__UNIVERSAL_DATA_FOR_REHYDRATION__']){
        const el=document.getElementById(id);if(!el||!el.textContent)continue;
        const data=JSON.parse(el.textContent);const stack=[data];let guardCount=0;
        while(stack.length&&guardCount++<20000){
          const cur=stack.pop();if(!cur||typeof cur!=='object')continue;
          const stats=cur.liveRoomStats||cur.stats;
          if(stats&&typeof stats==='object'&&Number.isFinite(+stats.userCount)){seedUsed=true;sendViewer(+stats.userCount,'seed');return 'seed'}
          for(const k in cur){const v=cur[k];if(v&&typeof v==='object')stack.push(v)}}}
    }catch(e){}
    seedUsed=true;}
  return 'none'};
const offlineWords=/(went offline|is offline|live has ended|ended the live|not live|LIVE has ended)/i;
const loginWall=()=>!!document.querySelector('[data-e2e="login-modal"],[id*="login-modal"],[class*="LoginModal"],[class*="login-modal"],[class*="Captcha"],[id*="captcha"],[class*="verify-bar"]');
const snapshot=()=>{
  const body=document.body?document.body.innerText||'':'';
  return {type:'snapshot',url:location.href,title:document.title,ready:document.readyState,
    videos:document.querySelectorAll('video').length,
    iframes:document.querySelectorAll('iframe').length,
    items:document.querySelectorAll(itemSel).length,
    users:document.querySelectorAll(userSel).length,
    bodies:document.querySelectorAll(bodySel).length,
    room:!!roomRoot(),liveBadge:/\bLIVE\b/.test(body),offline:offlineWords.test(body),
    loginWall:loginWall(),discovered:discoveredLabel.trim().slice(0,200),
    bodyChars:body.length,bodySample:body.replace(/\s+/g,' ').slice(0,400)}};
let lastViewerSource='none';
const scan=(root,allowActivity=true)=>{
  all(root,'video').forEach(guard);
  all(root,itemSel).forEach(n=>process(n));
  all(root,activitySel).forEach(n=>process(n));
  profileFallback(root);
  scanDiscovered();
  lastViewerSource=scanViewers()};
scan(document,false);activityReady=true;
new MutationObserver(ms=>ms.forEach(m=>{
  if(m.addedNodes&&m.addedNodes.length){noteChurn(m.target);m.addedNodes.forEach(n=>{if(n.nodeType===1)scan(n)})}
})).observe(document.documentElement,{childList:true,subtree:true});
setInterval(()=>{scan(document);if(++debugTick%10===0)send(snapshot())},2000);
send(snapshot());
})())JS"));
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
