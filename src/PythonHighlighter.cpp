#include "PythonHighlighter.h"

PythonHighlighter::PythonHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // Keywords (orange)
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(0xCC, 0x78, 0x32));
    keywordFormat.setFontWeight(QFont::Bold);

    const QString keywords[] = {
        "False", "None", "True", "and", "as", "assert", "async",
        "await", "break", "class", "continue", "def", "del",
        "elif", "else", "except", "finally", "for", "from",
        "global", "if", "import", "in", "is", "lambda",
        "nonlocal", "not", "or", "pass", "raise", "return",
        "try", "while", "with", "yield"
    };

    for (const QString &kw : keywords) {
        m_rules.append({
            QRegularExpression("\\b" + kw + "\\b"),
            keywordFormat
        });
    }

    // self/cls (purple, italic)
    QTextCharFormat selfFormat;
    selfFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    selfFormat.setFontItalic(true);
    m_rules.append({
        QRegularExpression("\\bself\\b"),
        selfFormat
    });
    m_rules.append({
        QRegularExpression("\\bcls\\b"),
        selfFormat
    });

    // Builtins (lavender/purple)
    QTextCharFormat builtinFormat;
    builtinFormat.setForeground(QColor(0xB3, 0x89, 0xC5));

    const QString builtins[] = {
        "abs", "all", "any", "bin", "bool", "bytearray", "bytes",
        "callable", "chr", "classmethod", "compile", "complex",
        "delattr", "dict", "dir", "divmod", "enumerate", "eval",
        "exec", "filter", "float", "format", "frozenset", "getattr",
        "globals", "hasattr", "hash", "help", "hex", "id", "input",
        "int", "isinstance", "issubclass", "iter", "len", "list",
        "locals", "map", "max", "memoryview", "min", "next",
        "object", "oct", "open", "ord", "pow", "print", "property",
        "range", "repr", "reversed", "round", "set", "setattr",
        "slice", "sorted", "staticmethod", "str", "sum", "super",
        "tuple", "type", "vars", "zip",
        "Exception", "BaseException", "ValueError", "TypeError",
        "KeyError", "IndexError", "AttributeError", "ImportError",
        "RuntimeError", "StopIteration", "OSError", "IOError",
        "FileNotFoundError", "NotImplementedError", "OverflowError",
        "ZeroDivisionError"
    };

    for (const QString &bi : builtins) {
        m_rules.append({
            QRegularExpression("\\b" + bi + "\\b"),
            builtinFormat
        });
    }

    // Decorators (yellow)
    QTextCharFormat decoratorFormat;
    decoratorFormat.setForeground(QColor(0xBB, 0xB5, 0x29));
    m_rules.append({
        QRegularExpression("@[A-Za-z_][A-Za-z0-9_.]*"),
        decoratorFormat
    });

    // Function calls (yellow) — applied first so def/class rules below override
    QTextCharFormat functionCallFormat;
    functionCallFormat.setForeground(QColor(0xFF, 0xC6, 0x6D));
    m_rules.append({
        QRegularExpression("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()"),
        functionCallFormat
    });

    // Function definitions (blue)
    QTextCharFormat functionDefFormat;
    functionDefFormat.setForeground(QColor(0x56, 0xA8, 0xF5));
    m_rules.append({
        QRegularExpression("(?<=\\bdef\\s)[A-Za-z_][A-Za-z0-9_]*"),
        functionDefFormat
    });

    // Class names (lavender)
    m_rules.append({
        QRegularExpression("(?<=\\bclass\\s)[A-Za-z_][A-Za-z0-9_]*"),
        builtinFormat
    });

    // Dunder names (pink, italic) — last so it wins for both definitions
    // and call sites
    QTextCharFormat dunderFormat;
    dunderFormat.setForeground(QColor(0xB5, 0x89, 0xF0));
    dunderFormat.setFontItalic(true);
    m_rules.append({
        QRegularExpression("\\b__[A-Za-z_][A-Za-z0-9_]*__\\b"),
        dunderFormat
    });

    // Numbers (blue)
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(0x68, 0x97, 0xBB));
    m_rules.append({
        QRegularExpression("\\b[0-9]+(\\.[0-9]+)?([eE][+-]?[0-9]+)?j?\\b"),
        numberFormat
    });
    m_rules.append({
        QRegularExpression("\\b0[xX][0-9a-fA-F]+\\b"),
        numberFormat
    });
    m_rules.append({
        QRegularExpression("\\b0[oO][0-7]+\\b"),
        numberFormat
    });
    m_rules.append({
        QRegularExpression("\\b0[bB][01]+\\b"),
        numberFormat
    });

    // UPPER_CASE constants (dark purple)
    QTextCharFormat constFormat;
    constFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    m_rules.append({
        QRegularExpression("\\b[A-Z][A-Z0-9_]{2,}\\b"),
        constFormat
    });

    // Single-line strings (green)
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(0x6A, 0x87, 0x59));
    // f-strings prefix
    m_rules.append({
        QRegularExpression("[fFrRbBuU]*\"(?:[^\"\\\\]|\\\\.)*\""),
        stringFormat
    });
    m_rules.append({
        QRegularExpression("[fFrRbBuU]*'(?:[^'\\\\]|\\\\.)*'"),
        stringFormat
    });

    // Comments (grey, italic)
    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor(0x80, 0x80, 0x80));
    commentFormat.setFontItalic(true);
    m_rules.append({
        QRegularExpression("#[^\n]*"),
        commentFormat
    });

    // Triple-quoted strings
    m_multiLineStringFormat = stringFormat;
    m_tripleDoubleStart = QRegularExpression("\"\"\"");
    m_tripleDoubleEnd = QRegularExpression("\"\"\"");
    m_tripleSingleStart = QRegularExpression("'''");
    m_tripleSingleEnd = QRegularExpression("'''");
}

