#include <QApplication>
#include <QFileInfo>
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
        const QFileInfo l(args[1]);
        const QFileInfo r(args[2]);
        if (l.isDir() && r.isDir()) {
            window.loadFolderPair(args[1], args[2]);
        } else {
            window.loadPair(args[1], args[2]);
        }
    }

    return app.exec();
}
