#include "MainWindow.hpp"
#include "AppController.hpp"
#include "Overlay.hpp"
#include "UpdateService.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
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

QString friendlyTimestamp(const QString& value) {
    const QDateTime timestamp = QDateTime::fromString(value, Qt::ISODate);
    if (!timestamp.isValid()) return value;
    return timestamp.toLocalTime().toString(QStringLiteral("MMM d, yyyy  •  h:mm AP"));
}

QString eventAction(const StreamEvent& event) {
    if (event.kind.contains(QStringLiteral("donation")))
        return event.amount.isEmpty() ? QStringLiteral("sent a donation")
                                      : QStringLiteral("donated %1").arg(event.amount);
    if (event.kind.contains(QStringLiteral("follow"))) return QStringLiteral("followed the channel");
    if (event.kind.contains(QStringLiteral("raid"))) return QStringLiteral("raided the channel");
    if (event.kind.contains(QStringLiteral("host"))) return QStringLiteral("hosted the channel");
    if (event.kind.contains(QStringLiteral("bits")))
        return event.amount.isEmpty() ? QStringLiteral("sent Bits")
                                      : QStringLiteral("sent %1 Bits").arg(event.amount);
    if (event.kind.contains(QStringLiteral("member"))) return QStringLiteral("became a member");
    return QStringLiteral("subscribed");
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Leapcast Studio • Multi-Chat & Moderation"));
    resize(1280, 760);
    setMinimumSize(1000, 620);
    setWindowIcon(QIcon(QStringLiteral(":/brand/lefroge_chat_icon.png")));
    applyTheme();
    controller_ = new AppController(this);
    overlay_ = new OverlayServer(this);
    overlay_->start(static_cast<quint16>(controller_->settings()->preference(QStringLiteral("port"),8080).toInt()));
    overlay_->setFadeSeconds(controller_->settings()->preference(QStringLiteral("overlay_fade_seconds"), 0).toInt());
    updater_ = new UpdateService(this);

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
    connect(controller_,&AppController::eventReady,this,[this](const StreamEvent&e){
        const QString colour=e.platform==QStringLiteral("twitch")?QStringLiteral("#b48cff"):
            e.platform==QStringLiteral("youtube")?QStringLiteral("#ff637d"):QStringLiteral("#63e6be");
        const QString message=QStringLiteral(
            "<div style='margin:7px 0;padding:9px 11px;background:#171d2d;border-left:3px solid %1;border-radius:7px'>"
            "<span style='color:#8f9bb5;font-size:9pt;font-weight:700'>STREAM ALERT</span><br>"
            "<b style='color:#f8fbff'>%2</b> <span style='color:%1'>%3</span>%4</div>")
            .arg(colour,e.user.toHtmlEscaped(),eventAction(e).toHtmlEscaped(),
                 e.message.isEmpty()?QString():QStringLiteral("<br><span style='color:#c7cede'>%1</span>").arg(e.message.toHtmlEscaped()));
        if(chatViews_.contains(QStringLiteral("combined")))chatViews_[QStringLiteral("combined")]->append(message);
        if(chatViews_.contains(e.platform))chatViews_[e.platform]->append(message);
        if(popout_)popout_->showEvent(e);
    });
    connect(controller_,&AppController::viewerCount,this,[this](const QString&p,int n){overlay_->setViewers(p,n);if(popout_)popout_->setViewers(p,n);});
    connect(controller_, &AppController::sourceStatus, this,
            [this](const QString& platform,const QString& state,const QString& detail){
        const QString colour=state=="ok"?"#63e6be":state=="error"?"#ff5573":"#f6c85f";
        if(sourceStates_.contains(platform)){
            auto* value=sourceStates_[platform]; value->setText(detail);
            value->setStyleSheet("color:"+colour);
        }
        if(platform==QStringLiteral("twitch")&&twitchModerationStatus_){
            twitchModerationStatus_->setText(detail);
            twitchModerationStatus_->setStyleSheet("color:"+colour);
        }
        if(platform==QStringLiteral("youtube")&&youtubeModerationStatus_){
            youtubeModerationStatus_->setText(detail);
            youtubeModerationStatus_->setStyleSheet("color:"+colour);
        }
    });
    connect(controller_, &AppController::bansUpdated, this,
            [this](const QString& platform, const QJsonArray& bans) {
        QListWidget* list = platform == QStringLiteral("twitch") ? twitchBans_ : youtubeBans_;
        if (!list) return;
        list->clear();
        for (const auto& value : bans) {
            const QJsonObject ban = value.toObject();
            const QString user = ban.value(QStringLiteral("user_name")).toString(
                ban.value(QStringLiteral("user_login")).toString(QStringLiteral("Unknown user")));
            const QString reason = ban.value(QStringLiteral("reason")).toString();
            const QString type = ban.value(QStringLiteral("type")).toString(QStringLiteral("Active restriction"));
            const QString created = ban.value(QStringLiteral("created_at")).toString();
            const QString summary = reason.isEmpty() ? type : reason;
            auto* item = new QListWidgetItem;
            item->setData(Qt::UserRole, platform == QStringLiteral("twitch")
                ? ban.value(QStringLiteral("user_id")).toString()
                : ban.value(QStringLiteral("id")).toString());
            item->setData(Qt::UserRole + 1, QStringLiteral("Account: %1\nReason: %2\nCreated: %3")
                .arg(user, summary, created.isEmpty() ? QStringLiteral("Not recorded") : friendlyTimestamp(created)));
            auto* card = new QWidget;
            card->setProperty("restrictionCard", true);
            auto* cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(13, 9, 13, 9);
            cardLayout->setSpacing(3);
            cardLayout->addWidget(label(user, "restrictionName"));
            auto* detail = label(summary, "restrictionReason");
            detail->setWordWrap(true);
            cardLayout->addWidget(detail);
            if (!created.isEmpty()) cardLayout->addWidget(label(friendlyTimestamp(created), "restrictionTime"));
            item->setSizeHint(card->sizeHint());
            list->addItem(item);
            list->setItemWidget(item, card);
        }
        if (list->count() == 0) {
            auto* empty = new QListWidgetItem(QStringLiteral("No active restrictions found."));
            empty->setFlags(Qt::NoItemFlags);
            list->addItem(empty);
        }
    });
    connect(controller_, &AppController::moderationResult, this,
            [this](const QString& platform, bool success, const QString& detail) {
        if (success) { lastModerationWarning_.remove(platform); return; }
        // AutoMod retries a moderation action on every matching chat message;
        // once we've told the user about a given failure (e.g. "not a moderator
        // on that channel"), don't reopen the same dialog for every repeat.
        if (lastModerationWarning_.value(platform) == detail) return;
        lastModerationWarning_[platform] = detail;
        QMessageBox::warning(this, QStringLiteral("Moderation"), detail);
    });
    connect(controller_, &AppController::twitchClipCreated, this, [this](const QUrl& url) {
        if (popout_) popout_->showClipResult(true, QStringLiteral("Clip created — link copied to clipboard."), url);
    });
    connect(controller_, &AppController::twitchClipFailed, this, [this](const QString& detail) {
        if (popout_) popout_->showClipResult(false, detail);
    });
    connect(controller_,&AppController::twitchAuthorizationUrl,this,[this](const QUrl&url){
        if(twitchModerationStatus_)twitchModerationStatus_->setText(QStringLiteral("Finish signing in on Twitch, then return here."));
        if(twitchConnectButton_){twitchConnectButton_->setText(QStringLiteral("Waiting for Twitch…"));twitchConnectButton_->setEnabled(false);}
        if(!QDesktopServices::openUrl(url))QMessageBox::warning(this,QStringLiteral("Twitch authorization"),QStringLiteral("Could not open Twitch in your browser."));
    });
    connect(controller_,&AppController::twitchAuthorized,this,[this](const QString&login){
        if(twitchModerationStatus_)twitchModerationStatus_->setText(QStringLiteral("Connected as %1").arg(login));
        if(twitchConnectButton_){twitchConnectButton_->setText(QStringLiteral("Reconnect Twitch"));twitchConnectButton_->setEnabled(true);}
        if(popout_)popout_->setClipAvailable(true);
        QMessageBox::information(this,QStringLiteral("Twitch connected"),QStringLiteral("You're all set. Leapcast Studio is connected to Twitch as %1.").arg(login));
    });
    connect(controller_,&AppController::twitchAuthorizationFailed,this,[this](const QString&detail){
        if(twitchConnectButton_){twitchConnectButton_->setText(QStringLiteral("Try Twitch again"));twitchConnectButton_->setEnabled(true);}
        const bool missingAppId=detail.contains(QStringLiteral("application ID"),Qt::CaseInsensitive);
        QMessageBox::critical(this,QStringLiteral("Twitch couldn't connect"),
            missingAppId
                ? QStringLiteral("This installer is missing Leapcast Studio's Twitch connection setting. This is a build problem—not something you need to find or paste.\n\nPlease install the next Leapcast Studio update and try again.")
                : QStringLiteral("Twitch did not finish connecting.\n\n%1\n\nNothing was changed. Select Try Twitch again when you're ready.").arg(detail));
    });
    connect(updater_, &UpdateService::updateAvailable, this,
            [this](const QString& version, const QString& notes, const QUrl& asset,
                   const QString& name, const QString& digest) {
        const QString detail = notes.trimmed().isEmpty()
            ? QStringLiteral("A new version is ready.")
            : notes.left(1200);
        if (QMessageBox::question(this, QStringLiteral("Leapcast Studio update"),
                QStringLiteral("Version %1 is available.\n\n%2\n\nInstall it now? Leapcast Studio will replace its old program files and reopen automatically.")
                    .arg(version, detail)) == QMessageBox::Yes) {
            auto* progress = new QProgressDialog(
                QStringLiteral("Downloading the update…\nLeapcast Studio will reopen automatically."),
                QString(), 0, 100, this);
            progress->setWindowTitle(QStringLiteral("Updating Leapcast Studio"));
            progress->setCancelButton(nullptr);
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumDuration(0);
            progress->setAutoClose(false);
            connect(updater_, &UpdateService::progress, progress,
                    [progress](qint64 received, qint64 total) {
                if (total <= 0) {
                    progress->setRange(0, 0);
                    return;
                }
                progress->setRange(0, 100);
                progress->setValue(static_cast<int>((received * 100) / total));
            });
            connect(updater_, &UpdateService::failed, progress, [progress] {
                progress->close();
                progress->deleteLater();
            });
            progress->show();
            updater_->downloadAndInstall(asset, name, digest);
        }
    });
    connect(updater_, &UpdateService::upToDate, this, [this] {
        QMessageBox::information(this, QStringLiteral("Updates"),
                                 QStringLiteral("Leapcast Studio is up to date."));
    });
    connect(updater_, &UpdateService::failed, this, [this](const QString& detail) {
        QMessageBox::warning(this, QStringLiteral("Update check"), detail);
    });
    QTimer::singleShot(0, controller_, &AppController::startConfiguredSources);
    QTimer::singleShot(7500, this, [this] { updater_->check(false); });
}

