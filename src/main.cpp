#include "MainWindow.hpp"
#include "BuildInfo.hpp"
#include "UpdateService.hpp"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSplashScreen>
#include <QTextBrowser>
#include <QTextDocument>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace {
bool showStartupUpdateNotes(const QString&version,const QString&notes){
    QDialog dialog;dialog.setWindowTitle(QStringLiteral("Leapcast Studio update"));dialog.setWindowIcon(QIcon(QStringLiteral(":/brand/lefroge_chat_icon.png")));dialog.resize(480,360);dialog.setMinimumSize(420,300);
    dialog.setStyleSheet(QStringLiteral("QDialog{background:#0b0d15;} QLabel{color:#eef2ff;font-family:'Segoe UI';} QTextBrowser{background:#111522;color:#eef2ff;border:1px solid #283149;border-radius:9px;padding:10px;font-family:'Segoe UI';} QPushButton{background:#20283a;color:#eef2ff;border:0;border-radius:8px;padding:8px 14px;font-weight:700;} QPushButton[primary='true']{background:#53cdf3;color:#071018;}"));
    auto* layout=new QVBoxLayout(&dialog);layout->setContentsMargins(18,16,18,16);layout->setSpacing(10);
    auto* title=new QLabel(QStringLiteral("✨ WHAT'S NEW IN %1").arg(version));title->setStyleSheet(QStringLiteral("font-size:17pt;font-weight:800;"));layout->addWidget(title);
    QString summary;for(const auto&line:notes.split(QLatin1Char('\n'))){QString clean=line.trimmed();clean.remove(QRegularExpression(QStringLiteral("^[#*\\-\\s]+")));if(!clean.isEmpty()){summary=clean;break;}}
    auto* summaryLabel=new QLabel(QStringLiteral("🚀 %1").arg(summary.isEmpty()?QStringLiteral("A new update is ready."):summary));summaryLabel->setWordWrap(true);summaryLabel->setStyleSheet(QStringLiteral("color:#9ca7bf;"));layout->addWidget(summaryLabel);
    auto* patchNotes=new QTextBrowser;patchNotes->setOpenExternalLinks(true);patchNotes->document()->setDefaultStyleSheet(QStringLiteral("body{line-height:1.45} h1,h2,h3{margin:8px 0 5px} p{margin:5px 0} li{margin:0 0 7px 0}"));patchNotes->setMarkdown(notes.trimmed().isEmpty()?QStringLiteral("- ✅ Performance improvements\n- 🛠️ Bug fixes"):notes.left(12000));layout->addWidget(patchNotes,1);
    auto* hint=new QLabel(QStringLiteral("🔄 Installs the patch, then reopens Leapcast automatically."));hint->setWordWrap(true);hint->setStyleSheet(QStringLiteral("color:#9ca7bf;"));layout->addWidget(hint);
    auto* buttons=new QDialogButtonBox;auto* later=buttons->addButton(QStringLiteral("Later"),QDialogButtonBox::RejectRole);auto* install=buttons->addButton(QStringLiteral("⬇ Update now"),QDialogButtonBox::AcceptRole);install->setProperty("primary",true);QObject::connect(later,&QPushButton::clicked,&dialog,&QDialog::reject);QObject::connect(install,&QPushButton::clicked,&dialog,&QDialog::accept);layout->addWidget(buttons);
    return dialog.exec()==QDialog::Accepted;
}
}

