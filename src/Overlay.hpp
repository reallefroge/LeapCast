#pragma once
#include "Core.hpp"
#include <QAbstractNativeEventFilter>
#include <QContextMenuEvent>
#include <QHash>
#include <QPoint>
#include <QTcpServer>
#include <QTextBrowser>
#include <QWidget>
class QLabel; class QPushButton;
class QStackedLayout;
class QWebEngineView;
class QUrl;
class QCloseEvent;

// A QTextBrowser that reports right-clicks via a signal instead of showing
// its own built-in Copy/Copy Link Location/Select All menu. Overriding
// contextMenuEvent() directly (rather than relying on
// Qt::CustomContextMenu + customContextMenuRequested on the viewport) means
// there's no dependency on how that policy gets routed through
// QAbstractScrollArea's viewport — this virtual override always wins.
class ChatBrowser final : public QTextBrowser {
    Q_OBJECT
public:
    using QTextBrowser::QTextBrowser;
signals:
    void chatContextMenuRequested(const QPoint& globalPos);
protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        emit chatContextMenuRequested(event->globalPos());
        event->accept();
    }
};

// Embedded Chromium (Qt WebEngine) window used to open the Twitch clip editor
// in-app instead of the user's external browser. Uses the default persistent
// profile, the same one TikTok's embedded view uses, so a Twitch web login
// made here is remembered on later opens — the app's own Twitch device-code
// authorization is a separate API token and doesn't carry a browser session.
class ClipEditorWindow final : public QWidget {
    Q_OBJECT
public:
    explicit ClipEditorWindow(QWidget* parent = nullptr);
    void openUrl(const QUrl& url);
protected:
    // Releases the loaded page's renderer memory once the window is closed,
    // instead of keeping a full Twitch tab resident for the rest of the
    // app's lifetime after a single clip. Reopening just reloads the URL.
    void closeEvent(QCloseEvent* event) override;
private:
    QWebEngineView* view_{};
    QLabel* addressLabel_{};
};
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
    // A muted, in-line note that AutoMod removed a message — shown here and in
    // the dashboard chat views only, never sent to the OBS overlay feed.
    void appendModerationNote(const QString& user, const QString& reason);
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
    // Enables the "Clip" button (Twitch clip creation, mirroring twitch.tv's own
    // Clip button) once a Twitch channel with clip access is available.
    void setClipAvailable(bool available);
    void showClipResult(bool success, const QString& text, const QUrl& editUrl = {});

signals:
    void ghostModeChanged(bool enabled);
    void clipRequested();
    // "Open editor" was clicked after a clip was created.
    void openClipEditor(const QUrl& url);
    // Manual moderation from a chat message's right-click menu, for whatever
    // AutoMod doesn't catch. seconds=0 means a permanent ban.
    void deleteMessageRequested(const ChatMessage& message);
    void timeoutUserRequested(const ChatMessage& message, int seconds, const QString& reason);

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void applyOpacity();
    void registerRestoreHotkeys();
    void unregisterRestoreHotkeys();
    void showChatContextMenu(const QPoint& globalPos);

    ChatBrowser* chat_{};
    QLabel* event_{};
    QLabel* viewers_{};
    QLabel* clipStatus_{};
    QLabel* toolbarTitle_{};
    QPushButton* clipButton_{};
    QHash<QString, int> counts_;
    // Messages currently rendered in chat_, keyed by an ever-increasing id
    // stamped onto each message's paragraph as a block property, so a
    // right-click can be traced back to the ChatMessage it landed on. Ids are
    // never reused, so trimming old entries doesn't invalidate the rest.
    QHash<qint64, ChatMessage> historyById_;
    qint64 nextMessageSeq_{};
    QStackedLayout* chatStack_{};
    QWebEngineView* alertView_{};
    int opacityPercent_{80};
    bool ghostMode_{};
    bool hotkeysRegistered_{};
    bool clearBackground_{};
};