bool MainWindow::event(QEvent* e) {
    if (e->type() == QEvent::WindowActivate && popout_ && popout_->ghostMode()) {
        popout_->setGhostMode(false);
    }
    return QMainWindow::event(e);
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
    railLayout->addWidget(label(QStringLiteral("LEAPCAST\nSTUDIO"), "brand"));

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
        else if (i == 1) pages_->addWidget(buildEventsPage());
        else if (i == 2) pages_->addWidget(buildModerationPage());
        else if (i == 3) pages_->addWidget(buildBansPage());
        else if (i == 4) pages_->addWidget(buildObsPage());
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
                                      const QColor& accent, const QString& action,
                                      const QString& credentialAction,
                                      const QUrl& credentialUrl) {
    auto* card = new QFrame;
    card->setProperty("card", true);
    auto* layout = new QVBoxLayout(card);
    auto* head = new QHBoxLayout;
    head->addWidget(label(name, "cardTitle"));
    head->addStretch();
    auto* state = label(status, "status"); state->setObjectName(QStringLiteral("cardStatus"));state->setStyleSheet("color:" + accent.name());
    head->addWidget(state);
    layout->addLayout(head);
    auto* actions = new QHBoxLayout;
    auto* button = new QPushButton(action);
    button->setObjectName(QStringLiteral("cardAction"));
    button->setStyleSheet("background:" + accent.name() + ";color:#081018;");
    if (!credentialAction.isEmpty() && credentialUrl.isValid()) {
        auto* credential = new QPushButton(credentialAction);
        credential->setObjectName(QStringLiteral("credentialLink"));
        credential->setToolTip(QStringLiteral("Open the provider's credential setup page in your browser."));
        actions->addWidget(credential, 1);
        connect(credential, &QPushButton::clicked, this, [credentialUrl] {
            QDesktopServices::openUrl(credentialUrl);
        });
    }
    actions->addWidget(button, 1);
    layout->addLayout(actions);
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

QWidget* MainWindow::buildEventsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->addWidget(label(QStringLiteral("STREAMLABS EVENTS"), "heroTitle"));
    layout->addWidget(label(QStringLiteral("Alerts appear in your chat preview and pop-out without entering the OBS overlay feed."), "muted"));
    auto* card = new QFrame;
    card->setProperty("card", true);
    auto* cardLayout = new QVBoxLayout(card);
    auto* token = new QLineEdit(controller_->settings()->secret(QStringLiteral("streamlabs_socket_token")));
    token->setEchoMode(QLineEdit::Password);
    token->setPlaceholderText(QStringLiteral("Streamlabs Socket API token"));
    cardLayout->addWidget(token);
    auto* buttons = new QHBoxLayout;
    auto* getKey = new QPushButton(QStringLiteral("Get Streamlabs API Key ↗"));
    auto* connectButton = new QPushButton(QStringLiteral("Connect Streamlabs"));
    auto* disconnectButton = new QPushButton(QStringLiteral("Disconnect"));
    buttons->addWidget(getKey); buttons->addWidget(connectButton); buttons->addWidget(disconnectButton);
    cardLayout->addLayout(buttons);
    connect(getKey, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://streamlabs.com/dashboard#/settings/api-settings")));
    });
    connect(connectButton, &QPushButton::clicked, this, [this, token] {
        if (token->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Streamlabs"), QStringLiteral("Paste the Socket API token first."));
            return;
        }
        controller_->connectStreamlabs(token->text());
    });
    connect(disconnectButton, &QPushButton::clicked, controller_, &AppController::disconnectStreamlabs);
    auto* audioDivider = label(QStringLiteral("OPTIONAL STREAMLABS ALERT AUDIO"), "cardTitle");
    cardLayout->addWidget(audioDivider);
    auto* audioEnabled = new QCheckBox(QStringLiteral("Use the sound selected in my Streamlabs Alert Box"));
    audioEnabled->setChecked(controller_->settings()->preference(QStringLiteral("streamlabs_audio_enabled"), false).toBool());
    cardLayout->addWidget(audioEnabled);
    auto* alertUrl = new QLineEdit(controller_->settings()->preference(QStringLiteral("streamlabs_alert_box_url")).toString());
    alertUrl->setPlaceholderText(QStringLiteral("https://streamlabs.com/alert-box/v3/your-widget-token"));
    cardLayout->addWidget(alertUrl);
    auto* audioButtons = new QHBoxLayout;
    auto* openAlertSettings = new QPushButton(QStringLiteral("Open Alert Box settings ↗"));
    auto* saveAudio = new QPushButton(QStringLiteral("Save audio sync"));
    audioButtons->addWidget(openAlertSettings); audioButtons->addWidget(saveAudio);
    cardLayout->addLayout(audioButtons);
    connect(openAlertSettings, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://streamlabs.com/dashboard#/alertbox")));
    });
    const auto applyAudio = [this, audioEnabled, alertUrl] {
        const bool enabled = audioEnabled->isChecked();
        const QUrl url(alertUrl->text().trimmed());
        if (enabled && (!url.isValid() || !url.host().endsWith(QStringLiteral("streamlabs.com")))) {
            QMessageBox::warning(this, QStringLiteral("Streamlabs audio"),
                                 QStringLiteral("Paste a valid Streamlabs Alert Box widget URL first."));
            return;
        }
        controller_->settings()->setPreference(QStringLiteral("streamlabs_audio_enabled"), enabled);
        controller_->settings()->setPreference(QStringLiteral("streamlabs_alert_box_url"), alertUrl->text().trimmed());
        if (popout_) popout_->setStreamlabsAlertAudio(enabled, url);
    };
    connect(saveAudio, &QPushButton::clicked, this, applyAudio);
    connect(audioEnabled, &QCheckBox::toggled, this, [applyAudio](bool) { applyAudio(); });
    layout->addWidget(card);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::buildObsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->addWidget(label(QStringLiteral("OBS OVERLAY"), "heroTitle"));
    const QString url = QStringLiteral("http://127.0.0.1:%1/").arg(overlay_->port());
    auto* urlCard = new QFrame; urlCard->setProperty("card", true);
    auto* urlLayout = new QVBoxLayout(urlCard);
    auto* urlValue = new QLineEdit(url); urlValue->setReadOnly(true); urlLayout->addWidget(urlValue);
    auto* urlButtons = new QHBoxLayout;
    auto* copy = new QPushButton(QStringLiteral("Copy URL"));
    auto* open = new QPushButton(QStringLiteral("Open overlay"));
    auto* test = new QPushButton(QStringLiteral("Test message"));
    auto* clear = new QPushButton(QStringLiteral("Clear overlay"));
    urlButtons->addWidget(copy); urlButtons->addWidget(open); urlButtons->addWidget(test); urlButtons->addWidget(clear);
    urlLayout->addLayout(urlButtons);
    connect(copy, &QPushButton::clicked, this, [url] { QApplication::clipboard()->setText(url); });
    connect(open, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
    connect(test, &QPushButton::clicked, this, [this] {
        ChatMessage message; message.user = QStringLiteral("Leapcast Studio");
        message.text = QStringLiteral("OBS overlay test message"); message.platform = QStringLiteral("twitch");
        message.color = QColor(QStringLiteral("#53cdf3")); overlay_->ingest(message);
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        overlay_->clear();
        if (popout_) popout_->clearMessages();
    });
    layout->addWidget(urlCard);

    auto* fadeCard = new QFrame; fadeCard->setProperty("card", true);
    auto* fadeLayout = new QHBoxLayout(fadeCard);
    auto* copyText = new QVBoxLayout;
    copyText->addWidget(label(QStringLiteral("MESSAGE FADE TIMER"), "cardTitle"));
    copyText->addWidget(label(QStringLiteral("How long each chat message remains visible in the OBS overlay."), "muted"));
    fadeLayout->addLayout(copyText, 1);
    auto* seconds = new QSpinBox;
    seconds->setRange(0, 3600);
    seconds->setSuffix(QStringLiteral(" seconds"));
    seconds->setSpecialValueText(QStringLiteral("Never fade"));
    seconds->setValue(overlay_->fadeSeconds());
    fadeLayout->addWidget(seconds);
    connect(seconds, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        overlay_->setFadeSeconds(value);
        controller_->settings()->setPreference(QStringLiteral("overlay_fade_seconds"), value);
    });
    layout->addWidget(fadeCard);

    auto* updateCard = new QFrame; updateCard->setProperty("card", true);
    auto* updateLayout = new QHBoxLayout(updateCard);
    auto* updateText = new QVBoxLayout;
    updateText->addWidget(label(QStringLiteral("AUTOMATIC UPDATES"), "cardTitle"));
    updateText->addWidget(label(QStringLiteral("Checks github.com/reallefroge/MultiStreamChat Releases whenever the app starts."), "muted"));
    updateLayout->addLayout(updateText, 1);
    auto* releases = new QPushButton(QStringLiteral("Open Releases"));
    auto* checkNow = new QPushButton(QStringLiteral("Check now"));
    updateLayout->addWidget(releases); updateLayout->addWidget(checkNow);
    connect(releases, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/reallefroge/MultiStreamChat/releases")));
    });
    connect(checkNow, &QPushButton::clicked, this, [this] { updater_->check(true); });
    layout->addWidget(updateCard);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::buildBansPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->setSpacing(12);
    auto* hero = new QFrame;
    hero->setObjectName(QStringLiteral("bansHero"));
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(18, 16, 18, 16);
    heroLayout->addWidget(label(QStringLiteral("BANS & TIMEOUTS"), "heroTitle"));
    auto* help = label(QStringLiteral("Review active Twitch restrictions and moderation actions created through Leapcast Studio."), "muted");
    help->setWordWrap(true);
    heroLayout->addWidget(help);
    layout->addWidget(hero);
    auto* tabs = new QTabWidget;
    tabs->setObjectName(QStringLiteral("moderationTabs"));
    const auto makePage = [this](const QString& platform, QListWidget** output) {
        auto* host = new QWidget; auto* hostLayout = new QVBoxLayout(host);
        hostLayout->setContentsMargins(14, 14, 14, 14);
        hostLayout->setSpacing(10);
        auto* list = new QListWidget;
        list->setObjectName(QStringLiteral("restrictionList"));
        list->setSpacing(6);
        *output = list; hostLayout->addWidget(list, 1);
        auto* controls = new QHBoxLayout;
        auto* refresh = new QPushButton(QStringLiteral("Refresh list"));
        auto* details = new QPushButton(QStringLiteral("View details"));
        auto* unban = new QPushButton(QStringLiteral("Remove selected"));
        unban->setProperty("danger", true);
        controls->addWidget(refresh); controls->addWidget(details); controls->addStretch(); controls->addWidget(unban);
        hostLayout->addLayout(controls);
        connect(refresh, &QPushButton::clicked, this, [this, platform] { controller_->refreshBans(platform); });
        connect(details, &QPushButton::clicked, this, [this, list] {
            if (auto* item = list->currentItem()) {
                const QString details = item->data(Qt::UserRole + 1).toString();
                if (!details.isEmpty()) QMessageBox::information(this, QStringLiteral("Restriction details"), details);
            }
        });
        connect(unban, &QPushButton::clicked, this, [this, list, platform] {
            auto* item = list->currentItem(); if (!item) return;
            const QString id = item->data(Qt::UserRole).toString(); if (id.isEmpty()) return;
            if (QMessageBox::question(this, QStringLiteral("Remove restriction"),
                    QStringLiteral("Remove the selected restriction?")) != QMessageBox::Yes) return;
            if (platform == QStringLiteral("twitch")) controller_->unbanTwitch(id);
            else controller_->unbanYouTube(id);
        });
        return host;
    };
    tabs->addTab(makePage(QStringLiteral("twitch"), &twitchBans_), QStringLiteral("Twitch"));
    tabs->addTab(makePage(QStringLiteral("youtube"), &youtubeBans_), QStringLiteral("YouTube"));
    layout->addWidget(tabs, 1);
    QTimer::singleShot(0, this, [this] { controller_->refreshBans(QStringLiteral("youtube")); });
    return page;
}

