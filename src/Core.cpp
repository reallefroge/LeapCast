#include "Core.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

QString badgeGlyphs(const QStringList& badges) {
    static const QHash<QString, QString> glyphs{
        {QStringLiteral("HOST"), QStringLiteral("\U0001F3A5")},   // broadcaster
        {QStringLiteral("MOD"), QStringLiteral("⚔️")},  // moderator
        {QStringLiteral("VIP"), QStringLiteral("\U0001F48E")},    // VIP
        {QStringLiteral("PRIME"), QStringLiteral("\U0001F451")},  // Prime/Turbo
        {QStringLiteral("SUB"), QStringLiteral("⭐")},        // subscriber/founder
        {QStringLiteral("CHECK"), QStringLiteral("✅")},      // verified/partner
        {QStringLiteral("MONEY"), QStringLiteral("\U0001F4B0")},  // paid/superchat
    };
    static const QStringList order{QStringLiteral("HOST"), QStringLiteral("MOD"), QStringLiteral("VIP"),
        QStringLiteral("PRIME"), QStringLiteral("SUB"), QStringLiteral("CHECK"), QStringLiteral("MONEY")};
    QString out;
    for (const auto& key : order) if (badges.contains(key)) out += glyphs.value(key);
    return out;
}

QJsonObject ChatMessage::toJson() const {
    return {{QStringLiteral("user"), user}, {QStringLiteral("text"), text},
            {QStringLiteral("platform"), platform},
            {QStringLiteral("badges"), QJsonArray::fromStringList(badges)},
            {QStringLiteral("badge_ids"), QJsonArray::fromStringList(badgeIds)},
            {QStringLiteral("color"), color.name()},
            {QStringLiteral("user_id"), userId}, {QStringLiteral("message_id"), messageId},
            {QStringLiteral("time"), timestamp.toString(Qt::ISODateWithMs)},
            {QStringLiteral("meta"), metadata}};
}

QJsonObject StreamEvent::toJson() const {
    return {{QStringLiteral("event_id"), eventId}, {QStringLiteral("kind"), kind},
            {QStringLiteral("user"), user}, {QStringLiteral("amount"), amount},
            {QStringLiteral("message"), message}, {QStringLiteral("platform"), platform},
            {QStringLiteral("time"), timestamp.toString(Qt::ISODateWithMs)},
            {QStringLiteral("raw"), raw}};
}

StreamEvent StreamEvent::fromJson(const QJsonObject& o) {
    StreamEvent e; e.eventId=o.value("event_id").toString(); e.kind=o.value("kind").toString();
    e.user=o.value("user").toString("Someone"); e.amount=o.value("amount").toString();
    e.message=o.value("message").toString(); e.platform=o.value("platform").toString("streamlabs");
    e.timestamp=QDateTime::fromString(o.value("time").toString(), Qt::ISODateWithMs);
    if (!e.timestamp.isValid()) e.timestamp=QDateTime::currentDateTime();
    e.raw=o.value("raw").toObject(); return e;
}

SettingsStore::SettingsStore(QObject* parent) : QObject(parent) {
    directory_=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(directory_);
    const QString legacyDirectory =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Lefroge/Multi-Chat Studio");
    for (const auto& fileName : {QStringLiteral("settings.json"),
                                 QStringLiteral("blocked_words.txt"),
                                 QStringLiteral("chat_history.jsonl"),
                                 QStringLiteral("stream_events.json")}) {
        const QString destination = directory_ + QLatin1Char('/') + fileName;
        const QString legacy = legacyDirectory + QLatin1Char('/') + fileName;
        if (!QFile::exists(destination) && QFile::exists(legacy)) QFile::copy(legacy, destination);
    }
    path_=directory_+QStringLiteral("/settings.json"); load();
}

