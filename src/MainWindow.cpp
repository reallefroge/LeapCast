#include "MainWindow.hpp"
#include "AppController.hpp"
#include "Overlay.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QLabel* label(const QString& text, const char* role = nullptr) {
    auto* value = new QLabel(text);
    if (role) value->setProperty("role", QString::fromLatin1(role));
    return value;
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Multi-Chat Studio • Twitch / YouTube / Shorts / TikTok"));
    resize(1280, 760);
    setMinimumSize(1000, 620);
    setWindowIcon(QIcon(QStringLiteral(":/brand/lefroge_chat_icon.png")));
    applyTheme();
    controller_ = new AppController(this);
    overlay_ = new OverlayServer(this);
    overlay_->start(static_cast<quint16>(controller_->settings()->preference(QStringLiteral("port"),8080).toInt()));

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildDashboard());
    splitter->addWidget(buildChatDock());
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({850, 430});
    setCentralWidget(splitter);
    connect(controller_, &AppController::messageReady, this, [this](const ChatMessage& m) {
        const QString colour = m.color.isValid() ? m.color.name() : QStringLiteral("#53cdf3");
        const QString html = QStringLiteral("<div style='margin:4px 0'><b style='color:%1'>%2</b> %3</div>")
            .arg(colour, m.user.toHtmlEscaped(), m.text.toHtmlEscaped());
        if (chatViews_.contains(QStringLiteral("combined"))) chatViews_[QStringLiteral("combined")]->append(html);
        if (chatViews_.contains(m.platform)) chatViews_[m.platform]->append(html);
        overlay_->ingest(m);
        if(popout_) popout_->appendMessage(m);
    });
    connect(controller_,&AppController::eventReady,this,[this](const StreamEvent&e){if(popout_)popout_->showEvent(e);});
    connect(controller_,&AppController::viewerCount,this,[this](const QString&p,int n){overlay_->setViewers(p,n);if(popout_)popout_->setViewers(p,n);});
    connect(controller_, &AppController::sourceStatus, this,
            [this](const QString& platform,const QString& state,const QString& detail){
        if(!sourceStates_.contains(platform)) return;
        auto* value=sourceStates_[platform]; value->setText(detail);
        const QString colour=state=="ok"?"#63e6be":state=="error"?"#ff5573":"#f6c85f";
        value->setStyleSheet("color:"+colour);
    });
    QTimer::singleShot(0, controller_, &AppController::startConfiguredSources);
}

QWidget* MainWindow::buildSidebar() { return new QWidget; }