QWidget* MainWindow::buildModerationPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 4, 8, 8);
    auto* hero = new QFrame; hero->setObjectName(QStringLiteral("safetyHero"));
    auto* heroLayout = new QHBoxLayout(hero);
    auto* copy = new QVBoxLayout;
    copy->addWidget(label(QStringLiteral("CREATOR SAFETY CENTER"), "heroTitle"));
    copy->addWidget(label(QStringLiteral("A multi-chat system and moderation device for every connected community."), "muted"));
    heroLayout->addLayout(copy, 1);
    heroLayout->addWidget(label(QStringLiteral("● LIVE ACTIONS\n● RIGHT-CLICK READY\n● LOCAL AUDIT"), "signal"));
    layout->addWidget(hero);

    auto* columns = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    const bool twitchAuthorized=!controller_->settings()->secret(QStringLiteral("twitch_access_token")).isEmpty();
    auto* twitchCard = makePlatformCard(QStringLiteral("Twitch"),
                                        twitchAuthorized?QStringLiteral("Connected"):QStringLiteral("Connect your Twitch account"),
                                        QColor("#9146ff"), twitchAuthorized?QStringLiteral("Reconnect Twitch"):QStringLiteral("Connect Twitch"));
    twitchModerationStatus_=twitchCard->findChild<QLabel*>(QStringLiteral("cardStatus"));
    twitchConnectButton_=twitchCard->findChild<QPushButton*>(QStringLiteral("cardAction"));
    left->addWidget(twitchCard);
    connect(twitchConnectButton_, &QPushButton::clicked, this, &MainWindow::authorizeTwitch);
    const bool youtubeAuthorized=!controller_->settings()->secret(QStringLiteral("youtube_access_token")).isEmpty();
    auto* youtubeCard = makePlatformCard(QStringLiteral("YouTube"),
                                         youtubeAuthorized?QStringLiteral("Connected"):QStringLiteral("Connect your YouTube account"),
                                         QColor("#ff334f"), youtubeAuthorized?QStringLiteral("Reconnect YouTube"):QStringLiteral("Connect YouTube"),
                                         QStringLiteral("Get YouTube keys ↗"),
                                         QUrl(QStringLiteral("https://console.cloud.google.com/apis/credentials")));
    youtubeModerationStatus_=youtubeCard->findChild<QLabel*>(QStringLiteral("cardStatus"));
    youtubeConnectButton_=youtubeCard->findChild<QPushButton*>(QStringLiteral("cardAction"));
    left->addWidget(youtubeCard);
    connect(youtubeConnectButton_, &QPushButton::clicked, this, &MainWindow::configureYouTubeModeration);
    left->addStretch();
    auto* right = new QVBoxLayout;
    auto* tiktokCard = makePlatformCard(QStringLiteral("TikTok"),
                                        QStringLiteral("Moderate directly on TikTok"),
                                        QColor("#18e0d5"), QStringLiteral("Open TikTok LIVE in browser"));
    right->addWidget(tiktokCard);
    connect(tiktokCard->findChild<QPushButton*>(QStringLiteral("cardAction")), &QPushButton::clicked,
            this, &MainWindow::openTikTokModeration);
    auto* automodCard = makePlatformCard(QStringLiteral("AutoMod"), QStringLiteral("Ready"),
                                         QColor("#63e6be"), QStringLiteral("Edit blocked words"));
    right->addWidget(automodCard);
    connect(automodCard->findChild<QPushButton*>(QStringLiteral("cardAction")), &QPushButton::clicked,
            this, &MainWindow::editBlockedWords);
    right->addStretch();
    columns->addLayout(left, 1); columns->addLayout(right, 1);
    layout->addLayout(columns, 1);
    return page;
}

