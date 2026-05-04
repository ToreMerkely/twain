#include "HighlighterFactory.h"

#include <QFileInfo>

#include "CppHighlighter.h"
#include "JsonHighlighter.h"
#include "MakefileHighlighter.h"
#include "MarkdownHighlighter.h"
#include "PythonHighlighter.h"
#include "ShellHighlighter.h"
#include "YamlHighlighter.h"

QSyntaxHighlighter* makeHighlighter(const QString& path, QTextDocument* doc) {
    QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString name = info.fileName().toLower();

    if (suffix == "cpp" || suffix == "cc" || suffix == "cxx" ||
        suffix == "h" || suffix == "hpp" || suffix == "hxx" || suffix == "c") {
        return new CppHighlighter(doc);
    }
    if (suffix == "py" || suffix == "pyw") return new PythonHighlighter(doc);
    if (suffix == "json") return new JsonHighlighter(doc);
    if (suffix == "yaml" || suffix == "yml") return new YamlHighlighter(doc);
    if (suffix == "sh" || suffix == "bash" || suffix == "zsh") return new ShellHighlighter(doc);
    if (suffix == "md" || suffix == "markdown") return new MarkdownHighlighter(doc);
    if (name == "makefile" || name == "gnumakefile" || suffix == "mk" || suffix == "make") {
        return new MakefileHighlighter(doc);
    }
    return nullptr;
}
