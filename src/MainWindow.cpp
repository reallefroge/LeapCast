#include "MainWindow.hpp"
#include "AppController.hpp"
#include "BuildInfo.hpp"
#include "Overlay.hpp"
#include "UpdateService.hpp"
#include "qrcodegen.hpp"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFrame>
#include <QFontComboBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScreen>
#include <QShowEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

namespace {
QPixmap phoneQrCode(const QUrl& url,int pixels=236){
    if(!url.isValid())return {};
    const auto qr=qrcodegen::QrCode::encodeText(url.toString(QUrl::FullyEncoded).toUtf8().constData(),qrcodegen::QrCode::Ecc::MEDIUM);
    constexpr int quiet=4;const int modules=qr.getSize()+quiet*2;const int scale=qMax(1,pixels/modules);const int side=modules*scale;
    QImage image(side,side,QImage::Format_RGB32);image.fill(Qt::white);QPainter painter(&image);painter.setPen(Qt::NoPen);painter.setBrush(Qt::black);
    for(int y=0;y<qr.getSize();++y)for(int x=0;x<qr.getSize();++x)if(qr.getModule(x,y))painter.drawRect((x+quiet)*scale,(y+quiet)*scale,scale,scale);
    return QPixmap::fromImage(image);
}
QLabel* label(const QString& text, const char* role = nullptr) {
    auto* value = new QLabel(text);
    if (role) {
        const QString roleName=QString::fromLatin1(role);value->setProperty("role",roleName);
        if(roleName==QStringLiteral("muted")){value->setWordWrap(true);value->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);value->setMinimumWidth(0);}
    }
    return value;
}

bool showUpdateNotesDialog(QWidget* parent,const QString&version,const QString&notes){
    QDialog dialog(parent);dialog.setWindowTitle(QStringLiteral("Leapcast Studio update"));dialog.setModal(true);dialog.resize(480,360);dialog.setMinimumSize(420,300);
    auto* layout=new QVBoxLayout(&dialog);layout->setContentsMargins(18,16,18,16);layout->setSpacing(10);
    auto* title=label(QStringLiteral("✨ WHAT'S NEW IN %1").arg(version),"heroTitle");layout->addWidget(title);
    QString summary;for(const auto&line:notes.split(QLatin1Char('\n'))){QString clean=line.trimmed();clean.remove(QRegularExpression(QStringLiteral("^[#*\\-\\s]+")));if(!clean.isEmpty()){summary=clean;break;}}
    auto* summaryLabel=label(QStringLiteral("🚀 %1").arg(summary.isEmpty()?QStringLiteral("A new update is ready."):summary),"muted");summaryLabel->setWordWrap(true);layout->addWidget(summaryLabel);
    auto* patchNotes=new QTextBrowser;patchNotes->setOpenExternalLinks(true);patchNotes->document()->setDefaultStyleSheet(QStringLiteral("body{line-height:1.45} h1,h2,h3{margin:8px 0 5px} p{margin:5px 0} li{margin:0 0 7px 0}"));patchNotes->setMarkdown(notes.trimmed().isEmpty()?QStringLiteral("- ✅ Performance improvements\n- 🛠️ Bug fixes"):notes.left(12000));patchNotes->setMinimumHeight(150);layout->addWidget(patchNotes,1);
    auto* hint=label(QStringLiteral("🔄 Installs the patch, then reopens Leapcast automatically."),"muted");hint->setWordWrap(true);layout->addWidget(hint);
    auto* buttons=new QDialogButtonBox;auto* later=buttons->addButton(QStringLiteral("Later"),QDialogButtonBox::RejectRole);auto* install=buttons->addButton(QStringLiteral("⬇ Update now"),QDialogButtonBox::AcceptRole);install->setProperty("primary",true);QObject::connect(later,&QPushButton::clicked,&dialog,&QDialog::reject);QObject::connect(install,&QPushButton::clicked,&dialog,&QDialog::accept);layout->addWidget(buttons);
    return dialog.exec()==QDialog::Accepted;
}

// Caps how many lines each dashboard chat view keeps, so a multi-hour stream
// doesn't grow these documents (and every future append/reflow cost) without
// bound. Chosen independently per view, mirroring PopoutChat's own cap.
constexpr int kMaxChatBlocks = 300;
void trimBlocks(QTextDocument* doc) {
    while (doc->blockCount() > kMaxChatBlocks) {
        QTextCursor cursor(doc->firstBlock());
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
}
void appendTrimmed(QTextBrowser* view, const QString& html) {
    if (!view) return;
    view->append(html);
    trimBlocks(view->document());
}

// A right-click on a chat line needs to find its way back to the ChatMessage
// it came from (see showDashboardChatMenu), so — like PopoutChat's own
// chat_ — each message is stamped with an id as a block property instead of
// just appended as plain HTML.
constexpr int kMessageIdProperty = QTextFormat::UserProperty + 1;
void appendChatMessage(QTextBrowser* view, const QString& html, qint64 id) {
    if (!view) return;
    auto* doc = view->document();
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat format;
    format.setProperty(kMessageIdProperty, id);
    if (!doc->isEmpty()) cursor.insertBlock(format);
    else cursor.setBlockFormat(format);
    QTextCharFormat messageFormat=cursor.charFormat();
    messageFormat.setProperty(kMessageIdProperty,id);
    cursor.setCharFormat(messageFormat);
    cursor.insertHtml(html);
    view->moveCursor(QTextCursor::End);
    trimBlocks(doc);
}

QString friendlyTimestamp(const QString& value) {
    const QDateTime timestamp = QDateTime::fromString(value, Qt::ISODate);
    if (!timestamp.isValid()) return value;
    return timestamp.toLocalTime().toString(QStringLiteral("MMM d, yyyy  •  h:mm AP"));
}

QString eventAction(const StreamEvent& event) {
    if (event.kind==QStringLiteral("twitch_redemption"))
        return QStringLiteral("redeemed %1").arg(event.amount.isEmpty()?QStringLiteral("a Channel Point reward"):event.amount);
    if (event.kind.contains(QStringLiteral("gifted_sub")))
        return QStringLiteral("gifted %1 subscription%2").arg(event.amount.isEmpty()?QStringLiteral("1"):event.amount,event.amount==QStringLiteral("1")?QString():QStringLiteral("s"));
    if (event.kind.contains(QStringLiteral("donation")))
        return event.amount.isEmpty() ? QStringLiteral("sent a donation")
                                      : QStringLiteral("donated %1").arg(event.amount);
    if (event.kind.contains(QStringLiteral("follow"))) return QStringLiteral("followed the channel");
    if (event.kind.contains(QStringLiteral("raid"))) return QStringLiteral("raided the channel");
    if (event.kind.contains(QStringLiteral("host"))) return QStringLiteral("hosted the channel");
    if (event.kind.contains(QStringLiteral("bits")))
        return event.amount.isEmpty() ? QStringLiteral("sent Bits")
                                      : QStringLiteral("sent %1 Bits").arg(event.amount);
    if (event.kind.contains(QStringLiteral("member"))) return QStringLiteral("became a member");
    return QStringLiteral("subscribed");
}
QString platformIconHtml(const QString&platform){static const QHash<QString,QString> paths{{"twitch",":/brand/twitch.png"},{"youtube",":/brand/youtube.png"},{"yt_shorts",":/brand/youtube_shorts.png"},{"tiktok",":/brand/tiktok.png"},{"kick",":/brand/kick.svg"},{"rumble",":/brand/rumble.svg"}};return paths.contains(platform)?QStringLiteral("<img src='%1' width='18' height='18' style='vertical-align:middle;margin-right:5px'>").arg(paths.value(platform)):QString();}

QDate easterSunday(int year){
    const int a=year%19,b=year/100,c=year%100,d=b/4,e=b%4,f=(b+8)/25,g=(b-f+1)/3;
    const int h=(19*a+b-d-g+15)%30,i=c/4,k=c%4,l=(32+2*e+2*i-h-k)%7,m=(a+11*h+22*l)/451;
    const int month=(h+l-7*m+114)/31,day=((h+l-7*m+114)%31)+1;return QDate(year,month,day);
}

bool birthdayWindow(SettingsStore* settings,const QDate& date){
    if(!settings||!settings->preference(QStringLiteral("birthday_effects_enabled"),true).toBool())return false;
    if(date>=QDate(2026,8,23)&&date<=QDate(2026,8,24))return true;
    const int month=settings->preference(QStringLiteral("birthday_month"),0).toInt();
    const int day=settings->preference(QStringLiteral("birthday_day"),0).toInt();
    if(month<1||month>12||day<1)return false;
    // Check this year's birthday and the next year's birthday so a Jan 1
    // birthday also activates correctly on Dec 31 of the previous year.
    const QDate thisYearsBirthday(date.year(), month, day);
    if (thisYearsBirthday.isValid() &&
        (date == thisYearsBirthday || date == thisYearsBirthday.addDays(-1))) return true;
    const QDate nextYearsBirthday(date.year() + 1, month, day);
    if (nextYearsBirthday.isValid() && date == nextYearsBirthday.addDays(-1)) return true;
    return false;
}

QString seasonalThemeFor(SettingsStore* settings,const QDateTime& now){
    if(!settings||!settings->preference(QStringLiteral("seasonal_effects_enabled"),true).toBool())return {};
    const QDate date=now.date();
    if(birthdayWindow(settings,date))return QStringLiteral("birthday");
    if(date.month()==10&&date.day()>=15&&date.day()<=31)return QStringLiteral("halloween");
    if(date.month()==12&&date.day()>=10&&date.day()<=28)return QStringLiteral("christmas");
    const QDate easter=easterSunday(date.year());if(date>=easter.addDays(-2)&&date<=easter.addDays(1))return QStringLiteral("easter");
    if(date.month()==7&&date.day()==4)return QStringLiteral("july4");
    if(date.month()==11&&date.day()==11)return QStringLiteral("veterans");
    if((date.month()==12&&date.day()==31)||(date.month()==1&&date.day()==1))return QStringLiteral("newyear");
    return {};
}

QString seasonalThemeLabel(const QString& theme){
    if(theme==QStringLiteral("birthday"))return QStringLiteral("Birthday celebration");
    if(theme==QStringLiteral("halloween"))return QStringLiteral("Halloween");
    if(theme==QStringLiteral("christmas"))return QStringLiteral("Christmas");
    if(theme==QStringLiteral("easter"))return QStringLiteral("Easter");
    if(theme==QStringLiteral("july4"))return QStringLiteral("Independence Day");
    if(theme==QStringLiteral("veterans"))return QStringLiteral("Veterans Day");
    if(theme==QStringLiteral("newyear"))return QStringLiteral("New Year");
    return {};
}

class SeasonalDecorationWidget final : public QWidget {
public:
    explicit SeasonalDecorationWidget(QWidget* parent):QWidget(parent){setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_NoSystemBackground);hide();}
    void setTheme(const QString& theme){theme_=theme;setVisible(!theme_.isEmpty());raise();update();}
protected:
    void paintEvent(QPaintEvent*) override {
        if(theme_.isEmpty())return;QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const QRect r=rect();
        const auto hat=[&](QPointF at,double scale){QPainterPath path;path.moveTo(at.x(),at.y()+34*scale);path.lineTo(at.x()+18*scale,at.y());path.lineTo(at.x()+36*scale,at.y()+34*scale);path.closeSubpath();p.setBrush(QColor("#ff5ca8"));p.setPen(QPen(QColor("#ffd166"),2));p.drawPath(path);p.setBrush(QColor("#53cdf3"));p.drawEllipse(QRectF(at.x()+14*scale,at.y()-5*scale,8*scale,8*scale));p.setPen(QPen(QColor("#ffffff"),2));for(int i=1;i<4;++i)p.drawLine(QPointF(at.x()+i*8*scale,at.y()+29*scale),QPointF(at.x()+i*8*scale-8*scale,at.y()+14*scale));};
        const auto streamer=[&](int y){static const QColor colors[]{QColor("#ff5ca8"),QColor("#53cdf3"),QColor("#ffd166"),QColor("#72efb0")};for(int x=20,i=0;x<r.width()-20;x+=54,++i){QPainterPath path;path.moveTo(x,y);path.cubicTo(x+12,y+12,x-12,y+24,x+8,y+38);p.setPen(QPen(colors[i%4],3,Qt::SolidLine,Qt::RoundCap));p.drawPath(path);}};
        const auto flag=[&](QRectF f){p.setPen(Qt::NoPen);for(int i=0;i<7;++i){p.setBrush(i%2?Qt::white:QColor("#d82c3b"));p.drawRect(QRectF(f.x(),f.y()+i*f.height()/7.0,f.width(),f.height()/7.0));}p.setBrush(QColor("#21468b"));p.drawRect(QRectF(f.x(),f.y(),f.width()*0.43,f.height()*0.55));p.setPen(QColor("#ffffff"));p.setFont(QFont(QStringLiteral("Segoe UI"),5,QFont::Bold));p.drawText(QRectF(f.x()+2,f.y()+1,f.width()*0.4,f.height()*0.5),Qt::AlignCenter,QStringLiteral("★ ★ ★\n ★ ★"));};
        if(theme_==QStringLiteral("birthday")){streamer(7);hat(QPointF(12,28),.85);hat(QPointF(r.width()-58,28),.85);p.setPen(QPen(QColor("#ff91a4"),2));for(int i=0;i<4;++i){const int x=(i%2)?r.width()-24:24,y=110+i*95;p.setBrush(i%2?QColor("#53cdf3"):QColor("#ff5ca8"));p.drawEllipse(QPointF(x,y),9,12);p.drawLine(x,y+12,x+(i%2?-6:6),y+32);}}
        else if(theme_==QStringLiteral("july4")||theme_==QStringLiteral("veterans")){flag(QRectF(10,10,62,36));flag(QRectF(r.width()-72,10,62,36));p.setPen(QColor("#ffffff"));p.setFont(QFont(QStringLiteral("Segoe UI"),9,QFont::Bold));if(theme_==QStringLiteral("veterans"))p.drawText(QRect(0,8,r.width(),32),Qt::AlignCenter,QStringLiteral("★  HONORING VETERANS  ★"));}
        else if(theme_==QStringLiteral("halloween")){p.setPen(QPen(QColor("#ff8b2c"),3));const int pumpkinX[2]={18,r.width()-48};for(int x:pumpkinX){p.setBrush(QColor("#ff8b2c"));p.drawEllipse(QRectF(x,14,30,24));p.setBrush(QColor("#111111"));p.drawPolygon(QPolygonF{QPointF(x+7,24),QPointF(x+12,19),QPointF(x+15,25)});p.drawPolygon(QPolygonF{QPointF(x+19,25),QPointF(x+24,19),QPointF(x+27,24)});}p.setPen(QPen(QColor("#cfd6e8"),1));for(int i=0;i<5;++i)p.drawLine(r.width()/2,0,r.width()/2-60+i*30,45);}
        else if(theme_==QStringLiteral("christmas")){for(int x=12,i=0;x<r.width()-12;x+=28,++i){p.setPen(QPen(QColor("#4a536d"),1));p.drawLine(x,0,x,9);p.setBrush(i%3==0?QColor("#ff5268"):i%3==1?QColor("#5ee8d3"):QColor("#ffd166"));p.setPen(Qt::NoPen);p.drawEllipse(QPointF(x,12),4,5);}QPainterPath tree;tree.moveTo(24,18);tree.lineTo(8,55);tree.lineTo(40,55);tree.closeSubpath();p.setBrush(QColor("#2ca66f"));p.drawPath(tree);p.setBrush(QColor("#f6c85f"));p.drawEllipse(QPointF(24,15),4,4);}
        else if(theme_==QStringLiteral("easter")){const int eggX[2]={15,r.width()-42};for(int x:eggX){p.setPen(QPen(QColor("#f7a8d2"),2));p.setBrush(QColor("#fce1f0"));p.drawEllipse(QRectF(x,12,28,38));p.setPen(QPen(QColor("#7fd8d0"),2));p.drawArc(QRectF(x+4,23,20,12),0,180*16);}}
        else if(theme_==QStringLiteral("newyear")){p.setPen(QColor("#ffd166"));p.setFont(QFont(QStringLiteral("Segoe UI"),11,QFont::Bold));p.drawText(QRect(0,7,r.width(),28),Qt::AlignCenter,QStringLiteral("✦  HAPPY NEW YEAR  ✦"));for(int i=0;i<8;++i){const QPointF c(i%2?r.width()-25:25,55+i*70);p.drawLine(c+QPointF(-8,0),c+QPointF(8,0));p.drawLine(c+QPointF(0,-8),c+QPointF(0,8));}}
    }
private:QString theme_;
};

class CelebrationOverlay final : public QWidget {
public:
    CelebrationOverlay(const QString& theme,bool ballDrop):theme_(theme),ballDrop_(ballDrop){
        setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowTransparentForInput);
        setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_NoSystemBackground);setAttribute(Qt::WA_DeleteOnClose);setFocusPolicy(Qt::NoFocus);
        QRect virtualRect;for(auto*screen:QGuiApplication::screens())virtualRect=virtualRect.united(screen->geometry());setGeometry(virtualRect);
        const int count = theme_ == QStringLiteral("birthday") ? 150
                        : theme_ == QStringLiteral("christmas") ? 110
                        : 90;
        particles_.reserve(count);
        for(int i=0;i<count;++i){Particle q;q.x=QRandomGenerator::global()->bounded(qMax(1,width()));q.y=QRandomGenerator::global()->bounded(qMax(1,height()/2))-height()/2.0;q.vx=(QRandomGenerator::global()->bounded(200)-100)/100.0;q.vy=1.4+QRandomGenerator::global()->bounded(240)/100.0;q.spin=(QRandomGenerator::global()->bounded(200)-100)/100.0;q.size=4+QRandomGenerator::global()->bounded(8);q.color=QColor::fromHsv(QRandomGenerator::global()->bounded(360),180+QRandomGenerator::global()->bounded(70),245);particles_<<q;}
        connect(&timer_,&QTimer::timeout,this,[this]{update();if(elapsed_.elapsed()>8500){timer_.stop();close();}});timer_.start(16);elapsed_.start();show();raise();
    }