void MainWindow::authorizeTwitch() {
    if(twitchModerationStatus_)twitchModerationStatus_->setText(QStringLiteral("Opening Twitch sign-in…"));
    if(twitchConnectButton_){twitchConnectButton_->setText(QStringLiteral("Connecting…"));twitchConnectButton_->setEnabled(false);}
    controller_->authorizeTwitch();
}

void MainWindow::configureYouTubeModeration() {
    bool ok = false;
    const QString token = QInputDialog::getText(this, QStringLiteral("YouTube moderation"),
        QStringLiteral("Google OAuth access token:"), QLineEdit::Password,
        controller_->settings()->secret(QStringLiteral("youtube_access_token")), &ok).trimmed();
    if (!ok || token.isEmpty()) return;
    controller_->configureYouTubeModeration(token);
    if(youtubeModerationStatus_){
        youtubeModerationStatus_->setText(QStringLiteral("Connected"));
        youtubeModerationStatus_->setStyleSheet(QStringLiteral("color:#63e6be"));
    }
    if(youtubeConnectButton_)youtubeConnectButton_->setText(QStringLiteral("Reconnect YouTube"));
    QMessageBox::information(this, QStringLiteral("YouTube connected"), QStringLiteral("YouTube moderation access was saved. You're all set."));
}