QWidget* MainWindow::buildDashboard() {
    auto* host = new QWidget;
    auto* outer = new QHBoxLayout(host);
    outer->setContentsMargins(14, 14, 10, 14);
    outer->setSpacing(12);

    auto* rail = new QFrame;
    rail->setObjectName(QStringLiteral("navigationRail"));
    rail->setFixedWidth(116);
    auto* railLayout = new QVBoxLayout(rail);
    auto* brand = new QLabel;
    brand->setPixmap(QPixmap(QStringLiteral(":/brand/lefroge_chat_icon.png"))
                         .scaled(62, 62, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brand->setAlignment(Qt::AlignCenter);
    railLayout->addWidget(brand);
    railLayout->addWidget(label(QStringLiteral("MULTI-CHAT\nSTUDIO"), "brand"));

    pages_ = new QStackedWidget;
    const QStringList names{QStringLiteral("Sources"), QStringLiteral("Events"),
                            QStringLiteral("Moderation"), QStringLiteral("Bans"),
                            QStringLiteral("OBS")};
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    for (qsizetype i = 0; i < names.size(); ++i) {
        auto* button = new QPushButton(names.at(i));
        button->setCheckable(true);
        button->setProperty("nav", true);
        group->addButton(button, static_cast<int>(i));
        railLayout->addWidget(button);
        if (i == 0) pages_->addWidget(buildSourcesPage());
        else if (i == 2) pages_->addWidget(buildModerationPage());
        else {
            auto* page = new QWidget;
            auto* layout = new QVBoxLayout(page);
            layout->addWidget(label(names.at(i).toUpper(), "pageTitle"));
            layout->addStretch();
            pages_->addWidget(page);
        }
    }
    railLayout->addStretch();
    group->button(2)->setChecked(true);
    pages_->setCurrentIndex(2);
    connect(group, &QButtonGroup::idClicked, pages_, &QStackedWidget::setCurrentIndex);
    outer->addWidget(rail);
    outer->addWidget(pages_, 1);
    return host;
}

QWidget* MainWindow::makePlatformCard(const QString& name, const QString& status,
                                      const QColor& accent, const QString& action) {
    auto* card = new QFrame;
    card->setProperty("card", true);
    auto* layout = new QVBoxLayout(card);
    auto* head = new QHBoxLayout;
    head->addWidget(label(name, "cardTitle"));
    head->addStretch();
    auto* state = label(status, "status"); state->setStyleSheet("color:" + accent.name());
    head->addWidget(state);
    layout->addLayout(head);
    auto* button = new QPushButton(action);
    button->setStyleSheet("background:" + accent.name() + ";color:#081018;");
    layout->addWidget(button);
    return card;
}

QWidget* MainWindow::buildSourcesPage() {
    auto* page=new QWidget; auto* layout=new QVBoxLayout(page);
    layout->setContentsMargins(8,4,8,8);
    layout->addWidget(label(QStringLiteral("CONNECTED COMMUNITIES"),"heroTitle"));
    layout->addWidget(label(QStringLiteral("Paste a channel or live-stream link. Each reader runs independently."),"muted"));
    const QList<QPair<QString,QString>> sources{{"twitch","Twitch"},{"youtube","YouTube"},{"yt_shorts","YouTube Shorts"},{"tiktok","TikTok"}};
    for(const auto& source:sources){
        auto* card=new QFrame;card->setProperty("card",true);auto* cardLayout=new QVBoxLayout(card);
        auto* head=new QHBoxLayout;head->addWidget(label(source.second,"cardTitle"));head->addStretch();
        auto* state=label(QStringLiteral("Offline"),"status");sourceStates_[source.first]=state;head->addWidget(state);cardLayout->addLayout(head);
        auto* row=new QHBoxLayout;auto* entry=new QLineEdit(controller_->settings()->link(source.first));entry->setPlaceholderText(source.first=="twitch"?"twitch.tv/yourname":source.first=="tiktok"?"tiktok.com/@yourname":"youtube.com/@yourname");row->addWidget(entry,1);
        auto* connectButton=new QPushButton(QStringLiteral("Connect"));row->addWidget(connectButton);auto* disconnectButton=new QPushButton(QStringLiteral("Disconnect"));row->addWidget(disconnectButton);cardLayout->addLayout(row);
        connect(connectButton,&QPushButton::clicked,this,[this,entry,key=source.first]{controller_->connectSource(key,entry->text());});
        connect(disconnectButton,&QPushButton::clicked,this,[this,key=source.first]{controller_->disconnectSource(key);});
        layout->addWidget(card);
    }
    layout->addStretch();return page;
}

QWidget* MainWindow::buildModerationPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 4, 8, 8);
    auto* hero = new QFrame; hero->setObjectName(QStringLiteral("safetyHero"));
    auto* heroLayout = new QHBoxLayout(hero);
    auto* copy = new QVBoxLayout;
    copy->addWidget(label(QStringLiteral("CREATOR SAFETY CENTER"), "heroTitle"));
    copy->addWidget(label(QStringLiteral("Moderate every connected community from one focused workspace."), "muted"));
    heroLayout->addLayout(copy, 1);
    heroLayout->addWidget(label(QStringLiteral("● LIVE ACTIONS\n● RIGHT-CLICK READY\n● LOCAL AUDIT"), "signal"));
    layout->addWidget(hero);

    auto* columns = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    left->addWidget(makePlatformCard(QStringLiteral("Twitch"), QStringLiteral("Not authorized"),
                                     QColor("#9146ff"), QStringLiteral("Authorize Twitch")));
    left->addWidget(makePlatformCard(QStringLiteral("YouTube"), QStringLiteral("Not authorized"),
                                     QColor("#ff334f"), QStringLiteral("Authorize YouTube")));
    left->addStretch();
    auto* right = new QVBoxLayout;
    right->addWidget(makePlatformCard(QStringLiteral("TikTok"), QStringLiteral("Not configured"),
                                      QColor("#18e0d5"), QStringLiteral("Configure moderation access")));
    right->addWidget(makePlatformCard(QStringLiteral("AutoMod"), QStringLiteral("Ready"),
                                      QColor("#63e6be"), QStringLiteral("Edit blocked words")));
    right->addStretch();
    columns->addLayout(left, 1); columns->addLayout(right, 1);
    layout->addLayout(columns, 1);
    return page;
}

