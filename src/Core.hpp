#pragma once

#include <QColor>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVariant>

struct ChatMessage {
    QString user;
    QString text;
    QString platform;
    QStringList badges;
    QStringList badgeIds;
    QColor color;
    QString userId;
    QString messageId;
    QJsonObject metadata;
    QDateTime timestamp{QDateTime::currentDateTime()};
    QJsonObject toJson() const;
};
Q_DECLARE_METATYPE(ChatMessage)

// Renders a message's parsed badges (HOST/MOD/VIP/PRIME/SUB/CHECK/MONEY —
// see TwitchChatService::parseLine and YouTubeChatService::poll) as a short
// glyph prefix, in a fixed order so a user's badges don't reshuffle between
// messages. No network/asset dependency: real Twitch/YouTube badge art
// requires an authenticated API call and bundled images per badge set, so
// this uses plain-text glyphs instead.
QString badgeGlyphs(const QStringList& badges);

struct StreamEvent {
    QString eventId;
    QString kind;
    QString user{QStringLiteral("Someone")};
    QString amount;
    QString message;
    QString platform{QStringLiteral("streamlabs")};
    QDateTime timestamp{QDateTime::currentDateTime()};
    bool replay{};
    QJsonObject raw;
    QJsonObject toJson() const;
    static StreamEvent fromJson(const QJsonObject& value);
};
Q_DECLARE_METATYPE(StreamEvent)

class SettingsStore final : public QObject {
    Q_OBJECT
public:
    explicit SettingsStore(QObject* parent = nullptr);
    QString link(const QString& platform) const;
    void setLink(const QString& platform, const QString& value);
    bool enabled(const QString& platform) const;
    void setEnabled(const QString& platform, bool value);
    QVariant preference(const QString& name, const QVariant& fallback = {}) const;
    void setPreference(const QString& name, const QVariant& value);
    QString secret(const QString& name) const;
    void setSecret(const QString& name, const QString& value);
    QJsonObject moderation() const;
    void setModeration(const QJsonObject& value);
    QString dataDirectory() const;
    bool save();
signals:
    void changed();
private:
    void load();
    QJsonObject root_;
    QString directory_;
    QString path_;
};

class AutoMod final : public QObject {
    Q_OBJECT
public:
    explicit AutoMod(SettingsStore* settings, QObject* parent = nullptr);
    bool reload();
    bool check(const ChatMessage& message, QString* reason = nullptr);
    QString blockedWordsPath() const { return path_; }
    static QString normalize(const QString& text);
private:
    struct Term { QString normalized; QString compact; QRegularExpression bypass; };
    static QString latinize(const QString& text);
    static QString compact(const QString& text);
    static bool distanceAtMostOne(const QString& a, const QString& b);
    bool blockedMatch(const QString& text) const;
    static bool promotionSpam(const QString& text);
    SettingsStore* settings_{};
    QString path_;
    QList<Term> terms_;
    QHash<QString, QQueue<QPair<qint64, QString>>> recentByUser_;
    QMutex mutex_;
    // Watches blocked_words.txt so edits made in the external editor opened by
    // "Edit blocked words" take effect immediately, instead of only on the
    // next app restart — previously reload() only ran once, right when the
    // file was opened and before the user had changed anything in it.
    QFileSystemWatcher watcher_;
};

class AuditStore final : public QObject {
    Q_OBJECT
public:
    explicit AuditStore(SettingsStore* settings, QObject* parent = nullptr);
    void appendMessage(const ChatMessage& message);
    QJsonArray messagesForUser(const QString& platform, const QString& userId,
                               const QString& userName, int limit = 2000) const;
    void appendEvent(const StreamEvent& event);
    QList<StreamEvent> loadEvents(int limit = 500) const;
private:
    QString messagesPath_;
    QString eventsPath_;
    mutable QMutex mutex_;
};