protected:
    void paintEvent(QPaintEvent*) override {QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const qreal t=elapsed_.elapsed()/1000.0;
        if(theme_==QStringLiteral("july4")||theme_==QStringLiteral("newyear")){for(int b=0;b<5;++b){const QPointF c(width()*(.14+.18*b),height()*(.16+.08*(b%2)));const qreal radius=20+std::fmod(t*85+b*23,120.0);QColor c1=b%2?QColor("#ffffff"):QColor("#ff445b");c1.setAlphaF(qMax(0.0,1.0-radius/145.0));p.setPen(QPen(c1,3));for(int a=0;a<16;++a){const qreal ang=a*6.283185307/16.0;p.drawLine(c+QPointF(std::cos(ang)*radius*.55,std::sin(ang)*radius*.55),c+QPointF(std::cos(ang)*radius,std::sin(ang)*radius));}}}
        for(int i=0;i<particles_.size();++i){auto&q=particles_[i];q.x+=q.vx;q.y+=q.vy;q.vy+=theme_==QStringLiteral("christmas")?.002:.006;if(q.y>height()+20){q.y=-20;q.x=QRandomGenerator::global()->bounded(qMax(1,width()));}p.save();p.translate(q.x,q.y);p.rotate(t*100*q.spin+i*11);p.setPen(Qt::NoPen);p.setBrush(theme_==QStringLiteral("christmas")?QColor(245,250,255,210):q.color);if(theme_==QStringLiteral("christmas"))p.drawEllipse(QPointF(0,0),q.size*.45,q.size*.45);else p.drawRect(QRectF(-q.size/2,-2,q.size,4));p.restore();}
        if(theme_==QStringLiteral("birthday")){p.setFont(QFont(QStringLiteral("Segoe UI"),24,QFont::Bold));p.setPen(QColor(255,255,255,220));p.drawText(QRect(0,40,width(),50),Qt::AlignCenter,QStringLiteral("HAPPY BIRTHDAY!"));}
        if(ballDrop_){const qreal progress=qBound<qreal>(0.0,t/5.0,1.0);const qreal y=-45+progress*(height()*.42+45);p.setPen(QPen(QColor("#f7e59b"),3));p.drawLine(width()/2,0,width()/2,y-28);QRadialGradient grad(QPointF(width()/2,y),34);grad.setColorAt(0,QColor("#fffbd1"));grad.setColorAt(1,QColor("#c4a33f"));p.setBrush(grad);p.drawEllipse(QPointF(width()/2,y),32,32);}
    }
private:
    struct Particle{qreal x{},y{},vx{},vy{},spin{},size{};QColor color;};QString theme_;bool ballDrop_{};QVector<Particle> particles_;QTimer timer_;QElapsedTimer elapsed_;
};
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Leapcast Studio • Multi-Chat & Moderation"));
    resize(1280, 760);
    setMinimumSize(1000, 620);
    setWindowIcon(QIcon(QStringLiteral(":/brand/lefroge_chat_icon.png")));
    controller_ = new AppController(this);
    applyTheme();
    overlay_ = new OverlayServer(this);
    const QString savedMobileToken=controller_->settings()->secret(QStringLiteral("mobile_companion_token"));
    if(savedMobileToken.isEmpty())controller_->settings()->setSecret(QStringLiteral("mobile_companion_token"),overlay_->mobileToken());
    else overlay_->setMobileToken(savedMobileToken);
    overlay_->start(static_cast<quint16>(controller_->settings()->preference(QStringLiteral("port"),8080).toInt()));
    overlay_->setFadeSeconds(controller_->settings()->preference(QStringLiteral("overlay_fade_seconds"), 0).toInt());
    overlay_->setAppearance(QColor(controller_->settings()->preference(QStringLiteral("overlay_background_color"),QStringLiteral("#000000")).toString()),controller_->settings()->preference(QStringLiteral("overlay_background_opacity"),0).toInt(),controller_->settings()->preference(QStringLiteral("chat_outline_thickness"),2).toInt());
    overlay_->setShowPlatformIcons(controller_->settings()->preference(QStringLiteral("overlay_show_platform_icons"),true).toBool());
    connect(overlay_,&OverlayServer::mobileDeleteRequested,controller_,&AppController::deleteChatMessage);
    connect(overlay_,&OverlayServer::mobileModerationRequested,controller_,&AppController::moderateMessage);
    connect(overlay_,&OverlayServer::mobileRefreshModerationRequested,this,[this]{controller_->refreshBans(QStringLiteral("twitch"));controller_->refreshBans(QStringLiteral("youtube"));controller_->refreshTwitchAppeals();});
    connect(overlay_,&OverlayServer::mobileUnbanRequested,this,[this](const QString&platform,const QString&id){if(platform==QStringLiteral("twitch"))controller_->unbanTwitch(id);else if(platform==QStringLiteral("youtube"))controller_->unbanYouTube(id);});
    connect(overlay_,&OverlayServer::mobileAppealRequested,this,[this](const QString&id,bool approved){controller_->resolveTwitchAppeal(id,approved,approved?QStringLiteral("Approved from Phone Connect"):QStringLiteral("Rejected from Phone Connect"));});
    connect(overlay_,&OverlayServer::mobileSettingRequested,this,[this](const QString&name,const QVariant&value){
        if(name==QStringLiteral("overlay_fade_seconds"))controller_->settings()->setPreference(name,qBound(0,value.toInt(),3600));
        else if(name==QStringLiteral("overlay_background_opacity"))controller_->settings()->setPreference(name,qBound(0,value.toInt(),100));
        else if(name==QStringLiteral("chat_outline_thickness"))controller_->settings()->setPreference(name,qBound(0,value.toInt(),8));
        else if(name==QStringLiteral("automod_enabled")){auto moderation=controller_->settings()->moderation();moderation[QStringLiteral("enabled")]=value.toBool();controller_->settings()->setModeration(moderation);}
        else if(name==QStringLiteral("chat_colour_mode")){const QString mode=value.toString();if(QStringList{QStringLiteral("random"),QStringLiteral("single"),QStringLiteral("gradient"),QStringLiteral("pattern")}.contains(mode))controller_->settings()->setPreference(name,mode);}
        else if(name==QStringLiteral("chat_name_pattern")){const QString pattern=value.toString();if(QStringList{QStringLiteral("repeat"),QStringLiteral("mirror"),QStringLiteral("blocks")}.contains(pattern))controller_->settings()->setPreference(name,pattern);}
        else if(name==QStringLiteral("chat_name_colour")){const QColor color(value.toString());if(color.isValid())controller_->settings()->setPreference(name,color.name());}
        else if(name==QStringLiteral("chat_name_palette")){QStringList palette=value.toStringList();palette.erase(std::remove_if(palette.begin(),palette.end(),[](const QString&v){return !QColor(v).isValid();}),palette.end());if(palette.size()>=2){while(palette.size()>8)palette.removeLast();controller_->settings()->setPreference(name,palette);controller_->regenerateNameColours();}}
        else if(name==QStringLiteral("chat_palette_randomize")){controller_->settings()->setPreference(name,value.toBool());controller_->regenerateNameColours();}
        else if(name==QStringLiteral("regenerate_name_colors")){controller_->regenerateNameColours();}
        else if(name==QStringLiteral("seasonal_effects_enabled")||name==QStringLiteral("birthday_effects_enabled"))controller_->settings()->setPreference(name,value.toBool());
        else if(name==QStringLiteral("birthday_confetti_playback")){const QString mode=value.toString();if(mode==QStringLiteral("always")||mode==QStringLiteral("first"))controller_->settings()->setPreference(name,mode);}
        else if(name==QStringLiteral("birthday_month"))controller_->settings()->setPreference(name,qBound(0,value.toInt(),12));
        else if(name==QStringLiteral("birthday_day"))controller_->settings()->setPreference(name,qBound(0,value.toInt(),31));
        overlay_->setFadeSeconds(controller_->settings()->preference(QStringLiteral("overlay_fade_seconds"),0).toInt());
        overlay_->setAppearance(QColor(controller_->settings()->preference(QStringLiteral("overlay_background_color"),QStringLiteral("#000000")).toString()),controller_->settings()->preference(QStringLiteral("overlay_background_opacity"),0).toInt(),controller_->settings()->preference(QStringLiteral("chat_outline_thickness"),2).toInt());
        updateSeasonalEffects(false);syncMobileSettings();
    });
    syncMobileSettings();
    connect(controller_->settings(),&SettingsStore::changed,this,[this]{syncMobileSettings();});
    updater_ = new UpdateService(this);
    connect(overlay_,&OverlayServer::mobileInstallUpdateRequested,this,[this]{if(mobileUpdateAsset_.isValid())updater_->downloadAndInstall(mobileUpdateAsset_,mobileUpdateName_,mobileUpdateDigest_);});

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildDashboard());
    splitter->addWidget(buildChatDock());
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({850, 430});
    setCentralWidget(splitter);
    connect(controller_, &AppController::messageReady, this, [this](const ChatMessage& m) {
        const QString badges = badgeGlyphs(m.badges);
        const QString icon=controller_->settings()->preference(QStringLiteral("program_popout_show_platform_icons"),true).toBool()?platformIconHtml(m.platform):QString();
        const QString html = QStringLiteral("%1%2%3 %4")
            .arg(icon,badges.isEmpty() ? QString() : badges + QStringLiteral(" "),chatNameHtml(m),chatMessageBodyHtml(m));
        // Stamped with an id (see appendChatMessage) so a right-click on
        // either view can be traced back to this message for moderation.
        const qint64 id = ++nextChatSeq_;
        chatHistoryById_.insert(id, m);
        chatHistoryById_.remove(id - 400);
        appendChatMessage(chatViews_.value(QStringLiteral("combined")), html, id);
        appendChatMessage(chatViews_.value(m.platform), html, id);
        overlay_->ingest(m);
        if(popout_) popout_->appendMessage(m);
    });
    connect(controller_,&AppController::pinnedMessageChanged,this,[this](const QString&platform,const ChatMessage&message,bool active){if(active)pinnedMessages_[platform]=message;else pinnedMessages_.remove(platform);refreshPinnedBanner();if(popout_)popout_->setPinnedMessage(platform,message,active);});
    connect(controller_, &AppController::messageModerated, this, [this](const ChatMessage& m, const QString& reason) {
        // Deliberately never touches overlay_ — viewers never see this note,
        // and since the original message was never ingested either, nothing
        // is "skipped" in the overlay's own message list.
        const QString note = QStringLiteral(
            "<div style='margin:3px 0;color:#7f8ba5;font-style:italic;font-size:9pt'>"
            "&#9888; %1's message was removed by AutoMod (%2)</div>")
            .arg(m.user.toHtmlEscaped(), reason.toHtmlEscaped());
        appendTrimmed(chatViews_.value(QStringLiteral("combined")), note);
        appendTrimmed(chatViews_.value(m.platform), note);
        if (popout_) popout_->appendModerationNote(m.user, reason);
    });
    connect(controller_,&AppController::tiktokActivityReady,this,[this](const StreamEvent&e){if(controller_->settings()->preference(QStringLiteral("tiktok_popout_activity_enabled"),false).toBool()&&popout_)popout_->showTikTokActivity(e);});
    connect(controller_,&AppController::twitchAppealsUpdated,this,[this](const QJsonArray&appeals){
        overlay_->setMobileAppeals(appeals);
        if(!twitchAppeals_)return;twitchAppeals_->clear();
        for(const auto&v:appeals){const auto a=v.toObject();auto*item=new QListWidgetItem;
            const QString user=a.value("user_name").toString(a.value("user_login").toString("Unknown user"));
            item->setText(QStringLiteral("%1\n%2").arg(user,a.value("text").toString("No appeal message provided.")));
            item->setData(Qt::UserRole,a.value("user_id").toString());item->setData(Qt::UserRole+1,user);item->setData(Qt::UserRole+2,a.toVariantMap());item->setToolTip(QStringLiteral("Submitted %1").arg(friendlyTimestamp(a.value("created_at").toString())));twitchAppeals_->addItem(item);}
        if(twitchAppeals_->count()==0){auto*i=new QListWidgetItem("No pending Twitch appeals.");i->setFlags(Qt::NoItemFlags);twitchAppeals_->addItem(i);}
    });
    connect(controller_,&AppController::userChatHistoryReady,this,[this](const QString&,const QJsonArray&messages){
        if(!twitchAppealHistory_)return;QString html;
        for(const auto&v:messages){const auto m=v.toObject();html+=QStringLiteral("<div style='margin:0 0 9px;padding:9px;background:#171d2b;border-radius:8px'><span style='color:#7f8ba5;font-size:8pt'>%1</span><br><b style='color:#b48cff'>%2</b> %3</div>").arg(friendlyTimestamp(m.value("time").toString()).toHtmlEscaped(),m.value("user").toString().toHtmlEscaped(),m.value("text").toString().toHtmlEscaped());}
        twitchAppealHistory_->setHtml(html.isEmpty()?QStringLiteral("<div style='color:#7f8ba5;text-align:center;margin-top:30px'>No stored chat messages were found for this user.</div>"):html);
    });
    connect(controller_,&AppController::eventReady,this,[this](const StreamEvent&e){
        overlay_->ingestEvent(e);
        if(e.kind==QStringLiteral("twitch_redemption"))return;
        const QString colour=e.platform==QStringLiteral("twitch")?QStringLiteral("#b48cff"):
            e.platform==QStringLiteral("youtube")?QStringLiteral("#ff637d"):
            e.platform==QStringLiteral("rumble")?QStringLiteral("#85c742"):QStringLiteral("#63e6be");
        const QString message=QStringLiteral(
            "<div style='margin:7px 0;padding:9px 11px;background:#171d2d;border-left:3px solid %1;border-radius:7px'>"
            "<span style='color:#8f9bb5;font-size:9pt;font-weight:700'>STREAM ALERT</span><br>"
            "<b style='color:#f8fbff'>%2</b> <span style='color:%1'>%3</span>%4</div>")
            .arg(colour,e.user.toHtmlEscaped(),eventAction(e).toHtmlEscaped(),
                 e.message.isEmpty()?QString():QStringLiteral("<br><span style='color:#c7cede'>%1</span>").arg(e.message.toHtmlEscaped()));
        appendTrimmed(chatViews_.value(QStringLiteral("combined")), message);
        appendTrimmed(chatViews_.value(e.platform), message);
        if(popout_)popout_->showEvent(e);
    });
    connect(controller_,&AppController::viewerCount,this,[this](const QString&p,int n){overlay_->setViewers(p,n);if(popout_)popout_->setViewers(p,n);});
    connect(controller_, &AppController::sourceStatus, this,
            [this](const QString& platform,const QString& state,const QString& detail){
        const QString colour=state=="ok"?"#63e6be":state=="error"?"#ff5573":"#f6c85f";
        if(sourceStates_.contains(platform)){
            auto* value=sourceStates_[platform]; value->setText(detail);
            value->setStyleSheet("color:"+colour);
        }
        if(state==QStringLiteral("ok")&&sourcesWelcome_){sourcesWelcome_->hide();sourcesWelcome_=nullptr;}
        if(platform==QStringLiteral("twitch")&&twitchModerationStatus_){
            twitchModerationStatus_->setText(detail);
            twitchModerationStatus_->setStyleSheet("color:"+colour);
        }
        if(platform==QStringLiteral("youtube")&&youtubeModerationStatus_){
            youtubeModerationStatus_->setText(detail);
            youtubeModerationStatus_->setStyleSheet("color:"+colour);
        }
    });
    connect(controller_, &AppController::bansUpdated, this,
        [this](const QString& platform, const QJsonArray& bans) {
            overlay_->setMobileBans(platform,bans);
            if(platform==QStringLiteral("twitch"))twitchBansCache_=bans;
        QListWidget* list = platform == QStringLiteral("twitch") ? twitchBans_ : youtubeBans_;
        if (!list) return;
        list->clear();
        // Twitch's API doesn't guarantee ban order; sort most-recent-first so
        // the list reads the same way regardless of the API's own ordering.
        QList<QJsonObject> sorted;
        sorted.reserve(bans.size());
        for (const auto& value : bans) sorted << value.toObject();
        std::sort(sorted.begin(), sorted.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return QDateTime::fromString(a.value(QStringLiteral("created_at")).toString(), Qt::ISODate)
                 > QDateTime::fromString(b.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
        });
        for (const auto& ban : sorted) {
            const QString user = ban.value(QStringLiteral("user_name")).toString(
                ban.value(QStringLiteral("user_login")).toString(QStringLiteral("Unknown user")));
            const QString reason = ban.value(QStringLiteral("reason")).toString();
            // Twitch marks a timed-out (as opposed to permanently banned) user
            // with a non-empty expires_at; YouTube restrictions recorded here
            // carry an explicit "Hide from channel" vs "Timeout (5 min)" type
            // (see AppController's banCreated handler).
            const bool isTimeout = platform == QStringLiteral("twitch")
                ? !ban.value(QStringLiteral("expires_at")).toString().isEmpty()
                : ban.value(QStringLiteral("type")).toString() != QStringLiteral("Hide from channel");
            const QString type = ban.value(QStringLiteral("type")).toString(
                isTimeout ? QStringLiteral("Timeout") : QStringLiteral("Ban"));
            const QString created = ban.value(QStringLiteral("created_at")).toString();
            const QString summary = reason.isEmpty() ? type : reason;
            auto* item = new QListWidgetItem;
            item->setData(Qt::UserRole, platform == QStringLiteral("twitch")
                ? ban.value(QStringLiteral("user_id")).toString()
                : ban.value(QStringLiteral("id")).toString());
            item->setData(Qt::UserRole + 1, QStringLiteral("Account: %1\nReason: %2\nCreated: %3")
                .arg(user, summary, created.isEmpty() ? QStringLiteral("Not recorded") : friendlyTimestamp(created)));
            item->setData(Qt::UserRole + 2, isTimeout);
            auto* card = new QWidget;
            card->setProperty("restrictionCard", true);
            auto* cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(13, 9, 13, 9);
            cardLayout->setSpacing(3);
            cardLayout->addWidget(label(user, "restrictionName"));
            auto* detail = label(summary, "restrictionReason");
            detail->setWordWrap(true);
            cardLayout->addWidget(detail);
            if (!created.isEmpty()) cardLayout->addWidget(label(friendlyTimestamp(created), "restrictionTime"));
            item->setSizeHint(card->sizeHint());
            list->addItem(item);
            list->setItemWidget(item, card);
        }
        if (list->count() == 0) {
            auto* empty = new QListWidgetItem(QStringLiteral("No active restrictions found."));
            empty->setFlags(Qt::NoItemFlags);
            list->addItem(empty);
        }
    });
    connect(controller_, &AppController::moderationResult, this,
            [this](const QString& platform, bool success, const QString& detail) {
        if (success) { lastModerationWarning_.remove(platform); return; }
        const bool appealScopeMissing=platform==QStringLiteral("twitch")&&
            (detail==QStringLiteral("TWITCH_SCOPE_UPGRADE_REQUIRED")||
             detail.contains(QStringLiteral("moderator:manage:unban_requests"),Qt::CaseInsensitive));
        if(appealScopeMissing){
            lastModerationWarning_.remove(platform);
            const auto answer=QMessageBox::question(this,QStringLiteral("Twitch permission needed"),
                QStringLiteral("Twitch has not granted Leapcast Studio permission to approve or deny appeals yet.\n\nReconnect Twitch once to add the Appeals permission. Your channel and local settings will stay saved.\n\nReconnect Twitch now?"),
                QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes);
            if(answer==QMessageBox::Yes)authorizeTwitch();
            return;
        }
        // AutoMod retries a moderation action on every matching chat message;
        // once we've told the user about a given failure (e.g. "not a moderator
        // on that channel"), don't reopen the same dialog for every repeat.
        if (lastModerationWarning_.value(platform) == detail) return;
        lastModerationWarning_[platform] = detail;
        QMessageBox::warning(this, QStringLiteral("Moderation"), detail);
    });
    connect(controller_, &AppController::twitchClipCreated, this, [this](const QUrl& url) {
        if (popout_) popout_->showClipResult(true, QStringLiteral("Clip created — link copied to clipboard."), url);
    });
    connect(controller_, &AppController::twitchClipFailed, this, [this](const QString& detail) {
        if (popout_) popout_->showClipResult(false, detail);
    });
    connect(controller_,&AppController::twitchAuthorizationUrl,this,[this](const QUrl&url){
        if(twitchModerationStatus_)twitchModerationStatus_->setText(QStringLiteral("Finish signing in on Twitch, then return here."));
        if(twitchConnectButton_){twitchConnectButton_->setText(QStringLiteral("Waiting for Twitch…"));twitchConnectButton_->setEnabled(false);}
        if(!QDesktopServices::openUrl(url))QMessageBox::warning(this,QStringLiteral("Twitch authorization"),QStringLiteral("Could not open Twitch in your browser."));
    });
    connect(controller_,&AppController::twitchAuthorized,this,[this](const QString&login){
        if(twitchModerationStatus_)twitchModerationStatus_->setText(QStringLiteral("Connected as %1").arg(login));
        if(twitchConnectButton_){twitchConnectButton_->setText(QStringLiteral("Reconnect Twitch"));twitchConnectButton_->setEnabled(true);}
        if(popout_)popout_->setClipAvailable(true);
        if(clipEditor_)clipEditor_->setTwitchAccessToken(controller_->settings()->secret(QStringLiteral("twitch_access_token")));
        QMessageBox::information(this,QStringLiteral("Twitch connected"),QStringLiteral("You're all set. Leapcast Studio is connected to Twitch as %1.").arg(login));
    });
    connect(controller_,&AppController::twitchAuthorizationFailed,this,[this](const QString&detail){
        if(twitchConnectButton_){twitchConnectButton_->setText(QStringLiteral("Try Twitch again"));twitchConnectButton_->setEnabled(true);}
        const bool missingAppId=detail.contains(QStringLiteral("application ID"),Qt::CaseInsensitive);
        QMessageBox::critical(this,QStringLiteral("Twitch couldn't connect"),
            missingAppId
                ? QStringLiteral("This installer is missing Leapcast Studio's Twitch connection setting. This is a build problem—not something you need to find or paste.\n\nPlease install the next Leapcast Studio update and try again.")
                : QStringLiteral("Twitch did not finish connecting.\n\n%1\n\nNothing was changed. Select Try Twitch again when you're ready.").arg(detail));
    });
    connect(updater_, &UpdateService::updateAvailable, this,
            [this](const QString& version, const QString& notes, const QUrl& asset,
                   const QString& name, const QString& digest) {
        mobileUpdateAsset_=asset;mobileUpdateName_=name;mobileUpdateDigest_=digest;
        overlay_->setMobileUpdate(QJsonObject{{"available",true},{"version",version},{"notes",notes.left(1200)}});
        if (silentUpdateCheck_) { silentUpdateCheck_=false; return; }
        if (showUpdateNotesDialog(this,version,notes)) {
            auto* progress = new QProgressDialog(
                QStringLiteral("Downloading the update…\nLeapcast Studio will reopen automatically."),
                QString(), 0, 100, this);
            progress->setWindowTitle(QStringLiteral("Updating Leapcast Studio"));
            progress->setCancelButton(nullptr);
            progress->setWindowModality(Qt::WindowModal);
            progress->setMinimumDuration(0);
            progress->setAutoClose(false);
            connect(updater_, &UpdateService::progress, progress,
                    [progress](qint64 received, qint64 total) {
                if (total <= 0) {
                    progress->setRange(0, 0);
                    return;
                }
                progress->setRange(0, 100);
                progress->setValue(static_cast<int>((received * 100) / total));
            });
            connect(updater_, &UpdateService::failed, progress, [progress] {
                progress->close();
                progress->deleteLater();
            });
            progress->show();
            updater_->downloadAndInstall(asset, name, digest);
        }
    });
    connect(updater_, &UpdateService::upToDate, this, [this] {
        overlay_->setMobileUpdate(QJsonObject{{"available",false}});
        mobileUpdateAsset_={};mobileUpdateName_.clear();mobileUpdateDigest_.clear();
        if (silentUpdateCheck_) { silentUpdateCheck_=false; return; }
        QMessageBox::information(this, QStringLiteral("Updates"),
                                 QStringLiteral("Leapcast Studio is up to date."));
    });
    connect(updater_, &UpdateService::failed, this, [this](const QString& detail) {
        if (silentUpdateCheck_) { silentUpdateCheck_=false; return; }
        QMessageBox::warning(this, QStringLiteral("Update check"), detail);
    });
    QTimer::singleShot(0, controller_, &AppController::startConfiguredSources);
    QTimer::singleShot(350, this, &MainWindow::showPostUpdateConnectionCheck);
    QTimer::singleShot(900, this, &MainWindow::showFirstLaunchUpdateLog);
    QTimer::singleShot(1600, this, [this]{controller_->refreshBans(QStringLiteral("twitch"));controller_->refreshBans(QStringLiteral("youtube"));controller_->refreshTwitchAppeals();});
    QTimer::singleShot(2400, updater_, [this]{silentUpdateCheck_=true;updater_->check(false);});
}

