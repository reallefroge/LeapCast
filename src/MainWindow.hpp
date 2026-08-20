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
class QCheckBox;
class QSlider;
class QFrame;
class AppController;
class OverlayServer;
class PopoutChat;
class ClipEditorWindow;
class UpdateService;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    bool event(QEvent* e) override;

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
    // Last moderation-failure detail shown per platform, so an unmoderated
    // channel that keeps tripping AutoMod doesn't reopen the same warning
    // dialog for every chat message.
    QHash<QString,QString> lastModerationWarning_;
    QListWidget* twitchBans_{};
    QListWidget* youtubeBans_{};
    // Getting-started card shown on the Sources page only while every link is
    // still blank (a fresh install). Hidden for good after the first source
    // connects successfully.
    QFrame* sourcesWelcome_{};
    QLabel* twitchModerationStatus_{};
    QPushButton* twitchConnectButton_{};
    QLabel* youtubeModerationStatus_{};
    QPushButton* youtubeConnectButton_{};
    QCheckBox* popoutClickThrough_{};
    QSlider* popoutOpacity_{};
    AppController* controller_{};
    OverlayServer* overlay_{};
    PopoutChat* popout_{};
    ClipEditorWindow* clipEditor_{};
    UpdateService* updater_{};
};
