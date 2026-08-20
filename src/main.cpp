#include "MainWindow.hpp"
#include "BuildInfo.hpp"
#include "UpdateService.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QIcon>
#include <QPainter>
#include <QScreen>
#include <QSplashScreen>
#include <QThread>
#include <QTimer>

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

    UpdateService startupUpdater;
    QEventLoop updateLoop;
    QTimer checkTimeout;checkTimeout.setSingleShot(true);
    bool installingUpdate=false;
    bool updateFailed=false;
    QObject::connect(&checkTimeout,&QTimer::timeout,&updateLoop,[&]{splash.setStatus(QStringLiteral("UPDATE CHECK TIMED OUT — OPENING..."));updateLoop.quit();});
    QObject::connect(&startupUpdater,&UpdateService::upToDate,&updateLoop,[&]{checkTimeout.stop();splash.setStatus(QStringLiteral("UP TO DATE — OPENING..."));splash.setProgress(1.0);updateLoop.quit();});
    QObject::connect(&startupUpdater,&UpdateService::failed,&updateLoop,[&](const QString&){updateFailed=true;checkTimeout.stop();splash.setStatus(QStringLiteral("UPDATE CHECK UNAVAILABLE — OPENING..."));updateLoop.quit();});
    QObject::connect(&startupUpdater,&UpdateService::updateAvailable,&updateLoop,[&](const QString&version,const QString&,const QUrl&asset,const QString&name,const QString&digest){installingUpdate=true;updateFailed=false;checkTimeout.stop();splash.setStatus(QStringLiteral("UPDATE V%1 FOUND — DOWNLOADING...").arg(version));startupUpdater.downloadAndInstall(asset,name,digest);});
    QObject::connect(&startupUpdater,&UpdateService::progress,&splash,[&](qint64 received,qint64 total){if(total>0)splash.setProgress(double(received)/double(total));});
    QObject::connect(&app,&QCoreApplication::aboutToQuit,&updateLoop,&QEventLoop::quit);
    checkTimeout.start(10000);startupUpdater.check(true);updateLoop.exec();
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
