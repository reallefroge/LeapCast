#pragma once

#include "Core.hpp"

#include <QHash>
#include <QMainWindow>
#include <QPoint>
#include <QUrl>
#include <QJsonArray>

class QStackedWidget;
class QTabWidget;
class QTextBrowser;
class QLabel;
class QListWidget;
class QPushButton;
class QCheckBox;
class QSlider;
class QFrame;
class QCloseEvent;
class QShowEvent;
class QTimer;
class QVBoxLayout;
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
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    QWidget* buildSidebar();
    QWidget* buildDashboard();
    QWidget* buildSourcesPage();
    QWidget* buildEventsPage();
    QWidget* buildObsPage();
    QWidget* buildPhoneConnectPage();
    QWidget* buildKeysPage();
    QWidget* buildSettingsPage();
    QWidget* buildBansPage();
    QWidget* buildChatDock();
    QWidget* buildModerationPage();
    QWidget* makePlatformCard(const QString& name, const QString& status,
                              const QColor& accent, const QString& action,
                              const QString& credentialAction = {},
                              const QUrl& credentialUrl = {});
    void applyTheme();
    void applyPlatformVisibility();
    void moveNavigationButton(const QString& key, int direction);
    void authorizeTwitch();
    void configureYouTubeModeration();
    void openTikTokModeration();
    void editBlockedWords();
    void editWhitelistedWords();
    // Right-click moderation menu for the dashboard's own chat views — same
    // idea as PopoutChat::showChatContextMenu, kept separate since it acts on
    // a different QTextBrowser (and its own message-id history) per tab.
    void showDashboardChatMenu(QTextBrowser* view, const QPoint& globalPos);
    void reviewTwitchAppeal(bool approve);
    void showPostUpdateConnectionCheck();
    void showFirstLaunchUpdateLog();
    void refreshPinnedBanner();
    void syncMobileSettings();
    void updateSeasonalEffects(bool allowCelebration = true);

    QStackedWidget* pages_{};
    QTabWidget* chatTabs_{};
    QTabWidget* moderationTabs_{};
    QHash<QString,QTextBrowser*> chatViews_;
    QHash<QString,ChatMessage> pinnedMessages_;
    QLabel* pinnedBanner_{};
    QHash<QString,QWidget*> sourceCards_;
    QHash<QString,QWidget*> platformChatWidgets_;
    QHash<QString,QPushButton*> navigationButtons_;
    QStringList navigationOrder_;
    QVBoxLayout* navigationLayout_{};
    // Messages currently rendered in the dashboard chat views, keyed by an
    // ever-increasing id stamped onto each message's paragraph as a block
    // property (see appendChatMessage in MainWindow.cpp), so a right-click
    // can be traced back to the ChatMessage it landed on.
    QHash<qint64, ChatMessage> chatHistoryById_;
    qint64 nextChatSeq_{};
    QHash<QString,QLabel*> sourceStates_;
    // Last moderation-failure detail shown per platform, so an unmoderated
    // channel that keeps tripping AutoMod doesn't reopen the same warning
    // dialog for every chat message.
    QHash<QString,QString> lastModerationWarning_;
    QListWidget* twitchBans_{};
    QListWidget* youtubeBans_{};
    QListWidget* twitchAppeals_{};
    QTextBrowser* twitchAppealHistory_{};
    QJsonArray twitchBansCache_;
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
    QUrl mobileUpdateAsset_;
    QString mobileUpdateName_;
    QString mobileUpdateDigest_;
    QWidget* seasonalDecoration_{};
    QTimer* seasonalTimer_{};
    QString seasonalTheme_;
    bool birthdayCelebrationPlayedThisLaunch_{};
    bool silentUpdateCheck_{};
};
