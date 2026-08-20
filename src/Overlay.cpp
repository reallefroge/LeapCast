#include "Overlay.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QHostAddress>
#include <QLabel>
#include <QTcpSocket>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineSettings>
#include <QWebEngineView>
namespace {
QByteArray reply(int code,const QByteArray&type,const QByteArray&body){return "HTTP/1.1 "+QByteArray::number(code)+(code==200?" OK\r\n":" Not Found\r\n")+"Content-Type: "+type+"\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body;}
const char overlayHtml[]=R"HTML(<!doctype html><meta charset=utf-8><style>
html,body{margin:0;background:transparent;overflow:hidden;font-family:'Segoe UI',sans-serif;color:white}
.m{margin:5px 8px;padding:6px 9px;border-radius:12px;background:#101522d9;text-shadow:0 1px 3px #000;opacity:1;transform:translateY(0);transition:opacity .65s ease,transform .65s ease}
.m.fade{opacity:0;transform:translateY(-8px)}.u{font-weight:800;margin-right:7px}.twitch{border-left:4px solid #9146ff}.youtube,.yt_shorts{border-left:4px solid #ff334f}.tiktok{border-left:4px solid #18e0d5}
</style><main id=c></main><script>
let n=0;async function p(){try{let r=await fetch('/api/messages?since='+n),d=await r.json();for(let m of d.messages){n=Math.max(n,m.cursor);let x=document.createElement('div');x.className='m '+m.platform;x.innerHTML='<span class=u style="color:'+m.color+'"></span><span class=x></span>';x.querySelector('.u').textContent=m.user;x.querySelector('.x').textContent=m.text;c.append(x);if(d.fade_seconds>0)setTimeout(()=>{x.classList.add('fade');setTimeout(()=>x.remove(),700)},d.fade_seconds*1000)}while(c.children.length>80)c.firstChild.remove();scrollTo(0,document.body.scrollHeight)}catch(e){}setTimeout(p,600)}p()
</script>)HTML";
}
OverlayServer::OverlayServer(QObject*p):QObject(p){connect(&server_,&QTcpServer::newConnection,this,&OverlayServer::accept);}
bool OverlayServer::start(quint16 p){for(int i=0;i<20;++i)if(server_.listen(QHostAddress::LocalHost,p+i))return true;return false;} void OverlayServer::stop(){server_.close();}
void OverlayServer::ingest(const ChatMessage&m){messages_.append({++cursor_,m});while(messages_.size()>400)messages_.removeFirst();} void OverlayServer::setViewers(const QString&p,int n){viewers_[p]=n;} void OverlayServer::clear(){messages_.clear();++cursor_;}
void OverlayServer::accept(){while(auto*s=server_.nextPendingConnection()){connect(s,&QTcpSocket::readyRead,this,[this,s]{const QByteArray first=s->readAll().split('\n').value(0);s->write(responseFor(first.split(' ').value(1)));s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);}}
QByteArray OverlayServer::responseFor(const QByteArray&t){if(t=="/"||t.startsWith("/?"))return reply(200,"text/html; charset=utf-8",overlayHtml);if(t.startsWith("/api/messages")){quint64 since=0;const int at=t.indexOf("since=");if(at>=0)since=t.mid(at+6).split('&').value(0).toULongLong();QJsonArray a;for(const auto&x:messages_)if(x.first>since){auto o=x.second.toJson();o["cursor"]=static_cast<qint64>(x.first);a.append(o);}return reply(200,"application/json",QJsonDocument(QJsonObject{{"messages",a},{"cursor",static_cast<qint64>(cursor_)},{"fade_seconds",fadeSeconds_}}).toJson(QJsonDocument::Compact));}if(t.startsWith("/api/viewers")){QJsonObject o;int total=0;for(auto i=viewers_.cbegin();i!=viewers_.cend();++i){o[i.key()]=i.value();total+=i.value();}o["total"]=total;return reply(200,"application/json",QJsonDocument(o).toJson(QJsonDocument::Compact));}return reply(404,"text/plain","Not found");}
PopoutChat::PopoutChat(QWidget*p):QWidget(p){setWindowTitle("Multi-Chat Pop-out");setWindowIcon(QIcon(":/brand/lefroge_chat_icon.png"));resize(460,720);setWindowFlag(Qt::WindowStaysOnTopHint,true);auto*l=new QVBoxLayout(this);l->setContentsMargins(8,8,8,8);event_=new QLabel;event_->setAlignment(Qt::AlignCenter);event_->setWordWrap(true);event_->hide();event_->setStyleSheet("background:#171d2dee;border:1px solid #53cdf3;border-radius:12px;padding:12px;font-size:14pt;font-weight:800;");l->addWidget(event_);chat_=new QTextBrowser;chat_->setStyleSheet("background:#10131dcc;border:0;border-radius:12px;font-size:12pt;");l->addWidget(chat_,1);viewers_=new QLabel("0 watching");viewers_->setStyleSheet("background:#10131d;padding:8px;border-radius:9px;color:#68e9d5;font-weight:700;");l->addWidget(viewers_);}
void PopoutChat::appendMessage(const ChatMessage&m){chat_->append(QString("<p><b style='color:%1'>%2</b> %3</p>").arg(m.color.name(),m.user.toHtmlEscaped(),m.text.toHtmlEscaped()));}
void PopoutChat::showEvent(const StreamEvent&e){QString action=e.kind.contains("donation")?"Donated "+e.amount:e.kind.contains("follow")?"has Followed":"has Subscribed";QString colour=e.kind.contains("donation")?"#f6c85f":e.platform=="twitch"?"#b48cff":e.platform=="youtube"?"#ff5573":"#55e5d3";event_->setText(QString("<span style='color:#a8b0c7'>SYSTEM MESSAGE</span><br><b>%1</b> <span style='color:%2'>%3</span>").arg(e.user.toHtmlEscaped(),colour,action.toHtmlEscaped()));event_->show();QTimer::singleShot(6000,event_,&QWidget::hide);}
void PopoutChat::setViewers(const QString&p,int n){counts_[p]=n;int total=0;for(int v:counts_)total+=v;viewers_->setText(QString::number(total)+" WATCHING");} void PopoutChat::setGhostMode(bool on){setWindowFlag(Qt::WindowTransparentForInput,on);show();} void PopoutChat::setClearBackground(bool on){setAttribute(Qt::WA_TranslucentBackground,on);} void PopoutChat::setOpacityPercent(int n){setWindowOpacity(qBound(.2,n/100.0,1.0));}
void PopoutChat::setStreamlabsAlertAudio(bool enabled,const QUrl&url){
    if(!enabled||!url.isValid()||url.host().isEmpty()){if(alertAudio_){alertAudio_->setUrl(QUrl(QStringLiteral("about:blank")));alertAudio_->deleteLater();alertAudio_=nullptr;}return;}
    if(!alertAudio_){alertAudio_=new QWebEngineView(this);alertAudio_->setFixedSize(1,1);alertAudio_->setAttribute(Qt::WA_TransparentForMouseEvents,true);alertAudio_->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture,false);layout()->addWidget(alertAudio_);}
    alertAudio_->setUrl(url);
}