void MainWindow::syncMobileSettings(){
    if(!overlay_||!controller_)return;
    QStringList palette=controller_->settings()->preference(QStringLiteral("chat_name_palette"),QStringList{QStringLiteral("#6c4cff"),QStringLiteral("#18dfd1"),QStringLiteral("#ff5ca8"),QStringLiteral("#ffd166")}).toStringList();
    QJsonArray paletteJson;for(const auto&value:palette)if(QColor(value).isValid())paletteJson.append(QColor(value).name());
    const QString theme=seasonalThemeFor(controller_->settings(),QDateTime::currentDateTime());
    overlay_->setMobileSettings(QJsonObject{
        {"overlay_fade_seconds",overlay_->fadeSeconds()},
        {"overlay_background_opacity",controller_->settings()->preference(QStringLiteral("overlay_background_opacity"),0).toInt()},
        {"chat_outline_thickness",controller_->settings()->preference(QStringLiteral("chat_outline_thickness"),2).toInt()},
        {"automod_enabled",controller_->settings()->moderation().value(QStringLiteral("enabled")).toBool(true)},
        {"chat_colour_mode",controller_->settings()->preference(QStringLiteral("chat_colour_mode"),QStringLiteral("random")).toString()},
        {"chat_name_colour",controller_->settings()->preference(QStringLiteral("chat_name_colour"),QStringLiteral("#53cdf3")).toString()},
        {"chat_name_pattern",controller_->settings()->preference(QStringLiteral("chat_name_pattern"),QStringLiteral("repeat")).toString()},
        {"chat_name_palette",paletteJson},
        {"chat_palette_randomize",controller_->settings()->preference(QStringLiteral("chat_palette_randomize"),false).toBool()},
        {"seasonal_effects_enabled",controller_->settings()->preference(QStringLiteral("seasonal_effects_enabled"),true).toBool()},
        {"birthday_effects_enabled",controller_->settings()->preference(QStringLiteral("birthday_effects_enabled"),true).toBool()},
        {"birthday_confetti_playback",controller_->settings()->preference(QStringLiteral("birthday_confetti_playback"),QStringLiteral("always")).toString()},
        {"birthday_month",controller_->settings()->preference(QStringLiteral("birthday_month"),0).toInt()},
        {"birthday_day",controller_->settings()->preference(QStringLiteral("birthday_day"),0).toInt()},
        {"seasonal_theme",theme},
        {"seasonal_theme_label",seasonalThemeLabel(theme)}
    });
}

void MainWindow::updateSeasonalEffects(bool allowCelebration){
    if(!controller_)return;const QDateTime now=QDateTime::currentDateTime();const QString theme=seasonalThemeFor(controller_->settings(),now);seasonalTheme_=theme;
    if(!seasonalDecoration_){seasonalDecoration_=new SeasonalDecorationWidget(this);seasonalDecoration_->setGeometry(rect());}
    static_cast<SeasonalDecorationWidget*>(seasonalDecoration_)->setTheme(theme);seasonalDecoration_->setGeometry(rect());seasonalDecoration_->raise();
    if(popout_)popout_->setSeasonalTheme(theme);
    syncMobileSettings();
    if(!allowCelebration||theme.isEmpty()||theme==QStringLiteral("veterans"))return;
    const QString dateKey=now.date().toString(Qt::ISODate);
    const QString seenKey=QStringLiteral("seasonal_effect_seen_")+theme;
    if(theme==QStringLiteral("birthday")){
        const QString playback=controller_->settings()->preference(QStringLiteral("birthday_confetti_playback"),QStringLiteral("always")).toString();
        if(playback==QStringLiteral("always")){
            // The 30-second seasonal timer also checks this function. Keep
            // "Every launch" to exactly one party animation per running
            // Leapcast process; reopening the app resets this flag.
            if(birthdayCelebrationPlayedThisLaunch_)return;
            birthdayCelebrationPlayedThisLaunch_=true;
        }else{
            if(controller_->settings()->preference(seenKey).toString()==dateKey)return;
            controller_->settings()->setPreference(seenKey,dateKey);
        }
    }else{
        if(controller_->settings()->preference(seenKey).toString()==dateKey)return;
        controller_->settings()->setPreference(seenKey,dateKey);
    }
    const bool ballDrop=theme==QStringLiteral("newyear")&&now.date().month()==1&&now.date().day()==1&&now.time().hour()==0&&now.time().minute()<5;
    new CelebrationOverlay(theme,ballDrop);
}

bool MainWindow::event(QEvent* e) {
    if (e->type() == QEvent::WindowActivate && popout_ && popout_->ghostMode()) popout_->setGhostMode(false);
    if(e->type()==QEvent::Resize&&seasonalDecoration_){seasonalDecoration_->setGeometry(rect());seasonalDecoration_->raise();}
    return QMainWindow::event(e);
}

void MainWindow::showEvent(QShowEvent* e){
    QMainWindow::showEvent(e);
    if(!seasonalTimer_){seasonalTimer_=new QTimer(this);seasonalTimer_->setInterval(30000);connect(seasonalTimer_,&QTimer::timeout,this,[this]{updateSeasonalEffects(true);});seasonalTimer_->start();}
    QTimer::singleShot(250,this,[this]{updateSeasonalEffects(true);});
}

void MainWindow::closeEvent(QCloseEvent* e){
    controller_->settings()->save();
    QMainWindow::closeEvent(e);
}

void MainWindow::showFirstLaunchUpdateLog(){
    const QString version=QString::fromLatin1(leapcast::Version);
    // Keep the user-visible version at 3.0.6 while allowing cumulative 3.0.6
    // hotfix notes to appear once even if an earlier 3.0.6 note set was seen.
    const QString notesRevision=version+QStringLiteral("-cumulative-r5");
    if(controller_->settings()->preference(QStringLiteral("update_log_seen_revision")).toString()==notesRevision)return;
    controller_->settings()->setPreference(QStringLiteral("update_log_seen_revision"),notesRevision);
    QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("What's new in Leapcast %1").arg(version));dialog.resize(500,390);dialog.setMinimumSize(420,310);
    auto* layout=new QVBoxLayout(&dialog);layout->setContentsMargins(18,16,18,16);layout->setSpacing(10);
    auto* title=label(QStringLiteral("WHAT'S NEW • %1").arg(version),"heroTitle");layout->addWidget(title);
    auto* summary=label(QStringLiteral("A quick summary of this cumulative 3.0.6 update. This note appears once for this revision."),"muted");summary->setWordWrap(true);layout->addWidget(summary);
    auto* notes=new QTextBrowser;notes->setMarkdown(QStringLiteral("- **Pinned messages:** Twitch and YouTube/Shorts pins can appear in chat with a 📌 indicator and can be enabled or disabled globally in Settings.\n- **TikTok compatibility possibly improved:** LIVE chat/view-count collection uses broader fallbacks, but TikTok changes its page frequently so this is not guaranteed.\n- **Twitch login possibly improved:** Twitch authorization stays in the normal system browser with a best-effort handoff back to the in-app Clips browser.\n- **Pop-out fixes:** the close X is drawn instead of relying on a font glyph, every edge/corner can resize even at 0% opacity, and a first-open crash from birthday/holiday decoration painting has been corrected by isolating the decorations in a safe transparent child layer.\n- **AutoMod whitelist:** `smash`, `pass`, and `as` are allowed by default, the whitelist is editable, and normal English vocabulary is protected from substring/fuzzy false positives.\n- **YouTube custom emojis:** supported channel/member emojis render as their actual inline images in dashboard chat, pop-out chat, OBS overlay, and Phone Connect.\n- **Custom palette randomizer:** selected 2–8 color palettes can now generate a different stable variation for every chatter, with a one-click reroll. The custom palette editor, reorder/add/remove controls, and reroll controls are also available in Phone Connect.\n- **Birthday effects:** users can save a month/day for birthday effects. Leapcast checks the PC's local system clock and activates the celebration only on the day before and the birthday itself. The special 2026 birthday launch celebration runs only Aug 23–24.\n- **Birthday confetti playback:** choose **Every launch** (the default) to replay the party confetti once whenever Leapcast is relaunched during the birthday window, or **First launch only** to show it just once per active birthday date. The same control is available in Phone Connect.\n- **Birthday celebration visuals:** transparent screen-wide confetti, balloons, sparkles, streamers and party accents can animate outside the Leapcast window; party hats decorate the app and hanging confetti/streamers also decorate the transparent pop-out without covering chat.\n- **Seasonal & holiday themes:** Halloween runs Oct 15–31, Christmas Dec 10–28, Easter over Easter weekend, Independence Day on July 4 with fireworks, Veterans Day on Nov 11 with American-flag accents, and New Year's Eve/Day with fireworks. A New Year ball drop can play during the first minutes after the local system clock reaches midnight on Jan 1.\n- **Mobile seasonal controls:** Phone Connect includes the birthday field/toggles and mirrors the active seasonal theme.\n- **Build stability:** the earlier 3.0.6 YouTube pinned-message compile error and the birthday/seasonal overlay MSVC compile error are corrected."));layout->addWidget(notes,1);
    auto* close=new QPushButton(QStringLiteral("Got it"));close->setProperty("primary",true);connect(close,&QPushButton::clicked,&dialog,&QDialog::accept);layout->addWidget(close,0,Qt::AlignRight);dialog.exec();
}

QWidget* MainWindow::buildSidebar() { return new QWidget; }