void SettingsStore::load() {
    QFile f(path_);
    if (f.open(QIODevice::ReadOnly)) root_=QJsonDocument::fromJson(f.readAll()).object();
    if (!root_.contains("links")) root_["links"]=QJsonObject{{"twitch",""},{"youtube",""},{"yt_shorts",""},{"tiktok",""}};
    if (!root_.contains("enabled")) root_["enabled"]=QJsonObject{{"twitch",true},{"youtube",true},{"yt_shorts",true},{"tiktok",true}};
    if (!root_.contains("prefs")) root_["prefs"]=QJsonObject{};
    if (!root_.contains("port")) root_["port"]=8080;
}
QString SettingsStore::link(const QString& p) const { return root_["links"].toObject()[p].toString(); }
void SettingsStore::setLink(const QString& p,const QString& v){auto o=root_["links"].toObject();o[p]=v;root_["links"]=o;save();}
bool SettingsStore::enabled(const QString& p) const{return root_["enabled"].toObject().value(p).toBool(true);}
void SettingsStore::setEnabled(const QString&p,bool v){auto o=root_["enabled"].toObject();o[p]=v;root_["enabled"]=o;save();}
QVariant SettingsStore::preference(const QString& n,const QVariant& f)const{return root_["prefs"].toObject().value(n).toVariant().isValid()?root_["prefs"].toObject().value(n).toVariant():f;}
void SettingsStore::setPreference(const QString&n,const QVariant&v){auto o=root_["prefs"].toObject();o[n]=QJsonValue::fromVariant(v);root_["prefs"]=o;save();}
QString SettingsStore::secret(const QString&n)const{return root_.value(n).toString();}
void SettingsStore::setSecret(const QString&n,const QString&v){root_[n]=v.trimmed();save();}
QJsonObject SettingsStore::moderation()const{return root_.value("moderation").toObject();}
void SettingsStore::setModeration(const QJsonObject&v){root_["moderation"]=v;save();}
QString SettingsStore::dataDirectory()const{return directory_;}
bool SettingsStore::save(){QSaveFile f(path_);if(!f.open(QIODevice::WriteOnly))return false;f.write(QJsonDocument(root_).toJson(QJsonDocument::Indented));if(!f.commit())return false;emit changed();return true;}

AutoMod::AutoMod(SettingsStore*s,QObject*p):QObject(p),settings_(s){path_=s->dataDirectory()+"/blocked_words.txt";if(!QFile::exists(path_)){QFile f(path_);if(f.open(QIODevice::WriteOnly))f.write("# Add one word or phrase per line.\n# Leetspeak, separators, repeated letters and Unicode bypasses are checked.\n");}reload();}