class BrandSplash final : public QSplashScreen {
public:
    BrandSplash() : QSplashScreen(QPixmap(560, 340)), base_(560, 340) {
        base_.fill(QColor("#0b0d15"));
        QPainter p(&base_);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#16213a")); p.setPen(Qt::NoPen);
        p.drawEllipse(QRect(-150, -220, 510, 510));
        p.setBrush(QColor("#251a46"));
        p.drawEllipse(QRect(340, 165, 370, 370));
        const QPixmap frog(QStringLiteral(":/brand/lefroge_chat_icon.png"));
        p.drawPixmap(QRect(216, 32, 128, 128), frog);
        p.setPen(QColor("#f6f8ff"));
        QFont title(QStringLiteral("Segoe UI"), 21, QFont::Bold);
        p.setFont(title);
        p.drawText(QRect(0, 181, 560, 38), Qt::AlignCenter,
                   QStringLiteral("LEAPCAST STUDIO"));
        p.setPen(QColor("#a8b0c7"));
        p.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
        p.drawText(QRect(0, 222, 560, 25), Qt::AlignCenter,
                   QStringLiteral("MULTI-CHAT & MODERATION"));
        p.setPen(QColor("#15e0d2"));
        p.drawText(QRect(0, 248, 560, 22),Qt::AlignCenter,QStringLiteral("V%1").arg(QString::fromLatin1(leapcast::Version)));
        p.setPen(QPen(QColor("#293044"), 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(54, 285, 506, 285);
        status_=QStringLiteral("CHECKING FOR UPDATES...");
        setProgress(0.0);
    }

    void setStatus(const QString&status){status_=status;setProgress(progress_);}

    void setProgress(double progress) {
        progress_=qBound(0.0,progress,1.0);
        QPixmap frame=base_;
        QPainter p(&frame);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#15e0d2"),5,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(54,285,54+qRound(452*progress_),285);
        p.setPen(QColor("#a8b0c7"));
        p.setFont(QFont(QStringLiteral("Segoe UI"),9,QFont::DemiBold));
        p.drawText(QRect(0,302,560,20),Qt::AlignCenter,status_);
        setPixmap(frame);
    }

private:
    QPixmap base_;
    QString status_;
    double progress_{};
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Leapcast Studio"));
    QApplication::setApplicationVersion(QString::fromLatin1(leapcast::Version));
    QApplication::setOrganizationName(QStringLiteral("Lefroge"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/brand/lefroge_chat_icon.png")));

    BrandSplash splash;
    splash.show();
    app.processEvents();

    QElapsedTimer minimumBrandTime;
    minimumBrandTime.start();

    // Automatic updates are gated behind leapcast::AutoUpdate (CMake option
    // LEAPCAST_AUTO_UPDATE, default OFF). With it off the splash never contacts
    // GitHub, so a locally built test binary can never be replaced by the
    // published release. Flip the CMake option back ON to restore this.
    bool installingUpdate=false;
    bool updateFailed=false;
    if constexpr (leapcast::AutoUpdate) {
        UpdateService startupUpdater;
        QEventLoop updateLoop;
        QTimer checkTimeout;checkTimeout.setSingleShot(true);
        QObject::connect(&checkTimeout,&QTimer::timeout,&updateLoop,[&]{splash.setStatus(QStringLiteral("UPDATE CHECK TIMED OUT — OPENING..."));updateLoop.quit();});
        QObject::connect(&startupUpdater,&UpdateService::upToDate,&updateLoop,[&]{checkTimeout.stop();splash.setStatus(QStringLiteral("UP TO DATE — OPENING..."));splash.setProgress(1.0);updateLoop.quit();});
        QObject::connect(&startupUpdater,&UpdateService::failed,&updateLoop,[&](const QString&){updateFailed=true;checkTimeout.stop();splash.setStatus(QStringLiteral("UPDATE CHECK UNAVAILABLE — OPENING..."));updateLoop.quit();});
        QObject::connect(&startupUpdater,&UpdateService::updateAvailable,&updateLoop,[&](const QString&version,const QString&notes,const QUrl&asset,const QString&name,const QString&digest){checkTimeout.stop();splash.hide();if(!showStartupUpdateNotes(version,notes)){splash.show();splash.setStatus(QStringLiteral("UPDATE POSTPONED — OPENING..."));updateLoop.quit();return;}installingUpdate=true;updateFailed=false;splash.show();splash.setStatus(QStringLiteral("UPDATE V%1 FOUND — DOWNLOADING...").arg(version));startupUpdater.downloadAndInstall(asset,name,digest);});
        QObject::connect(&startupUpdater,&UpdateService::progress,&splash,[&](qint64 received,qint64 total){if(total>0)splash.setProgress(double(received)/double(total));});
        QObject::connect(&app,&QCoreApplication::aboutToQuit,&updateLoop,&QEventLoop::quit);
        checkTimeout.start(10000);startupUpdater.check(true);updateLoop.exec();
    } else {
        splash.setStatus(QStringLiteral("AUTOMATIC UPDATES DISABLED — OPENING..."));
        app.processEvents();
    }
    if(installingUpdate&&!updateFailed)return 0;

    MainWindow window;
    while (minimumBrandTime.elapsed() < 5000) {
        splash.setProgress(minimumBrandTime.elapsed()/5000.0);
        app.processEvents();
        QThread::msleep(10);
    }
    splash.setProgress(1.0);
    app.processEvents();
    window.show();
    splash.finish(&window);
    return app.exec();
}