QWidget* MainWindow::buildDashboard() {
    auto* host = new QWidget;
    auto* outer = new QHBoxLayout(host);
    outer->setContentsMargins(14, 14, 10, 14);
    outer->setSpacing(12);

    auto* rail = new QFrame;
    rail->setObjectName(QStringLiteral("navigationRail"));
    // Wide enough for the longest label ("Moderation") at the nav button's
    // reduced padding/font without truncating.
    rail->setFixedWidth(140);
    auto* railLayout = new QVBoxLayout(rail);navigationLayout_=railLayout;
    auto* brand = new QLabel;
    brand->setPixmap(QPixmap(QStringLiteral(":/brand/lefroge_chat_icon.png"))
                         .scaled(62, 62, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    brand->setAlignment(Qt::AlignCenter);
    railLayout->addWidget(brand);
    railLayout->addWidget(label(QStringLiteral("LEAPCAST\nSTUDIO"), "brand"));

    pages_ = new QStackedWidget;
    const QStringList keys{QStringLiteral("sources"),QStringLiteral("events"),QStringLiteral("keys"),QStringLiteral("moderation"),QStringLiteral("bans"),QStringLiteral("obs"),QStringLiteral("phone"),QStringLiteral("settings")};
    const QHash<QString,QString> names{{"sources","Sources"},{"events","Events"},{"keys","Keys"},{"moderation","Moderation"},{"bans","Bans"},{"obs","Chat Overlay"},{"phone","Phone Connect"},{"settings","Settings"}};
    const QStringList saved=controller_->settings()->preference(QStringLiteral("navigation_order"),keys).toStringList();
    navigationOrder_=saved;for(const auto&key:keys)if(!navigationOrder_.contains(key))navigationOrder_<<key;
    for(int i=navigationOrder_.size()-1;i>=0;--i)if(!keys.contains(navigationOrder_[i]))navigationOrder_.removeAt(i);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    pages_->addWidget(buildSourcesPage());pages_->addWidget(buildEventsPage());pages_->addWidget(buildKeysPage());pages_->addWidget(buildModerationPage());pages_->addWidget(buildBansPage());pages_->addWidget(buildObsPage());pages_->addWidget(buildPhoneConnectPage());pages_->addWidget(buildSettingsPage());
    for (const auto&key:navigationOrder_) {
        const int i=keys.indexOf(key);
        auto* button = new QPushButton(names.value(key));
        button->setCheckable(true);
        button->setProperty("nav", true);
        button->setProperty("navKey",key);navigationButtons_[key]=button;
        group->addButton(button, i);
        railLayout->addWidget(button);
    }
    railLayout->addStretch();
    auto* version = label(QStringLiteral("v%1").arg(QString::fromLatin1(leapcast::Version)), "version");
    version->setAlignment(Qt::AlignCenter);
    version->setMinimumHeight(24);
    version->setToolTip(QStringLiteral("Leapcast Studio version %1").arg(QString::fromLatin1(leapcast::Version)));
    railLayout->addWidget(version);
    group->button(keys.indexOf(QStringLiteral("moderation")))->setChecked(true);
    pages_->setCurrentIndex(keys.indexOf(QStringLiteral("moderation")));
    connect(group, &QButtonGroup::idClicked, pages_, &QStackedWidget::setCurrentIndex);
    outer->addWidget(rail);
    outer->addWidget(pages_, 1);
    return host;
}

QWidget* MainWindow::makePlatformCard(const QString& name, const QString& status,
                                      const QColor& accent, const QString& action,
                                      const QString& credentialAction,
                                      const QUrl& credentialUrl) {
    auto* card = new QFrame;
    card->setProperty("card", true);
    auto* layout = new QVBoxLayout(card);
    auto* head = new QHBoxLayout;
    head->addWidget(label(name, "cardTitle"));
    head->addStretch();
    auto* state = label(status, "status"); state->setObjectName(QStringLiteral("cardStatus"));state->setStyleSheet("color:" + accent.name());
    head->addWidget(state);
    layout->addLayout(head);
    auto* actions = new QHBoxLayout;
    auto* button = new QPushButton(action);
    button->setObjectName(QStringLiteral("cardAction"));
    button->setStyleSheet("background:" + accent.name() + ";color:#081018;");
    if (!credentialAction.isEmpty() && credentialUrl.isValid()) {
        auto* credential = new QPushButton(credentialAction);
        credential->setObjectName(QStringLiteral("credentialLink"));
        credential->setToolTip(QStringLiteral("Open the provider's credential setup page in your browser."));
        actions->addWidget(credential, 1);
        connect(credential, &QPushButton::clicked, this, [credentialUrl] {
            QDesktopServices::openUrl(credentialUrl);
        });
    }
    actions->addWidget(button, 1);
    layout->addLayout(actions);
    return card;
}

QWidget* MainWindow::buildSourcesPage() {
    auto* page=new QWidget;auto* outer=new QVBoxLayout(page);outer->setContentsMargins(0,0,0,0);auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto* body=new QWidget;auto* layout=new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll);
    layout->setContentsMargins(8,4,8,8);
    layout->addWidget(label(QStringLiteral("CONNECTED COMMUNITIES"),"heroTitle"));
    layout->addWidget(label(QStringLiteral("Paste a channel or live-stream link. Each reader runs independently."),"muted"));
    const QList<QPair<QString,QString>> sources{{"twitch","Twitch"},{"youtube","YouTube"},{"yt_shorts","YouTube Shorts"},{"tiktok","TikTok"},{"kick","Kick"},{"rumble","Rumble"}};
    // A fresh install has every link blank (see SettingsStore::load()) — walk
    // the reader through the first connection instead of leaving four empty
    // cards with no explanation.
    const bool freshInstall = std::all_of(sources.begin(), sources.end(), [this](const auto& source) {
        return controller_->settings()->link(source.first).trimmed().isEmpty();
    });
    if (freshInstall) {
        auto* welcome = new QFrame;
        welcome->setObjectName(QStringLiteral("welcomeCard"));
        sourcesWelcome_ = welcome;
        auto* welcomeLayout = new QVBoxLayout(welcome);
        welcomeLayout->addWidget(label(QStringLiteral("GETTING STARTED"), "cardTitle"));
        auto* steps = label(QStringLiteral(
            "1. Paste a channel or live link below for each community you want to read — leave the rest blank.\n"
            "2. Select Connect on each one you filled in.\n"
            "3. Open Keys to connect Twitch/YouTube moderation access (optional).\n"
            "4. Open Chat Overlay to copy the browser-source URL, or use Open Pop-out Chat in the chat preview."), "muted");
        steps->setWordWrap(true);
        welcomeLayout->addWidget(steps);
        layout->addWidget(welcome);
    }
    for(const auto& source:sources){
        auto* card=new QFrame;card->setProperty("card",true);card->setProperty("platform",source.first);sourceCards_[source.first]=card;auto* cardLayout=new QVBoxLayout(card);
        auto* head=new QHBoxLayout;head->addWidget(label(source.second,"cardTitle"));head->addStretch();
        auto* state=label(QStringLiteral("Offline"),"status");sourceStates_[source.first]=state;head->addWidget(state);cardLayout->addLayout(head);
        auto* row=new QHBoxLayout;auto* entry=new QLineEdit(controller_->settings()->link(source.first));entry->setPlaceholderText(source.first=="twitch"?"twitch.tv/yourname":source.first=="tiktok"?"tiktok.com/@yourname":source.first=="kick"?"kick.com/yourname":source.first=="rumble"?"rumble.com/c/yourchannel":"youtube.com/@yourname");row->addWidget(entry,1);
        auto* connectButton=new QPushButton(QStringLiteral("Connect"));row->addWidget(connectButton);auto* disconnectButton=new QPushButton(QStringLiteral("Disconnect"));row->addWidget(disconnectButton);cardLayout->addLayout(row);
        connect(connectButton,&QPushButton::clicked,this,[this,entry,key=source.first]{controller_->connectSource(key,entry->text());});
        connect(disconnectButton,&QPushButton::clicked,this,[this,key=source.first]{controller_->disconnectSource(key);});
        layout->addWidget(card);
    }
    layout->addStretch();return page;
}

QWidget* MainWindow::buildEventsPage() {
    auto* page = new QWidget;
    auto* outer=new QVBoxLayout(page);outer->setContentsMargins(0,0,0,0);auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto* body=new QWidget;auto* layout = new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->addWidget(label(QStringLiteral("STREAM ALERTS"), "heroTitle"));
    layout->addWidget(label(QStringLiteral("Alerts appear in your chat preview and pop-out without entering the OBS overlay feed."), "muted"));
    auto* card = new QFrame;
    card->setProperty("card", true);
    auto* cardLayout = new QVBoxLayout(card);
    auto* token = new QLineEdit(controller_->settings()->secret(QStringLiteral("streamlabs_socket_token")));
    token->setEchoMode(QLineEdit::Password);
    token->setPlaceholderText(QStringLiteral("Streamlabs Socket API token"));
    cardLayout->addWidget(token);
    auto* buttons = new QHBoxLayout;
    auto* getKey = new QPushButton(QStringLiteral("Get Streamlabs API Key ↗"));
    auto* connectButton = new QPushButton(QStringLiteral("Connect Streamlabs"));
    auto* disconnectButton = new QPushButton(QStringLiteral("Disconnect"));
    buttons->addWidget(getKey); buttons->addWidget(connectButton); buttons->addWidget(disconnectButton);
    cardLayout->addLayout(buttons);
    connect(getKey, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://streamlabs.com/dashboard#/settings/api-settings")));
    });
    connect(connectButton, &QPushButton::clicked, this, [this, token] {
        if (token->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Streamlabs"), QStringLiteral("Paste the Socket API token first."));
            return;
        }
        controller_->connectStreamlabs(token->text());
    });
    connect(disconnectButton, &QPushButton::clicked, controller_, &AppController::disconnectStreamlabs);
    auto* audioDivider = label(QStringLiteral("OPTIONAL STREAMLABS ALERT AUDIO"), "cardTitle");
    cardLayout->addWidget(audioDivider);
    auto* audioEnabled = new QCheckBox(QStringLiteral("Use the sound selected in my Streamlabs Alert Box"));
    audioEnabled->setChecked(controller_->settings()->preference(QStringLiteral("streamlabs_audio_enabled"), false).toBool());
    cardLayout->addWidget(audioEnabled);
    auto* alertUrl = new QLineEdit(controller_->settings()->preference(QStringLiteral("streamlabs_alert_box_url")).toString());
    alertUrl->setPlaceholderText(QStringLiteral("https://streamlabs.com/alert-box/v3/your-widget-token"));
    cardLayout->addWidget(alertUrl);
    auto* audioButtons = new QHBoxLayout;
    auto* openAlertSettings = new QPushButton(QStringLiteral("Open Alert Box settings ↗"));
    auto* saveAudio = new QPushButton(QStringLiteral("Save audio sync"));
    audioButtons->addWidget(openAlertSettings); audioButtons->addWidget(saveAudio);
    cardLayout->addLayout(audioButtons);
    connect(openAlertSettings, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://streamlabs.com/dashboard#/alertbox")));
    });
    const auto applyAudio = [this, audioEnabled, alertUrl] {
        const bool enabled = audioEnabled->isChecked();
        const QUrl url(alertUrl->text().trimmed());
        if (enabled && (!url.isValid() || !url.host().endsWith(QStringLiteral("streamlabs.com")))) {
            QMessageBox::warning(this, QStringLiteral("Streamlabs audio"),
                                 QStringLiteral("Paste a valid Streamlabs Alert Box widget URL first."));
            return;
        }
        controller_->settings()->setPreference(QStringLiteral("streamlabs_audio_enabled"), enabled);
        controller_->settings()->setPreference(QStringLiteral("streamlabs_alert_box_url"), alertUrl->text().trimmed());
        if (popout_) popout_->setStreamlabsAlertAudio(enabled, url);
    };
    connect(saveAudio, &QPushButton::clicked, this, applyAudio);
    connect(audioEnabled, &QCheckBox::toggled, this, [applyAudio](bool) { applyAudio(); });
    layout->addWidget(card);
    auto* rumbleAlerts=new QFrame;rumbleAlerts->setProperty("card",true);auto* rumbleAlertsLayout=new QVBoxLayout(rumbleAlerts);rumbleAlertsLayout->addWidget(label(QStringLiteral("RUMBLE FOLLOW & SUB ALERTS"),"cardTitle"));auto* rumbleAlertHelp=label(QStringLiteral("Rumble followers, subscribers, and gifted subscriptions automatically use the same Leapcast alert display as Streamlabs after you save the Rumble Live Stream API URL under Keys. Existing history is ignored when connecting, so only genuinely new activity creates an alert."),"muted");rumbleAlertHelp->setWordWrap(true);rumbleAlertsLayout->addWidget(rumbleAlertHelp);auto* rumbleAlertState=label(controller_->settings()->secret(QStringLiteral("rumble_api_url")).isEmpty()?QStringLiteral("Not connected — finish Rumble setup under Keys"):QStringLiteral("Ready — Rumble alerts are connected"),"status");rumbleAlertState->setStyleSheet(controller_->settings()->secret(QStringLiteral("rumble_api_url")).isEmpty()?QStringLiteral("color:#f6c85f"):QStringLiteral("color:#85c742"));rumbleAlertsLayout->addWidget(rumbleAlertState);layout->addWidget(rumbleAlerts);
    layout->addStretch();
    return page;
}

QWidget* MainWindow::buildObsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->addWidget(label(QStringLiteral("CHAT OVERLAY"), "heroTitle"));
    layout->addWidget(label(QStringLiteral("Customize and control the browser-source chat shown in OBS."), "muted"));
    const QString url = QStringLiteral("http://127.0.0.1:%1/").arg(overlay_->port());
    auto* urlCard = new QFrame; urlCard->setProperty("card", true);
    auto* urlLayout = new QVBoxLayout(urlCard);
    auto* urlValue = new QLineEdit(url); urlValue->setReadOnly(true); urlLayout->addWidget(urlValue);
    auto* urlButtons = new QHBoxLayout;
    auto* copy = new QPushButton(QStringLiteral("Copy URL"));
    auto* open = new QPushButton(QStringLiteral("Open overlay"));
    auto* test = new QPushButton(QStringLiteral("Test message"));
    auto* clear = new QPushButton(QStringLiteral("Clear overlay"));
    urlButtons->addWidget(copy); urlButtons->addWidget(open); urlButtons->addWidget(test); urlButtons->addWidget(clear);
    urlLayout->addLayout(urlButtons);
    connect(copy, &QPushButton::clicked, this, [url] { QApplication::clipboard()->setText(url); });
    connect(open, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
    connect(test, &QPushButton::clicked, this, [this] {
        ChatMessage message; message.user = QStringLiteral("Leapcast Studio");
        message.text = QStringLiteral("OBS overlay test message"); message.platform = QStringLiteral("twitch");
        message.color = QColor(QStringLiteral("#53cdf3")); overlay_->ingest(message);
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        overlay_->clear();
    });
    layout->addWidget(urlCard);

    auto* overlayAppearance=new QFrame;overlayAppearance->setProperty("card",true);auto* overlayLayout=new QVBoxLayout(overlayAppearance);
    overlayLayout->addWidget(label(QStringLiteral("OVERLAY APPEARANCE"),"cardTitle"));
    overlayLayout->addWidget(label(QStringLiteral("Customize the browser-source background and outline around chat lettering."),"muted"));
    QColor overlayColor(controller_->settings()->preference(QStringLiteral("overlay_background_color"),QStringLiteral("#000000")).toString());if(!overlayColor.isValid())overlayColor=Qt::black;
    auto* colorRow=new QHBoxLayout;colorRow->addWidget(label(QStringLiteral("Background color")));auto* colorButton=new QPushButton(overlayColor.name());colorButton->setStyleSheet(QStringLiteral("background:%1;color:%2;").arg(overlayColor.name(),overlayColor.lightness()<128?QStringLiteral("white"):QStringLiteral("black")));colorRow->addWidget(colorButton);overlayLayout->addLayout(colorRow);
    auto* opacityRow=new QHBoxLayout;opacityRow->addWidget(label(QStringLiteral("Overlay background opacity")));auto* overlayOpacity=new QSlider(Qt::Horizontal);overlayOpacity->setRange(0,100);overlayOpacity->setValue(controller_->settings()->preference(QStringLiteral("overlay_background_opacity"),0).toInt());opacityRow->addWidget(overlayOpacity,1);auto* overlayOpacityValue=label(QString::number(overlayOpacity->value())+QStringLiteral("%"));opacityRow->addWidget(overlayOpacityValue);overlayLayout->addLayout(opacityRow);
    auto* outlineRow=new QHBoxLayout;outlineRow->addWidget(label(QStringLiteral("Chat text outline thickness")));auto* outline=new QSlider(Qt::Horizontal);outline->setRange(0,8);outline->setValue(controller_->settings()->preference(QStringLiteral("chat_outline_thickness"),2).toInt());outlineRow->addWidget(outline,1);auto* outlineValue=label(QString::number(outline->value())+QStringLiteral(" px"));outlineRow->addWidget(outlineValue);overlayLayout->addLayout(outlineRow);
    const auto applyOverlayAppearance=[this,overlayOpacity,outline]{const QColor color(controller_->settings()->preference(QStringLiteral("overlay_background_color"),QStringLiteral("#000000")).toString());overlay_->setAppearance(color,overlayOpacity->value(),outline->value());};
    connect(colorButton,&QPushButton::clicked,this,[this,colorButton,applyOverlayAppearance]{QColor current(controller_->settings()->preference(QStringLiteral("overlay_background_color"),QStringLiteral("#000000")).toString());const QColor chosen=QColorDialog::getColor(current,this,QStringLiteral("Chat overlay background color"));if(!chosen.isValid())return;controller_->settings()->setPreference(QStringLiteral("overlay_background_color"),chosen.name());colorButton->setText(chosen.name());colorButton->setStyleSheet(QStringLiteral("background:%1;color:%2;").arg(chosen.name(),chosen.lightness()<128?QStringLiteral("white"):QStringLiteral("black")));applyOverlayAppearance();});
    connect(overlayOpacity,&QSlider::valueChanged,this,[this,overlayOpacityValue,applyOverlayAppearance](int value){overlayOpacityValue->setText(QString::number(value)+QStringLiteral("%"));controller_->settings()->setPreference(QStringLiteral("overlay_background_opacity"),value);applyOverlayAppearance();});
    connect(outline,&QSlider::valueChanged,this,[this,outlineValue,applyOverlayAppearance](int value){outlineValue->setText(QString::number(value)+QStringLiteral(" px"));controller_->settings()->setPreference(QStringLiteral("chat_outline_thickness"),value);applyOverlayAppearance();});
    layout->addWidget(overlayAppearance);

    auto* fadeCard = new QFrame; fadeCard->setProperty("card", true);
    auto* fadeLayout = new QHBoxLayout(fadeCard);
    auto* copyText = new QVBoxLayout;
    copyText->addWidget(label(QStringLiteral("MESSAGE FADE TIMER"), "cardTitle"));
    copyText->addWidget(label(QStringLiteral("How long each chat message remains visible in the OBS overlay."), "muted"));
    fadeLayout->addLayout(copyText, 1);
    auto* seconds = new QSpinBox;
    seconds->setRange(0, 3600);
    seconds->setSuffix(QStringLiteral(" seconds"));
    seconds->setSpecialValueText(QStringLiteral("Never fade"));
    seconds->setValue(overlay_->fadeSeconds());
    fadeLayout->addWidget(seconds);
    connect(seconds, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        overlay_->setFadeSeconds(value);
        controller_->settings()->setPreference(QStringLiteral("overlay_fade_seconds"), value);
    });
    layout->addWidget(fadeCard);

    layout->addStretch();
    return page;
}

