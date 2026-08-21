#include "Overlay.hpp"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
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
namespace {
constexpr int kMessageIdProperty = QTextFormat::UserProperty + 1;
// Caps how many lines chat_'s QTextDocument keeps. Without this, a
// multi-hour stream's chat/moderation-note history would grow the document
// (and the cost of every future append/reflow) without bound.
constexpr int kMaxChatBlocks = 300;
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
.m{margin:5px 8px;padding:6px 9px;border-radius:12px;background:transparent;text-shadow:var(--outline,none);opacity:1;transform:translateY(0);transition:opacity .65s ease,transform .65s ease}
.m.fade{opacity:0;transform:translateY(-8px)}.u{font-weight:800;margin-right:7px}.twitch{border-left:4px solid #9146ff}.youtube,.yt_shorts{border-left:4px solid #ff334f}.tiktok{border-left:4px solid #18e0d5}
</style><main id=c></main><script>
const BG={HOST:'\u{1F3A5}',MOD:'⚔️',VIP:'\u{1F48E}',PRIME:'\u{1F451}',SUB:'⭐',CHECK:'✅',MONEY:'\u{1F4B0}'};const BO=['HOST','MOD','VIP','PRIME','SUB','CHECK','MONEY'];function badges(l){return BO.filter(k=>(l||[]).includes(k)).map(k=>BG[k]).join('')}function outline(n){n=Math.max(0,Math.min(8,n|0));if(!n)return'none';let s=[];for(let x=-n;x<=n;x++)for(let y=-n;y<=n;y++)if((x||y)&&x*x+y*y<=n*n+n)s.push(`${x}px ${y}px 0 #000`);return s.join(',')}
let n=0;async function p(){try{let r=await fetch('/api/messages?since='+n),d=await r.json();document.body.style.background=d.background;document.documentElement.style.background=d.background;document.documentElement.style.setProperty('--outline',outline(d.outline_thickness));for(let m of d.messages){n=Math.max(n,m.cursor);let x=document.createElement('div');x.className='m '+m.platform;x.innerHTML='<span class=u style="color:'+m.color+'"></span><span class=x></span>';let b=badges(m.badges);x.querySelector('.u').textContent=(b?b+' ':'')+m.user;x.querySelector('.x').textContent=m.text;c.append(x);if(d.fade_seconds>0)setTimeout(()=>{x.classList.add('fade');setTimeout(()=>x.remove(),700)},d.fade_seconds*1000)}while(c.children.length>80)c.firstChild.remove();scrollTo(0,document.body.scrollHeight)}catch(e){}setTimeout(p,600)}p()
</script>)HTML";
}
OverlayServer::OverlayServer(QObject*p):QObject(p){connect(&server_,&QTcpServer::newConnection,this,&OverlayServer::accept);}
bool OverlayServer::start(quint16 p){for(int i=0;i<20;++i)if(server_.listen(QHostAddress::LocalHost,p+i))return true;return false;} void OverlayServer::stop(){server_.close();}
void OverlayServer::ingest(const ChatMessage&m){messages_.append({++cursor_,m});while(messages_.size()>400)messages_.removeFirst();} void OverlayServer::setViewers(const QString&p,int n){viewers_[p]=n;} void OverlayServer::clear(){messages_.clear();++cursor_;}
void OverlayServer::setAppearance(const QColor&background,int opacity,int outline){backgroundColor_=background.isValid()?background:QColor(Qt::black);backgroundOpacityPercent_=qBound(0,opacity,100);outlineThickness_=qBound(0,outline,8);}
void OverlayServer::accept(){while(auto*s=server_.nextPendingConnection()){connect(s,&QTcpSocket::readyRead,this,[this,s]{const QByteArray first=s->readAll().split('\n').value(0);s->write(responseFor(first.split(' ').value(1)));s->disconnectFromHost();});connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);}}
QByteArray OverlayServer::responseFor(const QByteArray&t){if(t=="/"||t.startsWith("/?"))return reply(200,"text/html; charset=utf-8",overlayHtml);if(t.startsWith("/api/messages")){quint64 since=0;const int at=t.indexOf("since=");if(at>=0)since=t.mid(at+6).split('&').value(0).toULongLong();QJsonArray a;for(const auto&x:messages_)if(x.first>since){auto o=x.second.toJson();o["cursor"]=static_cast<qint64>(x.first);a.append(o);}const QString bg=QStringLiteral("rgba(%1,%2,%3,%4)").arg(backgroundColor_.red()).arg(backgroundColor_.green()).arg(backgroundColor_.blue()).arg(QString::number(backgroundOpacityPercent_/100.0,'f',2));return reply(200,"application/json",QJsonDocument(QJsonObject{{"messages",a},{"cursor",static_cast<qint64>(cursor_)},{"fade_seconds",fadeSeconds_},{"background",bg},{"outline_thickness",outlineThickness_}}).toJson(QJsonDocument::Compact));}if(t.startsWith("/api/viewers")){QJsonObject o;int total=0;for(auto i=viewers_.cbegin();i!=viewers_.cend();++i){o[i.key()]=i.value();total+=i.value();}o["total"]=total;return reply(200,"application/json",QJsonDocument(o).toJson(QJsonDocument::Compact));}return reply(404,"text/plain","Not found");}
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
    auto*closeButton=new QPushButton(QStringLiteral("✕"));
    closeButton->setToolTip(QStringLiteral("Close pop-out"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedSize(22,22);
    closeButton->setStyleSheet(
        "QPushButton{background:#20283a;color:#c7cede;border:0;border-radius:6px;font-weight:700;}"
        "QPushButton:hover{background:#44202b;color:#ff91a4;}");
    connect(closeButton,&QPushButton::clicked,this,&QWidget::close);
    toolbar->addWidget(closeButton);
    l->addWidget(titleBar_);

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
    const QString badges=badgeGlyphs(m.badges);
    // Chat lines remain unfilled so the game/stream is visible between and
    // behind every message in both the pop-out and OBS overlay.
    const int messageStart=cursor.position();
    cursor.insertHtml(QString("<span style='background-color:transparent;color:#ffffff'>%1<b style='color:%2'>%3</b> <span style='color:#ffffff;font-weight:600'>%4</span></span>")
        .arg(badges.isEmpty()?QString():badges+QStringLiteral(" "),m.color.name(),m.user.toHtmlEscaped(),m.text.toHtmlEscaped()));
    const int messageEnd=cursor.position();
    cursor.setPosition(messageStart);cursor.setPosition(messageEnd,QTextCursor::KeepAnchor);
    QTextCharFormat outlineFormat;
    outlineFormat.setTextOutline(outlineThickness_>0?QPen(Qt::black,outlineThickness_,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin):QPen(Qt::NoPen));
    cursor.mergeCharFormat(outlineFormat);
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
            connect(menu.addAction(QStringLiteral("Timeout")),&QAction::triggered,this,[this,msg]{bool ok=false;const QString reason=QInputDialog::getMultiLineText(this,"Timeout reason","Why is this user being timed out?",QString(),&ok).trimmed();if(ok&&!reason.isEmpty())emit timeoutUserRequested(msg,300,reason);});
            // Twitch calls this a "Ban"; YouTube Studio calls the same
            // permanent action "Hide user on this channel".
            connect(menu.addAction(isYouTube?QStringLiteral("Hide from channel"):QStringLiteral("Ban")),
                    &QAction::triggered,this,[this,msg]{bool ok=false;const QString reason=QInputDialog::getMultiLineText(this,"Ban reason","Why is this user being banned?",QString(),&ok).trimmed();if(ok&&!reason.isEmpty())emit timeoutUserRequested(msg,0,reason);});
        }
    }
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Select All")),&QAction::triggered,chat_,&QTextBrowser::selectAll);
    menu.exec(globalPos);
}
void PopoutChat::showEvent(const StreamEvent&e){QString action=e.kind.contains("donation")?"Donated "+e.amount:e.kind.contains("follow")?"has Followed":"has Subscribed";QString colour=e.kind.contains("donation")?"#f6c85f":e.platform=="twitch"?"#b48cff":e.platform=="youtube"?"#ff5573":"#55e5d3";event_->setText(QString("<span style='color:#a8b0c7'>SYSTEM MESSAGE</span><br><b>%1</b> <span style='color:%2'>%3</span>").arg(e.user.toHtmlEscaped(),colour,action.toHtmlEscaped()));if(opacityPercent_>0)event_->show();QTimer::singleShot(6000,event_,&QWidget::hide);}
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
void PopoutChat::setOpacityPercent(int n){opacityPercent_=qBound(0,n,100);applyOpacity();}
void PopoutChat::setAppearance(const QColor&background,int outline){backgroundColor_=background.isValid()?background:QColor(Qt::black);outlineThickness_=qBound(0,outline,8);applyOpacity();applyTextOutline();}
void PopoutChat::applyTextOutline(){
    if(!chat_)return;
    QTextCursor cursor(chat_->document());cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setTextOutline(outlineThickness_>0?QPen(Qt::black,outlineThickness_,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin):QPen(Qt::NoPen));
    cursor.mergeCharFormat(format);chat_->viewport()->update();
}

