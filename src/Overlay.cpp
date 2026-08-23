#include "Overlay.hpp"
#include <algorithm>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFrame>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QNetworkCookie>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QInputDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QRandomGenerator>
#include <QSizeGrip>
#include <QStackedLayout>
#include <QTcpSocket>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextFrameFormat>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>
namespace {
constexpr int kMessageIdProperty = QTextFormat::UserProperty + 1;
// Caps how many lines chat_'s QTextDocument keeps. Without this, a
// multi-hour stream's chat/moderation-note history would grow the document
// (and the cost of every future append/reflow) without bound.
constexpr int kMaxChatBlocks = 300;
class DrawnCloseButton final : public QPushButton {
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent*event) override {
        QPushButton::paintEvent(event);QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(isDown()?QColor(QStringLiteral("#ffffff")):underMouse()?QColor(QStringLiteral("#ff91a4")):QColor(QStringLiteral("#c7cede")),2.0,Qt::SolidLine,Qt::RoundCap));
        const QRectF r=rect().adjusted(7,7,-7,-7);p.drawLine(r.topLeft(),r.bottomRight());p.drawLine(r.topRight(),r.bottomLeft());
    }
};
void trimChatBlocks(QTextDocument* doc) {
    while (doc->blockCount() > kMaxChatBlocks) {
        QTextCursor cursor(doc->firstBlock());
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
}
}
#ifdef Q_OS_WIN
#include <windows.h>
#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#endif
namespace {
#ifdef Q_OS_WIN
constexpr int kRestoreHotkeyEscape = 0xC201;
constexpr int kRestoreHotkeyAltC = 0xC202;
#endif
QByteArray reply(int code,const QByteArray&type,const QByteArray&body){return "HTTP/1.1 "+QByteArray::number(code)+(code==200?" OK\r\n":" Not Found\r\n")+"Content-Type: "+type+"\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;}
const char overlayHtml[]=R"HTML(<!doctype html><meta charset=utf-8><style>
html,body{margin:0;background:transparent;overflow:hidden;font-family:'Segoe UI',sans-serif;color:white}
.m{margin:5px 8px;padding:6px 9px;border-radius:12px;background:transparent;text-shadow:var(--outline,none);opacity:1;transform:translateY(0);transition:opacity .65s ease,transform .65s ease}.pi{width:18px;height:18px;object-fit:contain;vertical-align:-4px;margin-right:6px}
.m.fade{opacity:0;transform:translateY(-8px)}.u{font-weight:800;margin-right:7px}.twitch{border-left:4px solid #9146ff}.youtube,.yt_shorts{border-left:4px solid #ff334f}.tiktok{border-left:4px solid #18e0d5}.kick{border-left:4px solid #53fc18}.rumble{border-left:4px solid #85c742}
</style><main id=c></main><script>
const BG={HOST:'\u{1F3A5}',MOD:'⚔️',VIP:'\u{1F48E}',PRIME:'\u{1F451}',SUB:'⭐',CHECK:'✅',MONEY:'\u{1F4B0}'};const BO=['HOST','MOD','VIP','PRIME','SUB','CHECK','MONEY'];function badges(l){return BO.filter(k=>(l||[]).includes(k)).map(k=>BG[k]).join('')}function outline(n){n=Math.max(0,Math.min(8,n|0));if(!n)return'none';let s=[];for(let x=-n;x<=n;x++)for(let y=-n;y<=n;y++)if((x||y)&&x*x+y*y<=n*n+n)s.push(`${x}px ${y}px 0 #000`);return s.join(',')}
let n=0,clearGeneration=-1;async function p(){try{let r=await fetch('/api/messages?since='+n),d=await r.json();if(clearGeneration<0)clearGeneration=d.clear_generation;else if(clearGeneration!==d.clear_generation){c.replaceChildren();clearGeneration=d.clear_generation}document.body.style.background=d.background;document.documentElement.style.background=d.background;document.documentElement.style.setProperty('--outline',outline(d.outline_thickness));for(let m of d.messages){n=Math.max(n,m.cursor);let x=document.createElement('div');x.className='m '+m.platform;x.innerHTML=(d.show_platform_icons?'<img class=pi src="/platform-icon/'+m.platform+'">':'')+'<span class=u style="color:'+m.color+'"></span><span class=x></span>';let b=badges(m.badges),pin=m.meta?.pinned?'📌 ':'';x.querySelector('.u').textContent=pin+(b?b+' ':'')+m.user;x.querySelector('.x').textContent=m.text;c.append(x);if(d.fade_seconds>0)setTimeout(()=>{x.classList.add('fade');setTimeout(()=>x.remove(),700)},d.fade_seconds*1000)}while(c.children.length>80)c.firstChild.remove();scrollTo(0,document.body.scrollHeight)}catch(e){}setTimeout(p,600)}p()
</script>)HTML";

const char mobileHtml[]=R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover,user-scalable=no">
<meta name="theme-color" content="#090d18"><meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent"><meta name="apple-mobile-web-app-title" content="Leapcast">
<link rel="manifest" href="/mobile-manifest/%TOKEN%.webmanifest"><link rel="apple-touch-icon" href="/mobile-icon.png"><title>Leapcast Phone Connect</title>
<style>:root{color-scheme:dark;--bg:#090d18;--panel:#111827;--line:#28334a;--muted:#909bb2;--cyan:#53cdf3}*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}html,body{margin:0;min-height:100%;background:var(--bg);color:#f7f9ff;font:15px -apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}body{padding:calc(env(safe-area-inset-top) + 12px) 12px calc(env(safe-area-inset-bottom) + 82px)}header{display:flex;align-items:center;gap:10px;min-height:48px}.mark{width:40px;height:40px;border-radius:13px;background:linear-gradient(145deg,#53cdf3,#7857ff);display:grid;place-items:center;font-weight:900;color:#07101a}.brand{font-weight:900;letter-spacing:.7px}.sub,.muted{color:var(--muted);font-size:12px}.status{margin-left:auto;border:1px solid var(--line);border-radius:999px;padding:6px 9px;color:#ff8292;font-size:11px;font-weight:800}.status.ok{color:#72efb0}.card{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:14px;margin:12px 0}button{border:1px solid var(--line);background:#172238;color:#eef4ff;border-radius:11px;padding:10px 12px;font:inherit;font-weight:750}button.primary{width:100%;background:linear-gradient(135deg,#437fe8,#7857ff);border:0}.stats{display:grid;grid-template-columns:1fr 1fr;gap:8px}.num{font-size:23px;font-weight:900}.filters{display:flex;gap:7px;overflow:auto;margin:10px 0}.filters button{white-space:nowrap;border-radius:999px}.filters button.on{background:#274263;border-color:#4e7fac}.msg,.item{background:var(--panel);border:1px solid var(--line);border-left:4px solid var(--accent,#7a879f);border-radius:14px;padding:11px;margin:8px 0;overflow-wrap:anywhere}.who{font-weight:850;margin-right:6px}.actions{display:flex;gap:6px;margin-top:9px;flex-wrap:wrap}.actions button{font-size:12px;padding:7px 9px}.danger{color:#ff91a4}.twitch{--accent:#9146ff}.youtube,.yt_shorts{--accent:#ff334f}.tiktok{--accent:#18e0d5}.kick{--accent:#53fc18}.rumble{--accent:#85c742}.empty{text-align:center;color:var(--muted);padding:50px 15px}.install{background:#122235;border:1px solid #315377;border-radius:14px;padding:11px;margin:10px 0;color:#dbeeff;font-size:13px}.update{display:none;background:linear-gradient(135deg,#30214c,#172b49);border:1px solid #795bd3}.update.show{display:block}.view{display:none}.view.on{display:block}.mainnav{position:fixed;z-index:8;left:8px;right:8px;bottom:calc(env(safe-area-inset-bottom) + 8px);display:grid;grid-template-columns:repeat(4,1fr);gap:5px;padding:7px;background:rgba(13,18,31,.96);border:1px solid var(--line);border-radius:17px;backdrop-filter:blur(16px)}.mainnav button{padding:10px 3px;font-size:11px}.mainnav button.on{background:#674fdd;border-color:#8b7aef}.sectionTitle{font-size:18px;font-weight:900;margin:17px 2px 8px}.setting{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;margin:14px 0}.setting input[type=range]{grid-column:1/-1;margin:0}.setting input[type=checkbox]{width:24px;height:24px;margin:0}.pill{display:inline-block;border-radius:999px;background:#202b40;padding:4px 8px;font-size:11px;color:#b9c5dc}@media(min-width:700px){body{max-width:720px;margin:auto}.mainnav{max-width:704px;margin:auto}}</style></head><body onload="polling=true;poll();viewers();control()">
<main id="app"><header><div class="mark">L</div><div><div class="brand">LEAPCAST PHONE CONNECT</div><div class="sub">Windows control center</div></div><div id="status" class="status">CONNECTING</div></header><section id="updateBanner" class="card update"><b id="updateTitle">Windows update available</b><div id="updateNotes" class="muted"></div><div class="actions"><button id="updateNow" class="primary">Update Windows App</button></div><div class="muted">Leapcast will close, update, and relaunch. Return here afterward; if the address changes, scan the new QR code shown on the PC.</div></section><div class="install">Optional install: in Safari tap <b>Share → Add to Home Screen</b>, keep <b>Open as Web App</b> enabled, then tap Add.</div>
<section id="chatView" class="view on"><section class="stats"><div class="card"><div id="total" class="num">0</div><div class="muted">Watching</div></div><div class="card"><div id="active" class="num">0</div><div class="muted">Live sources</div></div></section><nav id="filters" class="filters"></nav><section id="chat"><div class="empty">Waiting for chat…</div></section></section>
<section id="eventsView" class="view"><div class="sectionTitle">Stream Events</div><div class="muted">Followers, subscriptions, donations, memberships, and supported platform alerts from the Windows session.</div><section id="events"><div class="empty">Waiting for events…</div></section></section>
<section id="moderationView" class="view"><div class="sectionTitle">Moderation</div><div class="actions"><button id="refreshModeration">Refresh Bans & Requests</button></div><div class="sectionTitle">Active Restrictions</div><section id="bans"><div class="empty">No restrictions loaded.</div></section><div class="sectionTitle">Twitch Unban Requests</div><section id="appeals"><div class="empty">No pending requests.</div></section></section>
<section id="settingsView" class="view"><div class="sectionTitle">Windows Settings</div><div class="card"><label class="setting"><span>AutoMod enabled</span><input id="automodSetting" type="checkbox"></label><label class="setting"><span>Overlay opacity</span><span id="opacityValue" class="pill">0%</span><input id="opacitySetting" type="range" min="0" max="100"></label><label class="setting"><span>Text outline</span><span id="outlineValue" class="pill">2 px</span><input id="outlineSetting" type="range" min="0" max="8"></label><label class="setting"><span>Message fade timer</span><span id="fadeValue" class="pill">Never</span><input id="fadeSetting" type="range" min="0" max="300" step="5"></label></div><div class="muted">Changes apply immediately to Leapcast Studio on the connected Windows PC.</div></section>
<nav class="mainnav"><button data-view="chatView" class="on">Chat</button><button data-view="eventsView">Events</button><button data-view="moderationView">Moderation</button><button data-view="settingsView">Settings</button></nav></main>
<script>const KEY='%TOKEN%',names={all:'All',twitch:'Twitch',youtube:'YouTube',yt_shorts:'Shorts',tiktok:'TikTok',kick:'Kick',rumble:'Rumble'},order=Object.keys(names),byId=id=>document.getElementById(id),appEl=byId('app'),statusEl=byId('status'),filtersEl=byId('filters'),chatEl=byId('chat'),totalEl=byId('total'),activeEl=byId('active'),eventsEl=byId('events'),bansEl=byId('bans'),appealsEl=byId('appeals');let PW='',cursor=0,current='all',messages=[],clearGen=-1,polling=false,settingsLoaded=false;const q=()=>'?key='+encodeURIComponent(KEY),post=(path,args='')=>fetch(path+q()+args,{method:'POST'});document.querySelectorAll('.mainnav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('.mainnav button').forEach(x=>x.classList.toggle('on',x===b));document.querySelectorAll('.view').forEach(x=>x.classList.toggle('on',x.id===b.dataset.view))});for(const p of order){const b=document.createElement('button');b.textContent=names[p];b.className=p==='all'?'on':'';b.onclick=()=>{current=p;document.querySelectorAll('#filters button').forEach(x=>x.classList.toggle('on',x===b));renderChat()};filtersEl.append(b)}function renderChat(){chatEl.replaceChildren();const list=messages.filter(m=>current==='all'||m.platform===current);if(!list.length){chatEl.innerHTML='<div class="empty">Waiting for '+names[current]+' chat…</div>';return}for(const m of list.slice(-150)){const d=document.createElement('article');d.className='msg '+m.platform;const u=document.createElement('span');u.className='who';u.style.color=m.color||'#53cdf3';u.textContent=m.user;const t=document.createElement('span');t.textContent=m.text;if(m.meta?.pinned)u.textContent='📌 '+u.textContent;d.append(u,t);if(['twitch','youtube','yt_shorts'].includes(m.platform)){const a=document.createElement('div');a.className='actions';[['Delete','delete',0],['Timeout 5m','timeout',300],['Ban','ban',0]].forEach(([label,action,seconds])=>{const b=document.createElement('button');b.textContent=label;if(action==='ban')b.className='danger';b.onclick=()=>moderate(m.cursor,action,seconds,m.user);a.append(b)});d.append(a)}chatEl.append(d)}}async function moderate(id,action,seconds,user){if(!confirm(action==='ban'?'Permanently ban '+user+'?':action==='timeout'?'Timeout '+user+' for 5 minutes?':'Delete this message?'))return;const r=await post('/api/mobile/moderate','&cursor='+id+'&action='+action+'&seconds='+seconds);if(!r.ok)alert('Moderation request failed. Check moderation on the PC.')}function renderEvents(list){eventsEl.replaceChildren();if(!list?.length){eventsEl.innerHTML='<div class="empty">Waiting for events…</div>';return}for(const e of list){const d=document.createElement('article');d.className='item '+(e.platform||'');const title=document.createElement('b');title.textContent=e.kind==='twitch_redemption'?(e.user||'Someone')+' • redeemed '+(e.amount||'a Channel Point reward'):(e.user||'Someone')+' • '+(e.kind||'Event');const detail=document.createElement('div');detail.className='muted';detail.textContent=e.kind==='twitch_redemption'?(e.message||''):[e.amount,e.message].filter(Boolean).join(' — ');d.append(title,detail);eventsEl.append(d)}}function banId(b){return b.user_id||b.id||''}function renderBans(groups){bansEl.replaceChildren();let count=0;for(const [platform,list] of Object.entries(groups||{}))for(const b of list){count++;const d=document.createElement('article');d.className='item '+platform;const title=document.createElement('b');title.textContent=(b.user_name||b.user_login||'Unknown user')+' • '+(platform==='twitch'?'Twitch':'YouTube');const detail=document.createElement('div');detail.className='muted';detail.textContent=b.reason||b.type||'Active restriction';const actions=document.createElement('div');actions.className='actions';const unban=document.createElement('button');unban.textContent='Unban';unban.onclick=async()=>{if(confirm('Remove this restriction?')){await post('/api/mobile/unban','&platform='+platform+'&id='+encodeURIComponent(banId(b)));setTimeout(refreshControl,700)}};actions.append(unban);d.append(title,detail,actions);bansEl.append(d)}if(!count)bansEl.innerHTML='<div class="empty">No active restrictions.</div>'}function renderAppeals(list){appealsEl.replaceChildren();if(!list?.length){appealsEl.innerHTML='<div class="empty">No pending requests.</div>';return}for(const a of list){const d=document.createElement('article');d.className='item twitch';const title=document.createElement('b');title.textContent=a.user_name||a.user_login||'Unknown user';const text=document.createElement('div');text.className='muted';text.textContent=a.text||'No appeal message provided.';const actions=document.createElement('div');actions.className='actions';for(const [label,decision] of [['Approve','approve'],['Reject','reject']]){const b=document.createElement('button');b.textContent=label;if(decision==='reject')b.className='danger';b.onclick=async()=>{if(confirm(label+' this unban request?')){await post('/api/mobile/appeal','&id='+encodeURIComponent(a.id||'')+'&decision='+decision);setTimeout(refreshControl,700)}};actions.append(b)}d.append(title,text,actions);appealsEl.append(d)}}function loadSettings(s){if(settingsLoaded)return;settingsLoaded=true;byId('automodSetting').checked=s.automod_enabled!==false;byId('opacitySetting').value=s.overlay_background_opacity||0;byId('outlineSetting').value=s.chat_outline_thickness??2;byId('fadeSetting').value=Math.min(300,s.overlay_fade_seconds||0);showSettingValues()}function showSettingValues(){byId('opacityValue').textContent=byId('opacitySetting').value+'%';byId('outlineValue').textContent=byId('outlineSetting').value+' px';byId('fadeValue').textContent=+byId('fadeSetting').value?byId('fadeSetting').value+' sec':'Never'}async function saveSetting(name,value){await post('/api/mobile/setting','&name='+name+'&value='+encodeURIComponent(value))}byId('automodSetting').onchange=e=>saveSetting('automod_enabled',e.target.checked);for(const [id,name] of [['opacitySetting','overlay_background_opacity'],['outlineSetting','chat_outline_thickness'],['fadeSetting','overlay_fade_seconds']])byId(id).onchange=e=>{showSettingValues();saveSetting(name,e.target.value)};byId('refreshModeration').onclick=async()=>{await post('/api/mobile/refresh');setTimeout(refreshControl,700)};byId('updateNow').onclick=async()=>{if(confirm('Update Leapcast Studio on the Windows PC now? It will close and relaunch.')){await post('/api/mobile/install-update');statusEl.textContent='UPDATING PC';statusEl.classList.remove('ok')}};async function refreshControl(){const r=await fetch('/api/mobile/control'+q(),{cache:'no-store'});if(!r.ok)return;const d=await r.json();renderEvents(d.events);renderBans(d.bans);renderAppeals(d.appeals);loadSettings(d.settings||{});if(d.update?.available){byId('updateBanner').classList.add('show');byId('updateTitle').textContent='Leapcast '+d.update.version+' is available';byId('updateNotes').textContent=(d.update.notes||'A new Windows release is ready.').slice(0,600)}else byId('updateBanner').classList.remove('show')}async function poll(){try{const r=await fetch('/api/mobile/messages'+q()+'&since='+cursor,{cache:'no-store'});if(!r.ok)throw 0;const d=await r.json();if(clearGen<0)clearGen=d.clear_generation;else if(clearGen!==d.clear_generation){messages=[];clearGen=d.clear_generation}for(const m of d.messages){cursor=Math.max(cursor,m.cursor);messages.push(m)}if(messages.length>300)messages=messages.slice(-300);if(d.messages.length)renderChat();statusEl.textContent='CONNECTED';statusEl.classList.add('ok')}catch(e){statusEl.textContent='OFFLINE';statusEl.classList.remove('ok')}setTimeout(poll,700)}async function viewers(){try{const r=await fetch('/api/mobile/viewers'+q(),{cache:'no-store'}),d=await r.json();totalEl.textContent=d.total||0;activeEl.textContent=Object.entries(d).filter(([k,v])=>k!=='total'&&v>0).length}catch(e){}setTimeout(viewers,2500)}async function control(){try{await refreshControl()}catch(e){}setTimeout(control,2500)}</script></body></html>)HTML";
}
OverlayServer::OverlayServer(QObject*p):QObject(p){mobileToken_=QByteArray::number(QRandomGenerator::global()->generate64(),16)+QByteArray::number(QRandomGenerator::global()->generate64(),16);connect(&server_,&QTcpServer::newConnection,this,&OverlayServer::accept);}
void OverlayServer::setMobileToken(const QString&token){const QByteArray clean=token.trimmed().toLatin1();if(clean.size()>=24&&std::all_of(clean.cbegin(),clean.cend(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}))mobileToken_=clean.toLower();}
QString OverlayServer::regenerateMobileToken(){mobileToken_=QByteArray::number(QRandomGenerator::global()->generate64(),16)+QByteArray::number(QRandomGenerator::global()->generate64(),16);return QString::fromLatin1(mobileToken_);}
bool OverlayServer::start(quint16 p){for(int i=0;i<20;++i)if(server_.listen(QHostAddress::AnyIPv4,p+i))return true;return false;} void OverlayServer::stop(){server_.close();}
QUrl OverlayServer::mobileUrl()const{QString host;for(const auto&address:QNetworkInterface::allAddresses()){if(address.protocol()!=QAbstractSocket::IPv4Protocol||address.isLoopback())continue;const QString candidate=address.toString();if(candidate.startsWith("10.")||candidate.startsWith("192.168.")||QRegularExpression(QStringLiteral("^172\\.(1[6-9]|2[0-9]|3[01])\\.")).match(candidate).hasMatch()){host=candidate;break;}}if(host.isEmpty())return {};return QUrl(QStringLiteral("http://%1:%2/mobile/%3").arg(host).arg(port()).arg(QString::fromLatin1(mobileToken_)));}
void OverlayServer::ingest(const ChatMessage&m){messages_.append({++cursor_,m});while(messages_.size()>400)messages_.removeFirst();} void OverlayServer::setViewers(const QString&p,int n){viewers_[p]=n;} void OverlayServer::clear(){messages_.clear();++cursor_;++clearGeneration_;}
void OverlayServer::ingestEvent(const StreamEvent&e){mobileEvents_.prepend(e.toJson());while(mobileEvents_.size()>200)mobileEvents_.removeLast();}
void OverlayServer::setMobileBans(const QString&p,const QJsonArray&b){mobileBans_[p]=b;}
void OverlayServer::setMobileAppeals(const QJsonArray&a){mobileAppeals_=a;}
void OverlayServer::setMobileSettings(const QJsonObject&s){mobileSettings_=s;}
void OverlayServer::setMobileUpdate(const QJsonObject&u){mobileUpdate_=u;}
void OverlayServer::setAppearance(const QColor&background,int opacity,int outline){backgroundColor_=background.isValid()?background:QColor(Qt::black);backgroundOpacityPercent_=qBound(0,opacity,100);outlineThickness_=qBound(0,outline,8);}
void OverlayServer::accept(){while(auto*s=server_.nextPendingConnection()){connect(s,&QTcpSocket::readyRead,this,[this,s]{const auto parts=s->readAll().split('\n').value(0).split(' ');s->write(responseFor(parts.value(0),parts.value(1),s->peerAddress()));s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);}}
QByteArray OverlayServer::responseFor(const QByteArray&method,const QByteArray&t,const QHostAddress&peer){const auto value=[&](const QByteArray&key){const QByteArray field=key+"=";const int p=t.indexOf(field);return p<0?QByteArray():t.mid(p+field.size()).split('&').value(0);};const bool local=peer.isLoopback();const QByteArray mobilePath="/mobile/"+mobileToken_;const bool tokenOk=t.startsWith(mobilePath)||value("key")==mobileToken_;if((t=="/"||t.startsWith("/?"))&&local)return reply(200,"text/html; charset=utf-8",overlayHtml);if(t.startsWith(mobilePath)){QByteArray html(mobileHtml);html.replace("%TOKEN%",mobileToken_);return reply(200,"text/html; charset=utf-8",html);}if(t.startsWith("/mobile-manifest/")&&t.contains(mobileToken_)){const QJsonArray icons{QJsonObject{{"src","/mobile-icon.png"},{"sizes","512x512"},{"type","image/png"},{"purpose","any maskable"}}};const QJsonObject manifest{{"name","Leapcast Phone Connect"},{"short_name","Leapcast"},{"display","standalone"},{"background_color","#090d18"},{"theme_color","#090d18"},{"start_url",QString::fromLatin1(mobilePath)},{"icons",icons}};return reply(200,"application/manifest+json",QJsonDocument(manifest).toJson(QJsonDocument::Compact));}if(t.startsWith("/mobile-icon.png")){QFile icon(QStringLiteral(":/brand/lefroge_chat_icon.png"));if(icon.open(QIODevice::ReadOnly))return reply(200,"image/png",icon.readAll());}if(t.startsWith("/platform-icon/")){const QString platform=QString::fromLatin1(t.mid(15).split('?').value(0));static const QHash<QString,QString> paths{{"twitch",":/brand/twitch.png"},{"youtube",":/brand/youtube.png"},{"yt_shorts",":/brand/youtube_shorts.png"},{"tiktok",":/brand/tiktok.png"},{"kick",":/brand/kick.svg"},{"rumble",":/brand/rumble.svg"}};QFile icon(paths.value(platform));if(icon.open(QIODevice::ReadOnly))return reply(200,platform=="kick"||platform=="rumble"?"image/svg+xml":"image/png",icon.readAll());}const bool mobileOk=tokenOk;const bool mobileMessages=t.startsWith("/api/mobile/messages")&&mobileOk;const bool overlayMessages=t.startsWith("/api/messages")&&local;if(mobileMessages||overlayMessages){quint64 since=0;const int at=t.indexOf("since=");if(at>=0)since=t.mid(at+6).split('&').value(0).toULongLong();QJsonArray a;for(const auto&x:messages_)if(x.first>since){auto o=x.second.toJson();o["cursor"]=static_cast<qint64>(x.first);a.append(o);}const QString bg=QStringLiteral("rgba(%1,%2,%3,%4)").arg(backgroundColor_.red()).arg(backgroundColor_.green()).arg(backgroundColor_.blue()).arg(QString::number(backgroundOpacityPercent_/100.0,'f',2));return reply(200,"application/json",QJsonDocument(QJsonObject{{"messages",a},{"cursor",static_cast<qint64>(cursor_)},{"clear_generation",static_cast<qint64>(clearGeneration_)},{"fade_seconds",fadeSeconds_},{"background",bg},{"outline_thickness",outlineThickness_},{"show_platform_icons",showPlatformIcons_}}).toJson(QJsonDocument::Compact));}const bool mobileViewers=t.startsWith("/api/mobile/viewers")&&mobileOk;const bool overlayViewers=t.startsWith("/api/viewers")&&local;if(mobileViewers||overlayViewers){QJsonObject o;int total=0;for(auto i=viewers_.cbegin();i!=viewers_.cend();++i){o[i.key()]=i.value();total+=i.value();}o["total"]=total;return reply(200,"application/json",QJsonDocument(o).toJson(QJsonDocument::Compact));}if(t.startsWith("/api/mobile/control")&&mobileOk){QJsonObject bans;for(auto i=mobileBans_.cbegin();i!=mobileBans_.cend();++i)bans[i.key()]=i.value();return reply(200,"application/json",QJsonDocument(QJsonObject{{"events",mobileEvents_},{"bans",bans},{"appeals",mobileAppeals_},{"settings",mobileSettings_},{"update",mobileUpdate_}}).toJson(QJsonDocument::Compact));}if(method=="POST"&&mobileOk){if(t.startsWith("/api/mobile/moderate")){const quint64 wanted=value("cursor").toULongLong();const QByteArray action=value("action");for(const auto&x:messages_)if(x.first==wanted){if(action=="delete")emit mobileDeleteRequested(x.second);else emit mobileModerationRequested(x.second,action=="ban"?0:qBound(30,value("seconds").toInt(),86400),QStringLiteral("Phone Connect moderation"));return reply(200,"application/json","{\"ok\":true}");}return reply(404,"application/json","{\"ok\":false}");}if(t.startsWith("/api/mobile/refresh")){emit mobileRefreshModerationRequested();return reply(200,"application/json","{\"ok\":true}");}if(t.startsWith("/api/mobile/unban")){emit mobileUnbanRequested(QString::fromLatin1(value("platform")),QString::fromLatin1(value("id")));return reply(200,"application/json","{\"ok\":true}");}if(t.startsWith("/api/mobile/appeal")){emit mobileAppealRequested(QString::fromLatin1(value("id")),value("decision")=="approve");return reply(200,"application/json","{\"ok\":true}");}if(t.startsWith("/api/mobile/setting")){const QString name=QString::fromLatin1(value("name"));const QByteArray raw=value("value");emit mobileSettingRequested(name,raw=="true"?QVariant(true):raw=="false"?QVariant(false):QVariant(raw.toInt()));return reply(200,"application/json","{\"ok\":true}");}if(t.startsWith("/api/mobile/install-update")){emit mobileInstallUpdateRequested();return reply(200,"application/json","{\"ok\":true}");}}return reply(404,"text/plain","Not found");}
PopoutChat::PopoutChat(QWidget*p):QWidget(p){
    setObjectName(QStringLiteral("popoutWindow"));
    // The application theme gives every QWidget an opaque background.  A
    // QTextBrowser owns child widgets (most importantly its viewport and the
    // scroll-area corner), so styling only this top-level widget still allowed
    // one of those children to paint the large white/opaque rectangle seen in
    // the pop-out.  Keep the entire pop-out subtree transparent; controls that
    // need a background provide their own more-specific styles below.
    setStyleSheet(QStringLiteral(
        "QWidget#popoutWindow, QWidget#popoutWindow QWidget{background-color:transparent;border:0;}"));
    setWindowTitle("Leapcast Studio Pop-out");
    setWindowIcon(QIcon(":/brand/lefroge_chat_icon.png"));
    resize(460,720);
    // A window that keeps the native Windows title bar/frame relies on DWM
    // glass composition to make its client area translucent, and that
    // composition silently falls back to an opaque (white) client surface
    // whenever DWM composition or the system's "Transparency effects"
    // setting isn't fully active — which is exactly the symptom reported
    // here. Going frameless makes Qt use a per-pixel-alpha layered window
    // instead, which blends correctly regardless of DWM/compositor state.
    // The custom toolbar below replaces the lost native title bar/close
    // button/drag handle; a QSizeGrip in the corner replaces native resize.
    setWindowFlag(Qt::FramelessWindowHint,true);
    setWindowFlag(Qt::WindowStaysOnTopHint,true);
    setAttribute(Qt::WA_TranslucentBackground,true);
    setAttribute(Qt::WA_NoSystemBackground,true);
    setAutoFillBackground(false);
    qApp->installNativeEventFilter(this);
    auto*l=new QVBoxLayout(this);
    l->setContentsMargins(8,8,8,8);
    l->setSpacing(6);

    titleBar_=new QWidget;
    titleBar_->setCursor(Qt::SizeAllCursor);
    titleBar_->installEventFilter(this);
    // Always visible regardless of the chat/panel opacity setting — with the
    // window frameless, this strip is the only way to find the drag handle
    // and close button, so it can't fade away with everything else at 0%.
    titleBar_->setStyleSheet(QStringLiteral(
        "background-color:rgba(10,12,20,215);border-top-left-radius:10px;border-top-right-radius:10px;"));
    auto*toolbar=new QHBoxLayout(titleBar_);
    toolbar->setContentsMargins(0,0,0,0);
    toolbar->setSpacing(6);
    toolbarTitle_=new QLabel("LEAPCAST STUDIO");
    toolbarTitle_->setStyleSheet("color:#8f9bb5;font-size:8pt;font-weight:800;letter-spacing:1px;");
    toolbarTitle_->setCursor(Qt::SizeAllCursor);
    toolbarTitle_->installEventFilter(this);
    toolbar->addWidget(toolbarTitle_);
    toolbar->addStretch();
    clipButton_=new QPushButton("Clip");
    clipButton_->setToolTip("Create a Twitch clip of this stream, just like the Clip button on twitch.tv");
    clipButton_->setCursor(Qt::PointingHandCursor);
    clipButton_->setStyleSheet(
        "QPushButton{background:#9146ff;color:white;border:0;border-radius:8px;padding:5px 12px;font-weight:700;font-size:9pt;}"
        "QPushButton:disabled{background:#2a2f42;color:#66708c;}"
        "QPushButton:hover:!disabled{background:#a366ff;}");
    clipButton_->setEnabled(false);
    connect(clipButton_,&QPushButton::clicked,this,&PopoutChat::clipRequested);
    toolbar->addWidget(clipButton_);
    auto*closeButton=new DrawnCloseButton;
    closeButton->setToolTip(QStringLiteral("Close pop-out"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedSize(22,22);
    closeButton->setStyleSheet(
        "QPushButton{background:#20283a;color:#c7cede;border:0;border-radius:6px;font-weight:700;}"
        "QPushButton:hover{background:#44202b;color:#ff91a4;}");
    connect(closeButton,&QPushButton::clicked,this,&QWidget::close);
    toolbar->addWidget(closeButton);
    l->addWidget(titleBar_);

    pinned_=new QLabel;pinned_->setTextFormat(Qt::RichText);pinned_->setWordWrap(true);pinned_->setTextInteractionFlags(Qt::TextSelectableByMouse);pinned_->hide();l->addWidget(pinned_);
    event_=new QLabel;
    event_->setAlignment(Qt::AlignCenter);
    event_->setWordWrap(true);
    event_->hide();
    event_->setStyleSheet("background:#171d2dee;border:1px solid #53cdf3;border-radius:12px;padding:12px;font-size:14pt;font-weight:800;");
    l->addWidget(event_);

    auto*chatHost=new QWidget;
    chatHost->setAttribute(Qt::WA_TranslucentBackground,true);
    chatHost->setAttribute(Qt::WA_NoSystemBackground,true);
    chatHost->setAutoFillBackground(false);
    chatHost->setStyleSheet(QStringLiteral("background:transparent;border:0;"));
    chatStack_=new QStackedLayout(chatHost);
    chatStack_->setContentsMargins(0,0,0,0);
    chatStack_->setStackingMode(QStackedLayout::StackAll);
    chat_=new ChatBrowser;
    chat_->setObjectName(QStringLiteral("popoutChat"));
    chat_->setFrameShape(QFrame::NoFrame);
    chat_->setAttribute(Qt::WA_TranslucentBackground,true);
    chat_->setAttribute(Qt::WA_OpaquePaintEvent,false);
    chat_->setAutoFillBackground(false);
    chat_->viewport()->setAttribute(Qt::WA_TranslucentBackground,true);
    chat_->viewport()->setAttribute(Qt::WA_OpaquePaintEvent,false);
    chat_->viewport()->setAttribute(Qt::WA_NoSystemBackground,true);
    chat_->viewport()->setAutoFillBackground(false);
    connect(chat_,&ChatBrowser::chatContextMenuRequested,this,&PopoutChat::showChatContextMenu);
    chatStack_->addWidget(chat_);
    l->addWidget(chatHost,1);

    clipStatus_=new QLabel;
    clipStatus_->setWordWrap(true);
    clipStatus_->setAlignment(Qt::AlignCenter);
    clipStatus_->setTextFormat(Qt::RichText);
    // Opened in the embedded clip editor (see openClipEditor) rather than the
    // user's external browser.
    clipStatus_->setOpenExternalLinks(false);
    connect(clipStatus_,&QLabel::linkActivated,this,[this](const QString&link){emit openClipEditor(QUrl(link));});
    clipStatus_->hide();
    l->addWidget(clipStatus_);

    auto*bottomRow=new QHBoxLayout;
    bottomRow->setSpacing(6);
    viewers_=new QLabel("0 watching");
    bottomRow->addWidget(viewers_,1);
    // Frameless windows lose the native resize border; a QSizeGrip in the
    // corner is the standard Qt replacement.
    auto*sizeGrip=new QSizeGrip(this);
    sizeGrip->setStyleSheet(QStringLiteral("background:transparent;"));
    bottomRow->addWidget(sizeGrip,0,Qt::AlignBottom|Qt::AlignRight);
    l->addLayout(bottomRow);
    applyOpacity();
}
PopoutChat::~PopoutChat(){
    unregisterRestoreHotkeys();
    qApp->removeNativeEventFilter(this);
}
void PopoutChat::appendMessage(const ChatMessage&m){
    const qint64 id=++nextMessageSeq_;
    historyById_.insert(id,m);
    historyById_.remove(id-400);
    QTextCursor cursor(chat_->document());
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat format;
    format.setProperty(kMessageIdProperty,id);
    if(!chat_->document()->isEmpty())cursor.insertBlock(format);
    else cursor.setBlockFormat(format);
    QTextCharFormat messageFormat=cursor.charFormat();
    messageFormat.setProperty(kMessageIdProperty,id);
    cursor.setCharFormat(messageFormat);
    const QString badges=badgeGlyphs(m.badges);static const QHash<QString,QString> icons{{"twitch",":/brand/twitch.png"},{"youtube",":/brand/youtube.png"},{"yt_shorts",":/brand/youtube_shorts.png"},{"tiktok",":/brand/tiktok.png"},{"kick",":/brand/kick.svg"},{"rumble",":/brand/rumble.svg"}};const QString platformIcon=showPlatformIcons_&&icons.contains(m.platform)?QStringLiteral("<img src='%1' width='18' height='18' style='vertical-align:middle;margin-right:5px'>").arg(icons.value(m.platform)):QString();
    // Chat lines remain unfilled so the game/stream is visible between and
    // behind every message. Font, size, and outline are applied after the
    // rich-text colors so the user's Pop-out Chat settings win consistently.
    const int messageStart=cursor.position();
    cursor.insertHtml(QString("<span style='background-color:transparent;color:#ffffff'>%1%2%3 <span style='color:#ffffff;font-weight:600'>%4</span></span>")
        .arg(platformIcon,badges.isEmpty()?QString():badges+QStringLiteral(" "),chatNameHtml(m),m.text.toHtmlEscaped()));
    const int messageEnd=cursor.position();
    cursor.setPosition(messageStart);cursor.setPosition(messageEnd,QTextCursor::KeepAnchor);
    QTextCharFormat appearance;appearance.setFontFamily(fontFamily_);appearance.setFontPointSize(fontSizePoints_);
    appearance.setTextOutline(outlineThickness_>0?QPen(QColor(QStringLiteral("#000000")),outlineThickness_,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin):QPen(Qt::NoPen));
    cursor.mergeCharFormat(appearance);cursor.clearSelection();cursor.setPosition(messageEnd);
    chat_->moveCursor(QTextCursor::End);
    chat_->ensureCursorVisible();
    trimChatBlocks(chat_->document());
}
void PopoutChat::appendModerationNote(const QString&user,const QString&reason){
    QTextCursor cursor(chat_->document());
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat format;
    if(!chat_->document()->isEmpty())cursor.insertBlock(format);
    else cursor.setBlockFormat(format);
    const bool manual=reason==QStringLiteral("Message Moderated");
    cursor.insertHtml(manual
        ? QStringLiteral("<span style='color:#7f8ba5;font-style:italic;font-size:9pt'>&lt;Message Moderated&gt;</span>")
        : QStringLiteral("<span style='color:#7f8ba5;font-style:italic;font-size:9pt'>&#9888; %1's message was removed by AutoMod (%2)</span>").arg(user.toHtmlEscaped(),reason.toHtmlEscaped()));
    chat_->moveCursor(QTextCursor::End);
    chat_->ensureCursorVisible();
    trimChatBlocks(chat_->document());
}
void PopoutChat::showChatContextMenu(const QPoint&globalPos){
    const QPoint pos=chat_->viewport()->mapFromGlobal(globalPos);
    QTextCursor clicked=chat_->cursorForPosition(pos);
    qint64 id=clicked.blockFormat().property(kMessageIdProperty).toLongLong();
    if(!historyById_.contains(id))id=clicked.charFormat().property(kMessageIdProperty).toLongLong();
    // Rich-text layout can land a right-click on the spacer block immediately
    // beside a message. Resolve only the directly adjacent blocks—never jump
    // to an unrelated, older user.
    if(!historyById_.contains(id)){
        const QTextBlock original=clicked.block();
        for(const auto&candidate:{original.previous(),original.next()}){
            if(!candidate.isValid())continue;
            const qint64 candidateId=candidate.blockFormat().property(kMessageIdProperty).toLongLong();
            if(historyById_.contains(candidateId)){id=candidateId;break;}
        }
    }
    QMenu menu(this);
    auto*copyAction=menu.addAction(QStringLiteral("Copy"));
    copyAction->setEnabled(chat_->textCursor().hasSelection());
    connect(copyAction,&QAction::triggered,chat_,&QTextBrowser::copy);
    if(historyById_.contains(id)){
        const ChatMessage msg=historyById_.value(id);
        const bool isTwitch=msg.platform==QStringLiteral("twitch")&&!msg.metadata.value(QStringLiteral("channel_point_redemption")).toBool();
        const bool isYouTube=msg.platform==QStringLiteral("youtube")||msg.platform==QStringLiteral("yt_shorts");
        // Only Twitch and YouTube/Shorts have a working moderation API wired up
        // here; TikTok moderation happens in TikTok's own LIVE room (see the
        // "Open TikTok LIVE in browser" action), so no actions are offered for it.
        if(isTwitch||isYouTube){
            menu.addSeparator();
            auto*header=menu.addAction(QStringLiteral("Moderate %1").arg(msg.user));
            header->setEnabled(false);
            connect(menu.addAction(QStringLiteral("Delete message")),&QAction::triggered,this,[this,msg]{emit deleteMessageRequested(msg);});
            connect(menu.addAction(QStringLiteral("Timeout")),&QAction::triggered,this,[this,msg]{bool ok=false;const QString reason=QInputDialog::getMultiLineText(this,"Timeout reason","Why is this user being timed out?",QString(),&ok).trimmed();if(ok&&!reason.isEmpty())emit timeoutUserRequested(msg,300,reason);});
            // Twitch calls this a "Ban"; YouTube Studio calls the same
            // permanent action "Hide user on this channel".
            connect(menu.addAction(isYouTube?QStringLiteral("Hide from channel"):QStringLiteral("Ban")),
                    &QAction::triggered,this,[this,msg]{bool ok=false;const QString reason=QInputDialog::getMultiLineText(this,"Ban reason","Why is this user being banned?",QString(),&ok).trimmed();if(ok&&!reason.isEmpty())emit timeoutUserRequested(msg,0,reason);});
        }else if(msg.platform==QStringLiteral("rumble")){
            menu.addSeparator();
            connect(menu.addAction(QStringLiteral("Open Rumble moderation")),&QAction::triggered,this,[]{QDesktopServices::openUrl(QUrl(QStringLiteral("https://rumble.com/account/livestreams")));});
        }
    }
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Select All")),&QAction::triggered,chat_,&QTextBrowser::selectAll);
    menu.exec(globalPos);
}
void PopoutChat::showEvent(const StreamEvent&e){QString action=e.kind==QStringLiteral("twitch_redemption")?QStringLiteral("redeemed %1").arg(e.amount):e.kind.contains("donation")?"Donated "+e.amount:e.kind.contains("follow")?"has Followed":e.kind.contains("gifted_sub")?QStringLiteral("gifted %1 subscription%2").arg(e.amount.isEmpty()?QStringLiteral("1"):e.amount,e.amount==QStringLiteral("1")?QString():QStringLiteral("s")):"has Subscribed";QString colour=e.kind.contains("donation")?"#f6c85f":e.platform=="twitch"?"#b48cff":e.platform=="youtube"?"#ff5573":e.platform=="rumble"?"#85c742":"#55e5d3";event_->setText(QString("<span style='color:#a8b0c7'>SYSTEM MESSAGE</span><br><b>%1</b> <span style='color:%2'>%3</span>%4").arg(e.user.toHtmlEscaped(),colour,action.toHtmlEscaped(),e.message.isEmpty()?QString():QStringLiteral("<br><span style='color:#c7cede'>%1</span>").arg(e.message.toHtmlEscaped())));if(opacityPercent_>0)event_->show();QTimer::singleShot(6000,event_,&QWidget::hide);}
void PopoutChat::showTikTokActivity(const StreamEvent&e){QTextCursor cursor(chat_->document());cursor.movePosition(QTextCursor::End);if(!chat_->document()->isEmpty())cursor.insertBlock();const QString icon=showPlatformIcons_?QStringLiteral("<img src=':/brand/tiktok.png' width='18' height='18' style='vertical-align:middle;margin-right:5px'>"):QString();const QString action=e.kind.endsWith(QStringLiteral("join"))?QStringLiteral("joined the LIVE"):e.kind.endsWith(QStringLiteral("follow"))?QStringLiteral("followed the creator"):e.amount.isEmpty()?QStringLiteral("sent likes"):QStringLiteral("sent %1 likes").arg(e.amount);cursor.insertHtml(QStringLiteral("%1<span style='color:#7f8ba5;font-style:italic'><b style='color:#18e0d5'>%2</b> %3</span>").arg(icon,e.user.toHtmlEscaped(),action.toHtmlEscaped()));chat_->moveCursor(QTextCursor::End);chat_->ensureCursorVisible();trimChatBlocks(chat_->document());}
void PopoutChat::setPinnedMessage(const QString&platform,const ChatMessage&message,bool active){
    if(active)pinnedMessages_[platform]=message;else pinnedMessages_.remove(platform);if(!pinned_)return;
    QStringList lines;const QHash<QString,QString> names{{QStringLiteral("twitch"),QStringLiteral("Twitch")},{QStringLiteral("youtube"),QStringLiteral("YouTube")},{QStringLiteral("yt_shorts"),QStringLiteral("YouTube Shorts")}};
    for(const auto&key:{QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("yt_shorts")})if(pinnedMessages_.contains(key)){const auto&m=pinnedMessages_[key];lines<<QStringLiteral("<b>&#128204; %1</b> &nbsp; <b>%2</b>: %3").arg(names.value(key,key).toHtmlEscaped(),m.user.toHtmlEscaped(),m.text.toHtmlEscaped());}
    pinned_->setText(lines.join(QStringLiteral("<br>")));pinned_->setVisible(!lines.isEmpty());applyOpacity();
}
void PopoutChat::setViewers(const QString&p,int n){
    counts_[p]=n;
    int total=0;
    for(int v:counts_)total+=v;
    // Fixed display order so the breakdown doesn't reshuffle as counts update.
    // Keep a fixed display order so the breakdown stays stable as sources update.
    static const QList<QPair<QString,QString>> platforms{
        {QStringLiteral("twitch"),QStringLiteral("Twitch")},
        {QStringLiteral("youtube"),QStringLiteral("YouTube")},
        {QStringLiteral("yt_shorts"),QStringLiteral("Shorts")},
        {QStringLiteral("tiktok"),QStringLiteral("TikTok")},
        {QStringLiteral("kick"),QStringLiteral("Kick")},
        {QStringLiteral("rumble"),QStringLiteral("Rumble")}};
    QStringList parts;
    for(const auto&platform:platforms){
        const int count=counts_.value(platform.first,0);
        if(count>0)parts<<QStringLiteral("%1 %2").arg(platform.second,QString::number(count));
    }
    QString text=QString::number(total)+QStringLiteral(" WATCHING");
    // Only worth breaking down once two or more sources are actually live —
    // otherwise it's just repeating the total.
    if(parts.size()>1)text+=QStringLiteral("  ·  ")+parts.join(QStringLiteral("  ·  "));
    viewers_->setText(text);
}
void PopoutChat::clearMessages(){chat_->clear();historyById_.clear();}
void PopoutChat::setGhostMode(bool on){
    if(ghostMode_==on)return;
    ghostMode_=on;
    setWindowFlag(Qt::WindowTransparentForInput,on);
    if(on)registerRestoreHotkeys();else unregisterRestoreHotkeys();
    show();
    emit ghostModeChanged(ghostMode_);
}
void PopoutChat::setClearBackground(bool on){clearBackground_=on;applyOpacity();}
void PopoutChat::setOpacityPercent(int n){opacityPercent_=qBound(0,n,100);applyOpacity();}
void PopoutChat::setAppearance(const QColor&background,int outline,const QString&fontFamily,int fontSizePoints){backgroundColor_=background.isValid()?background:QColor(Qt::black);outlineThickness_=qBound(0,outline,8);fontFamily_=fontFamily.trimmed().isEmpty()?QStringLiteral("Segoe UI"):fontFamily;fontSizePoints_=qBound(8,fontSizePoints,36);applyOpacity();applyTextOutline();}
void PopoutChat::applyTextOutline(){
    if(!chat_)return;
    QFont font(fontFamily_);font.setPointSize(fontSizePoints_);chat_->setFont(font);chat_->document()->setDefaultFont(font);
    QTextCursor cursor(chat_->document());cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setFontFamily(fontFamily_);format.setFontPointSize(fontSizePoints_);
    format.setTextOutline(outlineThickness_>0?QPen(QColor(QStringLiteral("#000000")),outlineThickness_,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin):QPen(Qt::NoPen));
    cursor.mergeCharFormat(format);
    QTextBlock block=chat_->document()->firstBlock();
    while(block.isValid()){
        QTextBlockFormat blockFormat=block.blockFormat();
        blockFormat.setTopMargin(0);
        blockFormat.setBottomMargin(0);
        QTextCursor(block).setBlockFormat(blockFormat);
        block=block.next();
    }
    chat_->viewport()->update();
}

void ChatBrowser::paintEvent(QPaintEvent* event){
    // Clear the WHOLE viewport (not just event->rect()) before every paint.
    // A layered, per-pixel-alpha top-level window has no automatic
    // double-buffering safety net the way a normal composited window does:
    // clearing only the damaged rectangle can leave Windows compositing a
    // stretched copy of the last full frame into the newly exposed area
    // during/after a resize, which shows up as a distorted, duplicated-text
    // frame frozen behind the current one.
    QPainter clearPainter(viewport());
    clearPainter.setCompositionMode(QPainter::CompositionMode_Source);
    clearPainter.fillRect(viewport()->rect(),Qt::transparent);
    clearPainter.end();
    QTextBrowser::paintEvent(event);
}
void PopoutChat::resizeEvent(QResizeEvent* event){
    QWidget::resizeEvent(event);
    applyOpacity();
    if(chat_)chat_->viewport()->update();
    // Force an immediate, full-window synchronous repaint rather than a
    // queued update(). During a live resize of a layered window, Windows can
    // otherwise keep showing a stretched copy of the pre-resize frame until
    // something else happens to trigger the next paint.
    repaint();
}
void PopoutChat::paintEvent(QPaintEvent* event){
    Q_UNUSED(event)
    // Always clear the full window, not just the damaged rect — see the
    // comment in ChatBrowser::paintEvent for why partial clears corrupt a
    // layered translucent window's backing store during/after a resize.
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(),Qt::transparent);
}
void PopoutChat::applyOpacity(){
    const int alpha=clearBackground_?0:qRound(opacityPercent_*255.0/100.0);
    // The slider has one simple visual range: transparent -> translucent black
    // -> solid black. Never blend toward the platform's default white base.
    const QString panel=QStringLiteral("rgba(%1,%2,%3,%4)").arg(backgroundColor_.red()).arg(backgroundColor_.green()).arg(backgroundColor_.blue()).arg(alpha);
    // QTextBrowser uses a separate viewport widget. Styling only the outer
    // control lets Windows/Qt paint that viewport with its default white base,
    // which is why the chat became a white sheet after native transparency was
    // enabled. Style both surfaces explicitly and set a readable text palette.
    chat_->setStyleSheet(QStringLiteral(
        "QTextBrowser#popoutChat{background-color:%1;color:#f6f8ff;border:0;border-radius:12px;font-size:12pt;}"
        "QTextBrowser#popoutChat QWidget{background-color:transparent;border:0;}"
        "QTextBrowser#popoutChat QScrollBar{background-color:transparent;}").arg(panel));
    // The viewport is the surface that actually fills the text area on
    // Windows.  Give it the selected alpha directly rather than relying on
    // stylesheet inheritance from QAbstractScrollArea.
    chat_->viewport()->setStyleSheet(QStringLiteral(
        "background-color:%1;color:#f6f8ff;border:0;border-radius:12px;").arg(panel));
    viewers_->setStyleSheet(QStringLiteral("background:%1;padding:8px;border-radius:9px;color:#68e9d5;font-weight:700;").arg(panel));
    if(pinned_)pinned_->setStyleSheet(QStringLiteral("background:%1;padding:8px;border:1px solid rgba(255,210,90,150);border-radius:9px;color:#f6f8ff;").arg(panel));
    QPalette chatPalette=chat_->palette();const QColor panelColor(backgroundColor_.red(),backgroundColor_.green(),backgroundColor_.blue(),alpha);
    chatPalette.setColor(QPalette::Base,panelColor);chatPalette.setColor(QPalette::Window,panelColor);
    chatPalette.setColor(QPalette::AlternateBase,panelColor);
    chatPalette.setColor(QPalette::Text,QColor(QStringLiteral("#f6f8ff")));
    chatPalette.setColor(QPalette::WindowText,QColor(QStringLiteral("#f6f8ff")));
    chat_->setPalette(chatPalette);chat_->viewport()->setPalette(chatPalette);
    chat_->setAutoFillBackground(false);chat_->viewport()->setAutoFillBackground(false);
    // QTextDocument also owns a root-frame background brush. Leaving that at
    // the platform default can resurrect the old white tile when the viewport
    // backing store is reused after growing and then shrinking the window.
    QTextFrameFormat rootFormat=chat_->document()->rootFrame()->frameFormat();
    rootFormat.setBackground(panelColor);
    chat_->document()->rootFrame()->setFrameFormat(rootFormat);
    chat_->document()->setDefaultStyleSheet(QStringLiteral(
        "body{background:transparent;color:#ffffff;}"
        "span{color:#ffffff;}"));
    // Opacity controls the chat/viewer panel backgrounds only. The title bar
    // has its own permanent backdrop (set in the constructor) so it, the
    // label, the Clip button, and the close button all stay visible at every
    // opacity level; hiding them at 0% made them impossible to find.
    if(viewers_)viewers_->show();
}
void PopoutChat::registerRestoreHotkeys(){
#ifdef Q_OS_WIN
    if(hotkeysRegistered_)return;
    RegisterHotKey(nullptr,kRestoreHotkeyEscape,MOD_NOREPEAT,VK_ESCAPE);
    RegisterHotKey(nullptr,kRestoreHotkeyAltC,MOD_ALT|MOD_NOREPEAT,'C');
    hotkeysRegistered_=true;
#endif
}
void PopoutChat::unregisterRestoreHotkeys(){
#ifdef Q_OS_WIN
    if(!hotkeysRegistered_)return;
    UnregisterHotKey(nullptr,kRestoreHotkeyEscape);
    UnregisterHotKey(nullptr,kRestoreHotkeyAltC);
    hotkeysRegistered_=false;
#endif
}
bool PopoutChat::eventFilter(QObject*watched,QEvent*event){
    // The frameless window has no native title bar to drag; let a left-button
    // press on titleBar_ move the window via the platform's own system move,
    // same as dragging a normal title bar would.
    if((watched==titleBar_||watched==toolbarTitle_)&&event->type()==QEvent::MouseButtonPress){
        auto*mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton&&windowHandle()){
            windowHandle()->startSystemMove();
            return true;
        }
    }
    return QWidget::eventFilter(watched,event);
}
bool PopoutChat::nativeEventFilter(const QByteArray&eventType,void*message,qintptr*result){
#ifdef Q_OS_WIN
    if(eventType=="windows_generic_MSG"||eventType=="windows_dispatcher_MSG"){
        auto*msg=static_cast<MSG*>(message);
        if(msg->hwnd==reinterpret_cast<HWND>(winId())&&msg->message==WM_NCHITTEST&&!ghostMode_){
            constexpr int border=9;const POINTS pt=MAKEPOINTS(msg->lParam);const QPoint local=mapFromGlobal(QPoint(pt.x,pt.y));const bool left=local.x()>=0&&local.x()<border,right=local.x()<=width()&&local.x()>width()-border,top=local.y()>=0&&local.y()<border,bottom=local.y()<=height()&&local.y()>height()-border;
            if(top&&left)*result=HTTOPLEFT;else if(top&&right)*result=HTTOPRIGHT;else if(bottom&&left)*result=HTBOTTOMLEFT;else if(bottom&&right)*result=HTBOTTOMRIGHT;else if(left)*result=HTLEFT;else if(right)*result=HTRIGHT;else if(top)*result=HTTOP;else if(bottom)*result=HTBOTTOM;else return false;return true;
        }
        if(msg->message==WM_HOTKEY&&(msg->wParam==kRestoreHotkeyEscape||msg->wParam==kRestoreHotkeyAltC)){setGhostMode(false);return true;}
    }
#else
    Q_UNUSED(eventType) Q_UNUSED(message) Q_UNUSED(result)
#endif
    return false;
}
void PopoutChat::setClipAvailable(bool available){if(clipButton_)clipButton_->setEnabled(available);}
void PopoutChat::showClipResult(bool success,const QString&text,const QUrl&editUrl){
    if(!clipStatus_)return;
    QString body=text.toHtmlEscaped();
    if(success&&editUrl.isValid()){
        QApplication::clipboard()->setText(editUrl.toString());
        body+=QStringLiteral(" <a href='%1' style='color:inherit;text-decoration:underline'>Open editor &#8599;</a>").arg(editUrl.toString());
    }
    clipStatus_->setText(body);
    clipStatus_->setStyleSheet(success
        ?QStringLiteral("background:#12301fee;border:1px solid #63e6be;border-radius:10px;padding:8px;color:#9ff5d4;font-weight:700;")
        :QStringLiteral("background:#301217ee;border:1px solid #ff5573;border-radius:10px;padding:8px;color:#ffb3c0;font-weight:700;"));
    clipStatus_->show();
    QTimer::singleShot(8000,clipStatus_,&QWidget::hide);
}
void PopoutChat::setStreamlabsAlertAudio(bool enabled,const QUrl&url){
    if(!enabled||!url.isValid()||url.host().isEmpty()){
        if(alertView_){
            alertView_->setUrl(QUrl(QStringLiteral("about:blank")));
            chatStack_->removeWidget(alertView_);
            alertView_->deleteLater();
            alertView_=nullptr;
        }
        return;
    }
    if(!alertView_){
        alertView_=new QWebEngineView;
        alertView_->setAttribute(Qt::WA_TransparentForMouseEvents,true);
        alertView_->setStyleSheet(QStringLiteral("background:transparent"));
        alertView_->page()->setBackgroundColor(Qt::transparent);
        alertView_->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture,false);
        chatStack_->addWidget(alertView_);
        chatStack_->setCurrentWidget(alertView_);
    }
    alertView_->setUrl(url);
}
ClipEditorWindow::ClipEditorWindow(QWidget*p):QWidget(p){
    setWindowTitle(QStringLiteral("Leapcast Studio — Clip Editor"));
    setWindowIcon(QIcon(":/brand/lefroge_chat_icon.png"));
    resize(1040,760);
    auto*layout=new QVBoxLayout(this);
    layout->setContentsMargins(6,6,6,6);
    layout->setSpacing(6);
    auto*toolbar=new QHBoxLayout;
    auto*back=new QPushButton(QStringLiteral("←"));
    auto*forward=new QPushButton(QStringLiteral("→"));
    auto*reload=new QPushButton(QStringLiteral("↻"));
    for(auto*button:{back,forward,reload})button->setFixedWidth(34);
    toolbar->addWidget(back);
    toolbar->addWidget(forward);
    toolbar->addWidget(reload);
    addressLabel_=new QLabel;
    addressLabel_->setStyleSheet(QStringLiteral("color:#8f9bb5;font-size:9pt;"));
    toolbar->addWidget(addressLabel_,1);
    auto*openExternal=new QPushButton(QStringLiteral("Open in browser ↗"));
    toolbar->addWidget(openExternal);
    layout->addLayout(toolbar);
    // Twitch rejects Qt WebEngine's product-token user agent as an unsupported
    // browser even though the underlying Chromium engine can render the clip
    // editor. Keep Twitch in its own persistent profile and identify it as a
    // current desktop Chromium browser. The separate profile also prevents a
    // Twitch login/cookie change from affecting the embedded TikTok collector.
    profile_=new QWebEngineProfile(QStringLiteral("LeapcastTwitchClips"),this);
    profile_->setHttpUserAgent(QStringLiteral(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/151.0.0.0 Safari/537.36"));
    profile_->setHttpAcceptLanguage(QStringLiteral("en-US,en;q=0.9"));
    profile_->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    view_=new QWebEngineView(this);
    view_->setPage(new QWebEnginePage(profile_,view_));
    view_->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled,true);
    view_->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled,true);
    view_->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture,false);
    view_->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled,true);
    layout->addWidget(view_,1);
    connect(back,&QPushButton::clicked,view_,&QWebEngineView::back);
    connect(forward,&QPushButton::clicked,view_,&QWebEngineView::forward);
    connect(reload,&QPushButton::clicked,view_,&QWebEngineView::reload);
    connect(openExternal,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(view_->url().isValid()?view_->url():pendingUrl_);});
    connect(view_,&QWebEngineView::urlChanged,this,[this](const QUrl&u){addressLabel_->setText(u.toString());const QString host=u.host().toLower(),path=u.path().toLower();if((host==QStringLiteral("id.twitch.tv")||host.endsWith(QStringLiteral(".twitch.tv")))&&(path.contains(QStringLiteral("login"))||path.contains(QStringLiteral("activate")))&&!loginRequestInProgress_){loginRequestInProgress_=true;emit twitchBrowserLoginRequested();}});
}
void ClipEditorWindow::setTwitchAccessToken(const QString&token){
    const QByteArray clean=token.trimmed().toUtf8();if(clean.isEmpty())return;QNetworkCookie cookie(QByteArray("auth-token"),clean);cookie.setDomain(QStringLiteral(".twitch.tv"));cookie.setPath(QStringLiteral("/"));cookie.setSecure(true);profile_->cookieStore()->setCookie(cookie,QUrl(QStringLiteral("https://www.twitch.tv/")));loginRequestInProgress_=false;if(pendingUrl_.isValid())QTimer::singleShot(250,this,[this]{view_->setUrl(pendingUrl_);});
}
void ClipEditorWindow::openUrl(const QUrl&url){
    pendingUrl_=url;view_->setUrl(url);
    show();
    raise();
    activateWindow();
}
void ClipEditorWindow::closeEvent(QCloseEvent*event){
    view_->setUrl(QUrl(QStringLiteral("about:blank")));
    QWidget::closeEvent(event);
}