QWidget* MainWindow::buildPhoneConnectPage(){
    auto* page=new QWidget;
    auto* layout=new QVBoxLayout(page);layout->setContentsMargins(8,4,8,8);layout->setSpacing(10);
    layout->addWidget(label(QStringLiteral("PHONE CONNECT"),"heroTitle"));
    auto* intro=label(QStringLiteral("Optional iPhone companion for keeping combined chat, viewer counts, and essential moderation controls close while you stream."),"muted");intro->setWordWrap(true);layout->addWidget(intro);

    auto* content=new QWidget;auto* contentLayout=new QVBoxLayout(content);contentLayout->setContentsMargins(0,0,0,0);contentLayout->setSpacing(10);
    auto* connectCard=new QFrame;connectCard->setProperty("card",true);auto* connectLayout=new QVBoxLayout(connectCard);connectLayout->addWidget(label(QStringLiteral("CONNECT THIS IPHONE"),"cardTitle"));
    auto* connectHelp=label(QStringLiteral("Keep Leapcast Studio open on this PC. Connect the iPhone to the same private Wi-Fi, then scan the QR code or open the private link in Safari."),"muted");connectHelp->setWordWrap(true);connectLayout->addWidget(connectHelp);
    auto* connectionStatus=label(QString(),"status");
    const QUrl mobileUrl=overlay_->mobileUrl();auto* link=new QLineEdit(mobileUrl.isValid()?mobileUrl.toString():QStringLiteral("Connect this PC to Wi-Fi or Ethernet, then restart Leapcast Studio."));link->setReadOnly(true);connectLayout->addWidget(link);
    auto* qrRow=new QHBoxLayout;auto* qrImage=new QLabel;qrImage->setFixedSize(252,252);qrImage->setAlignment(Qt::AlignCenter);qrImage->setStyleSheet(QStringLiteral("background:white;border:8px solid white;border-radius:14px;"));qrImage->setPixmap(phoneQrCode(mobileUrl).scaled(236,236,Qt::KeepAspectRatio,Qt::FastTransformation));auto* qrText=new QVBoxLayout;qrText->addWidget(label(QStringLiteral("SCAN WITH IPHONE CAMERA"),"cardTitle"));auto* qrHelp=label(QStringLiteral("Point the iPhone Camera at this code and tap the Leapcast banner. Reset it before showcases or immediately if the link may have leaked."),"muted");qrHelp->setWordWrap(true);qrText->addWidget(qrHelp);auto* rotate=new QPushButton(QStringLiteral("Reset / Generate New QR Code"));rotate->setToolTip(QStringLiteral("Creates a new private link and immediately invalidates every previous QR code and copied link."));qrText->addWidget(rotate);qrText->addStretch();qrRow->addWidget(qrImage);qrRow->addLayout(qrText,1);connectLayout->addLayout(qrRow);
    auto* actions=new QGridLayout;auto* copy=new QPushButton(QStringLiteral("Copy iPhone Link"));auto* preview=new QPushButton(QStringLiteral("Open Phone Preview"));auto* install=new QPushButton(QStringLiteral("Copy Install Link"));copy->setEnabled(mobileUrl.isValid());preview->setEnabled(mobileUrl.isValid());install->setEnabled(mobileUrl.isValid());rotate->setEnabled(mobileUrl.isValid());actions->addWidget(copy,0,0);actions->addWidget(preview,0,1);actions->addWidget(install,1,0,1,2);connectLayout->addLayout(actions);
    connectLayout->addWidget(connectionStatus);
    connect(copy,&QPushButton::clicked,this,[link,connectionStatus]{QApplication::clipboard()->setText(link->text());connectionStatus->setText(QStringLiteral("Private iPhone link copied."));});
    connect(install,&QPushButton::clicked,this,[link,connectionStatus]{QApplication::clipboard()->setText(link->text());connectionStatus->setText(QStringLiteral("Install link copied. Open it in Safari, then use Add to Home Screen."));});
    connect(preview,&QPushButton::clicked,this,[link]{QDesktopServices::openUrl(QUrl(link->text()));});
    connect(rotate,&QPushButton::clicked,this,[this,link,qrImage,connectionStatus]{if(QMessageBox::question(this,QStringLiteral("Generate new QR code?"),QStringLiteral("The current Phone Connect link and any existing Home Screen icon will stop working. Continue?"))!=QMessageBox::Yes)return;controller_->settings()->setSecret(QStringLiteral("mobile_companion_token"),overlay_->regenerateMobileToken());const QUrl fresh=overlay_->mobileUrl();link->setText(fresh.toString());qrImage->setPixmap(phoneQrCode(fresh).scaled(236,236,Qt::KeepAspectRatio,Qt::FastTransformation));connectionStatus->setText(QStringLiteral("New private QR code created. The previous link is no longer valid."));});contentLayout->addWidget(connectCard);

    auto* stepsCard=new QFrame;stepsCard->setProperty("card",true);auto* steps=new QVBoxLayout(stepsCard);steps->addWidget(label(QStringLiteral("INSTALL AS AN IPHONE APP"),"cardTitle"));auto* stepText=label(QStringLiteral("1. Scan the QR code or open the copied link in Safari.\n2. Confirm chat and viewer counts connect.\n3. Tap Safari's Share button.\n4. Choose Add to Home Screen.\n5. Keep Open as Web App enabled, then tap Add."),"muted");stepText->setWordWrap(true);steps->addWidget(stepText);contentLayout->addWidget(stepsCard);

    auto* requirementsCard=new QFrame;requirementsCard->setProperty("card",true);auto* requirements=new QVBoxLayout(requirementsCard);requirements->addWidget(label(QStringLiteral("IPHONE REQUIREMENTS"),"cardTitle"));auto* requirementsText=label(QStringLiteral("• iPhone only — iPad, Mac, Android, and Apple TV are not enabled for this test.\n• iOS 16.4 or newer recommended; iOS 17 or newer provides the best Home Screen web-app experience.\n• iPhone 8 / iPhone X generation or newer; fully optimized for iPhone 16 and Dynamic Island layouts.\n• Safari must be available to perform Add to Home Screen.\n• The iPhone and Windows 10/11 PC must use the same private Wi-Fi network.\n• Leapcast Studio must stay open on the PC while Phone Connect is used.\n• Allow Leapcast through Windows Firewall for Private networks.\n• Platform moderation must already be connected and authorized inside Leapcast Studio."),"muted");requirementsText->setWordWrap(true);requirements->addWidget(requirementsText);contentLayout->addWidget(requirementsCard);

    auto* privacyCard=new QFrame;privacyCard->setProperty("card",true);auto* privacy=new QVBoxLayout(privacyCard);privacy->addWidget(label(QStringLiteral("LOCAL & OPTIONAL"),"cardTitle"));auto* privacyText=label(QStringLiteral("Phone Connect is optional. It does not upload the mobile page to a public host. The private link works only while Leapcast Studio is running and should not be shared outside your trusted local network."),"muted");privacyText->setWordWrap(true);privacy->addWidget(privacyText);contentLayout->addWidget(privacyCard);contentLayout->addStretch();auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setWidget(content);layout->addWidget(scroll,1);

    return page;
}

QWidget* MainWindow::buildSettingsPage(){
    auto* page=new QWidget;auto* layout=new QVBoxLayout(page);layout->setContentsMargins(8,4,8,8);layout->setSpacing(7);
    layout->addWidget(label(QStringLiteral("SETTINGS"),"heroTitle"));

    auto* appearance=new QFrame;appearance->setProperty("card",true);auto* appearanceLayout=new QVBoxLayout(appearance);
    appearanceLayout->addWidget(label(QStringLiteral("APPEARANCE"),"cardTitle"));
    auto* fontRow=new QHBoxLayout;fontRow->addWidget(label(QStringLiteral("Program font")));
    auto* fonts=new QFontComboBox;fonts->setCurrentFont(QFont(controller_->settings()->preference("ui_font_family","Segoe UI").toString()));fontRow->addWidget(fonts,1);
    auto* fontSize=new QSpinBox;fontSize->setRange(8,20);fontSize->setSuffix(QStringLiteral(" pt"));fontSize->setValue(controller_->settings()->preference("ui_font_size",10).toInt());fontRow->addWidget(fontSize);appearanceLayout->addLayout(fontRow);
    auto* scaleRow=new QHBoxLayout;scaleRow->addWidget(label(QStringLiteral("Button and control size")));
    auto* controlScale=new QSlider(Qt::Horizontal);controlScale->setRange(80,160);controlScale->setValue(controller_->settings()->preference("ui_control_scale",100).toInt());scaleRow->addWidget(controlScale,1);
    auto* scaleValue=label(QString::number(controlScale->value())+QStringLiteral("%"));scaleRow->addWidget(scaleValue);appearanceLayout->addLayout(scaleRow);
    connect(fonts,&QFontComboBox::currentFontChanged,this,[this](const QFont&font){controller_->settings()->setPreference("ui_font_family",font.family());applyTheme();});
    connect(fontSize,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){controller_->settings()->setPreference("ui_font_size",value);applyTheme();});
    connect(controlScale,&QSlider::valueChanged,this,[this,scaleValue](int value){scaleValue->setText(QString::number(value)+"%");controller_->settings()->setPreference("ui_control_scale",value);applyTheme();});
    layout->addWidget(appearance);

    auto* popoutAppearance=new QFrame;popoutAppearance->setProperty("card",true);auto* popoutLayout=new QVBoxLayout(popoutAppearance);
    popoutLayout->addWidget(label(QStringLiteral("POP-OUT CHAT APPEARANCE"),"cardTitle"));
    const QString savedPopoutFont=controller_->settings()->preference(QStringLiteral("popout_font_family"),QStringLiteral("Segoe UI")).toString();
    auto* popoutFontRow=new QHBoxLayout;popoutFontRow->addWidget(label(QStringLiteral("Font")));auto* popoutFont=new QFontComboBox;popoutFont->setCurrentFont(QFont(savedPopoutFont));popoutFontRow->addWidget(popoutFont,1);popoutFontRow->addWidget(label(QStringLiteral("Size")));auto* popoutSize=new QSlider(Qt::Horizontal);popoutSize->setRange(8,36);popoutSize->setValue(controller_->settings()->preference(QStringLiteral("popout_font_size"),12).toInt());popoutFontRow->addWidget(popoutSize,1);auto* popoutSizeValue=label(QString::number(popoutSize->value())+QStringLiteral(" pt"));popoutFontRow->addWidget(popoutSizeValue);popoutLayout->addLayout(popoutFontRow);
    auto* popoutOutlineRow=new QHBoxLayout;popoutOutlineRow->addWidget(label(QStringLiteral("Outline")));auto* popoutOutline=new QSlider(Qt::Horizontal);popoutOutline->setRange(0,8);popoutOutline->setValue(controller_->settings()->preference(QStringLiteral("popout_outline_thickness"),2).toInt());popoutOutlineRow->addWidget(popoutOutline,1);auto* popoutOutlineValue=label(QString::number(popoutOutline->value())+QStringLiteral(" px"));popoutOutlineRow->addWidget(popoutOutlineValue);popoutOutlineRow->addWidget(label(QStringLiteral("Name colors")));auto* colourMode=new QComboBox;colourMode->addItem(QStringLiteral("Random per chatter"),QStringLiteral("random"));colourMode->addItem(QStringLiteral("One color"),QStringLiteral("single"));colourMode->addItem(QStringLiteral("Custom gradient"),QStringLiteral("gradient"));colourMode->addItem(QStringLiteral("Repeating pattern"),QStringLiteral("pattern"));const int colourModeIndex=colourMode->findData(controller_->settings()->preference(QStringLiteral("chat_colour_mode"),QStringLiteral("random")));colourMode->setCurrentIndex(qMax(0,colourModeIndex));popoutOutlineRow->addWidget(colourMode,1);popoutLayout->addLayout(popoutOutlineRow);
    QColor nameColour(controller_->settings()->preference(QStringLiteral("chat_name_colour"),QStringLiteral("#53cdf3")).toString());if(!nameColour.isValid())nameColour=QColor(QStringLiteral("#53cdf3"));
    auto* nameColourRow=new QHBoxLayout;nameColourRow->addWidget(label(QStringLiteral("Specific name color")));auto* nameColourButton=new QPushButton(nameColour.name());nameColourButton->setStyleSheet(QStringLiteral("background:%1;color:%2;").arg(nameColour.name(),nameColour.lightness()<128?QStringLiteral("white"):QStringLiteral("black")));nameColourButton->setEnabled(colourMode->currentData().toString()==QStringLiteral("single"));nameColourRow->addWidget(nameColourButton);popoutLayout->addLayout(nameColourRow);
    QStringList namePalette=controller_->settings()->preference(QStringLiteral("chat_name_palette"),QStringList{QStringLiteral("#6c4cff"),QStringLiteral("#18dfd1"),QStringLiteral("#ff5ca8"),QStringLiteral("#ffd166")}).toStringList();while(namePalette.size()<4)namePalette<<QStringLiteral("#ffffff");
    auto* paletteRow=new QHBoxLayout;auto* paletteLabel=label(QStringLiteral("Multi-color palette"));paletteRow->addWidget(paletteLabel);QList<QPushButton*> paletteButtons;for(int index=0;index<4;++index){QColor color(namePalette.at(index));if(!color.isValid())color=Qt::white;auto*button=new QPushButton(QString::number(index+1));button->setToolTip(QStringLiteral("Choose palette color %1").arg(index+1));button->setStyleSheet(QStringLiteral("background:%1;color:%2;min-width:52px;").arg(color.name(),color.lightness()<128?QStringLiteral("white"):QStringLiteral("black")));paletteButtons<<button;paletteRow->addWidget(button);connect(button,&QPushButton::clicked,this,[this,button,index]{QStringList palette=controller_->settings()->preference(QStringLiteral("chat_name_palette"),QStringList{QStringLiteral("#6c4cff"),QStringLiteral("#18dfd1"),QStringLiteral("#ff5ca8"),QStringLiteral("#ffd166")}).toStringList();while(palette.size()<4)palette<<QStringLiteral("#ffffff");QColor current(palette.at(index));const QColor chosen=QColorDialog::getColor(current,this,QStringLiteral("Name palette color %1").arg(index+1));if(!chosen.isValid())return;palette[index]=chosen.name();controller_->settings()->setPreference(QStringLiteral("chat_name_palette"),palette);button->setStyleSheet(QStringLiteral("background:%1;color:%2;min-width:52px;").arg(chosen.name(),chosen.lightness()<128?QStringLiteral("white"):QStringLiteral("black")));});}auto*patternStyle=new QComboBox;patternStyle->addItem(QStringLiteral("Repeat"),QStringLiteral("repeat"));patternStyle->addItem(QStringLiteral("Mirror"),QStringLiteral("mirror"));patternStyle->addItem(QStringLiteral("Color blocks"),QStringLiteral("blocks"));patternStyle->setCurrentIndex(qMax(0,patternStyle->findData(controller_->settings()->preference(QStringLiteral("chat_name_pattern"),QStringLiteral("repeat")))));paletteRow->addWidget(patternStyle);popoutLayout->addLayout(paletteRow);
    auto* randomizePalette=new QCheckBox(QStringLiteral("Unique randomized palette variation per chatter"));randomizePalette->setChecked(controller_->settings()->preference(QStringLiteral("chat_palette_randomize"),false).toBool());popoutLayout->addWidget(randomizePalette);auto* paletteActionRow=new QHBoxLayout;auto* editPalette=new QPushButton(QStringLiteral("Edit custom palette (2–8 colors)"));auto* rerollPalette=new QPushButton(QStringLiteral("Randomize palette for chatters"));paletteActionRow->addWidget(editPalette,1);paletteActionRow->addWidget(rerollPalette,1);popoutLayout->addLayout(paletteActionRow);
    connect(editPalette,&QPushButton::clicked,this,[this,paletteButtons]{QStringList palette=controller_->settings()->preference(QStringLiteral("chat_name_palette"),QStringList{QStringLiteral("#6c4cff"),QStringLiteral("#18dfd1"),QStringLiteral("#ff5ca8"),QStringLiteral("#ffd166")}).toStringList();bool ok=false;const QString text=QInputDialog::getMultiLineText(this,QStringLiteral("Custom name palette"),QStringLiteral("One hex color per line (2–8 colors). Reorder the lines to reorder the palette."),palette.join(QLatin1Char('\n')),&ok);if(!ok)return;QStringList next;for(QString value:text.split(QRegularExpression(QStringLiteral("[\r\n,]+")),Qt::SkipEmptyParts)){value=value.trimmed();QColor c(value);if(c.isValid())next<<c.name();}if(next.size()<2||next.size()>8){QMessageBox::warning(this,QStringLiteral("Custom palette"),QStringLiteral("Choose between 2 and 8 valid colors."));return;}controller_->settings()->setPreference(QStringLiteral("chat_name_palette"),next);controller_->regenerateNameColours();for(int i=0;i<paletteButtons.size();++i){QColor c(i<next.size()?next.at(i):QStringLiteral("#ffffff"));paletteButtons[i]->setStyleSheet(QStringLiteral("background:%1;color:%2;min-width:52px;").arg(c.name(),c.lightness()<128?QStringLiteral("white"):QStringLiteral("black")));}});
    connect(randomizePalette,&QCheckBox::toggled,this,[this](bool enabled){controller_->settings()->setPreference(QStringLiteral("chat_palette_randomize"),enabled);controller_->regenerateNameColours();});
    connect(rerollPalette,&QPushButton::clicked,this,[this,randomizePalette]{if(!randomizePalette->isChecked())randomizePalette->setChecked(true);controller_->regenerateNameColours();});
    auto* programIcons=new QCheckBox(QStringLiteral("Show platform icons in program chat and pop-out"));programIcons->setChecked(controller_->settings()->preference(QStringLiteral("program_popout_show_platform_icons"),true).toBool());popoutLayout->addWidget(programIcons);
    auto* overlayIcons=new QCheckBox(QStringLiteral("Show platform icons in OBS overlay"));overlayIcons->setChecked(controller_->settings()->preference(QStringLiteral("overlay_show_platform_icons"),true).toBool());popoutLayout->addWidget(overlayIcons);
    auto* pinnedMessages=new QCheckBox(QStringLiteral("Show pinned messages for Twitch and YouTube/Shorts (📌)"));pinnedMessages->setChecked(controller_->settings()->preference(QStringLiteral("show_pinned_messages"),true).toBool());popoutLayout->addWidget(pinnedMessages);
    auto* tiktokActivity=new QCheckBox(QStringLiteral("Show TikTok joins, follows, and likes in pop-out only"));tiktokActivity->setChecked(controller_->settings()->preference(QStringLiteral("tiktok_popout_activity_enabled"),false).toBool());popoutLayout->addWidget(tiktokActivity);
    const auto applyPopoutAppearance=[this,popoutFont,popoutSize,popoutOutline]{if(popout_)popout_->setAppearance(QColor(controller_->settings()->preference(QStringLiteral("overlay_background_color"),QStringLiteral("#000000")).toString()),popoutOutline->value(),popoutFont->currentFont().family(),popoutSize->value());};
    connect(popoutFont,&QFontComboBox::currentFontChanged,this,[this,applyPopoutAppearance](const QFont&font){controller_->settings()->setPreference(QStringLiteral("popout_font_family"),font.family());applyPopoutAppearance();});
    connect(popoutSize,&QSlider::valueChanged,this,[this,popoutSizeValue,applyPopoutAppearance](int value){popoutSizeValue->setText(QString::number(value)+QStringLiteral(" pt"));controller_->settings()->setPreference(QStringLiteral("popout_font_size"),value);applyPopoutAppearance();});
    connect(popoutOutline,&QSlider::valueChanged,this,[this,popoutOutlineValue,applyPopoutAppearance](int value){popoutOutlineValue->setText(QString::number(value)+QStringLiteral(" px"));controller_->settings()->setPreference(QStringLiteral("popout_outline_thickness"),value);applyPopoutAppearance();});
    const auto updateColourControls=[colourMode,nameColourButton,paletteLabel,paletteButtons,patternStyle]{const QString mode=colourMode->currentData().toString();nameColourButton->setEnabled(mode==QStringLiteral("single"));const bool multi=mode==QStringLiteral("gradient")||mode==QStringLiteral("pattern");paletteLabel->setEnabled(multi);for(auto*button:paletteButtons)button->setEnabled(multi);patternStyle->setEnabled(mode==QStringLiteral("pattern"));};updateColourControls();
    connect(colourMode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,colourMode,updateColourControls](int){controller_->settings()->setPreference(QStringLiteral("chat_colour_mode"),colourMode->currentData().toString());updateColourControls();});
    connect(patternStyle,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,patternStyle](int){controller_->settings()->setPreference(QStringLiteral("chat_name_pattern"),patternStyle->currentData().toString());});
    connect(nameColourButton,&QPushButton::clicked,this,[this,nameColourButton]{QColor current(controller_->settings()->preference(QStringLiteral("chat_name_colour"),QStringLiteral("#53cdf3")).toString());const QColor chosen=QColorDialog::getColor(current,this,QStringLiteral("Chat name color"));if(!chosen.isValid())return;controller_->settings()->setPreference(QStringLiteral("chat_name_colour"),chosen.name());nameColourButton->setText(chosen.name());nameColourButton->setStyleSheet(QStringLiteral("background:%1;color:%2;").arg(chosen.name(),chosen.lightness()<128?QStringLiteral("white"):QStringLiteral("black")));});
    connect(programIcons,&QCheckBox::toggled,this,[this](bool enabled){controller_->settings()->setPreference(QStringLiteral("program_popout_show_platform_icons"),enabled);if(popout_)popout_->setShowPlatformIcons(enabled);});
    connect(overlayIcons,&QCheckBox::toggled,this,[this](bool enabled){controller_->settings()->setPreference(QStringLiteral("overlay_show_platform_icons"),enabled);overlay_->setShowPlatformIcons(enabled);});
    connect(pinnedMessages,&QCheckBox::toggled,this,[this](bool enabled){controller_->setPinnedMessagesEnabled(enabled);});
    connect(tiktokActivity,&QCheckBox::toggled,this,[this](bool enabled){controller_->settings()->setPreference(QStringLiteral("tiktok_popout_activity_enabled"),enabled);});
    layout->addWidget(popoutAppearance);

    auto* seasonal=new QFrame;seasonal->setProperty("card",true);auto* seasonalLayout=new QVBoxLayout(seasonal);seasonalLayout->addWidget(label(QStringLiteral("SEASONAL & BIRTHDAY EFFECTS"),"cardTitle"));
    auto* seasonalEnabled=new QCheckBox(QStringLiteral("Enable seasonal and holiday effects"));seasonalEnabled->setChecked(controller_->settings()->preference(QStringLiteral("seasonal_effects_enabled"),true).toBool());seasonalLayout->addWidget(seasonalEnabled);
    auto* birthdayEnabled=new QCheckBox(QStringLiteral("Birthday effects — confetti, party hats, balloons, sparkles, and streamers"));birthdayEnabled->setChecked(controller_->settings()->preference(QStringLiteral("birthday_effects_enabled"),true).toBool());seasonalLayout->addWidget(birthdayEnabled);
    auto* birthdayPlaybackRow=new QHBoxLayout;birthdayPlaybackRow->addWidget(label(QStringLiteral("Birthday confetti playback")));auto* birthdayPlayback=new QComboBox;birthdayPlayback->addItem(QStringLiteral("Every launch during birthday window"),QStringLiteral("always"));birthdayPlayback->addItem(QStringLiteral("First launch only (once per day)"),QStringLiteral("first"));birthdayPlayback->setCurrentIndex(qMax(0,birthdayPlayback->findData(controller_->settings()->preference(QStringLiteral("birthday_confetti_playback"),QStringLiteral("always")).toString())));birthdayPlaybackRow->addWidget(birthdayPlayback,1);seasonalLayout->addLayout(birthdayPlaybackRow);
    auto* birthdayRow=new QHBoxLayout;birthdayRow->addWidget(label(QStringLiteral("Birthday (month / day)")));auto* birthdayMonth=new QComboBox;birthdayMonth->addItem(QStringLiteral("Not set"),0);for(int month=1;month<=12;++month)birthdayMonth->addItem(QLocale().monthName(month,QLocale::LongFormat),month);birthdayMonth->setCurrentIndex(qMax(0,birthdayMonth->findData(controller_->settings()->preference(QStringLiteral("birthday_month"),0))));birthdayRow->addWidget(birthdayMonth,1);auto* birthdayDay=new QSpinBox;birthdayDay->setRange(0,31);birthdayDay->setSpecialValueText(QStringLiteral("Day"));birthdayDay->setValue(controller_->settings()->preference(QStringLiteral("birthday_day"),0).toInt());birthdayRow->addWidget(birthdayDay);seasonalLayout->addLayout(birthdayRow);
    auto* birthdayHelp=label(QStringLiteral("Birthday effects are date-only: they play on the day before and on the saved birthday, after Leapcast verifies the PC's local system date. 'Every launch' replays the party animation once each time Leapcast is reopened during that window; 'First launch only' limits it to once per active birthday date. Holiday themes are automatic: Halloween Oct 15–31, Christmas Dec 10–28, Easter weekend, July 4, Veterans Day Nov 11, and New Year's Eve/Day. The New Year ball drop is limited to the first minutes after local midnight on Jan 1."),"muted");birthdayHelp->setWordWrap(true);seasonalLayout->addWidget(birthdayHelp);
    connect(seasonalEnabled,&QCheckBox::toggled,this,[this](bool enabled){controller_->settings()->setPreference(QStringLiteral("seasonal_effects_enabled"),enabled);updateSeasonalEffects(false);});
    connect(birthdayEnabled,&QCheckBox::toggled,this,[this](bool enabled){controller_->settings()->setPreference(QStringLiteral("birthday_effects_enabled"),enabled);updateSeasonalEffects(false);});
    connect(birthdayPlayback,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,birthdayPlayback](int){controller_->settings()->setPreference(QStringLiteral("birthday_confetti_playback"),birthdayPlayback->currentData().toString());syncMobileSettings();});
    connect(birthdayMonth,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,birthdayMonth](int){controller_->settings()->setPreference(QStringLiteral("birthday_month"),birthdayMonth->currentData().toInt());updateSeasonalEffects(false);});
    connect(birthdayDay,qOverload<int>(&QSpinBox::valueChanged),this,[this](int day){controller_->settings()->setPreference(QStringLiteral("birthday_day"),day);updateSeasonalEffects(false);});
    layout->addWidget(seasonal);

    auto* navigation=new QFrame;navigation->setProperty("card",true);auto* navSettings=new QHBoxLayout(navigation);
    navSettings->addWidget(label(QStringLiteral("SIDEBAR ORDER"),"cardTitle"));navSettings->addStretch();
    auto* navChoice=new QComboBox;for(const auto&key:navigationOrder_)navChoice->addItem(key==QStringLiteral("obs")?QStringLiteral("Chat Overlay"):key==QStringLiteral("phone")?QStringLiteral("Phone Connect"):key.left(1).toUpper()+key.mid(1),key);navSettings->addWidget(navChoice);
    auto* up=new QPushButton(QStringLiteral("Move up"));auto* down=new QPushButton(QStringLiteral("Move down"));navSettings->addWidget(up);navSettings->addWidget(down);
    connect(up,&QPushButton::clicked,this,[this,navChoice]{moveNavigationButton(navChoice->currentData().toString(),-1);});
    connect(down,&QPushButton::clicked,this,[this,navChoice]{moveNavigationButton(navChoice->currentData().toString(),1);});layout->addWidget(navigation);

    auto* platforms=new QFrame;platforms->setProperty("card",true);auto* platformLayout=new QGridLayout(platforms);platformLayout->addWidget(label(QStringLiteral("VISIBLE PLATFORMS"),"cardTitle"),0,0,1,3);
    const QList<QPair<QString,QString>> options{{"twitch","Twitch"},{"youtube","YouTube"},{"yt_shorts","YouTube Shorts"},{"tiktok","TikTok"},{"kick","Kick"},{"rumble","Rumble"}};
    int platformIndex=0;for(const auto&option:options){auto*box=new QCheckBox(option.second);box->setChecked(controller_->settings()->enabled(option.first));platformLayout->addWidget(box,1+platformIndex/3,platformIndex%3);++platformIndex;connect(box,&QCheckBox::toggled,this,[this,key=option.first](bool enabled){controller_->settings()->setEnabled(key,enabled);if(enabled){const QString link=controller_->settings()->link(key);if(!link.isEmpty())controller_->connectSource(key,link);}else controller_->disconnectSource(key);applyPlatformVisibility();});}
    layout->addWidget(platforms);

    auto* updateCard = new QFrame; updateCard->setProperty("card", true);
    auto* updateLayout = new QHBoxLayout(updateCard);
    auto* updateText = new QVBoxLayout;
    updateText->addWidget(label(QStringLiteral("AUTOMATIC UPDATES"), "cardTitle"));
    updateLayout->addLayout(updateText, 1);
    auto* releases = new QPushButton(QStringLiteral("Open Releases"));
    auto* checkNow = new QPushButton(QStringLiteral("Check now"));
    updateLayout->addWidget(releases); updateLayout->addWidget(checkNow);
    connect(releases, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/reallefroge/LeapCast/releases")));
    });
    connect(checkNow, &QPushButton::clicked, this, [this] { silentUpdateCheck_=false;updater_->check(true); });
    layout->addWidget(updateCard);
    layout->addStretch();
    return page;
}