void MainWindow::openTikTokModeration() {
    QString saved=controller_->settings()->link(QStringLiteral("tiktok")).trimmed();
    if(saved.isEmpty()){
        if(QMessageBox::information(this,QStringLiteral("Add your TikTok profile first"),
            QStringLiteral("Open Sources and paste your TikTok profile, such as tiktok.com/@yourname. Then this button will open your LIVE stream directly."),
            QStringLiteral("Go to Sources"),QStringLiteral("Cancel"))==0)pages_->setCurrentIndex(0);
        return;
    }

    if(saved.startsWith(QLatin1Char('@')))saved.remove(0,1);
    if(!saved.contains(QStringLiteral("tiktok.com"),Qt::CaseInsensitive))
        saved=QStringLiteral("https://www.tiktok.com/@")+saved;
    QUrl url=QUrl::fromUserInput(saved);
    QString path=url.path();
    while(path.endsWith(QLatin1Char('/')))path.chop(1);
    if(!path.endsWith(QStringLiteral("/live"),Qt::CaseInsensitive))path+=QStringLiteral("/live");
    url.setPath(path);url.setQuery(QString());url.setFragment(QString());
    if(!url.isValid()||!QDesktopServices::openUrl(url))
        QMessageBox::warning(this,QStringLiteral("TikTok couldn't open"),
                             QStringLiteral("Check the TikTok profile saved under Sources and try again."));
}

