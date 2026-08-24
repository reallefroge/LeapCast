#pragma once
#include "Core.hpp"
#include <QAbstractNativeEventFilter>
#include <QContextMenuEvent>
#include <QHash>
#include <QNetworkAccessManager>
#include <QSet>
#include <QPoint>
#include <QUrl>
#include <QTcpServer>
#include <QTextBrowser>
#include <QWidget>
class QResizeEvent;
class QPaintEvent;
class QEvent;
class QLabel; class QPushButton;
class QStackedLayout;
class QWebEngineView;
class QWebEngineProfile;
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
    explicit ChatBrowser(QWidget* parent=nullptr);
signals:
    void chatContextMenuRequested(const QPoint& globalPos);
protected:
    void paintEvent(QPaintEvent* event) override;
    QVariant loadResource(int type,const QUrl& name) override;
    void contextMenuEvent(QContextMenuEvent* event) override {
        emit chatContextMenuRequested(event->globalPos());
        event->accept();
    }
private:
    QNetworkAccessManager imageNetwork_;
    QSet<QString> pendingImageUrls_;
};

// Embedded Chromium (Qt WebEngine) window used to open the Twitch clip editor
// in-app instead of the user's external browser. It has its own persistent
// browser profile so Twitch login cookies survive restarts without affecting
// the TikTok collector. The app's Twitch device-code authorization is a
// separate API token; v3.0.6 can reuse it as a best-effort browser-session handoff.
class ClipEditorWindow final : public QWidget {
    Q_OBJECT
public:
    explicit ClipEditorWindow(QWidget* parent = nullptr);
    void openUrl(const QUrl& url);
    // Uses the OAuth token obtained through Twitch's system-browser device
    // login as a best-effort web-session handoff for the embedded clip editor.
    void setTwitchAccessToken(const QString& token);
signals:
    void twitchBrowserLoginRequested();
protected:
    // Releases the loaded page's renderer memory once the window is closed,
    // instead of keeping a full Twitch tab resident for the rest of the
    // app's lifetime after a single clip. Reopening just reloads the URL.
    void closeEvent(QCloseEvent* event) override;
private:
    QWebEngineProfile* profile_{};
    QWebEngineView* view_{};
    QLabel* addressLabel_{};
    QUrl pendingUrl_;
    bool loginRequestInProgress_{};
};
class OverlayServer final : public QObject {
    Q_OBJECT
public:
    explicit OverlayServer(QObject* parent = nullptr);
    bool start(quint16 preferredPort = 8080);
    void stop();
    quint16 port() const { return server_.serverPort(); }
    QUrl mobileUrl() const;
    QString mobileToken() const { return QString::fromLatin1(mobileToken_); }
    void setMobileToken(const QString& token);
    QString regenerateMobileToken();
    void ingest(const ChatMessage& message);
    void ingestEvent(const StreamEvent& event);
    void setMobileBans(const QString& platform, const QJsonArray& bans);
    void setMobileAppeals(const QJsonArray& appeals);
    void setMobileSettings(const QJsonObject& settings);
    void setMobileUpdate(const QJsonObject& update);
    void setViewers(const QString& platform, int count);
    void setFadeSeconds(int seconds) { fadeSeconds_ = qMax(0, seconds); }
    void setAppearance(const QColor& background, int backgroundOpacityPercent,
                       int outlineThickness);
    void setShowPlatformIcons(bool enabled) { showPlatformIcons_ = enabled; }
    int fadeSeconds() const { return fadeSeconds_; }
    void clear();

signals:
    void mobileDeleteRequested(const ChatMessage& message);
    void mobileModerationRequested(const ChatMessage& message, int seconds, const QString& reason);
    void mobileRefreshModerationRequested();
    void mobileUnbanRequested(const QString& platform, const QString& id);
    void mobileAppealRequested(const QString& id, bool approved);
    void mobileSettingRequested(const QString& name, const QVariant& value);
    void mobileInstallUpdateRequested();

private:
    void accept();
    QByteArray responseFor(const QByteArray& method, const QByteArray& target, const QHostAddress& peer);
    QTcpServer server_;
    QList<QPair<quint64, ChatMessage>> messages_;
    QHash<QString, int> viewers_;
    QJsonArray mobileEvents_;
    QHash<QString,QJsonArray> mobileBans_;
    QJsonArray mobileAppeals_;
    QJsonObject mobileSettings_;
    QJsonObject mobileUpdate_;
    quint64 cursor_{};
    int fadeSeconds_{};
    QColor backgroundColor_{Qt::black};
    int backgroundOpacityPercent_{};
    int outlineThickness_{2};
    bool showPlatformIcons_{true};
    quint64 clearGeneration_{};
    QByteArray mobileToken_;
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
    void setAppearance(const QColor& background, int outlineThickness,
                       const QString& fontFamily = QStringLiteral("Segoe UI"),
                       int fontSizePoints = 12);
    void setShowPlatformIcons(bool enabled) { showPlatformIcons_ = enabled; }
    void showTikTokActivity(const StreamEvent& event);
    void setPinnedMessage(const QString& platform, const ChatMessage& message, bool active);
    void setSeasonalTheme(const QString& theme);
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
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyOpacity();
    void applyTextOutline();
    void registerRestoreHotkeys();
    void unregisterRestoreHotkeys();
    void showChatContextMenu(const QPoint& globalPos);

    ChatBrowser* chat_{};
    QLabel* event_{};
    QLabel* pinned_{};
    QLabel* viewers_{};
    QLabel* clipStatus_{};
    QLabel* toolbarTitle_{};
    QLabel* seasonalRibbon_{};
    // The window has no native title bar (see the constructor comment on
    // Qt::FramelessWindowHint), so this row doubles as a drag handle via
    // eventFilter() + QWindow::startSystemMove().
    QWidget* titleBar_{};
    QPushButton* clipButton_{};
    QHash<QString, int> counts_;
    QHash<QString,ChatMessage> pinnedMessages_;
    // Messages currently rendered in chat_, keyed by an ever-increasing id
    // stamped onto each message's paragraph as a block property, so a
    // right-click can be traced back to the ChatMessage it landed on. Ids are
    // never reused, so trimming old entries doesn't invalidate the rest.
    QHash<qint64, ChatMessage> historyById_;
    qint64 nextMessageSeq_{};
    QStackedLayout* chatStack_{};
    QWebEngineView* alertView_{};
    // A new installation opens as a true transparent chat overlay. Existing
    // users who deliberately saved another value still keep that preference.
    int opacityPercent_{0};
    QColor backgroundColor_{Qt::black};
    int outlineThickness_{2};
    QString fontFamily_{QStringLiteral("Segoe UI")};
    int fontSizePoints_{12};
    bool ghostMode_{};
    bool hotkeysRegistered_{};
    bool clearBackground_{};
    bool showPlatformIcons_{true};
    QString seasonalTheme_;
};
