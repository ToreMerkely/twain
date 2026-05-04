#include "ShellHighlighter.h"

ShellHighlighter::ShellHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    // Strings (green)
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(0x6A, 0x87, 0x59));
    m_rules.append({QRegularExpression("\"(?:[^\"\\\\]|\\\\.)*\""), stringFormat});
    m_rules.append({QRegularExpression("'[^']*'"), stringFormat});

    // Numbers (blue)
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(0x68, 0x97, 0xBB));
    m_rules.append({
        QRegularExpression("\\b\\d+(?:\\.\\d+)?\\b"),
        numberFormat
    });

    // Keywords (orange bold)
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(0xCC, 0x78, 0x32));
    keywordFormat.setFontWeight(QFont::Bold);
    const QString keywords[] = {
        "if", "then", "else", "elif", "fi", "for", "while", "do", "done",
        "case", "esac", "function", "return", "in", "select", "until",
        "break", "continue", "local", "readonly", "export", "declare",
        "typeset", "let", "shift", "trap", "set", "unset", "source"
    };
    for (const QString &kw : keywords)
        m_rules.append({QRegularExpression("\\b" + kw + "\\b"), keywordFormat});

    // Variables: $name, ${name...}, $1, $@, $*, $?, $$, $!, $-, $_
    // (Applied after strings so vars inside double-quoted strings are
    // recolored — close enough; single-quoted-string contents would
    // ideally not expand, but accepting the false positive.)
    QTextCharFormat varFormat;
    varFormat.setForeground(QColor(0x98, 0x76, 0xAA));
    m_rules.append({
        QRegularExpression("\\$(?:\\{[^}]*\\}|[A-Za-z_]\\w*|[0-9@*#?$!_-])"),
        varFormat
    });

    // Shebang
    QTextCharFormat shebangFormat;
    shebangFormat.setForeground(QColor(0x80, 0x80, 0x80));
    shebangFormat.setFontItalic(true);
    m_rules.append({QRegularExpression("^#!.*$"), shebangFormat});

    // Comments (gray italic) — last so they override anything else
    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor(0x80, 0x80, 0x80));
    commentFormat.setFontItalic(true);
    m_rules.append({QRegularExpression("(?<=^|\\s)#[^\\n]*"), commentFormat});
}

void ShellHighlighter::highlightBlock(const QString &text)
{
    for (const Rule &rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