void MainWindow::moveNavigationButton(const QString&key,int direction){
    const int from=navigationOrder_.indexOf(key),to=from+direction;if(from<0||to<0||to>=navigationOrder_.size())return;
    navigationOrder_.move(from,to);controller_->settings()->setPreference("navigation_order",navigationOrder_);
    for(const auto&ordered:navigationOrder_)if(navigationButtons_.contains(ordered))navigationLayout_->removeWidget(navigationButtons_[ordered]);
    int position=2;for(const auto&ordered:navigationOrder_)if(navigationButtons_.contains(ordered))navigationLayout_->insertWidget(position++,navigationButtons_[ordered]);
}

void MainWindow::applyPlatformVisibility(){
    for(const auto&key:{QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("yt_shorts"),QStringLiteral("tiktok"),QStringLiteral("kick"),QStringLiteral("rumble")}){
        const bool enabled=controller_->settings()->enabled(key);if(sourceCards_.contains(key))sourceCards_[key]->setVisible(enabled);if(chatTabs_&&platformChatWidgets_.contains(key)){const int index=chatTabs_->indexOf(platformChatWidgets_[key]);if(index>=0)chatTabs_->setTabVisible(index,enabled);}
    }
    for(auto*widget:findChildren<QWidget*>()){
        const QString key=widget->property("platform").toString();
        if(!key.isEmpty())widget->setVisible(controller_->settings()->enabled(key));
    }
    if(moderationTabs_){
        moderationTabs_->setTabVisible(0,controller_->settings()->enabled("twitch"));
        moderationTabs_->setTabVisible(1,controller_->settings()->enabled("youtube"));
        moderationTabs_->setTabVisible(2,controller_->settings()->enabled("twitch"));
    }
}

QWidget* MainWindow::buildBansPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->setSpacing(12);
    auto* hero = new QFrame;
    hero->setObjectName(QStringLiteral("bansHero"));
    auto* heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(18, 16, 18, 16);
    heroLayout->addWidget(label(QStringLiteral("BANS & TIMEOUTS"), "heroTitle"));
    auto* help = label(QStringLiteral("Review active Twitch restrictions and moderation actions created through Leapcast Studio."), "muted");
    help->setWordWrap(true);
    heroLayout->addWidget(help);
    layout->addWidget(hero);
    auto* tabs = new QTabWidget;moderationTabs_=tabs;
    tabs->setObjectName(QStringLiteral("moderationTabs"));
    const auto makePage = [this](const QString& platform, QListWidget** output) {
        auto* host = new QWidget; auto* hostLayout = new QVBoxLayout(host);
        hostLayout->setContentsMargins(14, 14, 14, 14);
        hostLayout->setSpacing(10);
        auto* list = new QListWidget;
        list->setObjectName(QStringLiteral("restrictionList"));
        list->setSpacing(6);
        *output = list; hostLayout->addWidget(list, 1);
        auto* controls = new QHBoxLayout;
        auto* refresh = new QPushButton(QStringLiteral("Refresh list"));
        auto* details = new QPushButton(QStringLiteral("View details"));
        auto* unban = new QPushButton(QStringLiteral("Unban"));
        unban->setProperty("danger", true);
        controls->addWidget(refresh); controls->addWidget(details); controls->addStretch(); controls->addWidget(unban);
        hostLayout->addLayout(controls);
        // The same button removes both permanent bans and timed-out restrictions;
        // relabel it to match whichever kind is currently selected (isTimeout is
        // stashed at UserRole+2 when the list is populated).
        connect(list, &QListWidget::currentItemChanged, this, [unban](QListWidgetItem* current, QListWidgetItem*) {
            unban->setText(current && current->data(Qt::UserRole + 2).toBool()
                ? QStringLiteral("Untimeout") : QStringLiteral("Unban"));
        });
        connect(refresh, &QPushButton::clicked, this, [this, platform] { controller_->refreshBans(platform); });
        connect(details, &QPushButton::clicked, this, [this, list] {
            if (auto* item = list->currentItem()) {
                const QString details = item->data(Qt::UserRole + 1).toString();
                if (!details.isEmpty()) QMessageBox::information(this, QStringLiteral("Restriction details"), details);
            }
        });
        connect(unban, &QPushButton::clicked, this, [this, list, platform, unban] {
            auto* item = list->currentItem(); if (!item) return;
            const QString id = item->data(Qt::UserRole).toString(); if (id.isEmpty()) return;
            const QString action = unban->text();
            if (QMessageBox::question(this, action,
                    QStringLiteral("%1 the selected restriction?").arg(action)) != QMessageBox::Yes) return;
            if (platform == QStringLiteral("twitch")) controller_->unbanTwitch(id);
            else controller_->unbanYouTube(id);
        });
        return host;
    };
    tabs->addTab(makePage(QStringLiteral("twitch"), &twitchBans_), QStringLiteral("Twitch"));
    tabs->addTab(makePage(QStringLiteral("youtube"), &youtubeBans_), QStringLiteral("YouTube"));
    auto* appealsPage=new QWidget;auto* appealsLayout=new QVBoxLayout(appealsPage);
    appealsLayout->setContentsMargins(14,14,14,14);appealsLayout->setSpacing(10);
    auto* appealsIntro=label(QStringLiteral("Pending Twitch ban appeals appear here. Select an appeal to review every stored chat message from that account."),"muted");
    appealsIntro->setWordWrap(true);appealsLayout->addWidget(appealsIntro);
    auto* appealSplit=new QSplitter(Qt::Horizontal);
    twitchAppeals_=new QListWidget;twitchAppeals_->setObjectName(QStringLiteral("restrictionList"));twitchAppeals_->setSpacing(6);
    twitchAppealHistory_=new QTextBrowser;twitchAppealHistory_->setPlaceholderText(QStringLiteral("Select an appeal to view all-time chat logs."));
    appealSplit->addWidget(twitchAppeals_);appealSplit->addWidget(twitchAppealHistory_);appealSplit->setStretchFactor(0,2);appealSplit->setStretchFactor(1,3);
    appealsLayout->addWidget(appealSplit,1);
    auto* appealControls=new QHBoxLayout;auto* refreshAppeals=new QPushButton(QStringLiteral("Refresh appeals"));
    auto*approveAppeal=new QPushButton(QStringLiteral("Approve Appeal"));approveAppeal->setProperty("primary",true);
    auto*denyAppeal=new QPushButton(QStringLiteral("Deny Appeal"));
    appealControls->addWidget(refreshAppeals);appealControls->addStretch();appealControls->addWidget(denyAppeal);appealControls->addWidget(approveAppeal);appealsLayout->addLayout(appealControls);
    connect(refreshAppeals,&QPushButton::clicked,this,[this]{controller_->refreshTwitchAppeals();});
    connect(approveAppeal,&QPushButton::clicked,this,[this]{reviewTwitchAppeal(true);});
    connect(denyAppeal,&QPushButton::clicked,this,[this]{reviewTwitchAppeal(false);});
    connect(twitchAppeals_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem*item,QListWidgetItem*){
        if(!item||!(item->flags()&Qt::ItemIsEnabled)){if(twitchAppealHistory_)twitchAppealHistory_->clear();return;}
        controller_->loadTwitchUserHistory(item->data(Qt::UserRole).toString(),item->data(Qt::UserRole+1).toString());
    });
    tabs->addTab(appealsPage,QStringLiteral("Twitch Appeals"));
    layout->addWidget(tabs, 1);
    QTimer::singleShot(0, this, [this] { controller_->refreshBans(QStringLiteral("youtube")); });
    return page;
}

