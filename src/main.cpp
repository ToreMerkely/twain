#include <QApplication>
#include <QStringList>

#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("twain");
    app.setOrganizationName("twain");

    MainWindow window;
    window.show();

    const QStringList args = app.arguments();
    if (args.size() >= 3) {
        window.loadPair(args[1], args[2]);
    }

    return app.exec();
}
