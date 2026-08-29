#include "Core.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

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

QString chatBadgeHtml(const ChatMessage& message) {
    const QJsonArray custom=message.metadata.value(QStringLiteral("custom_chatter_icons")).toArray();
    if(custom.isEmpty())return badgeGlyphs(message.badges);
    QStringList retained;
    for(const auto&badge:message.badges)if(badge==QStringLiteral("MOD")||(message.platform==QStringLiteral("twitch")&&badge==QStringLiteral("SUB")))retained<<badge;
    QString html=badgeGlyphs(retained);
    int count=0;
    for(const auto&value:custom){
        const QString data=value.toString();
        if(++count>3||!data.startsWith(QStringLiteral("data:image/png;base64,")))continue;
        html+=QStringLiteral("<img src='%1' width='20' height='20' style='vertical-align:-5px;margin:0 2px'>").arg(data.toHtmlEscaped());
    }
    return html;
}

QString chatNameHtml(const ChatMessage& message) {
    const QString mode=message.metadata.value(QStringLiteral("name_color_mode")).toString();
    QStringList palette;
    for(const auto&value:message.metadata.value(QStringLiteral("name_color_palette")).toArray()){
        const QColor color(value.toString());if(color.isValid())palette<<color.name();
    }
    if((mode!=QStringLiteral("gradient")&&mode!=QStringLiteral("pattern"))||palette.size()<2)
        return QStringLiteral("<b style='color:%1'>%2</b>").arg(message.color.name(),message.user.toHtmlEscaped());
    const int count=message.user.size();QString html=QStringLiteral("<b>");
    for(int index=0;index<count;++index){
        QColor color;
        if(mode==QStringLiteral("pattern")){
            const QString pattern=message.metadata.value(QStringLiteral("name_color_pattern")).toString(QStringLiteral("repeat"));int paletteIndex=index%palette.size();
            if(pattern==QStringLiteral("blocks"))paletteIndex=(index/2)%palette.size();
            else if(pattern==QStringLiteral("mirror")&&palette.size()>1){const int cycle=palette.size()*2-2;const int step=index%cycle;paletteIndex=step<palette.size()?step:cycle-step;}
            color=QColor(palette.at(paletteIndex));
        }
        else{
            const double position=count<=1?0.0:double(index)/double(count-1);
            const double scaled=position*(palette.size()-1);const int left=qMin(int(scaled),palette.size()-2);const double mix=scaled-left;
            const QColor a(palette.at(left)),b(palette.at(left+1));
            color=QColor(qRound(a.red()+(b.red()-a.red())*mix),qRound(a.green()+(b.green()-a.green())*mix),qRound(a.blue()+(b.blue()-a.blue())*mix));
        }
        html+=QStringLiteral("<span style='color:%1'>%2</span>").arg(color.name(),QString(message.user.at(index)).toHtmlEscaped());
    }
    return html+QStringLiteral("</b>");
}

