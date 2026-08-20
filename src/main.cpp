#include "MainWindow.hpp"
#include "BuildInfo.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QIcon>
#include <QPainter>
#include <QScreen>
#include <QSplashScreen>
#include <QThread>

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
        p.setPen(QPen(QColor("#293044"), 5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(54, 285, 506, 285);
        p.setPen(QColor("#a8b0c7"));
        p.drawText(QRect(0, 302, 560, 20), Qt::AlignCenter,
                   QStringLiteral("OPENING YOUR CREATOR CONSOLE..."));
        setProgress(0.0);
    }

    void setProgress(double progress) {
        QPixmap frame=base_;
        QPainter p(&frame);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor("#15e0d2"),5,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(54,285,54+qRound(452*qBound(0.0,progress,1.0)),285);
        setPixmap(frame);
    }

private:
    QPixmap base_;
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