QString AutoMod::latinize(const QString& input){
    static const QHash<QChar,QChar> map{{'@','a'},{'4','a'},{'3','e'},{'1','i'},{'!','i'},{'0','o'},{'$','s'},{'5','s'},{'7','t'},{'+','t'},{'8','b'},{'9','g'},
        {u'а','a'},{u'α','a'},{u'с','c'},{u'е','e'},{u'ε','e'},{u'і','i'},{u'ι','i'},{u'о','o'},{u'ο','o'},{u'р','p'},{u'ρ','p'},{u'х','x'},{u'χ','x'},{u'у','y'}};
    QString out=input.normalized(QString::NormalizationForm_D).toLower();
    out.remove(QRegularExpression(QStringLiteral("[\\x{0300}-\\x{036f}\\x{200b}-\\x{200f}\\x{202a}-\\x{202e}\\x{2060}\\x{feff}]")));
    for(auto& ch:out)if(map.contains(ch))ch=map.value(ch);return out;
}
QString AutoMod::normalize(const QString&t){QString s=latinize(t);s.replace(QRegularExpression("[^a-z0-9\\s]")," ");s.replace(QRegularExpression("(.)\\1+"),"\\1");return s.simplified();}
QString AutoMod::compact(const QString&t){QString s=latinize(t);s.remove(QRegularExpression("[^a-z0-9]"));return s;}
bool AutoMod::distanceAtMostOne(const QString&a0,const QString&b0){if(a0==b0)return true;if(qAbs(a0.size()-b0.size())>1)return false;QString a=a0,b=b0;if(a.size()>b.size())qSwap(a,b);int i=0,j=0,e=0;while(i<a.size()&&j<b.size()){if(a[i]==b[j]){++i;++j;}else{if(++e>1)return false;if(a.size()==b.size())++i;++j;}}return true;}
bool AutoMod::reload(){terms_.clear();QFile f(path_);if(!f.open(QIODevice::ReadOnly|QIODevice::Text))return false;while(!f.atEnd()){QString line=QString::fromUtf8(f.readLine()).trimmed();if(line.isEmpty()||line.startsWith('#'))continue;QString c=compact(line);if(c.isEmpty())continue;QStringList chars;for(const auto ch:c)chars<<QRegularExpression::escape(QString(ch))+"+";terms_.append({normalize(line),c,QRegularExpression("(?<![a-z0-9])"+chars.join("[^a-z0-9]*")+"(?![a-z0-9])",QRegularExpression::CaseInsensitiveOption)});}return true;}
bool AutoMod::blockedMatch(const QString&t)const{const QString canonical=latinize(t),norm=normalize(t);const auto tokens=norm.split(' ',Qt::SkipEmptyParts);for(const auto&term:terms_){if(QRegularExpression("(?<![a-z0-9])"+QRegularExpression::escape(term.normalized)+"(?![a-z0-9])").match(norm).hasMatch()||term.bypass.match(canonical).hasMatch())return true;if(term.compact.size()>=5)for(const auto&tok:tokens)if(qAbs(tok.size()-term.compact.size())<=1&&distanceAtMostOne(tok,term.compact))return true;}return false;}
bool AutoMod::promotionSpam(const QString&t){const QString n=normalize(t);const bool target=QRegularExpression("\\b(followers?|views?|viewers?|subs?|subscribers?|likes?|viewbot|followbot)\\b").match(n).hasMatch();const bool pitch=QRegularExpression("\\b(free|cheap|instant|buy|boost|grow|promote|service|provider|selling|offer|dm|contact|click|discord|telegram)\\b").match(n).hasMatch();return target&&pitch;}
bool AutoMod::check(const ChatMessage&m,QString*reason){auto cfg=settings_->moderation();if(!cfg.value("enabled").toBool(true))return false;if(cfg.value("ignore_mods").toBool(true)&&(m.badges.contains("MOD")||m.badges.contains("HOST")))return false;auto fail=[&](const QString&r){if(reason)*reason=r;return true;};if(cfg.value("blocked_words_enabled").toBool(true)&&blockedMatch(m.text))return fail("blocked word / bypass");if(cfg.value("spam_enabled").toBool(true)&&promotionSpam(m.text))return fail("follower/viewer promotion spam");const qint64 now=QDateTime::currentMSecsSinceEpoch();QMutexLocker lock(&mutex_);auto&bucket=recentByUser_[m.user.toLower()];const QString n=normalize(m.text);bucket.enqueue({now,n});while(!bucket.isEmpty()&&now-bucket.head().first>10000)bucket.dequeue();if(cfg.value("spam_enabled").toBool(true)){if(bucket.size()>cfg.value("max_messages_10s").toInt(6))return fail("message flood");int same=0;for(const auto&item:bucket)if(item.second==n&&now-item.first<=cfg.value("duplicate_window_s").toDouble(12)*1000)++same;if(!n.isEmpty()&&same>=cfg.value("duplicate_count").toInt(3))return fail("repeated message");}int letters=0,caps=0;for(const auto c:m.text)if(c.isLetter()){++letters;if(c.isUpper())++caps;}if(cfg.value("spam_enabled").toBool(true)&&letters>=cfg.value("min_caps_chars").toInt(8)&&double(caps)/qMax(letters,1)>=cfg.value("max_caps_ratio").toDouble(.82))return fail("excessive caps");if(cfg.value("block_links").toBool(false)&&QRegularExpression("https?://|www\\.|discord\\.gg/|t\\.me/",QRegularExpression::CaseInsensitiveOption).match(m.text).hasMatch())return fail("link");return false;}

AuditStore::AuditStore(SettingsStore*s,QObject*p):QObject(p){messagesPath_=s->dataDirectory()+"/chat_history.jsonl";eventsPath_=s->dataDirectory()+"/stream_events.json";}
void AuditStore::appendMessage(const ChatMessage&m){QMutexLocker l(&mutex_);QFile f(messagesPath_);if(f.open(QIODevice::Append|QIODevice::Text)){f.write(QJsonDocument(m.toJson()).toJson(QJsonDocument::Compact));f.write("\n");}}
void AuditStore::appendEvent(const StreamEvent&e){auto all=loadEvents();all.prepend(e);while(all.size()>500)all.removeLast();QJsonArray a;for(const auto&x:all)a.append(x.toJson());QMutexLocker l(&mutex_);QSaveFile f(eventsPath_);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(a).toJson(QJsonDocument::Indented));f.commit();}}
QList<StreamEvent> AuditStore::loadEvents(int limit)const{QMutexLocker l(&mutex_);QFile f(eventsPath_);QList<StreamEvent>out;if(!f.open(QIODevice::ReadOnly))return out;for(const auto&v:QJsonDocument::fromJson(f.readAll()).array()){out<<StreamEvent::fromJson(v.toObject());if(out.size()>=limit)break;}return out;}