QWidget* MainWindow::buildChatDock() {
    auto* dock = new QFrame; dock->setObjectName(QStringLiteral("chatDock"));
    auto* layout = new QVBoxLayout(dock);
    layout->addWidget(label(QStringLiteral("CHAT PREVIEW"), "pageTitle"));
    chatTabs_ = new QTabWidget;
    const QList<QPair<QString, QString>> tabs{
        {QStringLiteral("ALL"), QString()}, {QStringLiteral("Twitch"), QStringLiteral(":/icons/twitch.png")},
        {QStringLiteral("YouTube"), QString()}, {QStringLiteral("Shorts"), QStringLiteral(":/brand/youtube_shorts.png")},
        {QStringLiteral("TikTok"), QString()}};
    const QStringList keys{QStringLiteral("combined"),QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("yt_shorts"),QStringLiteral("tiktok")};
    for (qsizetype index=0; index<tabs.size(); ++index) {
        const auto& tab=tabs.at(index);
        auto* chat = new QTextBrowser;
        chat->setPlaceholderText(QStringLiteral("Connected messages appear here."));
        const QIcon icon(tab.second);
        if (!icon.isNull()) chatTabs_->addTab(chat, icon, QString());
        else chatTabs_->addTab(chat, tab.first);
        chatViews_.insert(keys.at(index),chat);
    }
    layout->addWidget(chatTabs_, 1);
    auto* popout = new QPushButton(QStringLiteral("Open Pop-out Chat"));
    popout->setProperty("primary", true);
    layout->addWidget(popout);
    connect(popout,&QPushButton::clicked,this,[this]{if(!popout_)popout_=new PopoutChat;popout_->show();popout_->raise();});
    return dock;
}

void MainWindow::applyTheme() {
    qApp->setStyleSheet(QStringLiteral(R"(
        * { font-family:'Segoe UI'; font-size:10pt; color:#eef2ff; }
        QMainWindow, QWidget { background:#0b0d15; }
        QFrame#navigationRail, QFrame#chatDock { background:#111522; border:1px solid #242b3d; border-radius:14px; }
        QLabel[role='brand'] { font-size:11pt; font-weight:800; color:#f8fbff; qproperty-alignment:AlignCenter; }
        QLabel[role='pageTitle'] { font-size:11pt; font-weight:800; color:#f8fbff; }
        QLabel[role='heroTitle'] { font-size:17pt; font-weight:800; }
        QLabel[role='muted'], QLabel[role='status'] { color:#9ca7bf; }
        QLabel[role='signal'] { color:#68e9d5; font-size:8pt; font-weight:700; }
        QLabel[role='cardTitle'] { font-size:12pt; font-weight:700; }
        QFrame#safetyHero { background:#171d2d; border-left:5px solid #53cdf3; border-radius:12px; }
        QFrame[card='true'] { background:#161b29; border:1px solid #252d42; border-radius:12px; }
        QPushButton { background:#20283a; border:0; border-radius:8px; padding:10px 12px; font-weight:700; }
        QPushButton:hover { background:#2b3650; }
        QPushButton[nav='true'] { text-align:left; margin:2px 4px; }
        QPushButton[nav='true']:checked { background:#7667ef; color:white; }
        QPushButton[primary='true'] { background:#53cdf3; color:#071018; }
        QTabWidget::pane { border:1px solid #242b3d; border-radius:9px; }
        QTabBar::tab { background:#171d2d; min-width:58px; padding:9px; margin-right:2px; }
        QTabBar::tab:selected { background:#7667ef; }
        QTextBrowser { background:#090b11; border:0; padding:10px; }
        QSplitter::handle { background:#242b3d; width:2px; }
    )"));
}
