#include "Overlay.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QStackedLayout>
#include <QTcpSocket>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWebEnginePage>
namespace { constexpr int kMessageIdProperty = QTextFormat::UserProperty + 1; }
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
PopoutChat::PopoutChat(QWidget*p):QWidget(p){
    setWindowTitle("Leapcast Studio Pop-out");
    setWindowIcon(QIcon(":/brand/lefroge_chat_icon.png"));
    resize(460,720);
    setWindowFlag(Qt::WindowStaysOnTopHint,true);
    setAttribute(Qt::WA_TranslucentBackground,true);
    qApp->installNativeEventFilter(this);
    auto*l=new QVBoxLayout(this);
    l->setContentsMargins(8,8,8,8);
    l->setSpacing(6);

    auto*toolbar=new QHBoxLayout;
    toolbar->setSpacing(6);
    auto*title=new QLabel("LEAPCAST STUDIO");
    title->setStyleSheet("color:#8f9bb5;font-size:8pt;font-weight:800;letter-spacing:1px;");
    toolbar->addWidget(title);
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
    l->addLayout(toolbar);

    event_=new QLabel;
    event_->setAlignment(Qt::AlignCenter);
    event_->setWordWrap(true);
    event_->hide();
    event_->setStyleSheet("background:#171d2dee;border:1px solid #53cdf3;border-radius:12px;padding:12px;font-size:14pt;font-weight:800;");
    l->addWidget(event_);

    auto*chatHost=new QWidget;
    chatStack_=new QStackedLayout(chatHost);
    chatStack_->setContentsMargins(0,0,0,0);
    chatStack_->setStackingMode(QStackedLayout::StackAll);
    chat_=new QTextBrowser;
    chat_->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(chat_->viewport(),&QWidget::customContextMenuRequested,this,&PopoutChat::showChatContextMenu);
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

    viewers_=new QLabel("0 watching");
    l->addWidget(viewers_);
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
    cursor.insertHtml(QString("<b style='color:%1'>%2</b> %3").arg(m.color.name(),m.user.toHtmlEscaped(),m.text.toHtmlEscaped()));
    chat_->moveCursor(QTextCursor::End);
    chat_->ensureCursorVisible();
}
void PopoutChat::appendModerationNote(const QString&user,const QString&reason){
    QTextCursor cursor(chat_->document());
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat format;
    if(!chat_->document()->isEmpty())cursor.insertBlock(format);
    else cursor.setBlockFormat(format);
    cursor.insertHtml(QStringLiteral("<span style='color:#7f8ba5;font-style:italic;font-size:9pt'>&#9888; %1's message was removed by AutoMod (%2)</span>")
        .arg(user.toHtmlEscaped(),reason.toHtmlEscaped()));
    chat_->moveCursor(QTextCursor::End);
    chat_->ensureCursorVisible();
}
void PopoutChat::showChatContextMenu(const QPoint&pos){
    const qint64 id=chat_->cursorForPosition(pos).blockFormat().property(kMessageIdProperty).toLongLong();
    QMenu menu(this);
    auto*copyAction=menu.addAction(QStringLiteral("Copy"));
    copyAction->setEnabled(chat_->textCursor().hasSelection());
    connect(copyAction,&QAction::triggered,chat_,&QTextBrowser::copy);
    if(historyById_.contains(id)){
        const ChatMessage msg=historyById_.value(id);
        const bool isTwitch=msg.platform==QStringLiteral("twitch");
        const bool isYouTube=msg.platform==QStringLiteral("youtube")||msg.platform==QStringLiteral("yt_shorts");
        // Only Twitch and YouTube/Shorts have a working moderation API wired up
        // here; TikTok moderation happens in TikTok's own LIVE room (see the
        // "Open TikTok LIVE in browser" action), so no actions are offered for it.
        if(isTwitch||isYouTube){
            menu.addSeparator();
            auto*header=menu.addAction(QStringLiteral("Moderate %1").arg(msg.user));
            header->setEnabled(false);
            connect(menu.addAction(QStringLiteral("Delete message")),&QAction::triggered,this,[this,msg]{emit deleteMessageRequested(msg);});
            connect(menu.addAction(QStringLiteral("Timeout")),&QAction::triggered,this,[this,msg]{emit timeoutUserRequested(msg,300);});
            // Twitch calls this a "Ban"; YouTube Studio calls the same
            // permanent action "Hide user on this channel".
            connect(menu.addAction(isYouTube?QStringLiteral("Hide from channel"):QStringLiteral("Ban")),
                    &QAction::triggered,this,[this,msg]{emit timeoutUserRequested(msg,0);});
        }
    }
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Select All")),&QAction::triggered,chat_,&QTextBrowser::selectAll);
    menu.exec(chat_->viewport()->mapToGlobal(pos));
}
void PopoutChat::showEvent(const StreamEvent&e){QString action=e.kind.contains("donation")?"Donated "+e.amount:e.kind.contains("follow")?"has Followed":"has Subscribed";QString colour=e.kind.contains("donation")?"#f6c85f":e.platform=="twitch"?"#b48cff":e.platform=="youtube"?"#ff5573":"#55e5d3";event_->setText(QString("<span style='color:#a8b0c7'>SYSTEM MESSAGE</span><br><b>%1</b> <span style='color:%2'>%3</span>").arg(e.user.toHtmlEscaped(),colour,action.toHtmlEscaped()));event_->show();QTimer::singleShot(6000,event_,&QWidget::hide);}
void PopoutChat::setViewers(const QString&p,int n){
    counts_[p]=n;
    int total=0;
    for(int v:counts_)total+=v;
    // Fixed display order so the breakdown doesn't reshuffle as counts update.
    // Kick has no live source wired up yet, so it never appears here even
    // though the pop-out's tab already exists as a "Coming Soon" placeholder.
    static const QList<QPair<QString,QString>> platforms{
        {QStringLiteral("twitch"),QStringLiteral("Twitch")},
        {QStringLiteral("youtube"),QStringLiteral("YouTube")},
        {QStringLiteral("yt_shorts"),QStringLiteral("Shorts")},
        {QStringLiteral("tiktok"),QStringLiteral("TikTok")}};
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
void PopoutChat::setOpacityPercent(int n){opacityPercent_=qBound(10,n,100);applyOpacity();}
void PopoutChat::applyOpacity(){
    const int alpha=clearBackground_?0:qRound(opacityPercent_*255.0/100.0);
    chat_->setStyleSheet(QStringLiteral("background:rgba(16,19,29,%1);border:0;border-radius:12px;font-size:12pt;").arg(alpha));
    viewers_->setStyleSheet(QStringLiteral("background:rgba(16,19,29,%1);padding:8px;border-radius:9px;color:#68e9d5;font-weight:700;").arg(alpha));
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
bool PopoutChat::nativeEventFilter(const QByteArray&eventType,void*message,qintptr*result){
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    if(eventType=="windows_generic_MSG"||eventType=="windows_dispatcher_MSG"){
        auto*msg=static_cast<MSG*>(message);
        if(msg->message==WM_HOTKEY&&(msg->wParam==kRestoreHotkeyEscape||msg->wParam==kRestoreHotkeyAltC)){
            setGhostMode(false);
            return true;
        }
    }
#else
    Q_UNUSED(eventType) Q_UNUSED(message)
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
    // Default (persistent) profile, same as TikTok's embedded view, so a
    // Twitch web login made here is remembered the next time this opens.
    view_=new QWebEngineView(this);
    layout->addWidget(view_,1);
    connect(back,&QPushButton::clicked,view_,&QWebEngineView::back);
    connect(forward,&QPushButton::clicked,view_,&QWebEngineView::forward);
    connect(reload,&QPushButton::clicked,view_,&QWebEngineView::reload);
    connect(openExternal,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(view_->url());});
    connect(view_,&QWebEngineView::urlChanged,this,[this](const QUrl&u){addressLabel_->setText(u.toString());});
}
void ClipEditorWindow::openUrl(const QUrl&url){
    view_->setUrl(url);
    show();
    raise();
    activateWindow();
}