void MainWindow::editBlockedWords() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(controller_->blockedWordsPath()));
    controller_->reloadAutoMod();
}

QWidget* MainWindow::buildChatDock() {
    auto* dock = new QFrame; dock->setObjectName(QStringLiteral("chatDock"));
    auto* layout = new QVBoxLayout(dock);
    layout->addWidget(label(QStringLiteral("CHAT PREVIEW"), "pageTitle"));
    chatTabs_ = new QTabWidget;
    chatTabs_->setIconSize(QSize(30, 30));
    const QList<QPair<QString, QString>> tabs{
        {QStringLiteral("ALL"), QString()}, {QStringLiteral("Twitch"), QStringLiteral(":/brand/twitch.png")},
        {QStringLiteral("YouTube"), QStringLiteral(":/brand/youtube.png")},
        {QStringLiteral("Shorts"), QStringLiteral(":/brand/youtube_shorts.png")},
        {QStringLiteral("TikTok"), QStringLiteral(":/brand/tiktok.png")}};
    const QStringList keys{QStringLiteral("combined"),QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("yt_shorts"),QStringLiteral("tiktok")};
    for (qsizetype index=0; index<tabs.size(); ++index) {
        const auto& tab=tabs.at(index);
        auto* chat = new QTextBrowser;
        chat->setPlaceholderText(QStringLiteral("Connected messages appear here."));
        const QIcon icon(tab.second);
        if (!icon.isNull()) chatTabs_->addTab(chat, icon, QString());
        else chatTabs_->addTab(chat, tab.first);
        chatTabs_->setTabToolTip(static_cast<int>(index), tab.first);
        chatViews_.insert(keys.at(index),chat);
    }

    auto* kickPreview = new QTextBrowser;
    kickPreview->setReadOnly(true);
    kickPreview->setHtml(QStringLiteral(
        "<div style='text-align:center;margin-top:44px;color:#7f8ba5'>"
        "<div style='font-size:13pt;font-weight:800;color:#53cdf3'>KICK</div>"
        "<div style='margin-top:6px;font-weight:700'>Coming Soon</div>"
        "<div style='margin-top:2px;font-size:9pt'>Kick chat support is on the way.</div></div>"));
    const int kickTabIndex = chatTabs_->addTab(kickPreview, QStringLiteral("Kick"));
    chatTabs_->setTabEnabled(kickTabIndex, false);
    chatTabs_->setTabToolTip(kickTabIndex, QStringLiteral("Kick — Coming Soon"));

    layout->addWidget(chatTabs_, 1);

    const auto ensurePopout = [this] {
        if (popout_) return popout_;
        popout_ = new PopoutChat;
        popout_->setOpacityPercent(controller_->settings()->preference(QStringLiteral("popout_opacity_percent"), 80).toInt());
        popout_->setClipAvailable(!controller_->settings()->secret(QStringLiteral("twitch_access_token")).isEmpty());
        connect(popout_, &PopoutChat::ghostModeChanged, this, [this](bool enabled) {
            if (!popoutClickThrough_) return;
            QSignalBlocker blocker(popoutClickThrough_);
            popoutClickThrough_->setChecked(enabled);
        });
        connect(popout_, &PopoutChat::clipRequested, this, [this] { controller_->createTwitchClip(); });
        return popout_;
    };

    const int savedOpacity = controller_->settings()->preference(QStringLiteral("popout_opacity_percent"), 80).toInt();
    auto* opacityRow = new QHBoxLayout;
    opacityRow->addWidget(label(QStringLiteral("Pop-out opacity")));
    popoutOpacity_ = new QSlider(Qt::Horizontal);
    popoutOpacity_->setRange(10, 100);
    popoutOpacity_->setValue(savedOpacity);
    opacityRow->addWidget(popoutOpacity_, 1);
    auto* opacityValue = label(QString::number(savedOpacity) + QStringLiteral("%"));
    opacityRow->addWidget(opacityValue);
    layout->addLayout(opacityRow);
    connect(popoutOpacity_, &QSlider::valueChanged, this, [this, opacityValue](int value) {
        opacityValue->setText(QString::number(value) + QStringLiteral("%"));
        controller_->settings()->setPreference(QStringLiteral("popout_opacity_percent"), value);
        if (popout_) popout_->setOpacityPercent(value);
    });

    popoutClickThrough_ = new QCheckBox(QStringLiteral("Click-through (see and click what's behind it)"));
    layout->addWidget(popoutClickThrough_);
    layout->addWidget(label(QStringLiteral("Press Esc or Alt+C, or click back into Leapcast Studio, to regain control of the pop-out."), "muted"));
    connect(popoutClickThrough_, &QCheckBox::toggled, this, [ensurePopout](bool enabled) {
        auto* popout = ensurePopout();
        popout->setGhostMode(enabled);
        popout->raise();
    });

    auto* popout = new QPushButton(QStringLiteral("Open Pop-out Chat"));
    popout->setProperty("primary", true);
    layout->addWidget(popout);
    connect(popout,&QPushButton::clicked,this,[this,ensurePopout]{
        auto* view = ensurePopout();
        view->setStreamlabsAlertAudio(
            controller_->settings()->preference(QStringLiteral("streamlabs_audio_enabled"),false).toBool(),
            QUrl(controller_->settings()->preference(QStringLiteral("streamlabs_alert_box_url")).toString()));
        view->show();view->raise();
    });
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
        QFrame#bansHero { background:#141a29; border:1px solid #28334a; border-left:5px solid #7667ef; border-radius:12px; }
        QFrame[card='true'] { background:#161b29; border:1px solid #252d42; border-radius:12px; }
        QPushButton { background:#20283a; border:0; border-radius:8px; padding:10px 12px; font-weight:700; }
        QPushButton:hover { background:#2b3650; }
        QPushButton[nav='true'] { text-align:left; margin:2px 4px; }
        QPushButton[nav='true']:checked { background:#7667ef; color:white; }
        QPushButton[primary='true'] { background:#53cdf3; color:#071018; }
        QPushButton[danger='true'] { background:#44202b; color:#ff91a4; }
        QPushButton[danger='true']:hover { background:#5a2635; color:#ffd3da; }
        QTabWidget::pane { border:1px solid #242b3d; border-radius:9px; }
        QTabBar::tab { background:#171d2d; min-width:62px; min-height:38px; padding:5px 10px; margin-right:2px; }
        QTabBar::tab:selected { background:#7667ef; }
        QTabBar::tab:disabled { color:#565f78; }
        QTextBrowser { background:#090b11; border:0; padding:10px; }
        QTabWidget#moderationTabs::pane { background:#101522; border:1px solid #283149; border-radius:10px; }
        QListWidget#restrictionList { background:transparent; border:0; outline:0; padding:2px; }
        QListWidget#restrictionList::item { background:#171d2b; border:1px solid #273149; border-radius:9px; }
        QListWidget#restrictionList::item:selected { background:#202944; border:1px solid #7667ef; }
        QWidget[restrictionCard='true'] { background:transparent; }
        QLabel[role='restrictionName'] { color:#ffffff; font-size:11pt; font-weight:750; }
        QLabel[role='restrictionReason'] { color:#c7cede; }
        QLabel[role='restrictionTime'] { color:#7f8ba5; font-size:9pt; }
        QSplitter::handle { background:#242b3d; width:2px; }
    )"));
}
