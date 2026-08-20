#pragma once
#include "Core.hpp"
#include <QHash>
#include <QTcpServer>
#include <QWidget>
class QTextBrowser; class QLabel;
class OverlayServer final : public QObject {
    Q_OBJECT

public:
    explicit OverlayServer(QObject* parent = nullptr);
    bool start(quint16 preferredPort = 8080);
    void stop();
    quint16 port() const { return server_.serverPort(); }
    void ingest(const ChatMessage& message);
    void setViewers(const QString& platform, int count);
    void clear();

private:
    void accept();
    QByteArray responseFor(const QByteArray& target);
    QTcpServer server_;
    QList<QPair<quint64, ChatMessage>> messages_;
    QHash<QString, int> viewers_;
    quint64 cursor_{};
};

class PopoutChat final : public QWidget {
    Q_OBJECT

public:
    explicit PopoutChat(QWidget* parent = nullptr);

public slots:
    void appendMessage(const ChatMessage& message);
    void showEvent(const StreamEvent& event);
    void setViewers(const QString& platform, int count);
    void setGhostMode(bool enabled);
    void setClearBackground(bool enabled);
    void setOpacityPercent(int percent);

private:
    QTextBrowser* chat_{};
    QLabel* event_{};
    QLabel* viewers_{};
    QHash<QString, int> counts_;
};
