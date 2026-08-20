#pragma once
#include "Core.hpp"
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QTcpServer>
#include <QWidget>
class QTextBrowser; class QLabel;
class QStackedLayout;
class QWebEngineView;
class QUrl;
class OverlayServer final : public QObject {
public:
    explicit OverlayServer(QObject* parent = nullptr);
    bool start(quint16 preferredPort = 8080);
    void stop();
    quint16 port() const { return server_.serverPort(); }
    void ingest(const ChatMessage& message);
    void setViewers(const QString& platform, int count);
    void setFadeSeconds(int seconds) { fadeSeconds_ = qMax(0, seconds); }
    int fadeSeconds() const { return fadeSeconds_; }
    void clear();

private:
    void accept();
    QByteArray responseFor(const QByteArray& target);
    QTcpServer server_;
    QList<QPair<quint64, ChatMessage>> messages_;
    QHash<QString, int> viewers_;
    quint64 cursor_{};
    int fadeSeconds_{};
};

class PopoutChat final : public QWidget, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit PopoutChat(QWidget* parent = nullptr);
    ~PopoutChat() override;
    void appendMessage(const ChatMessage& message);
    void showEvent(const StreamEvent& event);
    void setViewers(const QString& platform, int count);
    // Click-through "ghost" mode: the window ignores mouse/keyboard input so the
    // desktop underneath stays fully usable. Escape / Alt+C (registered as OS-wide
    // hotkeys while ghost mode is on, since the window itself cannot receive input)
    // or reactivating the main Leapcast Studio window hand control back.
    void setGhostMode(bool enabled);
    bool ghostMode() const { return ghostMode_; }
    void setClearBackground(bool enabled);
    // Percent opacity of the chat/viewer panel backgrounds only; message text and
    // Streamlabs alerts always stay fully opaque so they remain readable.
    void setOpacityPercent(int percent);
    int opacityPercent() const { return opacityPercent_; }
    void clearMessages();
    void setStreamlabsAlertAudio(bool enabled, const QUrl& alertBoxUrl);

signals:
    void ghostModeChanged(bool enabled);

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void applyOpacity();
    void registerRestoreHotkeys();
    void unregisterRestoreHotkeys();

    QTextBrowser* chat_{};
    QLabel* event_{};
    QLabel* viewers_{};
    QHash<QString, int> counts_;
    QStackedLayout* chatStack_{};
    QWebEngineView* alertView_{};
    int opacityPercent_{80};
    bool ghostMode_{};
    bool hotkeysRegistered_{};
    bool clearBackground_{};
};