void PythonHighlighter::highlightBlock(const QString &text)
{
    // Apply single-line rules
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Triple-quoted string handling
    // State: 0 = normal, 1 = inside """, 2 = inside '''
    int state = previousBlockState();
    if (state < 0) state = 0;

    int start = 0;

    if (state == 1) {
        // Continue inside """
        auto match = m_tripleDoubleEnd.match(text, 0);
        if (match.hasMatch()) {
            int end = match.capturedEnd();
            setFormat(0, end, m_multiLineStringFormat);
            state = 0;
            start = end;
        } else {
            setFormat(0, text.length(), m_multiLineStringFormat);
            setCurrentBlockState(1);
            return;
        }
    } else if (state == 2) {
        // Continue inside '''
        auto match = m_tripleSingleEnd.match(text, 0);
        if (match.hasMatch()) {
            int end = match.capturedEnd();
            setFormat(0, end, m_multiLineStringFormat);
            state = 0;
            start = end;
        } else {
            setFormat(0, text.length(), m_multiLineStringFormat);
            setCurrentBlockState(2);
            return;
        }
    }

    // Look for new triple-quoted strings
    while (start < text.length()) {
        auto dMatch = m_tripleDoubleStart.match(text, start);
        auto sMatch = m_tripleSingleStart.match(text, start);

        int dPos = dMatch.hasMatch() ? dMatch.capturedStart() : text.length();
        int sPos = sMatch.hasMatch() ? sMatch.capturedStart() : text.length();

        if (dPos >= text.length() && sPos >= text.length())
            break;

        if (dPos <= sPos) {
            // Found """
            auto endMatch = m_tripleDoubleEnd.match(text, dPos + 3);
            if (endMatch.hasMatch()) {
                int len = endMatch.capturedEnd() - dPos;
                setFormat(dPos, len, m_multiLineStringFormat);
                start = dPos + len;
            } else {
                setFormat(dPos, text.length() - dPos, m_multiLineStringFormat);
                setCurrentBlockState(1);
                return;
            }
        } else {
            // Found '''
            auto endMatch = m_tripleSingleEnd.match(text, sPos + 3);
            if (endMatch.hasMatch()) {
                int len = endMatch.capturedEnd() - sPos;
                setFormat(sPos, len, m_multiLineStringFormat);
                start = sPos + len;
            } else {
                setFormat(sPos, text.length() - sPos, m_multiLineStringFormat);
                setCurrentBlockState(2);
                return;
            }
        }
    }

    setCurrentBlockState(0);
}