QString chatMessageBodyHtml(const ChatMessage& message) {
    const QJsonArray runs=message.metadata.value(QStringLiteral("youtube_runs")).toArray();
    if((message.platform==QStringLiteral("youtube")||message.platform==QStringLiteral("yt_shorts"))&&!runs.isEmpty()) {
        QString html;
        for(const auto& value:runs) {
            const QJsonObject run=value.toObject();
            if(run.contains(QStringLiteral("text"))) {
                html+=run.value(QStringLiteral("text")).toString().toHtmlEscaped();
                continue;
            }
            const QString fallback=run.value(QStringLiteral("alt")).toString();
            const QUrl url(run.value(QStringLiteral("url")).toString());
            if(url.isValid()&&url.scheme()==QStringLiteral("https")) {
                html+=QStringLiteral("<img src='%1' alt='%2' title='%2' width='24' height='24' style='vertical-align:-6px;margin:0 1px'>")
                    .arg(url.toString(QUrl::FullyEncoded).toHtmlEscaped(),fallback.toHtmlEscaped());
            } else html+=fallback.toHtmlEscaped();
        }
        if(!html.isEmpty()) return html;
    }
    return message.text.toHtmlEscaped();
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
                                 QStringLiteral("word_whitelist.txt"),
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
    // Updates preserve AppData. If a previous installer nevertheless damaged
    // settings.json, recover the pre-update copy before deciding the user is
    // disconnected from Twitch, YouTube, or Streamlabs.
    if(root_.isEmpty()){
        QFile backup(path_+QStringLiteral(".update-backup"));
        if(backup.open(QIODevice::ReadOnly))root_=QJsonDocument::fromJson(backup.readAll()).object();
        if(!root_.isEmpty())save();
    }
    if (!root_.contains("links")) root_["links"]=QJsonObject{{"twitch",""},{"youtube",""},{"yt_shorts",""},{"tiktok",""},{"kick",""},{"rumble",""}};
    else {auto links=root_["links"].toObject();if(!links.contains("kick"))links["kick"]="";if(!links.contains("rumble"))links["rumble"]="";root_["links"]=links;}
    if (!root_.contains("enabled")) root_["enabled"]=QJsonObject{{"twitch",true},{"youtube",true},{"yt_shorts",true},{"tiktok",true},{"kick",true},{"rumble",true}};
    else {auto enabled=root_["enabled"].toObject();if(!enabled.contains("kick"))enabled["kick"]=true;if(!enabled.contains("rumble"))enabled["rumble"]=true;root_["enabled"]=enabled;}
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

AutoMod::AutoMod(SettingsStore*s,QObject*p):QObject(p),settings_(s){
    path_=s->dataDirectory()+QStringLiteral("/blocked_words.txt");
    whitelistPath_=s->dataDirectory()+QStringLiteral("/word_whitelist.txt");
    if(!QFile::exists(path_)){
        QFile f(path_);
        if(f.open(QIODevice::WriteOnly|QIODevice::Text))
            f.write("# Add one word or phrase per line.\n# Leetspeak, separators, repeated letters and Unicode bypasses are checked.\n");
    }
    if(!QFile::exists(whitelistPath_)){
        QFile f(whitelistPath_);
        if(f.open(QIODevice::WriteOnly|QIODevice::Text))
            f.write("# Words or phrases here are allowed through blocked-word matching.\n# This only exempts the whitelisted text; other blocked words in the same message are still moderated.\nsmash\npass\nas\n");
    }
    // v3.0.6 cumulative migration: existing installs keep their edited whitelist file,
    // but gain the explicitly requested innocent word "as" once. The matcher
    // fix below is the real protection for ordinary English vocabulary.
    if(!s->preference(QStringLiteral("automod_vocab_306_vocab_seeded"),false).toBool()){
        QFile f(whitelistPath_);QString contents;
        if(f.open(QIODevice::ReadOnly|QIODevice::Text)){contents=QString::fromUtf8(f.readAll());f.close();}
        bool hasAs=false;
        for(const auto&line:contents.split(QLatin1Char('\n')))if(line.trimmed().compare(QStringLiteral("as"),Qt::CaseInsensitive)==0){hasAs=true;break;}
        if(!hasAs&&f.open(QIODevice::Append|QIODevice::Text)){if(!contents.isEmpty()&&!contents.endsWith(QLatin1Char('\n')))f.write("\n");f.write("as\n");f.close();}
        s->setPreference(QStringLiteral("automod_vocab_306_vocab_seeded"),true);
    }
    reload();
    for(const auto& watchedPath:{path_,whitelistPath_})
        if(QFile::exists(watchedPath)&&!watcher_.files().contains(watchedPath))watcher_.addPath(watchedPath);
    connect(&watcher_,&QFileSystemWatcher::fileChanged,this,[this](const QString&changedPath){
        reload();
        // Most editors save by replacing the file, which drops it from the
        // watch list. Re-add it so every later save is picked up too.
        if(!watcher_.files().contains(changedPath)&&QFile::exists(changedPath))watcher_.addPath(changedPath);
    });
}

QString AutoMod::latinize(const QString& input){
    static const QHash<QChar,QChar> map{{'@','a'},{'4','a'},{'3','e'},{'1','i'},{'!','i'},{'0','o'},{'$','s'},{'5','s'},{'7','t'},{'+','t'},{'8','b'},{'9','g'},
        {u'а','a'},{u'α','a'},{u'с','c'},{u'е','e'},{u'ε','e'},{u'і','i'},{u'ι','i'},{u'о','o'},{u'ο','o'},{u'р','p'},{u'ρ','p'},{u'х','x'},{u'χ','x'},{u'у','y'}};
    QString out=input.normalized(QString::NormalizationForm_D).toLower();
    out.remove(QRegularExpression(QStringLiteral("[\\x{0300}-\\x{036f}\\x{200b}-\\x{200f}\\x{202a}-\\x{202e}\\x{2060}\\x{feff}]")));
    for(qsizetype i=0;i<out.size();++i){
        const QChar ch=out.at(i);if(!map.contains(ch))continue;
        // A trailing exclamation mark is punctuation, not an "i". Keep the
        // leetspeak interpretation only when ! appears inside a token.
        if(ch==QLatin1Char('!')&&(i==0||i+1>=out.size()||!out.at(i-1).isLetterOrNumber()||!out.at(i+1).isLetterOrNumber()))continue;
        out[i]=map.value(ch);
    }
    return out;
}
QString AutoMod::normalize(const QString&t){QString s=latinize(t);s.replace(QRegularExpression("[^a-z0-9\\s]")," ");return s.simplified();}
QString AutoMod::compact(const QString&t){QString s=latinize(t);s.remove(QRegularExpression("[^a-z0-9]"));return s;}
bool AutoMod::reload(){
    const auto loadTerms=[](const QString&filePath,QList<Term>&target){
        target.clear();
        QFile f(filePath);
        if(!f.open(QIODevice::ReadOnly|QIODevice::Text))return false;
        while(!f.atEnd()){
            QString line=QString::fromUtf8(f.readLine()).trimmed();
            if(line.isEmpty()||line.startsWith('#'))continue;
            QString c=compact(line);
            if(c.isEmpty())continue;
            const QString normalized=normalize(line);
            QStringList words;for(const auto&word:normalized.split(QLatin1Char(' '),Qt::SkipEmptyParts))words<<QRegularExpression::escape(word);
            QStringList chars;for(const auto ch:c)chars<<QRegularExpression::escape(QString(ch))+"+";
            // Exact terms and separator/repetition bypasses are bounded as
            // complete words/phrases. This prevents a blocked term such as
            // "ass" from matching innocent words like class, glass, pass,
            // assistant, or assignment merely because it appears inside them.
            const QString left=QStringLiteral("(?<![a-z0-9])"),right=QStringLiteral("(?![a-z0-9])");
            target.append({normalized,c,
                QRegularExpression(left+words.join(QStringLiteral("\\s+"))+right,QRegularExpression::CaseInsensitiveOption),
                QRegularExpression(left+chars.join(QStringLiteral("[^a-z0-9]*"))+right,QRegularExpression::CaseInsensitiveOption)});
        }
        return true;
    };
    const bool blockedOk=loadTerms(path_,terms_);
    const bool whitelistOk=loadTerms(whitelistPath_,whitelistTerms_);
    return blockedOk&&whitelistOk;
}

QStringList AutoMod::words(bool whitelist)const{
    QFile f(whitelist?whitelistPath_:path_);QStringList result;
    if(!f.open(QIODevice::ReadOnly|QIODevice::Text))return result;
    while(!f.atEnd()){const QString line=QString::fromUtf8(f.readLine()).trimmed();if(!line.isEmpty()&&!line.startsWith(QLatin1Char('#')))result<<line;}
    result.removeDuplicates();result.sort(Qt::CaseInsensitive);return result;
}
bool AutoMod::addWord(const QString&word,bool whitelist){
    const QString clean=word.trimmed();if(clean.isEmpty()||clean.contains(QLatin1Char('\n'))||clean.contains(QLatin1Char('\r')))return false;
    const QStringList existing=words(whitelist);for(const auto&item:existing)if(item.compare(clean,Qt::CaseInsensitive)==0)return true;
    QFile f(whitelist?whitelistPath_:path_);if(!f.open(QIODevice::Append|QIODevice::Text))return false;f.write(clean.toUtf8());f.write("\n");f.close();return reload();
}
bool AutoMod::removeWords(const QStringList&remove,bool whitelist){
    const QString filePath=whitelist?whitelistPath_:path_;QFile input(filePath);if(!input.open(QIODevice::ReadOnly|QIODevice::Text))return false;
    const QStringList lines=QString::fromUtf8(input.readAll()).split(QLatin1Char('\n'));input.close();QSaveFile output(filePath);if(!output.open(QIODevice::WriteOnly|QIODevice::Text))return false;
    for(const auto&line:lines){const QString clean=line.trimmed();bool drop=false;if(!clean.startsWith(QLatin1Char('#')))for(const auto&word:remove)if(clean.compare(word.trimmed(),Qt::CaseInsensitive)==0){drop=true;break;}if(!drop&&!line.isNull()){output.write(line.toUtf8());output.write("\n");}}
    return output.commit()&&reload();
}

QString AutoMod::maskWhitelisted(const QString&t)const{
    // Mask allowed words before leetspeak translation so punctuation and
    // separator variants of an explicitly allowed term remain exempt.
    QString masked=t.normalized(QString::NormalizationForm_D).toLower();
    masked.remove(QRegularExpression(QStringLiteral("[\\x{0300}-\\x{036f}\\x{200b}-\\x{200f}\\x{202a}-\\x{202e}\\x{2060}\\x{feff}]")));
    for(const auto&term:whitelistTerms_){
        QStringList words;for(const auto&word:term.normalized.split(QLatin1Char(' '),Qt::SkipEmptyParts))words<<QRegularExpression::escape(word);
        const QRegularExpression allowed(
            QStringLiteral("(?<![a-z0-9])")+words.join(QStringLiteral("\\s+"))+QStringLiteral("(?![a-z0-9])"),
            QRegularExpression::CaseInsensitiveOption);
        masked.replace(allowed,QStringLiteral(" "));
    }
    return latinize(masked);
}

bool AutoMod::blockedMatch(const QString&t)const{
    // Remove only explicitly whitelisted words/phrases before scanning. The
    // matcher itself now uses complete-word boundaries, so ordinary English
    // vocabulary is safe without maintaining a huge dictionary whitelist.
    const QString canonical=maskWhitelisted(t),norm=normalize(canonical);
    for(const auto&term:terms_){
        if(term.exact.match(norm).hasMatch()||term.bypass.match(canonical).hasMatch())return true;
    }
    return false;
}
bool AutoMod::promotionSpam(const QString&t){const QString n=normalize(t);const bool target=QRegularExpression("\\b(followers?|views?|viewers?|subs?|subscribers?|likes?|viewbot|followbot)\\b").match(n).hasMatch();const bool pitch=QRegularExpression("\\b(free|cheap|instant|buy|boost|grow|promote|service|provider|selling|offer|dm|contact|click|discord|telegram)\\b").match(n).hasMatch();return target&&pitch;}
bool AutoMod::check(const ChatMessage&m,QString*reason){auto cfg=settings_->moderation();if(!cfg.value("enabled").toBool(true))return false;if(cfg.value("ignore_mods").toBool(true)&&(m.badges.contains("MOD")||m.badges.contains("HOST")))return false;auto fail=[&](const QString&r){if(reason)*reason=r;return true;};if(cfg.value("blocked_words_enabled").toBool(true)&&blockedMatch(m.text))return fail("blocked word / bypass");if(cfg.value("spam_enabled").toBool(true)&&promotionSpam(m.text))return fail("follower/viewer promotion spam");const qint64 now=QDateTime::currentMSecsSinceEpoch();QMutexLocker lock(&mutex_);auto&bucket=recentByUser_[m.user.toLower()];const QString n=normalize(m.text);bucket.enqueue({now,n});while(!bucket.isEmpty()&&now-bucket.head().first>10000)bucket.dequeue();if(cfg.value("spam_enabled").toBool(true)){if(bucket.size()>cfg.value("max_messages_10s").toInt(6))return fail("message flood");int same=0;for(const auto&item:bucket)if(item.second==n&&now-item.first<=cfg.value("duplicate_window_s").toDouble(12)*1000)++same;if(!n.isEmpty()&&same>=cfg.value("duplicate_count").toInt(3))return fail("repeated message");}int letters=0,caps=0;for(const auto c:m.text)if(c.isLetter()){++letters;if(c.isUpper())++caps;}if(cfg.value("spam_enabled").toBool(true)&&letters>=cfg.value("min_caps_chars").toInt(8)&&double(caps)/qMax(letters,1)>=cfg.value("max_caps_ratio").toDouble(.82))return fail("excessive caps");if(cfg.value("block_links").toBool(false)&&QRegularExpression("https?://|www\\.|discord\\.gg/|t\\.me/",QRegularExpression::CaseInsensitiveOption).match(m.text).hasMatch())return fail("link");return false;}

AuditStore::AuditStore(SettingsStore*s,QObject*p):QObject(p){messagesPath_=s->dataDirectory()+"/chat_history.jsonl";eventsPath_=s->dataDirectory()+"/stream_events.json";}
void AuditStore::appendMessage(const ChatMessage&m){QMutexLocker l(&mutex_);QFile f(messagesPath_);if(f.open(QIODevice::Append|QIODevice::Text)){f.write(QJsonDocument(m.toJson()).toJson(QJsonDocument::Compact));f.write("\n");}}
QJsonArray AuditStore::messagesForUser(const QString&platform,const QString&userId,const QString&userName,int limit)const{
    QMutexLocker l(&mutex_); QFile f(messagesPath_); QJsonArray out;
    if(!f.open(QIODevice::ReadOnly|QIODevice::Text))return out;
    while(!f.atEnd()){
        const auto o=QJsonDocument::fromJson(f.readLine()).object();
        if(o.value("platform").toString()!=platform)continue;
        const bool idMatch=!userId.isEmpty()&&o.value("user_id").toString()==userId;
        const bool nameMatch=userId.isEmpty()&&!userName.isEmpty()&&o.value("user").toString().compare(userName,Qt::CaseInsensitive)==0;
        if(idMatch||nameMatch){out.append(o);while(out.size()>limit)out.removeAt(0);}
    }
    return out;
}
void AuditStore::appendEvent(const StreamEvent&e){auto all=loadEvents();all.prepend(e);while(all.size()>500)all.removeLast();QJsonArray a;for(const auto&x:all)a.append(x.toJson());QMutexLocker l(&mutex_);QSaveFile f(eventsPath_);if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(a).toJson(QJsonDocument::Indented));f.commit();}}
QList<StreamEvent> AuditStore::loadEvents(int limit)const{QMutexLocker l(&mutex_);QFile f(eventsPath_);QList<StreamEvent>out;if(!f.open(QIODevice::ReadOnly))return out;for(const auto&v:QJsonDocument::fromJson(f.readAll()).array()){out<<StreamEvent::fromJson(v.toObject());if(out.size()>=limit)break;}return out;}