void ChatBrowser::paintEvent(QPaintEvent* event){
    // QTextEdit paints through its viewport. Clear the complete damaged region
    // to alpha first so Windows cannot reuse an opaque (white) backing-store
    // tile when the pop-out grows and is then made smaller again.
    QPainter clearPainter(viewport());
    clearPainter.setCompositionMode(QPainter::CompositionMode_Source);
    clearPainter.fillRect(event->rect(),Qt::transparent);
    clearPainter.end();
    QTextBrowser::paintEvent(event);
}
void PopoutChat::resizeEvent(QResizeEvent* event){
    QWidget::resizeEvent(event);
    // On Windows, QTextBrowser can retain an opaque backing-store tile for the
    // portion of its viewport exposed by a resize. Reassert the alpha and
    // invalidate both surfaces so every newly exposed pixel is repainted.
    applyOpacity();
    if(chat_){
        chat_->update();
        chat_->viewport()->update();
    }
}
void PopoutChat::paintEvent(QPaintEvent* event){
    // A translucent top-level window must explicitly clear newly exposed
    // pixels. Otherwise the Windows backing store can reveal its default white
    // surface after a resize even though every child widget is transparent.
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(event->rect(),Qt::transparent);
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
    const bool backgroundVisible=opacityPercent_>0;
    // Opacity controls backgrounds only. Chat and the live viewer count must
    // remain visible at 0%; hiding them made the slider behave backwards.
    if(toolbarTitle_)toolbarTitle_->setVisible(backgroundVisible);
    if(clipButton_)clipButton_->setVisible(backgroundVisible);
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
void ClipEditorWindow::closeEvent(QCloseEvent*event){
    view_->setUrl(QUrl(QStringLiteral("about:blank")));
    QWidget::closeEvent(event);
}
