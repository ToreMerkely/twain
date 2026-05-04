#pragma once

#include <QString>

class QSyntaxHighlighter;
class QTextDocument;

QSyntaxHighlighter* makeHighlighter(const QString& path, QTextDocument* doc);