void MainWindow::reviewTwitchAppeal(bool approve){
    auto*item=twitchAppeals_?twitchAppeals_->currentItem():nullptr;
    if(!item||!(item->flags()&Qt::ItemIsEnabled)){QMessageBox::information(this,QStringLiteral("Twitch Appeals"),QStringLiteral("Select one pending appeal first."));return;}
    const QJsonObject appeal=QJsonObject::fromVariantMap(item->data(Qt::UserRole+2).toMap());
    const QString user=item->data(Qt::UserRole+1).toString();const QString userId=item->data(Qt::UserRole).toString();
    QJsonObject ban;for(const auto&value:twitchBansCache_){const auto candidate=value.toObject();if(candidate.value("user_id").toString()==userId){ban=candidate;break;}}
    // Qt::Popup gives this frameless review panel native click-away behaviour:
    // clicking anywhere outside dismisses it without taking moderation action.
    QDialog dialog(this,Qt::Popup|Qt::FramelessWindowHint);dialog.setObjectName(QStringLiteral("appealReview"));dialog.setModal(false);dialog.resize(920,600);
    auto*outer=new QVBoxLayout(&dialog);outer->setContentsMargins(18,18,18,18);outer->setSpacing(12);
    auto*title=label(QStringLiteral("TWITCH APPEAL — %1").arg(user),"heroTitle");outer->addWidget(title);
    auto*split=new QSplitter(Qt::Horizontal);
    auto*details=new QTextBrowser;details->setHtml(QStringLiteral("<h3>%1</h3><p><b>Ban reason</b><br>%2</p><p><b>Banned</b><br>%3</p><p><b>Banned by</b><br>%4</p><p><b>Appeal Reason</b><br>%5</p>")
        .arg(user.toHtmlEscaped(),ban.value("reason").toString("Not supplied by Twitch").toHtmlEscaped(),friendlyTimestamp(ban.value("created_at").toString()).toHtmlEscaped(),ban.value("moderator_name").toString("Not supplied by Twitch").toHtmlEscaped(),appeal.value("text").toString("No appeal message provided.").toHtmlEscaped()));
    auto*logs=new QTextBrowser;logs->setHtml(twitchAppealHistory_?twitchAppealHistory_->toHtml():QStringLiteral("No stored logs."));
    split->addWidget(details);split->addWidget(logs);split->setStretchFactor(0,2);split->setStretchFactor(1,3);outer->addWidget(split,1);
    auto*resolution=new QLineEdit;resolution->setPlaceholderText(approve?QStringLiteral("Approval response (optional)"):QStringLiteral("Reason for denying this appeal"));outer->addWidget(resolution);
    auto*buttons=new QHBoxLayout;auto*cancel=new QPushButton(QStringLiteral("Cancel"));auto*confirm=new QPushButton(approve?QStringLiteral("Approve Appeal"):QStringLiteral("Deny Appeal"));confirm->setProperty("primary",approve);buttons->addStretch();buttons->addWidget(cancel);buttons->addWidget(confirm);outer->addLayout(buttons);
    connect(cancel,&QPushButton::clicked,&dialog,&QDialog::reject);connect(confirm,&QPushButton::clicked,&dialog,&QDialog::accept);
    if(dialog.exec()!=QDialog::Accepted)return;
    controller_->resolveTwitchAppeal(appeal.value("id").toString(),approve,resolution->text().trimmed());
}

void MainWindow::showPostUpdateConnectionCheck(){
    const QString version=QCoreApplication::applicationVersion();
    if(controller_->settings()->preference(QStringLiteral("connection_check_version")).toString()==version)return;
    const bool twitch=!controller_->settings()->secret(QStringLiteral("twitch_access_token")).isEmpty();
    const bool youtube=!controller_->settings()->secret(QStringLiteral("youtube_access_token")).isEmpty();
    const bool existingInstall=twitch||youtube||!controller_->settings()->link(QStringLiteral("twitch")).isEmpty()||!controller_->settings()->link(QStringLiteral("youtube")).isEmpty();
    if(!existingInstall){controller_->settings()->setPreference(QStringLiteral("connection_check_version"),version);return;}
    const QString dataPath=QDir::toNativeSeparators(controller_->settings()->dataDirectory());
    const QString report=QStringLiteral("Local data: ✓ Found\n%1\n\nTwitch: %2\nYouTube: %3\n\nYour existing connections and settings remain stored outside the Program Files installation, so this update did not require reconnecting them.")
        .arg(dataPath,twitch?QStringLiteral("✓ Preserved and validating automatically"):QStringLiteral("Not previously connected"),youtube?QStringLiteral("✓ Preserved and ready"):QStringLiteral("Not previously connected"));
    QMessageBox::information(this,QStringLiteral("Update connection check"),report);
    controller_->settings()->setPreference(QStringLiteral("connection_check_version"),version);
}

QWidget* MainWindow::buildKeysPage() {
    auto* page=new QWidget;auto* outer=new QVBoxLayout(page);outer->setContentsMargins(0,0,0,0);auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto* body=new QWidget;auto* layout=new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll);layout->setContentsMargins(8,4,8,8);layout->setSpacing(9);
    layout->addWidget(label(QStringLiteral("CONNECTION KEYS"),"heroTitle"));
    auto* notice=new QFrame;notice->setObjectName(QStringLiteral("keyNotice"));auto* noticeLayout=new QVBoxLayout(notice);
    noticeLayout->addWidget(label(QStringLiteral("IMPORTANT — RELOAD KEYS AFTER EVERY UPDATE"),"cardTitle"));
    auto* noticeText=label(QStringLiteral("For every Leapcast Studio update—and before using the software again—reload or reconnect the necessary Twitch and YouTube keys here. This keeps chat and moderation permissions working correctly."),"muted");noticeText->setWordWrap(true);noticeLayout->addWidget(noticeText);layout->addWidget(notice);
    auto* columns=new QHBoxLayout;
    const bool twitchAuthorized=!controller_->settings()->secret(QStringLiteral("twitch_access_token")).isEmpty();
    auto* twitchCard=makePlatformCard(QStringLiteral("Twitch"),twitchAuthorized?QStringLiteral("Connected"):QStringLiteral("Connect your Twitch account"),QColor("#9146ff"),twitchAuthorized?QStringLiteral("Reload Twitch"):QStringLiteral("Connect Twitch"));
    twitchCard->setProperty("platform",QStringLiteral("twitch"));twitchModerationStatus_=twitchCard->findChild<QLabel*>(QStringLiteral("cardStatus"));twitchConnectButton_=twitchCard->findChild<QPushButton*>(QStringLiteral("cardAction"));columns->addWidget(twitchCard,1);connect(twitchConnectButton_,&QPushButton::clicked,this,&MainWindow::authorizeTwitch);
    const bool youtubeAuthorized=!controller_->settings()->secret(QStringLiteral("youtube_access_token")).isEmpty();
    auto* youtubeCard=makePlatformCard(QStringLiteral("YouTube"),youtubeAuthorized?QStringLiteral("Connected"):QStringLiteral("Connect your YouTube account"),QColor("#ff334f"),youtubeAuthorized?QStringLiteral("Reload YouTube"):QStringLiteral("Connect YouTube"),QStringLiteral("Get keys ↗"),QUrl(QStringLiteral("https://console.cloud.google.com/apis/credentials")));
    youtubeCard->setProperty("platform",QStringLiteral("youtube"));youtubeModerationStatus_=youtubeCard->findChild<QLabel*>(QStringLiteral("cardStatus"));youtubeConnectButton_=youtubeCard->findChild<QPushButton*>(QStringLiteral("cardAction"));columns->addWidget(youtubeCard,1);connect(youtubeConnectButton_,&QPushButton::clicked,this,&MainWindow::configureYouTubeModeration);
    layout->addLayout(columns);
    auto* additional=new QHBoxLayout;
    auto* kickCard=new QFrame;kickCard->setProperty("card",true);auto* kickLayout=new QVBoxLayout(kickCard);kickLayout->addWidget(label(QStringLiteral("KICK CHAT"),"cardTitle"));auto* kickHelp=label(QStringLiteral("No key is needed for Kick chat. Add your Kick channel under Sources and select Connect. Kick moderation is hidden until Leapcast supports Kick's authenticated moderation API safely."),"muted");kickHelp->setWordWrap(true);kickLayout->addWidget(kickHelp);additional->addWidget(kickCard,1);
    auto* rumbleCard=new QFrame;rumbleCard->setProperty("card",true);auto* rumbleLayout=new QVBoxLayout(rumbleCard);rumbleLayout->addWidget(label(QStringLiteral("RUMBLE LIVE STREAM API"),"cardTitle"));auto* rumbleHelp=label(QStringLiteral("1. Select Get Rumble API URL below.\n2. Sign in to Rumble if asked.\n3. On Rumble's Live Stream API page, create or copy your private API URL.\n4. Return to Leapcast and paste the entire URL into the box below.\n5. Select Save & Connect Rumble.\n\nKeep this URL private. If it is ever shared accidentally, reset it on Rumble and paste the new URL here."),"muted");rumbleHelp->setWordWrap(true);rumbleLayout->addWidget(rumbleHelp);auto* rumbleUrl=new QLineEdit(controller_->settings()->secret(QStringLiteral("rumble_api_url")));rumbleUrl->setEchoMode(QLineEdit::Password);rumbleUrl->setPlaceholderText(QStringLiteral("Paste the full private Rumble API URL here"));rumbleLayout->addWidget(rumbleUrl);auto* rumbleButtons=new QHBoxLayout;auto* getRumble=new QPushButton(QStringLiteral("Get Rumble API URL ↗"));auto* saveRumble=new QPushButton(QStringLiteral("Save & Connect Rumble"));rumbleButtons->addWidget(getRumble);rumbleButtons->addWidget(saveRumble);rumbleLayout->addLayout(rumbleButtons);connect(getRumble,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl(QStringLiteral("https://rumble.com/account/livestream-api")));});connect(saveRumble,&QPushButton::clicked,this,[this,rumbleUrl]{const QString value=rumbleUrl->text().trimmed();const QUrl url(value);const bool rumbleHost=url.host()==QStringLiteral("rumble.com")||url.host().endsWith(QStringLiteral(".rumble.com"));if(!url.isValid()||!rumbleHost){QMessageBox::warning(this,QStringLiteral("Rumble API"),QStringLiteral("That does not look like a Rumble API URL. Select Get Rumble API URL, copy the entire private URL from Rumble, then paste it here."));return;}controller_->settings()->setSecret(QStringLiteral("rumble_api_url"),value);const QString link=controller_->settings()->link(QStringLiteral("rumble"));if(!link.isEmpty())controller_->connectSource(QStringLiteral("rumble"),link);QMessageBox::information(this,QStringLiteral("Rumble connected"),link.isEmpty()?QStringLiteral("Your Rumble API URL is saved. Next, open Sources, enter your Rumble channel URL, and select Connect."):QStringLiteral("Rumble chat is saved and connecting now."));});additional->addWidget(rumbleCard,1);
    layout->addLayout(additional);layout->addStretch();return page;
}

QWidget* MainWindow::buildModerationPage() {
    auto* page = new QWidget;
    auto* outer=new QVBoxLayout(page);outer->setContentsMargins(0,0,0,0);auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto* body=new QWidget;auto* layout = new QVBoxLayout(body);scroll->setWidget(body);outer->addWidget(scroll);
    layout->setContentsMargins(8, 4, 8, 8);
    auto* hero = new QFrame; hero->setObjectName(QStringLiteral("safetyHero"));
    auto* heroLayout = new QHBoxLayout(hero);
    auto* copy = new QVBoxLayout;
    copy->addWidget(label(QStringLiteral("CREATOR SAFETY CENTER"), "heroTitle"));
    copy->addWidget(label(QStringLiteral("A multi-chat system and moderation device for every connected community."), "muted"));
    heroLayout->addLayout(copy, 1);
    heroLayout->addWidget(label(QStringLiteral("● LIVE ACTIONS\n● RIGHT-CLICK READY\n● LOCAL AUDIT"), "signal"));
    layout->addWidget(hero);

    auto* columns = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    auto* right = new QVBoxLayout;
    auto* tiktokCard = makePlatformCard(QStringLiteral("TikTok"),
                                        QStringLiteral("Moderate directly on TikTok"),
                                        QColor("#18e0d5"), QStringLiteral("Open TikTok LIVE in browser"));
    tiktokCard->setProperty("platform",QStringLiteral("tiktok"));
    connect(tiktokCard->findChild<QPushButton*>(QStringLiteral("cardAction")), &QPushButton::clicked,
            this, &MainWindow::openTikTokModeration);
    auto* automodCard = makePlatformCard(QStringLiteral("AutoMod"), QStringLiteral("Ready"),
                                         QColor("#63e6be"), QStringLiteral("Edit blocked words"));
    right->addWidget(automodCard);
    connect(automodCard->findChild<QPushButton*>(QStringLiteral("cardAction")), &QPushButton::clicked,
            this, &MainWindow::editBlockedWords);
    if (auto* automodLayout = qobject_cast<QVBoxLayout*>(automodCard->layout())) {
        auto* twitchTimeoutRow = new QHBoxLayout;
        twitchTimeoutRow->addWidget(label(QStringLiteral("Twitch AutoMod timeout")));
        auto* twitchTimeout = new QSpinBox;
        twitchTimeout->setRange(30, 86400);
        twitchTimeout->setSuffix(QStringLiteral(" sec"));
        twitchTimeout->setToolTip(QStringLiteral("Base Twitch timeout for AutoMod violations."));
        twitchTimeout->setValue(controller_->settings()->preference(
            QStringLiteral("twitch_automod_timeout_seconds"), 300).toInt());
        twitchTimeoutRow->addWidget(twitchTimeout, 1);
        automodLayout->addLayout(twitchTimeoutRow);
        auto* twitchLadderNote = label(QString(), "muted");
        const auto updateTwitchNote=[twitchLadderNote](int seconds){twitchLadderNote->setText(QStringLiteral("Twitch tracks violations per broadcast: %1s for violations 1–5, %2s for violation 6, then a permanent ban.").arg(seconds).arg(qMin(seconds*2,86400)));};
        updateTwitchNote(twitchTimeout->value());
        connect(twitchTimeout, qOverload<int>(&QSpinBox::valueChanged), this, [this,updateTwitchNote](int value) {
            controller_->settings()->setPreference(QStringLiteral("twitch_automod_timeout_seconds"), value);updateTwitchNote(value);
        });
        twitchLadderNote->setWordWrap(true);
        automodLayout->addWidget(twitchLadderNote);

        auto* whitelistNote=label(QStringLiteral("Word whitelist: allowed words/phrases bypass blocked-word matching only. ‘smash’ and ‘pass’ are included by default; other blocked content in the same message is still moderated."),"muted");
        whitelistNote->setWordWrap(true);
        automodLayout->addWidget(whitelistNote);
        auto* editWhitelist=new QPushButton(QStringLiteral("Edit word whitelist"));
        editWhitelist->setToolTip(QStringLiteral("Open the AutoMod whitelist. Add one allowed word or phrase per line."));
        automodLayout->addWidget(editWhitelist);
        connect(editWhitelist,&QPushButton::clicked,this,&MainWindow::editWhitelistedWords);
    }
    left->addWidget(tiktokCard);
    auto* rumbleCard=makePlatformCard(QStringLiteral("Rumble"),QStringLiteral("Moderate in the Rumble live chat"),QColor("#85c742"),QStringLiteral("Open Rumble moderation"));rumbleCard->setProperty("platform",QStringLiteral("rumble"));left->addWidget(rumbleCard);connect(rumbleCard->findChild<QPushButton*>(QStringLiteral("cardAction")),&QPushButton::clicked,this,[this]{const QString link=controller_->settings()->link(QStringLiteral("rumble"));QDesktopServices::openUrl(QUrl::fromUserInput(link.isEmpty()?QStringLiteral("https://rumble.com/account/livestreams"):link));});left->addStretch();
    right->addStretch();
    columns->addLayout(left, 1); columns->addLayout(right, 1);
    layout->addLayout(columns, 1);
    return page;
}

void MainWindow::authorizeTwitch() {
    if(twitchModerationStatus_)twitchModerationStatus_->setText(QStringLiteral("Opening Twitch sign-in…"));
    if(twitchConnectButton_){twitchConnectButton_->setText(QStringLiteral("Connecting…"));twitchConnectButton_->setEnabled(false);}
    controller_->authorizeTwitch();
}

void MainWindow::configureYouTubeModeration() {
    bool ok = false;
    const QString token = QInputDialog::getText(this, QStringLiteral("YouTube moderation"),
        QStringLiteral("Google OAuth access token:"), QLineEdit::Password,
        controller_->settings()->secret(QStringLiteral("youtube_access_token")), &ok).trimmed();
    if (!ok || token.isEmpty()) return;
    controller_->configureYouTubeModeration(token);
    if(youtubeModerationStatus_){
        youtubeModerationStatus_->setText(QStringLiteral("Connected"));
        youtubeModerationStatus_->setStyleSheet(QStringLiteral("color:#63e6be"));
    }
    if(youtubeConnectButton_)youtubeConnectButton_->setText(QStringLiteral("Reconnect YouTube"));
    QMessageBox::information(this, QStringLiteral("YouTube connected"), QStringLiteral("YouTube moderation access was saved. You're all set."));
}

void MainWindow::openTikTokModeration() {
    QString saved=controller_->settings()->link(QStringLiteral("tiktok")).trimmed();
    if(saved.isEmpty()){
        if(QMessageBox::information(this,QStringLiteral("Add your TikTok profile first"),
            QStringLiteral("Open Sources and paste your TikTok profile, such as tiktok.com/@yourname. Then this button will open your LIVE stream directly."),
            QStringLiteral("Go to Sources"),QStringLiteral("Cancel"))==0)pages_->setCurrentIndex(0);
        return;
    }

    if(saved.startsWith(QLatin1Char('@')))saved.remove(0,1);
    if(!saved.contains(QStringLiteral("tiktok.com"),Qt::CaseInsensitive))
        saved=QStringLiteral("https://www.tiktok.com/@")+saved;
    QUrl url=QUrl::fromUserInput(saved);
    QString path=url.path();
    while(path.endsWith(QLatin1Char('/')))path.chop(1);
    if(!path.endsWith(QStringLiteral("/live"),Qt::CaseInsensitive))path+=QStringLiteral("/live");
    url.setPath(path);url.setQuery(QString());url.setFragment(QString());
    if(!url.isValid()||!QDesktopServices::openUrl(url))
        QMessageBox::warning(this,QStringLiteral("TikTok couldn't open"),
                             QStringLiteral("Check the TikTok profile saved under Sources and try again."));
}

void MainWindow::editBlockedWords() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(controller_->blockedWordsPath()));
    controller_->reloadAutoMod();
}

void MainWindow::editWhitelistedWords() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(controller_->whitelistedWordsPath()));
    controller_->reloadAutoMod();
}

