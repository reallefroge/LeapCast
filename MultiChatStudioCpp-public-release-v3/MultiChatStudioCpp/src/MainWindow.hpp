#pragma once

#include <QHash>
#include <QMainWindow>

class QStackedWidget;
class QTabWidget;
class QTextBrowser;
class QLabel;
class AppController;
class OverlayServer;
class PopoutChat;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    QWidget* buildSidebar();
    QWidget* buildDashboard();
    QWidget* buildSourcesPage();
    QWidget* buildChatDock();
    QWidget* buildModerationPage();
    QWidget* makePlatformCard(const QString& name, const QString& status,
                              const QColor& accent, const QString& action);
    void applyTheme();

    QStackedWidget* pages_{};
    QTabWidget* chatTabs_{};
    QHash<QString,QTextBrowser*> chatViews_;
    QHash<QString,QLabel*> sourceStates_;
    AppController* controller_{};
    OverlayServer* overlay_{};
    PopoutChat* popout_{};
};
