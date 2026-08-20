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
    BrandSplash() : QSplashScreen(QPixmap(560, 340)) {
        QPixmap art(560, 340);
        art.fill(QColor("#0b0d15"));
        QPainter p(&art);
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
        p.setPen(QColor("#293044")); p.drawLine(54, 285, 506, 285);
        p.setPen(QPen(QColor("#15e0d2"), 3)); p.drawLine(54, 285, 190, 285);
        p.setPen(QColor("#a8b0c7"));
        p.drawText(QRect(0, 302, 560, 20), Qt::AlignCenter,
                   QStringLiteral("OPENING YOUR CREATOR CONSOLE..."));
        setPixmap(art);
    }
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

    MainWindow window;
    QElapsedTimer minimumBrandTime;
    minimumBrandTime.start();
    while (minimumBrandTime.elapsed() < 700) {
        app.processEvents();
        QThread::msleep(10);
    }
    window.show();
    splash.finish(&window);
    return app.exec();
}