void MainWindow::showDashboardChatMenu(QTextBrowser* view, const QPoint& globalPos) {
    const QPoint pos = view->viewport()->mapFromGlobal(globalPos);
    QTextCursor clicked=view->cursorForPosition(pos);
    qint64 id=clicked.blockFormat().property(kMessageIdProperty).toLongLong();
    if(!chatHistoryById_.contains(id))id=clicked.charFormat().property(kMessageIdProperty).toLongLong();
    if(!chatHistoryById_.contains(id)){
        const QTextBlock original=clicked.block();
        for(const auto&candidate:{original.previous(),original.next()}){
            if(!candidate.isValid())continue;
            const qint64 candidateId=candidate.blockFormat().property(kMessageIdProperty).toLongLong();
            if(chatHistoryById_.contains(candidateId)){id=candidateId;break;}
        }
    }
    QMenu menu(this);
    auto* copyAction = menu.addAction(QStringLiteral("Copy"));
    copyAction->setEnabled(view->textCursor().hasSelection());
    connect(copyAction, &QAction::triggered, view, &QTextBrowser::copy);
    if (chatHistoryById_.contains(id)) {
        const ChatMessage msg = chatHistoryById_.value(id);
        const bool isTwitch = msg.platform == QStringLiteral("twitch") && !msg.metadata.value(QStringLiteral("channel_point_redemption")).toBool();
        const bool isYouTube = msg.platform == QStringLiteral("youtube") || msg.platform == QStringLiteral("yt_shorts");
        // Only Twitch and YouTube/Shorts have a working moderation API wired
        // up here; see PopoutChat::showChatContextMenu for why TikTok has no
        // actions offered.
        if (isTwitch || isYouTube) {
            menu.addSeparator();
            auto* header = menu.addAction(QStringLiteral("Moderate %1").arg(msg.user));
            header->setEnabled(false);
            connect(menu.addAction(QStringLiteral("Delete message")), &QAction::triggered,
                    this, [this, msg] { controller_->deleteChatMessage(msg); const QString note=QStringLiteral("<div style='color:#7f8ba5;font-style:italic'>&lt;Message Moderated&gt;</div>");appendTrimmed(chatViews_.value("combined"),note);appendTrimmed(chatViews_.value(msg.platform),note);if(popout_)popout_->appendModerationNote(msg.user,"Message Moderated"); });
            connect(menu.addAction(QStringLiteral("Timeout")), &QAction::triggered,
                    this, [this, msg] {bool ok=false;const QString r=QInputDialog::getMultiLineText(this,"Timeout reason","Why is this user being timed out?",QString(),&ok).trimmed();if(ok&&!r.isEmpty()){controller_->moderateMessage(msg,300,r);const QString n="<div style='color:#7f8ba5;font-style:italic'>&lt;Message Moderated&gt;</div>";appendTrimmed(chatViews_.value("combined"),n);appendTrimmed(chatViews_.value(msg.platform),n);if(popout_)popout_->appendModerationNote(msg.user,"Message Moderated");}});
            connect(menu.addAction(isYouTube ? QStringLiteral("Hide from channel") : QStringLiteral("Ban")),
                    &QAction::triggered, this, [this, msg] {bool ok=false;const QString r=QInputDialog::getMultiLineText(this,"Ban reason","Why is this user being banned?",QString(),&ok).trimmed();if(ok&&!r.isEmpty()){controller_->moderateMessage(msg,0,r);const QString n="<div style='color:#7f8ba5;font-style:italic'>&lt;Message Moderated&gt;</div>";appendTrimmed(chatViews_.value("combined"),n);appendTrimmed(chatViews_.value(msg.platform),n);if(popout_)popout_->appendModerationNote(msg.user,"Message Moderated");}});
        } else if (msg.platform==QStringLiteral("rumble")) {
            menu.addSeparator();
            connect(menu.addAction(QStringLiteral("Open Rumble moderation")),&QAction::triggered,this,[this]{const QString link=controller_->settings()->link(QStringLiteral("rumble"));QDesktopServices::openUrl(QUrl::fromUserInput(link.isEmpty()?QStringLiteral("https://rumble.com/account/livestreams"):link));});
        }
    }
    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Select All")), &QAction::triggered, view, &QTextBrowser::selectAll);
    menu.exec(globalPos);
}

void MainWindow::refreshPinnedBanner(){
    if(!pinnedBanner_||!chatTabs_)return;QString selected=QStringLiteral("combined");auto*current=chatTabs_->currentWidget();for(auto it=chatViews_.cbegin();it!=chatViews_.cend();++it)if(it.value()==current){selected=it.key();break;}QStringList keys;if(selected==QStringLiteral("combined"))keys={QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("yt_shorts")};else keys={selected};const QHash<QString,QString>names{{QStringLiteral("twitch"),QStringLiteral("Twitch")},{QStringLiteral("youtube"),QStringLiteral("YouTube")},{QStringLiteral("yt_shorts"),QStringLiteral("YouTube Shorts")}};QStringList rows;for(const auto&key:keys)if(pinnedMessages_.contains(key)){const auto&m=pinnedMessages_.value(key);rows<<QStringLiteral("<b>&#128204; %1 pinned</b> &nbsp; <b>%2</b>: %3").arg(names.value(key,key).toHtmlEscaped(),m.user.toHtmlEscaped(),m.text.toHtmlEscaped());}pinnedBanner_->setText(rows.join(QStringLiteral("<br>")));pinnedBanner_->setVisible(!rows.isEmpty());
}

QWidget* MainWindow::buildChatDock() {
    auto* dock = new QFrame; dock->setObjectName(QStringLiteral("chatDock"));
    auto* layout = new QVBoxLayout(dock);
    layout->addWidget(label(QStringLiteral("CHAT PREVIEW"), "pageTitle"));
    pinnedBanner_=new QLabel;pinnedBanner_->setWordWrap(true);pinnedBanner_->setTextFormat(Qt::RichText);pinnedBanner_->setTextInteractionFlags(Qt::TextSelectableByMouse);pinnedBanner_->setStyleSheet(QStringLiteral("background:#211d12;border:1px solid #8a6f2b;border-radius:9px;padding:8px;color:#f7f1d1;"));pinnedBanner_->hide();layout->addWidget(pinnedBanner_);
    chatTabs_ = new QTabWidget;
    chatTabs_->setIconSize(QSize(44, 26));
    const QList<QPair<QString, QString>> tabs{
        {QStringLiteral("ALL"), QString()}, {QStringLiteral("Twitch"), QStringLiteral(":/brand/twitch.png")},
        {QStringLiteral("YouTube"), QStringLiteral(":/brand/youtube.png")},
        {QStringLiteral("Shorts"), QStringLiteral(":/brand/youtube_shorts.png")},
        {QStringLiteral("TikTok"), QStringLiteral(":/brand/tiktok.png")},
        {QStringLiteral("Kick"), QStringLiteral(":/brand/kick.svg")},
        {QStringLiteral("Rumble"), QStringLiteral(":/brand/rumble.svg")}};
    const QStringList keys{QStringLiteral("combined"),QStringLiteral("twitch"),QStringLiteral("youtube"),QStringLiteral("yt_shorts"),QStringLiteral("tiktok"),QStringLiteral("kick"),QStringLiteral("rumble")};
    for (qsizetype index=0; index<tabs.size(); ++index) {
        const auto& tab=tabs.at(index);
        auto* chat = new ChatBrowser;
        chat->setPlaceholderText(QStringLiteral("Connected messages appear here."));
        const QIcon icon(tab.second);
        int tabIndex=-1;
        if (!icon.isNull()) {
            tabIndex=chatTabs_->addTab(chat, QString());
            auto* iconHost=new QLabel;
            iconHost->setFixedSize(46,30);
            iconHost->setAlignment(Qt::AlignCenter);
            iconHost->setAttribute(Qt::WA_TransparentForMouseEvents);
            iconHost->setStyleSheet(QStringLiteral("background:transparent;border:0;padding:0;margin:0;"));
            const QPixmap source(tab.second);
            iconHost->setPixmap(source.scaled(QSize(42,24),Qt::KeepAspectRatio,Qt::SmoothTransformation));
            chatTabs_->tabBar()->setTabButton(tabIndex,QTabBar::LeftSide,iconHost);
            chatTabs_->tabBar()->setTabButton(tabIndex,QTabBar::RightSide,nullptr);
        } else tabIndex=chatTabs_->addTab(chat, tab.first);
        chatTabs_->setTabToolTip(tabIndex, tab.first);
        chatViews_.insert(keys.at(index),chat);
        if(keys.at(index)!=QStringLiteral("combined"))platformChatWidgets_[keys.at(index)]=chat;
        connect(chat, &ChatBrowser::chatContextMenuRequested, this,
                [this, chat](const QPoint& globalPos) { showDashboardChatMenu(chat, globalPos); });
    }

    layout->addWidget(chatTabs_, 1);
    connect(chatTabs_,&QTabWidget::currentChanged,this,[this]{refreshPinnedBanner();});
    applyPlatformVisibility();

    const auto ensurePopout = [this] {
        if (popout_) return popout_;
        popout_ = new PopoutChat;
        popout_->setOpacityPercent(controller_->settings()->preference(QStringLiteral("popout_opacity_percent"), 0).toInt());
        popout_->setAppearance(QColor(controller_->settings()->preference(QStringLiteral("overlay_background_color"),QStringLiteral("#000000")).toString()),controller_->settings()->preference(QStringLiteral("popout_outline_thickness"),2).toInt(),controller_->settings()->preference(QStringLiteral("popout_font_family"),QStringLiteral("Segoe UI")).toString(),controller_->settings()->preference(QStringLiteral("popout_font_size"),12).toInt());
        popout_->setShowPlatformIcons(controller_->settings()->preference(QStringLiteral("program_popout_show_platform_icons"),true).toBool());
        popout_->setClipAvailable(!controller_->settings()->secret(QStringLiteral("twitch_access_token")).isEmpty());
        for(auto it=pinnedMessages_.cbegin();it!=pinnedMessages_.cend();++it)popout_->setPinnedMessage(it.key(),it.value(),true);
        connect(popout_, &PopoutChat::ghostModeChanged, this, [this](bool enabled) {
            if (!popoutClickThrough_) return;
            QSignalBlocker blocker(popoutClickThrough_);
            popoutClickThrough_->setChecked(enabled);
        });
        connect(popout_, &PopoutChat::clipRequested, this, [this] { controller_->createTwitchClip(); });
        connect(popout_, &PopoutChat::openClipEditor, this, [this](const QUrl& url) {
            if (!clipEditor_) {clipEditor_ = new ClipEditorWindow;connect(clipEditor_,&ClipEditorWindow::twitchBrowserLoginRequested,this,[this]{controller_->authorizeTwitch();});clipEditor_->setTwitchAccessToken(controller_->settings()->secret(QStringLiteral("twitch_access_token")));}
            clipEditor_->openUrl(url);
        });
        connect(popout_, &PopoutChat::deleteMessageRequested, this,
                [this](const ChatMessage& message) { controller_->deleteChatMessage(message);if(popout_)popout_->appendModerationNote(message.user,QStringLiteral("Message Moderated")); });
        connect(popout_, &PopoutChat::timeoutUserRequested, this,
                [this](const ChatMessage& message, int seconds,const QString&reason) { controller_->moderateMessage(message, seconds,reason);if(popout_)popout_->appendModerationNote(message.user,QStringLiteral("Message Moderated")); });
        return popout_;
    };

    const int savedOpacity = controller_->settings()->preference(QStringLiteral("popout_opacity_percent"), 0).toInt();
    auto* opacityRow = new QHBoxLayout;
    opacityRow->addWidget(label(QStringLiteral("Pop-out opacity")));
    popoutOpacity_ = new QSlider(Qt::Horizontal);
    popoutOpacity_->setRange(0, 100);
    popoutOpacity_->setValue(savedOpacity);
    opacityRow->addWidget(popoutOpacity_, 1);
    auto* opacityValue = label(QString::number(savedOpacity) + QStringLiteral("%"));
    opacityRow->addWidget(opacityValue);
    layout->addLayout(opacityRow);
    connect(popoutOpacity_, &QSlider::valueChanged, this, [this, opacityValue](int value) {
        opacityValue->setText(QString::number(value) + QStringLiteral("%"));
        controller_->settings()->setPreference(QStringLiteral("popout_opacity_percent"), value);
        if (popout_) popout_->setOpacityPercent(value);
    });

    popoutClickThrough_ = new QCheckBox(QStringLiteral("Click-through (see and click what's behind it)"));
    layout->addWidget(popoutClickThrough_);
    layout->addWidget(label(QStringLiteral("Press Esc or Alt+C, or click back into Leapcast Studio, to regain control of the pop-out."), "muted"));
    connect(popoutClickThrough_, &QCheckBox::toggled, this, [ensurePopout](bool enabled) {
        auto* popout = ensurePopout();
        popout->setGhostMode(enabled);
        popout->raise();
    });

    auto* popout = new QPushButton(QStringLiteral("Open Pop-out Chat"));
    auto* clearPopout = new QPushButton(QStringLiteral("Clear Pop-out Chat"));
    popout->setProperty("primary", true);
    auto* popoutButtons=new QHBoxLayout;popoutButtons->addWidget(popout,1);popoutButtons->addWidget(clearPopout);layout->addLayout(popoutButtons);
    connect(popout,&QPushButton::clicked,this,[this,ensurePopout]{
        auto* view = ensurePopout();
        view->setStreamlabsAlertAudio(
            controller_->settings()->preference(QStringLiteral("streamlabs_audio_enabled"),false).toBool(),
            QUrl(controller_->settings()->preference(QStringLiteral("streamlabs_alert_box_url")).toString()));
        view->show();view->raise();
        // Apply layered pop-out decorations only after Windows/Qt has created
        // and shown the native frameless window. This avoids doing decorative
        // painting during the fragile first-show/native-window creation path.
        QTimer::singleShot(0,view,[this,view]{
            if(view&&view==popout_)view->setSeasonalTheme(seasonalThemeFor(controller_->settings(),QDateTime::currentDateTime()));
        });
    });
    connect(clearPopout,&QPushButton::clicked,this,[ensurePopout]{ensurePopout()->clearMessages();});
    return dock;
}

void MainWindow::applyTheme() {
    const QString family=controller_?controller_->settings()->preference("ui_font_family","Segoe UI").toString():QStringLiteral("Segoe UI");
    const int fontSize=controller_?controller_->settings()->preference("ui_font_size",10).toInt():10;
    const int scale=controller_?controller_->settings()->preference("ui_control_scale",100).toInt():100;
    QString sheet=QStringLiteral(R"(
        * { font-family:'__FONT__'; font-size:__SIZE__pt; color:#eef2ff; }
        QMainWindow, QWidget { background:#0b0d15; }
        QFrame#navigationRail, QFrame#chatDock { background:#111522; border:1px solid #242b3d; border-radius:14px; }
        QLabel[role='brand'] { font-size:11pt; font-weight:800; color:#f8fbff; qproperty-alignment:AlignCenter; }
        QLabel[role='version'] { background:#0b0e17; border-radius:5px; padding:4px; font-size:10pt; font-weight:700; color:#aeb8d0; }
        QLabel[role='pageTitle'] { font-size:11pt; font-weight:800; color:#f8fbff; }
        QLabel[role='heroTitle'] { font-size:17pt; font-weight:800; }
        QLabel[role='muted'], QLabel[role='status'] { color:#9ca7bf; }
        QLabel[role='signal'] { color:#68e9d5; font-size:8pt; font-weight:700; }
        QLabel[role='cardTitle'] { font-size:12pt; font-weight:700; }
        QFrame#safetyHero { background:#171d2d; border-left:5px solid #53cdf3; border-radius:12px; }
        QFrame#welcomeCard { background:#171d2d; border:1px solid #283149; border-left:5px solid #63e6be; border-radius:12px; padding:4px; }
        QFrame#keyNotice { background:#2a2010; border:1px solid #775a20; border-left:5px solid #f6c85f; border-radius:12px; padding:4px; }
        QFrame#bansHero { background:#141a29; border:1px solid #28334a; border-left:5px solid #7667ef; border-radius:12px; }
        QFrame[card='true'] { background:#161b29; border:1px solid #252d42; border-radius:12px; }
        QPushButton { background:#20283a; border:0; border-radius:8px; padding:__VPAD__px __HPAD__px; font-weight:700; }
        QPushButton:hover { background:#2b3650; }
        QPushButton[nav='true'] { text-align:left; margin:2px 4px; padding:9px 8px; font-size:9pt; }
        QPushButton[nav='true']:checked { background:#7667ef; color:white; }
        QPushButton[primary='true'] { background:#53cdf3; color:#071018; }
        QPushButton[danger='true'] { background:#44202b; color:#ff91a4; }
        QPushButton[danger='true']:hover { background:#5a2635; color:#ffd3da; }
        QTabWidget::pane { border:1px solid #242b3d; border-radius:9px; }
        QTabBar::tab { background:#171d2d; min-width:44px; min-height:38px; padding:4px 3px; margin-right:1px; }
        QTabBar::tab:selected { background:#7667ef; }
        QTabBar::tab:disabled { color:#565f78; }
        QTextBrowser { background:#090b11; border:0; padding:10px; }
        QTabWidget#moderationTabs::pane { background:#101522; border:1px solid #283149; border-radius:10px; }
        QListWidget#restrictionList { background:transparent; border:0; outline:0; padding:2px; }
        QListWidget#restrictionList::item { background:#171d2b; border:1px solid #273149; border-radius:9px; }
        QListWidget#restrictionList::item:selected { background:#202944; border:1px solid #7667ef; }
        QWidget[restrictionCard='true'] { background:transparent; }
        QLabel[role='restrictionName'] { color:#ffffff; font-size:11pt; font-weight:750; }
        QLabel[role='restrictionReason'] { color:#c7cede; }
        QLabel[role='restrictionTime'] { color:#7f8ba5; font-size:9pt; }
        QSplitter::handle { background:#242b3d; width:2px; }
    )");
    family.contains(QLatin1Char('\''))?sheet.replace("__FONT__",QStringLiteral("Segoe UI")):sheet.replace("__FONT__",family);
    sheet.replace("__SIZE__",QString::number(qBound(8,fontSize,20)));
    sheet.replace("__VPAD__",QString::number(qMax(5,10*scale/100)));
    sheet.replace("__HPAD__",QString::number(qMax(6,12*scale/100)));
    qApp->setStyleSheet(sheet);
}
