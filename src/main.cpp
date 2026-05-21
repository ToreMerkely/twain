#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QPixmap>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include "DebugLog.h"
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("twain");
    app.setApplicationVersion("0.4.0");
    app.setOrganizationName("twain");
    app.setWindowIcon(QIcon(":/icons/twain.svg"));
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

    TWAIN_LOG("startup: creating MainWindow");
    MainWindow window;
    TWAIN_LOG("startup: MainWindow constructed");
    // Preserve the position/size restored from settings before stashing the
    // window off-screen for the initial paint pass.
    const QPoint plannedPos = window.pos();
    const QSize plannedSize = window.size();
    TWAIN_LOG(QString("startup: planned pos=%1,%2 size=%3x%4")
                  .arg(plannedPos.x()).arg(plannedPos.y())
                  .arg(plannedSize.width()).arg(plannedSize.height()));
    // Show off-screen first so the compositor's first composite of this
    // window happens where nobody can see it. By the time we move it on
    // screen, Qt has painted into the backing store at least once.
    window.move(-30000, -30000);
    window.show();
    TWAIN_LOG("startup: show() returned, forcing render");
    QPixmap forcePaint(window.size());
    window.render(&forcePaint);
    TWAIN_LOG("startup: render returned, draining events");
    QApplication::processEvents();
    TWAIN_LOG("startup: processEvents drained, moving on-screen");
    window.move(plannedPos);
    window.resize(plannedSize);
    TWAIN_LOG("startup: move complete");

    const QStringList pos = parser.positionalArguments();
    if (pos.size() >= 2) {
        // Defer to the event loop so the window chrome paints first; the
        // user sees the menu/toolbar/status-bar (with spinner) instead of a
        // blank frame while archives extract.
        QTimer::singleShot(0, &window, [&window, pos]() {
            window.loadFromCli(pos[0], pos[1]);
        });
    }

    return app.exec();
}
