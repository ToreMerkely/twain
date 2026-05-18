#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include "DebugLog.h"
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("twain");
    app.setApplicationVersion("0.4.0");
    app.setOrganizationName("twain");
    DebugLog::init();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "twain — a side-by-side diff tool.\n\n"
        "Open two files or two folders side by side. With no arguments,\n"
        "starts with an empty workspace; use File > Open to load a pair.\n\n"
        "Run with --git-config to print the git config commands needed to\n"
        "register twain as a git difftool.");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption gitConfigOpt(
        "git-config",
        "Print the git config commands to register twain as a difftool, "
        "then exit. Pipe to 'sh' to apply.");
    parser.addOption(gitConfigOpt);
    parser.addPositionalArgument("left", "Left file or folder.", "[left]");
    parser.addPositionalArgument("right", "Right file or folder.", "[right]");
    parser.process(app);

    if (parser.isSet(gitConfigOpt)) {
        const QString bin = QCoreApplication::applicationFilePath();
        QTextStream out(stdout);
        out << "git config --global diff.tool twain\n"
            << "git config --global difftool.twain.cmd '"
            << bin << " \"$LOCAL\" \"$REMOTE\"'\n"
            << "git config --global difftool.prompt false\n";
        return 0;
    }

    MainWindow window;
    window.show();

    const QStringList pos = parser.positionalArguments();
    if (pos.size() >= 2) {
        const QFileInfo l(pos[0]);
        const QFileInfo r(pos[1]);
        if (l.isDir() && r.isDir()) {
            window.loadFolderPair(pos[0], pos[1]);
        } else {
            window.loadPair(pos[0], pos[1]);
        }
    }

    return app.exec();
}
