#include <QApplication>
#include "MainWindow.h"
#include "qcustomplot.h"
#include "DDSGenerator.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53,53,53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    // ... set other palette colors
    qApp->setPalette(darkPalette);

    return app.exec();
} 