#pragma once

#include <QHash>
#include <QMainWindow>
#include <QUrl>

class QStackedWidget;
class QTabWidget;
class QTextBrowser;
class QLabel;
class QListWidget;
class QPushButton;
class AppController;
class OverlayServer;
class PopoutChat;
class UpdateService;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    QWidget* buildSidebar();
    QWidget* buildDashboard();
    QWidget* buildSourcesPage();
    QWidget* buildEventsPage();
    QWidget* buildObsPage();
    QWidget* buildBansPage();
    QWidget* buildChatDock();
    QWidget* buildModerationPage();
    QWidget* makePlatformCard(const QString& name, const QString& status,
                              const QColor& accent, const QString& action,
                              const QString& credentialAction = {},
                              const QUrl& credentialUrl = {});
    void applyTheme();
    void authorizeTwitch();
    void configureYouTubeModeration();
    void openTikTokModeration();
    void editBlockedWords();

    QStackedWidget* pages_{};
    QTabWidget* chatTabs_{};
    QHash<QString,QTextBrowser*> chatViews_;
    QHash<QString,QLabel*> sourceStates_;
    QListWidget* twitchBans_{};
    QListWidget* youtubeBans_{};
    QLabel* twitchModerationStatus_{};
    QPushButton* twitchConnectButton_{};
    QLabel* youtubeModerationStatus_{};
    QPushButton* youtubeConnectButton_{};
    AppController* controller_{};
    OverlayServer* overlay_{};
    PopoutChat* popout_{};
    UpdateService* updater_{};
};
